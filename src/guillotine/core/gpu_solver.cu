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
}

struct DPJob {
    int16_t w, h;
    int32_t tile_idx;
    int16_t lx;
};

/* Threads per block for the diagonal kernel. Each thread handles one y-row
   within the assigned (w, h, sx) column. */
#define BLOCK_SIZE 128

/*
 * Reading slab->data here is safe even though other kernel launches write to it.
 * A cut at position z only ever reads subproblems smaller than (w, h), which
 * were computed in a previous diagonal and fully flushed by cudaDeviceSynchronize.
 */
__device__ void resolve_col_device(ColRef* cr, int w, int h, int sx, 
                                   const int32_t* F_values, int col_stride,
                                   const uint8_t* has_tiles, const TileIndex* tile_index,
                                   const Tile* tiles, const uint16_t* data) {
    int wh_idx = w * col_stride + h;
    cr->pure_val = F_values[wh_idx];
    cr->is_pure = true;

    if (!has_tiles[wh_idx]) return;
    const TileIndex* ti = &tile_index[wh_idx];
    if (ti->tile_count == 0) return;

    int L = 0, R = ti->tile_count - 1;
    while (L <= R) {
        int M = L + (R - L) / 2;
        const Tile* t = (M == 0) ? &ti->first_tile_inline : &tiles[ti->overflow_start + M - 1];
        if (sx < t->sheet_x_lo) R = M - 1;
        else if (sx > t->sheet_x_hi) L = M + 1;
        else {
            cr->is_pure = false;
            cr->y_span = t->y_span;
            cr->sheet_y_lo = t->sheet_y_lo;
            cr->tdata = &data[t->data_offset + (sx - t->sheet_x_lo) * t->y_span];
            return;
        }
    }
}

__device__ inline int32_t colref_get_device(const ColRef* cr, int sy) {
    if (cr->is_pure) return cr->pure_val;
    int ly = sy - cr->sheet_y_lo;
    if (ly >= 0 && ly < cr->y_span) return cr->pure_val - cr->tdata[ly];
    return cr->pure_val;
}

__global__ void solve_diagonal_kernel(const DPJob* jobs, int num_jobs, int col_stride,
                                      const int32_t* F_values, const int32_t* defect_prefix,
                                      const uint8_t* has_tiles, const TileIndex* tile_index,
                                      const Tile* tiles, uint16_t* data, int* overflow_flag) {
    
    int job_idx = blockIdx.x;
    if (job_idx >= num_jobs) return;

    DPJob job = jobs[job_idx];
    int w = job.w;
    int h = job.h;
    int wh_idx = w * col_stride + h;

    const TileIndex* ti = &tile_index[wh_idx];
    const Tile* tile = (job.tile_idx == 0) ? &ti->first_tile_inline : &tiles[ti->overflow_start + job.tile_idx - 1];
    
    int sx = tile->sheet_x_lo + job.lx;
    int pure_val = F_values[wh_idx];

    // Threads in the block process the 'y' values simultaneously
    for (int ly = threadIdx.x; ly < tile->y_span; ly += blockDim.x) {
        int sy = tile->sheet_y_lo + ly;
        
        int d_count = defect_prefix[(sx + w) * col_stride + (sy + h)]
                    - defect_prefix[sx * col_stride + (sy + h)]
                    - defect_prefix[(sx + w) * col_stride + sy]
                    + defect_prefix[sx * col_stride + sy];
        
        if (d_count == 0) {
            data[tile->data_offset + job.lx * tile->y_span + ly] = 0;
            continue;
        }

        int32_t best_val = 0;

        for (int z = 1; z < w; z++) {
            ColRef left, right;
            resolve_col_device(&left, z, h, sx, F_values, col_stride, has_tiles, tile_index, tiles, data);
            resolve_col_device(&right, w - z, h, sx + z, F_values, col_stride, has_tiles, tile_index, tiles, data);
            int32_t v = colref_get_device(&left, sy) + colref_get_device(&right, sy);
            if (v > best_val) best_val = v;
        }

        for (int z = 1; z < h; z++) {
            ColRef top, bottom;
            resolve_col_device(&top, w, z, sx, F_values, col_stride, has_tiles, tile_index, tiles, data);
            resolve_col_device(&bottom, w, h - z, sx, F_values, col_stride, has_tiles, tile_index, tiles, data);
            int32_t v = colref_get_device(&top, sy) + colref_get_device(&bottom, sy + z);
            if (v > best_val) best_val = v;
        }

        int32_t delta = pure_val - best_val;
        if (delta > 65535) atomicExch(overflow_flag, 1);
        data[tile->data_offset + job.lx * tile->y_span + ly] = (uint16_t)delta;
    }
}

