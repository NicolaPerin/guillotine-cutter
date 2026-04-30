/* =============================================================================
 * solver_core.c — Guillotine cutting-stock solver: core algorithm
 *
 * Implements the three DP phases declared in solver_core.h.
 * This file has no Python dependency — it can be compiled standalone
 * for testing, benchmarking, or linking into other language bindings.
 *
 * The solver operates in three phases:
 *   Phase 1 (tiling table)  best single-item tiling value for each (w, h)
 *   Phase 2 (pure table)    optimal value for defect-free rectangles
 *   Phase 3 (defect slab)   optimal value for defect-affected rectangles,
 *                            stored as a sparse slab with uint16 delta encoding
 *
 * Sparse slab design
 * ------------------
 * Only positions (sx, sy) where [sx, sx+w) × [sy, sy+h) overlaps at least
 * one defect need explicit storage.  All other positions equal
 * pure_values[w][h] and are recovered in O(1) without a data lookup.
 *
 * For each (w, h) pair the affected region is represented as a small set of
 * disjoint rectangular tiles merged along the x-axis.  Tiles are x-sorted
 * and x-disjoint, which is required for the binary search in resolve_col
 * to be correct.  All tile data is packed into one flat uint16 array; each
 * stored value is a delta = pure_values[w][h] - defect_value(w, h, sx, sy),
 * where defect_value is the optimal value for a defect-affected placement.
 * ============================================================================= */

#include "solver_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <omp.h>

/* =============================================================================
 * PositionInterval — scratch type used while building tiles for one (w, h)
 *
 * Represents the sheet-position range [x_lo, x_hi] × [y_lo, y_hi] of
 * placements whose rectangle overlaps a single defect.  Allocated once and
 * reused across all (w, h) iterations.
 * ============================================================================= */
typedef struct {
    int x_lo, x_hi;
    int y_lo, y_hi;
} PositionInterval;

/* =============================================================================
 * qsort comparators
 * ============================================================================= */

static int cmp_defects_x(const void *a, const void *b) {
    int32_t xa = *(const int32_t *)a, xb = *(const int32_t *)b;
    return (xa > xb) - (xa < xb);
}

static int cmp_iv_x(const void *a, const void *b) {
    return ((const PositionInterval *)a)->x_lo - ((const PositionInterval *)b)->x_lo;
}

/* =============================================================================
 * build_tiles — compute the tile set for one (w, h) pair
 *
 * For each defect, the set of placements (sx, sy) whose rectangle overlaps it
 * forms an axis-aligned rectangle in position space:
 *   x: [defect.x_start - w + 1,  defect.x_end - 1]  clamped to [0, sw-w]
 *   y: [defect.y_start - h + 1,  defect.y_end - 1]  clamped to [0, sh-h]
 *
 * These per-defect rectangles are sorted by x_lo and merged: adjacent or
 * overlapping intervals in x are unioned into a single tile (taking the
 * bounding box in y).  This produces x-sorted x-disjoint tiles, which is
 * required for the binary search in resolve_col to be correct.
 *
 * scratch  caller-provided buffer of at least n_defects PositionIntervals
 * out      caller-provided buffer of at least n_defects Tiles
 * returns  number of tiles written to out[], or 0 if no defect overlaps
 * ============================================================================= */
