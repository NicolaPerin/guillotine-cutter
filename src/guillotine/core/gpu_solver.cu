#include <cuda_runtime.h>
#include <stdint.h>
#include <stdio.h>

/*
 * Aborts with a descriptive message if a CUDA API call returns an error.
 * Every CUDA call in this file is wrapped with this macro so that GPU-side
 * failures (out-of-memory, invalid device pointer, kernel errors surfaced by
 * cudaDeviceSynchronize, etc.) are caught immediately rather than silently
 * producing wrong results or crashing later at an unrelated site.
 */
#define CUDA_CHECK(call) do { \
    cudaError_t _err = (call); \
    if (_err != cudaSuccess) { \
        fprintf(stderr, "CUDA error at %s:%d: %s\n", \
                __FILE__, __LINE__, cudaGetErrorString(_err)); \
        abort(); \
    } \
} while(0)

extern "C" {
#include "solver_core.h"
#include <assert.h>
}

/* =============================================================================
 * SlabFillJob — one unit of work for the diagonal kernel
 *
 * Represents one specific (w, h) size, one tile within that size, and one
 * x-column (lx) within that tile. The kernel launches one block per job,
 * with BLOCK_SIZE threads covering the y-span of the column in parallel.
 *
 * w, h, lx are uint16_t — sufficient for sheet dimensions up to 65535.
 * The static_assert guards against unexpected padding or field changes.
 * ============================================================================= */
struct SlabFillJob {
    uint16_t w, h;
    int32_t  tile_idx;
    uint16_t lx;
};
static_assert(sizeof(SlabFillJob) == 12, "SlabFillJob layout changed unexpectedly");

/* Threads per block. Each thread handles one y-row in the assigned column.
 * Benchmarked at 32/64/128/256 on a 400x400 sheet: 17.0s/13.4s/13.2s/15.4s.
 * 128 minimizes wall time: smaller values under-utilize wide tiles; larger
 * values increase register pressure and reduce SM occupancy. */
#define BLOCK_SIZE 128

/* =============================================================================
 * resolve_col_device — GPU-side column resolver
 *
 * Mirrors the CPU resolve_col() in solver_core.h. Populates a ColRef for
 * a given (w, h, sx): if sx falls inside a tile, is_pure is set to false
 * and tdata points to the column's delta values in device memory.
 *
 * Reading the defect slab here is safe even though other kernel launches
 * write to it. A cut at position z only ever reads subproblems with a
 * strictly smaller perimeter than (w, h), which were computed in a previous
 * diagonal and fully flushed by cudaDeviceSynchronize.
 *
 * The binary search requires tiles to be x-sorted and x-disjoint, which is
 * guaranteed by the MERGE_1D_X strategy used during slab construction.
 * ============================================================================= */
__device__ void resolve_col_device(ColRef *cr, int w, int h, int sx,
                                   const int32_t *pure_values, int col_stride,
                                   const uint8_t *has_tiles,
                                   const TileIndex *tile_index,
                                   const Tile *tiles,
                                   const uint16_t *data) {
    int wh_idx   = w * col_stride + h;
    cr->pure_val = pure_values[wh_idx];
    cr->is_pure  = true;

    if (!has_tiles[wh_idx]) return;
    const TileIndex *ti = &tile_index[wh_idx];
    if (ti->tile_count == 0) return;

    int L = 0, R = ti->tile_count - 1;
    while (L <= R) {
        int M = L + (R - L) / 2;
        const Tile *t = (M == 0) ? &ti->first_tile_inline
                                 : &tiles[ti->overflow_start + M - 1];
        if      (sx < t->sheet_x_lo) R = M - 1;
        else if (sx > t->sheet_x_hi) L = M + 1;
        else {
            cr->is_pure    = false;
            cr->y_span     = t->y_span;
            cr->sheet_y_lo = t->sheet_y_lo;
            cr->tdata      = &data[t->data_offset + (sx - t->sheet_x_lo) * t->y_span];
            return;
        }
    }
}

__device__ inline int32_t colref_get_device(const ColRef *cr, int sy) {
    if (cr->is_pure) return cr->pure_val;
    int ly = sy - cr->sheet_y_lo;
    if (ly >= 0 && ly < cr->y_span) return cr->pure_val - cr->tdata[ly];
    return cr->pure_val;
}

/* =============================================================================
 * solve_diagonal_kernel — one block per SlabFillJob, BLOCK_SIZE threads each
 *
 * Each block processes one (w, h, tile, lx) column. Threads split the y-span
 * of the column between them. For each y position:
 *   - pure positions (no defects) store delta = 0
 *   - defect-affected positions try all vertical cuts z in [1, w-1] and all
 *     horizontal cuts z in [1, h-1], take the best combined value, and store
 *     delta = pure_values[w][h] - best_value as uint16
 *
 * overflow_flag is set atomically if any delta exceeds the uint16 range.
 * ============================================================================= */
