/* =============================================================================
 * solver_core.c — Guillotine cutting-stock solver: core algorithm
 *
 * Implements the three DP phases declared in solver_core.h.
 * This file has no Python dependency — it can be compiled standalone
 * for testing, benchmarking, or linking into other language bindings.
 *
 * The solver operates in three phases:
 *   Phase 1 (g-table)   best single-item tiling value for each (w, h)
 *   Phase 2 (F-table)   optimal value for defect-free rectangles
 *   Phase 3 (Fd-table)  optimal value for defect-affected rectangles,
 *                        stored in a sparse slab with uint16 delta encoding
 *
 * Sparse slab design
 * ------------------
 * Only positions (sx, sy) where the rectangle [sx, sx+w) × [sy, sy+h)
 * overlaps at least one defect need explicit storage — all other positions
 * equal F_values[w][h] and are recovered in O(1) without a data lookup.
 *
 * For each (w, h) pair, the affected region is represented as a small set
 * of disjoint rectangular tiles.  Three merge strategies (1D-x, 1D-y, 2D)
 * are evaluated and the one with the best tile-count/data-size tradeoff is
 * selected per problem instance.  All tile data is packed into one flat
 * uint16 array; each stored value is a delta = F[w][h] - Fd(w,h,sx,sy).
 * ============================================================================= */

#include "solver_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>


/* =============================================================================
 * PositionInterval — scratch type used while building tiles for one (w, h).
 *
 * Represents the sheet-position range [x_lo, x_hi] × [y_lo, y_hi] of
 * positions whose rectangle overlaps a single defect.  These are temporary
 * and are allocated once and reused across all (w, h) iterations.
 * ============================================================================= */
typedef struct { int x_lo, x_hi, y_lo, y_hi; } PositionInterval;


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

static int cmp_iv_y(const void *a, const void *b) {
    return ((const PositionInterval *)a)->y_lo - ((const PositionInterval *)b)->y_lo;
}


/* =============================================================================
 * Phase 1 — g-table: best single-item tiling
 *
 * For every rectangle size (rw × rh), find the item type that maximises
 * covered area when tiled in a grid of nx × ny copies, and store the
 * resulting area in g_values[rw][rh].  g_item_index records which item
 * type achieved it, or -1 if no item fits.
 *
 * The outer loop over rw is parallelised: each width is independent.
 * ============================================================================= */
void fill_g_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
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
            g_values[base + rh]     = best_value;
            g_item_index[base + rh] = best_item;
        }
    }
}


/* =============================================================================
 * Phase 2 — F-table: optimal value for defect-free rectangles
 *
 * Bottom-up DP.  For each (rw, rh), the best value is the maximum of:
 *   - Tiling with copies of one item type          (from g_values)
 *   - A vertical guillotine cut at position z      (F[z][rh] + F[rw-z][rh])
 *   - A horizontal guillotine cut at position z    (F[rw][z] + F[rw][rh-z])
 *
 * Only positions in the normal-pattern arrays are tried, and only up to
 * half the dimension (symmetry: cutting at z == cutting at rw-z).
 *
 * Not parallelised: each (rw, rh) depends on all smaller values.
 * ============================================================================= */