extern "C" {
    void execute_phase_e_gpu_wavefront(FdSlab* slab, int sheet_width, int sheet_height, int col_stride, 
                                    const int32_t* host_defect_prefix, const int32_t* host_F_values) {
        
        /* Nothing to compute if no position requires a defect-adjusted value. */
        if (slab->total_data_entries == 0) return;

        int32_t *d_F_values, *d_defect_prefix;
        uint8_t *d_has_tiles; TileIndex *d_tile_index; Tile *d_tiles = nullptr;
        uint16_t *d_data; int *d_overflow_flag;

        size_t wh_size = (sheet_width + 1) * col_stride;
        CUDA_CHECK(cudaMalloc(&d_F_values, wh_size * sizeof(int32_t)));
        CUDA_CHECK(cudaMemcpy(d_F_values, host_F_values, wh_size * sizeof(int32_t), cudaMemcpyHostToDevice));

        CUDA_CHECK(cudaMalloc(&d_defect_prefix, wh_size * sizeof(int32_t)));
        CUDA_CHECK(cudaMemcpy(d_defect_prefix, host_defect_prefix, wh_size * sizeof(int32_t), cudaMemcpyHostToDevice));

        CUDA_CHECK(cudaMalloc(&d_has_tiles, wh_size * sizeof(uint8_t)));
        CUDA_CHECK(cudaMemcpy(d_has_tiles, slab->has_tiles, wh_size * sizeof(uint8_t), cudaMemcpyHostToDevice));

        CUDA_CHECK(cudaMalloc(&d_tile_index, wh_size * sizeof(TileIndex)));
        CUDA_CHECK(cudaMemcpy(d_tile_index, slab->tile_index, wh_size * sizeof(TileIndex), cudaMemcpyHostToDevice));

        if (slab->total_tile_count > 0) {
            CUDA_CHECK(cudaMalloc(&d_tiles, slab->total_tile_count * sizeof(Tile)));
            CUDA_CHECK(cudaMemcpy(d_tiles, slab->tiles, slab->total_tile_count * sizeof(Tile), cudaMemcpyHostToDevice));
        }

        CUDA_CHECK(cudaMalloc(&d_data, slab->total_data_entries * sizeof(uint16_t)));
        
        CUDA_CHECK(cudaMalloc(&d_overflow_flag, sizeof(int)));
        CUDA_CHECK(cudaMemset(d_overflow_flag, 0, sizeof(int)));

        /*
        * On a single diagonal (w + h = d), each (w, h) pair contributes at most
        * (sheet_width - w + 1) jobs — one per x-column that a tile can occupy.
        * Summing over all pairs on the widest diagonal gives sheet_width * sheet_height
        * as a strict upper bound on jobs per diagonal launch.
        */
        size_t max_jobs_per_diagonal = (size_t)sheet_width * sheet_height;
        DPJob* host_jobs = (DPJob*)malloc(max_jobs_per_diagonal * sizeof(DPJob));
        DPJob* d_jobs;
        CUDA_CHECK(cudaMalloc(&d_jobs, max_jobs_per_diagonal * sizeof(DPJob)));

        /*
        * Diagonals are processed in increasing order of w+h. Every cut candidate
        * for a cell (w, h) references strictly smaller subproblems, so by the time
        * a diagonal is launched all data it depends on has already been written and
        * synchronized.
        */
        for (int d = 2; d <= sheet_width + sheet_height; d++) {
            int num_jobs = 0;
            
            for (int w = 1; w <= sheet_width; w++) {
                int h = d - w;
                if (h >= 1 && h <= sheet_height) {
                    int wh_idx = w * col_stride + h;
                    if (!slab->has_tiles[wh_idx]) continue;
                    
                    const TileIndex* ti = &slab->tile_index[wh_idx];
                    for (int t = 0; t < ti->tile_count; t++) {
                        const Tile* tile = (t == 0) ? &ti->first_tile_inline : &slab->tiles[ti->overflow_start + t - 1];
                        for (int lx = 0; lx < tile->x_span; lx++) {
                            host_jobs[num_jobs++] = { (int16_t)w, (int16_t)h, (int32_t)t, (int16_t)lx };
                        }
                    }
                }
            }

            if (num_jobs == 0) continue;
            CUDA_CHECK(cudaMemcpy(d_jobs, host_jobs, num_jobs * sizeof(DPJob), cudaMemcpyHostToDevice));
            
            solve_diagonal_kernel<<<num_jobs, BLOCK_SIZE>>>(d_jobs, num_jobs, col_stride, d_F_values, d_defect_prefix,
                                                    d_has_tiles, d_tile_index, d_tiles, d_data, d_overflow_flag);
            CUDA_CHECK(cudaDeviceSynchronize());
        }

        CUDA_CHECK(cudaMemcpy(slab->data, d_data, slab->total_data_entries * sizeof(uint16_t), cudaMemcpyDeviceToHost));
        CUDA_CHECK(cudaMemcpy(&slab->overflow, d_overflow_flag, sizeof(int), cudaMemcpyDeviceToHost));

        CUDA_CHECK(cudaFree(d_F_values)); CUDA_CHECK(cudaFree(d_defect_prefix)); CUDA_CHECK(cudaFree(d_has_tiles));
        CUDA_CHECK(cudaFree(d_tile_index)); if (d_tiles) CUDA_CHECK(cudaFree(d_tiles));
        CUDA_CHECK(cudaFree(d_data)); CUDA_CHECK(cudaFree(d_overflow_flag)); CUDA_CHECK(cudaFree(d_jobs));
        free(host_jobs);
    }
}