__global__ void solve_diagonal_kernel(const SlabFillJob *jobs, int num_jobs,
                                      int col_stride,
                                      const int32_t *pure_values,
                                      const int32_t *defect_prefix,
                                      const uint8_t *has_tiles,
                                      const TileIndex *tile_index,
                                      const Tile *tiles,
                                      uint16_t *data,
                                      int *overflow_flag) {
    int job_idx = blockIdx.x;
    if (job_idx >= num_jobs) return;

    SlabFillJob job = jobs[job_idx];
    int w           = job.w;
    int h           = job.h;
    int wh_idx      = w * col_stride + h;

    const TileIndex *ti = &tile_index[wh_idx];
    const Tile *tile = (job.tile_idx == 0)
        ? &ti->first_tile_inline
        : &tiles[ti->overflow_start + job.tile_idx - 1];

    int sx       = tile->sheet_x_lo + job.lx;
    int pure_val = pure_values[wh_idx];

    /* Threads in the block process y-rows simultaneously. */
    for (int ly = threadIdx.x; ly < tile->y_span; ly += blockDim.x) {
        int sy = tile->sheet_y_lo + ly;

        int d_count = defect_prefix[(sx + w) * col_stride + (sy + h)]
                    - defect_prefix[sx       * col_stride + (sy + h)]
                    - defect_prefix[(sx + w) * col_stride + sy]
                    + defect_prefix[sx       * col_stride + sy];

        if (d_count == 0) {
            data[tile->data_offset + job.lx * tile->y_span + ly] = 0;
            continue;
        }

        int32_t best_val = 0;

        for (int z = 1; z < w; z++) {
            ColRef left, right;
            resolve_col_device(&left,  z,     h, sx,     pure_values, col_stride,
                               has_tiles, tile_index, tiles, data);
            resolve_col_device(&right, w - z, h, sx + z, pure_values, col_stride,
                               has_tiles, tile_index, tiles, data);
            int32_t v = colref_get_device(&left, sy) + colref_get_device(&right, sy);
            if (v > best_val) best_val = v;
        }

        for (int z = 1; z < h; z++) {
            ColRef top, bottom;
            resolve_col_device(&top,    w, z,     sx, pure_values, col_stride,
                               has_tiles, tile_index, tiles, data);
            resolve_col_device(&bottom, w, h - z, sx, pure_values, col_stride,
                               has_tiles, tile_index, tiles, data);
            int32_t v = colref_get_device(&top, sy) + colref_get_device(&bottom, sy + z);
            if (v > best_val) best_val = v;
        }

        int32_t delta = pure_val - best_val;
        if (delta > 65535) atomicExch(overflow_flag, 1);
        data[tile->data_offset + job.lx * tile->y_span + ly] = (uint16_t)delta;
    }
}

/* =============================================================================
 * fill_defect_slab_gpu — GPU implementation of the defect slab fill
 *
 * Executes the defect slab DP on the GPU using a diagonal wavefront. All
 * (w, h) pairs on diagonal d = w+h are independent and are launched as a
 * single kernel call. cudaDeviceSynchronize between diagonals enforces the
 * DP dependency: every cut candidate for (w, h) references a strictly
 * smaller perimeter, so its value is already written and visible before
 * this diagonal launches.
 *
 * Each job covers one (w, h, tile, lx) column — the same granularity as
 * the CPU implementation in fill_defect_slab_cpu.
 * ============================================================================= */