static int build_tiles(int rw, int rh, int sw, int sh,
                       const int32_t *defects, int n_defects,
                       PositionInterval *scratch, Tile *out) {
    int max_sx = sw - rw;
    int max_sy = sh - rh;
    int n = 0;

    for (int d = 0; d < n_defects; d++) {
        DefectRef df = defect_at(defects, d);
        int xl = defect_x_start(df) - rw + 1; if (xl < 0)      xl = 0;
        int xh = defect_x_end(df)   - 1;      if (xh > max_sx) xh = max_sx;
        int yl = defect_y_start(df) - rh + 1; if (yl < 0)      yl = 0;
        int yh = defect_y_end(df)   - 1;      if (yh > max_sy) yh = max_sy;
        if (xl <= xh && yl <= yh) {
            scratch[n].x_lo = xl; scratch[n].x_hi = xh;
            scratch[n].y_lo = yl; scratch[n].y_hi = yh;
            n++;
        }
    }
    if (n == 0) return 0;

    qsort(scratch, n, sizeof(PositionInterval), cmp_iv_x);

    int n_tiles = 0;
    PositionInterval cur = scratch[0];

    for (int i = 1; i < n; i++) {
        if (scratch[i].x_lo <= cur.x_hi + 1) {
            /* Extend the current merged region to cover scratch[i]. */
            if (scratch[i].x_hi > cur.x_hi) cur.x_hi = scratch[i].x_hi;
            if (scratch[i].y_hi > cur.y_hi) cur.y_hi = scratch[i].y_hi;
            if (scratch[i].x_lo < cur.x_lo) cur.x_lo = scratch[i].x_lo;
            if (scratch[i].y_lo < cur.y_lo) cur.y_lo = scratch[i].y_lo;
        } else {
            out[n_tiles].sheet_x_lo = cur.x_lo;
            out[n_tiles].sheet_x_hi = cur.x_hi;
            out[n_tiles].sheet_y_lo = cur.y_lo;
            out[n_tiles].sheet_y_hi = cur.y_hi;
            out[n_tiles].x_span     = cur.x_hi - cur.x_lo + 1;
            out[n_tiles].y_span     = cur.y_hi - cur.y_lo + 1;
            n_tiles++;
            cur = scratch[i];
        }
    }

    /* Flush the last accumulated region. */
    out[n_tiles].sheet_x_lo = cur.x_lo;
    out[n_tiles].sheet_x_hi = cur.x_hi;
    out[n_tiles].sheet_y_lo = cur.y_lo;
    out[n_tiles].sheet_y_hi = cur.y_hi;
    out[n_tiles].x_span     = cur.x_hi - cur.x_lo + 1;
    out[n_tiles].y_span     = cur.y_hi - cur.y_lo + 1;
    return n_tiles + 1;
}

/* =============================================================================
 * Phase 1 — tiling table
 *
 * For every rectangle size (rw × rh), find the item type that maximises
 * covered area when tiled in a grid of nx × ny copies.  Stores the resulting
 * area in tiling_values[rw][rh] and the item index in tiling_item_index[rw][rh]
 * (-1 if no item fits).
 *
 * The outer loop over rw is parallelised: each width is independent.
 * ============================================================================= */
void fill_tiling_table(int sheet_width, int sheet_height,
                       int32_t *tiling_values, int32_t *tiling_item_index,
                       int32_t *item_widths, int32_t *item_heights,
                       int32_t *item_areas, int n_items) {
    int col_stride = sheet_height + 1;

    #pragma omp parallel for schedule(dynamic)
    for (int rw = 1; rw <= sheet_width; rw++) {
        int base = rw * col_stride;
        for (int rh = 1; rh <= sheet_height; rh++) {
            int32_t best_value = 0, best_item = -1;
            for (int i = 0; i < n_items; i++) {
                int cx = rw / item_widths[i], cy = rh / item_heights[i];
                if (cx > 0 && cy > 0) {
                    int32_t area = item_areas[i] * cx * cy;
                    if (area > best_value) { best_value = area; best_item = i; }
                }
            }
            tiling_values[base + rh]     = best_value;
            tiling_item_index[base + rh] = best_item;
        }
    }
}

/* =============================================================================
 * Phase 2 — pure table
 *
 * Bottom-up DP for defect-free rectangles.  For each (rw, rh), the best value
 * is the maximum of:
 *   - Tiling with copies of one item type        (from tiling_values)
 *   - A vertical guillotine cut at position z    (pure[z][rh] + pure[rw-z][rh])
 *   - A horizontal guillotine cut at position z  (pure[rw][z] + pure[rw][rh-z])
 *
 * Only normal-pattern candidate positions are tried, and only up to half the
 * dimension (symmetry: cutting at z is equivalent to cutting at rw-z).
 *
 * Not parallelised: each (rw, rh) depends on all smaller values.
 * ============================================================================= */