void fill_F_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *F_values, int8_t *F_decision_type,
                  int32_t *F_decision_param,
                  int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                  int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts) {

    int col_stride = sheet_height + 1;

    for (int rw = 1; rw <= sheet_width; rw++) {
        int base = rw * col_stride;
        int nxc  = n_normal_cuts_x[rw];
        int xcb  = rw * max_x_cuts;
        int hw   = rw >> 1;

        for (int rh = 1; rh <= sheet_height; rh++) {
            int nyc = n_normal_cuts_y[rh];
            int ycb = rh * max_y_cuts;
            int hh  = rh >> 1;

            int32_t best_item  = g_item_index[base + rh];
            int32_t best_value = g_values[base + rh];
            int8_t  best_type  = (best_item >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (best_item >= 0) ? best_item : 0;

            for (int ci = 0; ci < nxc; ci++) {
                int z = normal_cuts_x[xcb + ci];
                if (z > hw) break;
                int32_t v = F_values[z * col_stride + rh]
                          + F_values[(rw - z) * col_stride + rh];
                if (v > best_value) {
                    best_value = v; best_type = DECISION_CUT_X; best_param = z;
                }
            }
            for (int ci = 0; ci < nyc; ci++) {
                int z = normal_cuts_y[ycb + ci];
                if (z > hh) break;
                int32_t v = F_values[base + z] + F_values[base + (rh - z)];
                if (v > best_value) {
                    best_value = v; best_type = DECISION_CUT_Y; best_param = z;
                }
            }

            F_values[base + rh]         = best_value;
            F_decision_type[base + rh]  = best_type;
            F_decision_param[base + rh] = best_param;
        }
    }
}


/* =============================================================================
 * Tile builder
 *
 * For a rectangle of size rw × rh, compute the set of sheet positions
 * (sx, sy) such that [sx, sx+rw) × [sy, sy+rh) overlaps at least one
 * defect.  The affected region is represented as a small set of disjoint
 * rectangular tiles.
 *
 * For each defect, the affected x-range is:
 *   [defect.x_start - rw + 1,  defect.x_end - 1]  (clamped to [0, sw-rw])
 * and similarly for y.  These per-defect rectangles are then merged
 * according to the chosen strategy to form the final tile list.
 *
 * Three merge strategies:
 *   MERGE_1D_X  sort by x_lo, merge adjacent/overlapping x-ranges
 *               (best when defects are spread horizontally)
 *   MERGE_1D_Y  sort by y_lo, merge adjacent/overlapping y-ranges
 *   MERGE_2D    iteratively merge any two rectangles that overlap in
 *               both x and y (fewest tiles, but O(n²) per merge step)
 *
 * Phase B evaluates all three strategies and picks the best for the
 * current problem instance (see fill_Fd_slab).
 *
 * scratch  caller-provided buffer of at least n_defects PositionIntervals
 * out      caller-provided buffer of at least n_defects Tiles
 * returns  number of tiles written to out[], or 0 if no defect overlaps
 * ============================================================================= */
typedef enum { MERGE_1D_X, MERGE_1D_Y, MERGE_2D } MergeStrategy;

static int build_tiles(int rw, int rh, int sw, int sh,
                       const int32_t *defects, int n_defects,
                       PositionInterval *scratch, Tile *out,
                       MergeStrategy strategy) {
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

    if (strategy == MERGE_1D_X || strategy == MERGE_1D_Y) {
        qsort(scratch, n, sizeof(PositionInterval),
              (strategy == MERGE_1D_X) ? cmp_iv_x : cmp_iv_y);

        int n_tiles = 0;
        PositionInterval cur = scratch[0];
        for (int i = 1; i < n; i++) {
            int adjacent = (strategy == MERGE_1D_X)
                ? (scratch[i].x_lo <= cur.x_hi + 1)
                : (scratch[i].y_lo <= cur.y_hi + 1);
            if (adjacent) {
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

    } else { /* MERGE_2D */
        /* Repeatedly merge any two intervals that overlap in both x and y
         * until no more merges are possible.  O(n²) per pass, but n is
         * small (one per defect) so this is acceptable. */
        int changed = 1;
        while (changed) {
            changed = 0;
            for (int i = 0; i < n && !changed; i++) {
                for (int j = i + 1; j < n && !changed; j++) {
                    int x_overlap = (scratch[i].x_lo <= scratch[j].x_hi) &&
                                    (scratch[j].x_lo <= scratch[i].x_hi);
                    int y_overlap = (scratch[i].y_lo <= scratch[j].y_hi) &&
                                    (scratch[j].y_lo <= scratch[i].y_hi);
                    if (x_overlap && y_overlap) {
                        if (scratch[j].x_lo < scratch[i].x_lo) scratch[i].x_lo = scratch[j].x_lo;
                        if (scratch[j].x_hi > scratch[i].x_hi) scratch[i].x_hi = scratch[j].x_hi;
                        if (scratch[j].y_lo < scratch[i].y_lo) scratch[i].y_lo = scratch[j].y_lo;
                        if (scratch[j].y_hi > scratch[i].y_hi) scratch[i].y_hi = scratch[j].y_hi;
                        scratch[j] = scratch[--n]; /* fill gap with last element */
                        changed = 1;
                    }
                }
            }
        }
        for (int t = 0; t < n; t++) {
            out[t].sheet_x_lo = scratch[t].x_lo;
            out[t].sheet_x_hi = scratch[t].x_hi;
            out[t].sheet_y_lo = scratch[t].y_lo;
            out[t].sheet_y_hi = scratch[t].y_hi;
            out[t].x_span     = scratch[t].x_hi - scratch[t].x_lo + 1;
            out[t].y_span     = scratch[t].y_hi - scratch[t].y_lo + 1;
        }
        return n;
    }
}


/* =============================================================================
 * Strategy evaluator — dry-run for one merge strategy
 *
 * Calls build_tiles() for every (w, h) and accumulates total tile count
 * and total data entries (sum of x_span × y_span across all tiles).
 * Used by Phase B to compare the three merge strategies.
 * ============================================================================= */
static void eval_strategy(int sheet_width, int sheet_height,
                           const int32_t *defects, int n_defects,
                           PositionInterval *scratch, Tile *tile_scratch,
                           MergeStrategy strategy,
                           int *out_total_tiles, int64_t *out_total_data) {
    int     total_tiles = 0;
    int64_t total_data  = 0;

    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {
            int n = build_tiles(w, h, sheet_width, sheet_height,
                                defects, n_defects, scratch, tile_scratch, strategy);
            total_tiles += n;
            for (int t = 0; t < n; t++)
                total_data += (int64_t)tile_scratch[t].x_span * tile_scratch[t].y_span;
        }
    }

    *out_total_tiles = total_tiles;
    *out_total_data  = total_data;
}


/* =============================================================================
 * phase_e_fill — bottom-up DP fill for defect-affected cells
 *
 * For each (w, h) that has tiles, iterates over its tiles and over all
 * sheet positions (sx, sy) within each tile.
 *
 * Pure positions (defect_count_in_rect == 0) are pre-filled with delta = 0
 * (meaning Fd == F) and skipped.  Defect-affected positions try every
 * integer vertical cut z in [1, w-1] and horizontal cut z in [1, h-1].
 *
 * Loop structure:
 *   sx (parallel, dynamic) → classify column → vertical z (outer) /
 *   ly (inner) → horizontal ly (outer) / z (inner) + delta write
 *
 * col_best[] and is_impure[] are per-thread stack allocations (alloca)
 * reused across all sx columns a thread processes.
 * ============================================================================= */
static void phase_e_fill(int sheet_width, int sheet_height,
                          int col_stride,
                          int32_t  *defect_count_prefix,
                          int32_t  *F_values,
                          FdSlab   *slab) {

    for (int w = 1; w <= sheet_width; w++) {
        int wbase = w * col_stride;

        for (int h = 1; h <= sheet_height; h++) {
            int wh_idx = wbase + h;
            if (!slab->has_tiles[wh_idx]) continue;

            TileIndex *ti    = &slab->tile_index[wh_idx];
            int32_t pure_val = F_values[wh_idx];

            for (int t = 0; t < ti->tile_count; t++) {
                Tile *tile = (t == 0)
                    ? &ti->first_tile_inline
                    : &slab->tiles[ti->overflow_start + t - 1];

                int      y_span = tile->y_span;
                uint16_t *tdata = &slab->data[tile->data_offset];

                #pragma omp parallel
                {
                    /* Per-thread scratch — allocated once per parallel
                     * region entry, reused across all sx columns this
                     * thread processes. */
                    int32_t *col_best  = (int32_t *)alloca(y_span * sizeof(int32_t));
                    uint8_t *is_impure = (uint8_t *)alloca(y_span * sizeof(uint8_t));

                    #pragma omp for schedule(dynamic, 1)
                    for (int sx = tile->sheet_x_lo; sx <= tile->sheet_x_hi; sx++) {
                        int lx = sx - tile->sheet_x_lo;

                        /* --- Pass 1: classify each sy as pure or impure --- */
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

                        /* --- Vertical cuts: z outer, ly inner ---
                         *
                         * All integer positions z in [1, w-1] are tried.
                         * Iterating z in the outer loop amortises the
                         * slab_lookup address computation across all ly
                         * values in the column. */
                        for (int z = 1; z < w; z++) {
                            for (int ly = 0; ly < y_span; ly++) {
                                if (!is_impure[ly]) continue;
                                int sy = tile->sheet_y_lo + ly;
                                int32_t v =
                                    slab_lookup(slab, F_values, col_stride,
                                                z,     h, sx,   sy)
                                  + slab_lookup(slab, F_values, col_stride,
                                                w - z, h, sx+z, sy);
                                if (v > col_best[ly]) col_best[ly] = v;
                            }
                        }

                        /* --- Horizontal cuts: ly outer, z inner ---
                         *
                         * All integer positions z in [1, h-1] are tried.
                         * ly is outer so the final delta write is in the
                         * same loop that finishes the horizontal cuts. */
                        for (int ly = 0; ly < y_span; ly++) {
                            if (!is_impure[ly]) continue;
                            int sy = tile->sheet_y_lo + ly;

                            for (int z = 1; z < h; z++) {
                                int32_t v =
                                    slab_lookup(slab, F_values, col_stride,
                                                w,     z,   sx, sy)
                                  + slab_lookup(slab, F_values, col_stride,
                                                w, h - z,   sx, sy+z);
                                if (v > col_best[ly]) col_best[ly] = v;
                            }

                            /* Store delta = F[w][h] - Fd.  delta >= 0 always.
                             * If it exceeds UINT16_MAX the overflow flag is
                             * set; the Python layer will raise rather than
                             * silently return a truncated result. */
                            int32_t delta = pure_val - col_best[ly];
                            if (delta > (int32_t)UINT16_MAX) {
                                #pragma omp atomic write
                                slab->overflow = 1;
                            }
                            tdata[(int64_t)lx * y_span + ly] = (uint16_t)delta;
                        }
                    } /* end omp for sx */
                } /* end omp parallel */
            } /* end tile loop */
        } /* end h loop */
    } /* end w loop */
}


/* =============================================================================
 * Phase 3 — Fd-table: optimal value for defect-affected rectangles
 *
 * Builds the sparse FdSlab and fills it with a bottom-up DP.
 *
 * Phase A  sort a working copy of the defect array by x for consistent
 *          interval merging across all (w, h) iterations
 *
 * Phase B  evaluate all three merge strategies in parallel, pick the best:
 *          - primary criterion: fewest tiles (drives slab_lookup cost)
 *          - constraint: data size must not exceed min_data × 1.20
 *
 * Phase C  build tile geometry and assign data offsets into the flat
 *          data[] array; store tiles in tile_index[] and tiles[]
 *
 * Phase D  allocate flat data[] array (uint16 deltas)
 *
 * Phase E  bottom-up DP fill: for each defect-affected cell (w, h, sx, sy),
 *          try every integer vertical cut z in [1, w-1] and every integer
 *          horizontal cut z in [1, h-1], take the best combined value, and
 *          store  delta = F[w][h] - best_Fd  as uint16.
 *
 *          Evaluating all integers guarantees optimality — the normal-pattern
 *          candidate sets used in the literature are subsets of the integers,
 *          so the full scan is at least as good.
 *
 *          OpenMP parallelises the sx loop within each tile.
 * ============================================================================= */
FdSlab *fill_Fd_slab(int sheet_width, int sheet_height,
                     int32_t *defect_count_prefix, int32_t *F_values,
                     int32_t *defect_array_in, int n_defects) {

    int col_stride = sheet_height + 1;

    /* --- Phase A: sorted working copy of defects --- */
    int32_t *defects = (int32_t *)malloc(n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    memcpy(defects, defect_array_in, n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    if (n_defects > 1)
        qsort(defects, n_defects, sizeof(int32_t) * DEFECT_FIELD_COUNT, cmp_defects_x);

    FdSlab *slab = (FdSlab *)calloc(1, sizeof(FdSlab));
    slab->sheet_width  = sheet_width;
    slab->sheet_height = sheet_height;

    int index_size = (sheet_width + 1) * col_stride;
    slab->tile_index = (TileIndex *)calloc(index_size, sizeof(TileIndex));
    slab->has_tiles  = (uint8_t  *)calloc(index_size, sizeof(uint8_t));

    /* --- Phase B: select best merge strategy --- */
    const char    *strategy_names[] = {"1D-x", "1D-y", "2D"};
    MergeStrategy  strategies[]     = {MERGE_1D_X, MERGE_1D_Y, MERGE_2D};
    int     n_tiles_per_strategy[3];
    int64_t n_data_per_strategy[3];

    /* Each strategy evaluation needs its own scratch buffers (build_tiles
     * mutates them), so allocate three independent sets and run in parallel. */
    PositionInterval *iv_scratch[3];
    Tile             *t_scratch[3];
    for (int s = 0; s < 3; s++) {
        iv_scratch[s] = (PositionInterval *)malloc(n_defects * sizeof(PositionInterval));
        t_scratch[s]  = (Tile *)malloc(n_defects * sizeof(Tile));
    }

    #pragma omp parallel for schedule(static)
    for (int s = 0; s < 3; s++)
        eval_strategy(sheet_width, sheet_height, defects, n_defects,
                      iv_scratch[s], t_scratch[s], strategies[s],
                      &n_tiles_per_strategy[s], &n_data_per_strategy[s]);

    /* Keep only strategy 0's scratch buffers for Phase C; free the rest. */
    free(iv_scratch[1]); free(t_scratch[1]);
    free(iv_scratch[2]); free(t_scratch[2]);

    fprintf(stderr, "Tile strategy comparison:\n");
    for (int s = 0; s < 3; s++)
        fprintf(stderr, "  %-4s  tiles=%d  data=%lld  (%.1f MB)\n",
                strategy_names[s],
                n_tiles_per_strategy[s],
                (long long)n_data_per_strategy[s],
                n_data_per_strategy[s] * 4.0 / 1024.0 / 1024.0);

    /* Pick the strategy with the fewest tiles whose data size is within
     * 20% of the global minimum.  Fewest tiles minimises slab_lookup cost
     * since it reduces the per-lookup tile scan.  The 20% tolerance
     * prevents choosing a strategy with many tiles just to save a little
     * memory. */
    int64_t min_data = n_data_per_strategy[0];
    for (int s = 1; s < 3; s++)
        if (n_data_per_strategy[s] < min_data) min_data = n_data_per_strategy[s];

    int best = -1;
    for (int s = 0; s < 3; s++) {
        if (n_data_per_strategy[s] > (int64_t)(min_data * 1.20)) continue;
        if (best == -1 || n_tiles_per_strategy[s] < n_tiles_per_strategy[best])
            best = s;
    }
    if (best == -1) { /* all strategies exceed tolerance — fall back to min data */
        best = 0;
        for (int s = 1; s < 3; s++)
            if (n_data_per_strategy[s] < n_data_per_strategy[best]) best = s;
    }

    fprintf(stderr, "  -> selected: %s\n", strategy_names[best]);
    MergeStrategy chosen = strategies[best];

    /* --- Phase C: tile geometry and data offsets ---
     *
     * For each (w, h) that has at least one tile:
     *   - build tile geometry with build_tiles() and store it in
     *     tile_index[] and tiles[]
     *   - assign each tile's data_offset (its slice of the future data[]) */
    slab->tiles = (Tile *)calloc(n_tiles_per_strategy[best], sizeof(Tile));

    int     overflow_cursor = 0; /* next free slot in slab->tiles[]  */
    int64_t data_total      = 0; /* total elements needed in data[]  */

    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {
            int wh_idx  = w * col_stride + h;
            int n_tiles = build_tiles(w, h, sheet_width, sheet_height,
                                      defects, n_defects,
                                      iv_scratch[0], t_scratch[0], chosen);
            if (n_tiles == 0) continue;

            slab->has_tiles[wh_idx] = 1;
            TileIndex *ti           = &slab->tile_index[wh_idx];
            ti->tile_count          = n_tiles;

            for (int t = 0; t < n_tiles; t++) {
                t_scratch[0][t].data_offset = data_total;
                data_total += (int64_t)t_scratch[0][t].x_span * t_scratch[0][t].y_span;
            }

            /* Store tile 0 inline in the TileIndex; remaining tiles go
             * into the overflow array slab->tiles[]. */
            ti->first_tile_inline = t_scratch[0][0];
            if (n_tiles > 1) {
                ti->overflow_start = overflow_cursor;
                for (int t = 1; t < n_tiles; t++)
                    slab->tiles[overflow_cursor++] = t_scratch[0][t];
            }
        }
    }
    slab->total_tile_count   = overflow_cursor;
    slab->total_data_entries = data_total;
    slab->overflow           = 0;

    /* --- Phase D: allocate delta array --- */
    slab->data = (uint16_t *)malloc(data_total * sizeof(uint16_t));

    /* --- Phase E --- */
    phase_e_fill(sheet_width, sheet_height, col_stride,
                 defect_count_prefix, F_values, slab);

    free(defects);
    free(iv_scratch[0]);
    free(t_scratch[0]);
    return slab;
}


/* =============================================================================
 * estimate_slab — dry run: report tile and data counts for all strategies
 *
 * Evaluates all three merge strategies without building the actual slab.
 * Used by the Python layer to preview memory usage before committing.
 * ============================================================================= */
SlabEstimate estimate_slab(int sheet_width, int sheet_height,
                           int32_t *defect_array_in, int n_defects) {

    int32_t *defects = (int32_t *)malloc(
        n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    memcpy(defects, defect_array_in,
           n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    if (n_defects > 1)
        qsort(defects, n_defects,
              sizeof(int32_t) * DEFECT_FIELD_COUNT, cmp_defects_x);

    PositionInterval *iv = (PositionInterval *)malloc(
        n_defects * sizeof(PositionInterval));
    Tile *ts = (Tile *)malloc(n_defects * sizeof(Tile));

    SlabEstimate est;
    eval_strategy(sheet_width, sheet_height, defects, n_defects, iv, ts,
                  MERGE_1D_X, &est.tiles_1dx, &est.data_1dx);
    eval_strategy(sheet_width, sheet_height, defects, n_defects, iv, ts,
                  MERGE_1D_Y, &est.tiles_1dy, &est.data_1dy);
    eval_strategy(sheet_width, sheet_height, defects, n_defects, iv, ts,
                  MERGE_2D,   &est.tiles_2d,  &est.data_2d);

    free(defects);
    free(iv);
    free(ts);
    return est;
}