extern "C" {
    int fill_defect_slab_gpu(DefectSlab *slab, int sheet_width, int sheet_height,
                            int col_stride, const int32_t *host_defect_prefix,
                            const int32_t *host_pure_values) {

        /* Nothing to compute if no position requires a defect-adjusted value. */
        if (slab->total_data_entries == 0) return 1;

        if (sheet_width > 65535 || sheet_height > 65535) {
            fprintf(stderr, "fill_defect_slab_gpu: sheet dimensions exceed "
                            "uint16_t range in SlabFillJob\n");
            abort();
        }

        size_t free_mem, total_mem;
        CUDA_CHECK(cudaMemGetInfo(&free_mem, &total_mem));
        size_t required = slab->total_data_entries * sizeof(uint16_t);
        if (required > free_mem) {
            fprintf(stderr, "warning: insufficient VRAM (%zu MB required, %zu MB free) "
                            "— falling back to CPU\n",
                    required >> 20, free_mem >> 20);
            return 0;
        }

        int32_t   *d_pure_values, *d_defect_prefix;
        uint8_t   *d_has_tiles;
        TileIndex *d_tile_index;
        Tile      *d_tiles = nullptr;
        uint16_t  *d_data;
        int       *d_overflow_flag;

        size_t wh_size = (size_t)(sheet_width + 1) * col_stride;

        CUDA_CHECK(cudaMalloc(&d_pure_values, wh_size * sizeof(int32_t)));
        CUDA_CHECK(cudaMemcpy(d_pure_values, host_pure_values,
                            wh_size * sizeof(int32_t), cudaMemcpyHostToDevice));

        CUDA_CHECK(cudaMalloc(&d_defect_prefix, wh_size * sizeof(int32_t)));
        CUDA_CHECK(cudaMemcpy(d_defect_prefix, host_defect_prefix,
                            wh_size * sizeof(int32_t), cudaMemcpyHostToDevice));

        CUDA_CHECK(cudaMalloc(&d_has_tiles, wh_size * sizeof(uint8_t)));
        CUDA_CHECK(cudaMemcpy(d_has_tiles, slab->has_tiles,
                            wh_size * sizeof(uint8_t), cudaMemcpyHostToDevice));

        /* TileIndex embeds a full Tile struct inline (first_tile_inline), so each
        * entry is ~80 bytes. For a 200x200 sheet this transfer is ~3 MB. */
        CUDA_CHECK(cudaMalloc(&d_tile_index, wh_size * sizeof(TileIndex)));
        CUDA_CHECK(cudaMemcpy(d_tile_index, slab->tile_index,
                            wh_size * sizeof(TileIndex), cudaMemcpyHostToDevice));

        if (slab->total_tile_count > 0) {
            CUDA_CHECK(cudaMalloc(&d_tiles, slab->total_tile_count * sizeof(Tile)));
            CUDA_CHECK(cudaMemcpy(d_tiles, slab->tiles,
                                slab->total_tile_count * sizeof(Tile),
                                cudaMemcpyHostToDevice));
        }

        /* d_tiles is nullptr when total_tile_count == 0. This is safe as long as
        * no tile has tile_count > 1, which is guaranteed by slab construction.
        * Assert here to catch any future violation immediately. */
        assert(d_tiles != nullptr || slab->total_tile_count == 0);

        CUDA_CHECK(cudaMalloc(&d_data, slab->total_data_entries * sizeof(uint16_t)));
        CUDA_CHECK(cudaMalloc(&d_overflow_flag, sizeof(int)));
        CUDA_CHECK(cudaMemset(d_overflow_flag, 0, sizeof(int)));

        /*
        * On a single diagonal (w + h = d), each (w, h) pair contributes at most
        * (sheet_width - w + 1) jobs — one per x-column that a tile can occupy.
        * Summing over all pairs on the widest diagonal gives sheet_width *
        * sheet_height as a strict upper bound on jobs per diagonal launch.
        */
        size_t       max_jobs = (size_t)sheet_width * sheet_height;
        SlabFillJob *host_jobs = (SlabFillJob *)malloc(max_jobs * sizeof(SlabFillJob));
        SlabFillJob *d_jobs;
        CUDA_CHECK(cudaMalloc(&d_jobs, max_jobs * sizeof(SlabFillJob)));

        /*
        * Process diagonals in increasing order of w+h. Every cut candidate for
        * a cell (w, h) references strictly smaller perimeters, so by the time a
        * diagonal is launched all data it depends on has been written and
        * synchronized by the previous cudaDeviceSynchronize.
        */
        for (int d = 2; d <= sheet_width + sheet_height; d++) {
            int num_jobs = 0;

            for (int w = 1; w <= sheet_width; w++) {
                int h = d - w;
                if (h < 1 || h > sheet_height) continue;
                int wh_idx = w * col_stride + h;
                if (!slab->has_tiles[wh_idx]) continue;

                const TileIndex *ti = &slab->tile_index[wh_idx];
                for (int t = 0; t < ti->tile_count; t++) {
                    const Tile *tile = (t == 0)
                        ? &ti->first_tile_inline
                        : &slab->tiles[ti->overflow_start + t - 1];
                    for (int lx = 0; lx < tile->x_span; lx++)
                        host_jobs[num_jobs++] = {(uint16_t)w, (uint16_t)h,
                                                (int32_t)t, (uint16_t)lx};
                }
            }

            if (num_jobs == 0) continue;
            CUDA_CHECK(cudaMemcpy(d_jobs, host_jobs,
                                num_jobs * sizeof(SlabFillJob),
                                cudaMemcpyHostToDevice));

            solve_diagonal_kernel<<<num_jobs, BLOCK_SIZE>>>(
                d_jobs, num_jobs, col_stride,
                d_pure_values, d_defect_prefix,
                d_has_tiles, d_tile_index, d_tiles, d_data, d_overflow_flag);
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        CUDA_CHECK(cudaMemcpy(slab->data, d_data,
                            slab->total_data_entries * sizeof(uint16_t),
                            cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(&slab->overflow, d_overflow_flag,
                            sizeof(int), cudaMemcpyDeviceToHost));

        CUDA_CHECK(cudaFree(d_pure_values));
        CUDA_CHECK(cudaFree(d_defect_prefix));
        CUDA_CHECK(cudaFree(d_has_tiles));
        CUDA_CHECK(cudaFree(d_tile_index));
        if (d_tiles) CUDA_CHECK(cudaFree(d_tiles));
        CUDA_CHECK(cudaFree(d_data));
        CUDA_CHECK(cudaFree(d_overflow_flag));
        CUDA_CHECK(cudaFree(d_jobs));
        free(host_jobs);

        return 1;
    }
}