void fill_pure_table(int sheet_width, int sheet_height,
                     int32_t *tiling_values, int32_t *tiling_item_index,
                     int32_t *pure_values, int8_t *pure_decision_type,
                     int32_t *pure_decision_param,
                     int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                     int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts) {
    int col_stride = sheet_height + 1;

    for (int rw = 1; rw <= sheet_width; rw++) {
        int base = rw * col_stride;
        int nxc  = n_normal_cuts_x[rw], xcb = rw * max_x_cuts, hw = rw >> 1;

        for (int rh = 1; rh <= sheet_height; rh++) {
            int nyc = n_normal_cuts_y[rh], ycb = rh * max_y_cuts, hh = rh >> 1;

            int32_t best_item  = tiling_item_index[base + rh];
            int32_t best_value = tiling_values[base + rh];
            int8_t  best_type  = (best_item >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (best_item >= 0) ? best_item : 0;

            for (int ci = 0; ci < nxc; ci++) {
                int z = normal_cuts_x[xcb + ci];
                if (z > hw) break;
                int32_t v = pure_values[z * col_stride + rh]
                          + pure_values[(rw - z) * col_stride + rh];
                if (v > best_value) {
                    best_value = v; best_type = DECISION_CUT_X; best_param = z;
                }
            }
            for (int ci = 0; ci < nyc; ci++) {
                int z = normal_cuts_y[ycb + ci];
                if (z > hh) break;
                int32_t v = pure_values[base + z] + pure_values[base + (rh - z)];
                if (v > best_value) {
                    best_value = v; best_type = DECISION_CUT_Y; best_param = z;
                }
            }

            pure_values[base + rh]         = best_value;
            pure_decision_type[base + rh]  = best_type;
            pure_decision_param[base + rh] = best_param;
        }
    }
}

/* =============================================================================
 * fill_defect_slab_cpu — CPU implementation of the defect slab fill
 *
 * For each (w, h) pair that has tiles, iterates over its tiles and over all
 * sheet positions (sx, sy) within each tile.  Pure positions (no defects) get
 * delta = 0.  Defect-affected positions try every integer vertical cut z in
 * [1, w-1] and horizontal cut z in [1, h-1], take the best combined value,
 * and store delta = pure_values[w][h] - best as uint16.
 *
 * Per-thread scratch buffers (col_best, is_impure) are allocated once upfront
 * and reused across all sx columns a thread processes, avoiding repeated
 * allocation in the hot loop.
 *
 * Loop structure: sx (parallel, dynamic) → classify column →
 *   vertical z (outer) / ly (inner) → horizontal z (outer) / ly (inner) →
 *   delta write
 * ============================================================================= */
void fill_defect_slab_cpu(DefectSlab *slab, int sheet_width, int sheet_height,
                           int col_stride, int32_t *defect_count_prefix,
                           int32_t *pure_values, int d_start) {
    int      nthreads       = omp_get_max_threads();
    int32_t *col_best_pool  = (int32_t *)malloc(nthreads * sheet_height * sizeof(int32_t));
    uint8_t *is_impure_pool = (uint8_t *)malloc(nthreads * sheet_height * sizeof(uint8_t));

    for (int w = 1; w <= sheet_width; w++) {
        int wbase = w * col_stride;
        for (int h = 1; h <= sheet_height; h++) {
            /* Skip cells already filled by the GPU. */
            if (w + h < d_start) continue;

            int wh_idx = wbase + h;
            if (!slab->has_tiles[wh_idx]) continue;

            TileIndex *ti    = &slab->tile_index[wh_idx];
            int32_t pure_val = pure_values[wh_idx];

            for (int t = 0; t < ti->tile_count; t++) {
                Tile *tile = (t == 0)
                    ? &ti->first_tile_inline
                    : &slab->tiles[ti->overflow_start + t - 1];

                int      y_span = tile->y_span;
                uint16_t *tdata = &slab->data[tile->data_offset];

                #pragma omp parallel
                {
                    int tid = omp_get_thread_num();
                    int32_t *col_best  = col_best_pool  + tid * sheet_height;
                    uint8_t *is_impure = is_impure_pool + tid * sheet_height;

                    #pragma omp for schedule(dynamic, 1)
                    for (int sx = tile->sheet_x_lo; sx <= tile->sheet_x_hi; sx++) {
                        int lx = sx - tile->sheet_x_lo;

                        /* Pass 1: classify each sy as pure or impure. */
                        int impure_count = 0;
                        for (int ly = 0; ly < y_span; ly++) {
                            int sy = tile->sheet_y_lo + ly;
                            if (defect_count_in_rect(defect_count_prefix, col_stride,
                                                     sx, sy, sx + w, sy + h) == 0) {
                                tdata[(int64_t)lx * y_span + ly] = 0;
                                is_impure[ly] = 0;
                            } else {
                                is_impure[ly] = 1;
                                col_best[ly]  = 0;
                                impure_count++;
                            }
                        }
                        if (impure_count == 0) continue;

                        /* Vertical cuts: z outer, ly inner. */
                        for (int z = 1; z < w; z++) {
                            ColRef left, right;
                            resolve_col(&left,  slab, pure_values, col_stride, z,     h, sx);
                            resolve_col(&right, slab, pure_values, col_stride, w - z, h, sx + z);
                            for (int ly = 0; ly < y_span; ly++) {
                                if (!is_impure[ly]) continue;
                                int sy = tile->sheet_y_lo + ly;
                                int32_t v = colref_get(&left, sy) + colref_get(&right, sy);
                                if (v > col_best[ly]) col_best[ly] = v;
                            }
                        }

                        /* Horizontal cuts: z outer, ly inner. */
                        for (int z = 1; z < h; z++) {
                            ColRef top, bot;
                            resolve_col(&top, slab, pure_values, col_stride, w,     z, sx);
                            resolve_col(&bot, slab, pure_values, col_stride, w, h - z, sx);
                            for (int ly = 0; ly < y_span; ly++) {
                                if (!is_impure[ly]) continue;
                                int sy = tile->sheet_y_lo + ly;
                                int32_t v = colref_get(&top, sy) + colref_get(&bot, sy + z);
                                if (v > col_best[ly]) col_best[ly] = v;
                            }
                        }

                        /* Delta write. */
                        for (int ly = 0; ly < y_span; ly++) {
                            if (!is_impure[ly]) continue;
                            int32_t delta = pure_val - col_best[ly];
                            if (delta > (int32_t)UINT16_MAX) {
                                #pragma omp atomic write
                                slab->overflow = 1;
                            }
                            tdata[(int64_t)lx * y_span + ly] = (uint16_t)delta;
                        }
                    }
                }
            }
        }
    }

    free(col_best_pool);
    free(is_impure_pool);
}

/* =============================================================================
 * Phase 3 — defect slab
 *
 * Builds the sparse DefectSlab and fills it.
 *
 * Phase A  sort a working copy of the defect array by x for consistent
 *          interval merging across all (w, h) iterations
 *
 * Phase B  build tile geometry: for each (w, h) >= (min_w, min_h) that has
 *          at least one tile, store tile geometry in tile_index[] and tiles[],
 *          and assign each tile's data_offset into the flat data[] array.
 *          Rectangles smaller than min_w × min_h cannot contain any item
 *          and are skipped entirely.
 *
 * Phase C  allocate flat data[] array (uint16 deltas)
 *
 * Phase D  fill via fill_defect_slab_gpu if CUDA is available,
 *           otherwise via fill_defect_slab_cpu
 * ============================================================================= */
DefectSlab *fill_defect_slab(int sheet_width, int sheet_height,
                              int32_t *defect_count_prefix, int32_t *pure_values,
                              int32_t *defect_array_in, int n_defects,
                              int min_w, int min_h) {
    int col_stride = sheet_height + 1;
    int wh_count   = (sheet_width + 1) * col_stride;

    DefectSlab *slab = (DefectSlab *)calloc(1, sizeof(DefectSlab));
    slab->sheet_width  = sheet_width;
    slab->sheet_height = sheet_height;
    slab->has_tiles    = (uint8_t   *)calloc(wh_count, sizeof(uint8_t));
    slab->tile_index   = (TileIndex *)calloc(wh_count, sizeof(TileIndex));

    if (n_defects == 0) {
        slab->tiles              = NULL;
        slab->data               = NULL;
        slab->total_tile_count   = 0;
        slab->total_data_entries = 0;
        return slab;
    }

    /* --- Phase A: sorted working copy of defects --- */
    int32_t *defects = (int32_t *)malloc(n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    memcpy(defects, defect_array_in, n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    if (n_defects > 1)
        qsort(defects, n_defects, sizeof(int32_t) * DEFECT_FIELD_COUNT, cmp_defects_x);

    PositionInterval *iv         = (PositionInterval *)malloc(n_defects * sizeof(PositionInterval));
    Tile             *temp_tiles = (Tile *)malloc(wh_count * n_defects * sizeof(Tile));
    int               t_count   = 0;
    int64_t           data_count = 0;

    /* --- Phase B: tile geometry --- */
    for (int w = min_w; w <= sheet_width; w++) {
        for (int h = min_h; h <= sheet_height; h++) {
            int wh_idx        = w * col_stride + h;
            int local_t_start = t_count;

            int local_tiles = build_tiles(w, h, sheet_width, sheet_height,
                                          defects, n_defects, iv,
                                          &temp_tiles[t_count]);
            if (local_tiles == 0) continue;

            slab->has_tiles[wh_idx]             = 1;
            slab->tile_index[wh_idx].tile_count = local_tiles;

            /* Count data entries — offsets assigned in diagonal order below. */
            for (int i = 0; i < local_tiles; i++)
                data_count += (int64_t)temp_tiles[local_t_start + i].x_span
                            * temp_tiles[local_t_start + i].y_span;

            /* Store tile 0 inline; remaining tiles go into the overflow array. */
            slab->tile_index[wh_idx].first_tile_inline = temp_tiles[local_t_start];
            if (local_tiles > 1)
                slab->tile_index[wh_idx].overflow_start = local_t_start + 1;

            t_count += local_tiles;
        }
    }

    slab->total_tile_count   = t_count;
    slab->total_data_entries = data_count;
    slab->tiles              = temp_tiles;  /* must be set before Phase B.5 */
    slab->overflow           = 0;

    /* --- Phase B.5: assign data_offset in diagonal order ---
     *
     * Offsets are assigned here rather than in Phase B so that tiles on
     * earlier diagonals always have smaller offsets than tiles on later
     * diagonals. This guarantees that d_data[0..diag_data_end[d_cutoff]-1]
     * is a self-contained region covering exactly the tiles the GPU fills,
     * with no gaps or out-of-bounds accesses from resolve_col_device. */
    int max_d = sheet_width + sheet_height;
    int64_t *diag_data_end = (int64_t *)calloc(max_d + 1, sizeof(int64_t));
    int64_t running = 0;

    for (int d = 2; d <= max_d; d++) {
        for (int w = min_w; w <= sheet_width; w++) {
            int h = d - w;
            if (h < min_h || h > sheet_height) continue;
            int wh_idx = w * col_stride + h;
            if (!slab->has_tiles[wh_idx]) continue;
            TileIndex *ti = &slab->tile_index[wh_idx];
            for (int t = 0; t < ti->tile_count; t++) {
                Tile *tile = (t == 0)
                    ? &ti->first_tile_inline
                    : &slab->tiles[ti->overflow_start + t - 1];
                tile->data_offset = running;
                running += (int64_t)tile->x_span * tile->y_span;
            }
        }
        diag_data_end[d] = running;
    }

    /* --- Phase C: allocate delta array --- */
    if (data_count == 0) {
        slab->data = NULL;
        free(defects);
        free(iv);
        return slab;
    }
    slab->data = (uint16_t *)malloc(data_count * sizeof(uint16_t));

    /* --- Phase D: fill --- */
#ifdef HAVE_CUDA
    int d_cutoff = fill_defect_slab_gpu(slab, sheet_width, sheet_height,
                                         col_stride, defect_count_prefix,
                                         pure_values, diag_data_end, max_d);
    if (d_cutoff < max_d)
        fill_defect_slab_cpu(slab, sheet_width, sheet_height, col_stride,
                             defect_count_prefix, pure_values, d_cutoff + 1);
#else
    fill_defect_slab_cpu(slab, sheet_width, sheet_height, col_stride,
                         defect_count_prefix, pure_values, 2);
#endif
    free(diag_data_end);

    if (slab->overflow)
        fprintf(stderr, "warning: defect slab delta overflow — one or more deltas "
                        "exceeded uint16 range and were truncated. "
                        "Solution quality may be degraded.\n");

    free(defects);
    free(iv);
    return slab;
}