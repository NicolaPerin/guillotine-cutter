/* =============================================================================
 * solver_core.c — Guillotine cutting-stock solver: core algorithm
 *
 * Implements the three DP phases declared in solver_core.h.
 * This file has no Python dependency — it can be compiled standalone
 * for testing, benchmarking, or linking into other language bindings.
 * ============================================================================= */

#include "solver_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* =============================================================================
 * PositionInterval — scratch struct used while building tiles for one (w, h).
 *
 * Represents the x-range [x_lo, x_hi] of sheet positions affected by one
 * defect, together with the corresponding y-range [y_lo, y_hi].
 * These are temporary — allocated on the heap once and reused for every
 * (w, h) iteration.
 * ============================================================================= */
typedef struct {
    int x_lo, x_hi;
    int y_lo, y_hi;
} PositionInterval;


/* =============================================================================
 * qsort comparators
 * ============================================================================= */

static int compare_defects_by_x(const void *a, const void *b) {
    int32_t xa = *(const int32_t *)a;
    int32_t xb = *(const int32_t *)b;
    return (xa > xb) - (xa < xb);
}

static int compare_intervals_by_x_lo(const void *a, const void *b) {
    return ((const PositionInterval *)a)->x_lo - ((const PositionInterval *)b)->x_lo;
}

static int compare_intervals_by_y_lo(const void *a, const void *b) {
    return ((const PositionInterval *)a)->y_lo - ((const PositionInterval *)b)->y_lo;
}


/* =============================================================================
 * Phase 1 — g-table: best single-item tiling
 *
 * For every rectangle size (rect_width × rect_height), compute:
 *   g_values[rect_width][rect_height]     = max area coverable by tiling with
 *                                           nx × ny copies of one item type
 *   g_item_index[rect_width][rect_height] = which item type achieves that,
 *                                           or -1 if no item fits at all
 *
 * Both output arrays are (sheet_width+1) × (sheet_height+1) in row-major order.
 *
 * The outer loop over rect_width is parallelized with OpenMP since each
 * width is independent — no data dependencies between different widths.
 * ============================================================================= */
void fill_g_table(int sheet_width, int sheet_height,
                  int32_t *g_values,
                  int32_t *g_item_index,
                  int32_t *item_widths,
                  int32_t *item_heights,
                  int32_t *item_areas,
                  int      n_items) {

    int col_stride = sheet_height + 1;

    #pragma omp parallel for schedule(dynamic)
    for (int rect_width = 1; rect_width <= sheet_width; rect_width++) {

        int row_base = rect_width * col_stride;

        for (int rect_height = 1; rect_height <= sheet_height; rect_height++) {

            int32_t best_value = 0;
            int32_t best_item  = -1;

            for (int item = 0; item < n_items; item++) {
                int copies_x = rect_width  / item_widths[item];
                int copies_y = rect_height / item_heights[item];

                if (copies_x > 0 && copies_y > 0) {
                    int32_t covered_area = item_areas[item] * copies_x * copies_y;
                    if (covered_area > best_value) {
                        best_value = covered_area;
                        best_item  = item;
                    }
                }
            }

            g_values    [row_base + rect_height] = best_value;
            g_item_index[row_base + rect_height] = best_item;
        }
    }
}


/* =============================================================================
 * Phase 2 — F-table: optimal value for pure (defect-free) rectangles
 *
 * Bottom-up DP. For each (rect_width, rect_height), the optimal decision is
 * the best among:
 *   - Tiling with copies of one item type         (from g_values)
 *   - A vertical guillotine cut at position z      (F[z][h] + F[w-z][h])
 *   - A horizontal guillotine cut at position z    (F[w][z] + F[w][h-z])
 *
 * Only cut positions listed in the normal-pattern arrays are tried,
 * and only up to half the dimension (symmetry: cutting at z is equivalent
 * to cutting at w-z, so we only try z <= w/2).
 *
 * This phase is NOT parallelized because each (w, h) depends on all
 * smaller (w', h') values — strict bottom-up ordering is required.
 *
 * Output arrays F_values, F_decision_type, F_decision_param are all
 * (sheet_width+1) × (sheet_height+1) in row-major order.
 * ============================================================================= */
void fill_F_table(int sheet_width, int sheet_height,
                  int32_t *g_values,
                  int32_t *g_item_index,
                  int32_t *F_values,
                  int8_t  *F_decision_type,
                  int32_t *F_decision_param,
                  int32_t *normal_cuts_x,
                  int32_t *n_normal_cuts_x,
                  int      max_x_cuts,
                  int32_t *normal_cuts_y,
                  int32_t *n_normal_cuts_y,
                  int      max_y_cuts) {

    int col_stride = sheet_height + 1;

    for (int rect_width = 1; rect_width <= sheet_width; rect_width++) {

        int row_base   = rect_width * col_stride;
        int n_x_cuts   = n_normal_cuts_x[rect_width];
        int x_cut_base = rect_width * max_x_cuts;
        int half_width = rect_width >> 1;

        for (int rect_height = 1; rect_height <= sheet_height; rect_height++) {

            int n_y_cuts    = n_normal_cuts_y[rect_height];
            int y_cut_base  = rect_height * max_y_cuts;
            int half_height = rect_height >> 1;

            /* Start with the best single-item tiling. */
            int32_t best_item  = g_item_index[row_base + rect_height];
            int32_t best_value = g_values    [row_base + rect_height];
            int8_t  best_type  = (best_item >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (best_item >= 0) ? best_item : 0;

            /* Try vertical cuts — symmetry: only cut_pos <= half_width. */
            for (int ci = 0; ci < n_x_cuts; ci++) {
                int cut_pos = normal_cuts_x[x_cut_base + ci];
                if (cut_pos > half_width) break;
                int32_t combined = F_values[cut_pos * col_stride + rect_height]
                                 + F_values[(rect_width - cut_pos) * col_stride + rect_height];
                if (combined > best_value) {
                    best_value = combined;
                    best_type  = DECISION_CUT_X;
                    best_param = cut_pos;
                }
            }

            /* Try horizontal cuts — symmetry: only cut_pos <= half_height. */
            for (int ci = 0; ci < n_y_cuts; ci++) {
                int cut_pos = normal_cuts_y[y_cut_base + ci];
                if (cut_pos > half_height) break;
                int32_t combined = F_values[row_base + cut_pos]
                                 + F_values[row_base + (rect_height - cut_pos)];
                if (combined > best_value) {
                    best_value = combined;
                    best_type  = DECISION_CUT_Y;
                    best_param = cut_pos;
                }
            }

            F_values        [row_base + rect_height] = best_value;
            F_decision_type [row_base + rect_height] = best_type;
            F_decision_param[row_base + rect_height] = best_param;
        }
    }
}


/* =============================================================================
 * Tile builder — merge defect-affected intervals for one (rect_width, rect_height)
 *
 * For each defect, the set of sheet positions (x, y) such that the rectangle
 * [x, x+rect_width) × [y, y+rect_height) overlaps that defect is:
 *   x ∈ [defect.x_start - rect_width  + 1,  defect.x_end - 1]
 *   y ∈ [defect.y_start - rect_height + 1,  defect.y_end - 1]
 * both clamped to the valid sheet range [0, sheet_dim - rect_dim].
 *
 * The scratch arrays (scratch_intervals, out_tiles) are caller-provided
 * and reused across all (w, h) iterations to avoid repeated allocation.
 *
 * Returns the number of tiles written to out_tiles[].
 * ============================================================================= */
typedef enum {
    MERGE_1D_X,
    MERGE_1D_Y,
    MERGE_2D,
} MergeStrategy;


static int build_tiles_for_rect_size(int rect_width, int rect_height,
                                     int sheet_width, int sheet_height,
                                     const int32_t *defect_array, int n_defects,
                                     PositionInterval *scratch_intervals,
                                     Tile *out_tiles,
                                     MergeStrategy strategy) {

    int max_x_pos = sheet_width  - rect_width;
    int max_y_pos = sheet_height - rect_height;

    int n_intervals = 0;
    for (int d = 0; d < n_defects; d++) {
        DefectRef def = defect_at(defect_array, d);

        int x_lo = defect_x_start(def) - rect_width  + 1;
        int x_hi = defect_x_end(def)   - 1;
        int y_lo = defect_y_start(def) - rect_height + 1;
        int y_hi = defect_y_end(def)   - 1;

        if (x_lo < 0)         x_lo = 0;
        if (x_hi > max_x_pos) x_hi = max_x_pos;
        if (y_lo < 0)         y_lo = 0;
        if (y_hi > max_y_pos) y_hi = max_y_pos;

        if (x_lo <= x_hi && y_lo <= y_hi) {
            scratch_intervals[n_intervals].x_lo = x_lo;
            scratch_intervals[n_intervals].x_hi = x_hi;
            scratch_intervals[n_intervals].y_lo = y_lo;
            scratch_intervals[n_intervals].y_hi = y_hi;
            n_intervals++;
        }
    }

    if (n_intervals == 0) return 0;

    if (strategy == MERGE_1D_X || strategy == MERGE_1D_Y) {

        if (strategy == MERGE_1D_X)
            qsort(scratch_intervals, n_intervals, sizeof(PositionInterval),
                  compare_intervals_by_x_lo);
        else
            qsort(scratch_intervals, n_intervals, sizeof(PositionInterval),
                  compare_intervals_by_y_lo);

        int n_tiles = 0;
        PositionInterval current = scratch_intervals[0];

        for (int i = 1; i < n_intervals; i++) {
            PositionInterval *next = &scratch_intervals[i];

            int primary_merge = (strategy == MERGE_1D_X)
                ? (next->x_lo <= current.x_hi + 1)
                : (next->y_lo <= current.y_hi + 1);

            if (primary_merge) {
                if (next->x_hi > current.x_hi) current.x_hi = next->x_hi;
                if (next->y_hi > current.y_hi) current.y_hi = next->y_hi;
                if (next->x_lo < current.x_lo) current.x_lo = next->x_lo;
                if (next->y_lo < current.y_lo) current.y_lo = next->y_lo;
            } else {
                out_tiles[n_tiles].sheet_x_lo = current.x_lo;
                out_tiles[n_tiles].sheet_x_hi = current.x_hi;
                out_tiles[n_tiles].sheet_y_lo = current.y_lo;
                out_tiles[n_tiles].sheet_y_hi = current.y_hi;
                out_tiles[n_tiles].x_span = current.x_hi - current.x_lo + 1;
                out_tiles[n_tiles].y_span = current.y_hi - current.y_lo + 1;
                n_tiles++;
                current = *next;
            }
        }
        out_tiles[n_tiles].sheet_x_lo = current.x_lo;
        out_tiles[n_tiles].sheet_x_hi = current.x_hi;
        out_tiles[n_tiles].sheet_y_lo = current.y_lo;
        out_tiles[n_tiles].sheet_y_hi = current.y_hi;
        out_tiles[n_tiles].x_span = current.x_hi - current.x_lo + 1;
        out_tiles[n_tiles].y_span = current.y_hi - current.y_lo + 1;
        return n_tiles + 1;

    } else {  /* MERGE_2D */

        int changed = 1;
        while (changed) {
            changed = 0;
            for (int i = 0; i < n_intervals && !changed; i++) {
                for (int j = i + 1; j < n_intervals && !changed; j++) {
                    int x_overlap = (scratch_intervals[i].x_lo <= scratch_intervals[j].x_hi) &&
                                    (scratch_intervals[j].x_lo <= scratch_intervals[i].x_hi);
                    int y_overlap = (scratch_intervals[i].y_lo <= scratch_intervals[j].y_hi) &&
                                    (scratch_intervals[j].y_lo <= scratch_intervals[i].y_hi);
                    if (x_overlap && y_overlap) {
                        if (scratch_intervals[j].x_lo < scratch_intervals[i].x_lo)
                            scratch_intervals[i].x_lo = scratch_intervals[j].x_lo;
                        if (scratch_intervals[j].x_hi > scratch_intervals[i].x_hi)
                            scratch_intervals[i].x_hi = scratch_intervals[j].x_hi;
                        if (scratch_intervals[j].y_lo < scratch_intervals[i].y_lo)
                            scratch_intervals[i].y_lo = scratch_intervals[j].y_lo;
                        if (scratch_intervals[j].y_hi > scratch_intervals[i].y_hi)
                            scratch_intervals[i].y_hi = scratch_intervals[j].y_hi;
                        scratch_intervals[j] = scratch_intervals[--n_intervals];
                        changed = 1;
                    }
                }
            }
        }

        for (int t = 0; t < n_intervals; t++) {
            out_tiles[t].sheet_x_lo = scratch_intervals[t].x_lo;
            out_tiles[t].sheet_x_hi = scratch_intervals[t].x_hi;
            out_tiles[t].sheet_y_lo = scratch_intervals[t].y_lo;
            out_tiles[t].sheet_y_hi = scratch_intervals[t].y_hi;
            out_tiles[t].x_span     = scratch_intervals[t].x_hi - scratch_intervals[t].x_lo + 1;
            out_tiles[t].y_span     = scratch_intervals[t].y_hi - scratch_intervals[t].y_lo + 1;
        }
        return n_intervals;
    }
}


/* =============================================================================
 * Strategy evaluator — dry run for one strategy, returns tiles and data count
 * ============================================================================= */
static void evaluate_strategy(int sheet_width, int sheet_height,
                               const int32_t *defects, int n_defects,
                               PositionInterval *interval_scratch,
                               Tile *tile_scratch,
                               MergeStrategy strategy,
                               int *out_total_tiles,
                               int64_t *out_total_data) {
    int     total_tiles = 0;
    int64_t total_data  = 0;

    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {
            int n = build_tiles_for_rect_size(w, h, sheet_width, sheet_height,
                                              defects, n_defects,
                                              interval_scratch, tile_scratch,
                                              strategy);
            total_tiles += n;
            for (int t = 0; t < n; t++)
                total_data += (int64_t)tile_scratch[t].x_span * tile_scratch[t].y_span;
        }
    }

    *out_total_tiles = total_tiles;
    *out_total_data  = total_data;
}


/* =============================================================================
 * Phase 3 — Fd-table: optimal value for defected rectangles
 *
 * Builds the sparse FdSlab and fills it with a bottom-up DP. The slab stores
 * uint16 deltas (F[w][h] - Fd) rather than raw Fd values, halving the
 * memory footprint of the data[] array.
 *
 * Phases:
 *   A. Sort defects by x for consistent interval merging.
 *   B. Evaluate all three merge strategies in parallel, pick the best.
 *   C. Allocate tiles[], compute per-tile data offsets, build local defect
 *      lists for each tile.
 *   D. Allocate flat data[] array (uint16).
 *   E. For each (w, h) bottom-up, iterate over tiles:
 *        - Pure positions (prefix-sum check) are pre-filled with delta 0.
 *        - Defected positions use Extended Normal Patterns (Zhang et al. 2023):
 *          Minkowski sum of reference points {0, defect edges} and normal cuts.
 *          The computed Fd value is converted to delta = pure_val - Fd and
 *          stored. An overflow flag in the slab is raised if delta exceeds
 *          UINT16_MAX so callers can detect and recover.
 *
 * OpenMP parallelizes the (sx, sy) loop within each tile.
 * ============================================================================= */
FdSlab *fill_Fd_slab(int sheet_width, int sheet_height,
                     int32_t *defect_count_prefix,
                     int32_t *F_values,
                     int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                     int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts,
                     int32_t *defect_array_in, int n_defects) {

    int col_stride = sheet_height + 1;

    /* --- Phase A: work on a sorted copy of the defect array --- */
    int32_t *defects = (int32_t *)malloc(n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    memcpy(defects, defect_array_in, n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    if (n_defects > 1)
        qsort(defects, n_defects, sizeof(int32_t) * DEFECT_FIELD_COUNT, compare_defects_by_x);

    FdSlab *slab = (FdSlab *)calloc(1, sizeof(FdSlab));
    slab->sheet_width  = sheet_width;
    slab->sheet_height = sheet_height;
    slab->tile_index   = (TileIndex *)calloc((sheet_width + 1) * col_stride, sizeof(TileIndex));

    /* --- Phase B: evaluate all 3 strategies, pick best ---
     *
     * Each strategy evaluation needs its own scratch buffers since
     * build_tiles_for_rect_size mutates them. We allocate 3 independent
     * sets and run them concurrently with OpenMP. */
    const char *strategy_names[] = {"1D-x", "1D-y", "2D"};
    MergeStrategy strategies[]   = {MERGE_1D_X, MERGE_1D_Y, MERGE_2D};
    int     strat_tiles[3];
    int64_t strat_data [3];

    PositionInterval *interval_scratches[3];
    Tile             *tile_scratches[3];

    for (int s = 0; s < 3; s++) {
        interval_scratches[s] = (PositionInterval *)malloc(n_defects * sizeof(PositionInterval));
        tile_scratches[s]     = (Tile *)malloc(n_defects * sizeof(Tile));
    }

    #pragma omp parallel for schedule(static)
    for (int s = 0; s < 3; s++)
        evaluate_strategy(sheet_width, sheet_height, defects, n_defects,
                          interval_scratches[s], tile_scratches[s], strategies[s],
                          &strat_tiles[s], &strat_data[s]);

    /* Free scratch sets we won't reuse (keep set 0 for Phase C). */
    free(interval_scratches[1]); free(tile_scratches[1]);
    free(interval_scratches[2]); free(tile_scratches[2]);

    PositionInterval *interval_scratch = interval_scratches[0];
    Tile             *tile_scratch     = tile_scratches[0];

    fprintf(stderr, "Tile strategy comparison:\n");
    for (int s = 0; s < 3; s++)
        fprintf(stderr, "  %-4s  tiles=%d  data=%lld  (%.1f MB)\n",
                strategy_names[s],
                strat_tiles[s],
                (long long)strat_data[s],
                strat_data[s] * 4.0 / 1024.0 / 1024.0);

    /* Pick best strategy:
     * Primary criterion: fewest tiles (drives slab_lookup cost).
     * Constraint: data must not exceed min_data * memory_tolerance. */
    int64_t min_data = strat_data[0];
    for (int s = 1; s < 3; s++)
        if (strat_data[s] < min_data) min_data = strat_data[s];

    const double memory_tolerance = 1.20;

    int best = -1;
    for (int s = 0; s < 3; s++) {
        if (strat_data[s] > (int64_t)(min_data * memory_tolerance))
            continue;
        if (best == -1 || strat_tiles[s] < strat_tiles[best])
            best = s;
    }
    if (best == -1) {
        best = 0;
        for (int s = 1; s < 3; s++)
            if (strat_data[s] < strat_data[best]) best = s;
    }

    fprintf(stderr, "  -> selected: %s\n", strategy_names[best]);
    MergeStrategy chosen = strategies[best];

    int total_tiles = strat_tiles[best];

    /* --- Phase C: allocate tiles, assign offsets, build local defect lists --- */
    slab->tiles            = (Tile *)calloc(total_tiles, sizeof(Tile));
    slab->total_tile_count = total_tiles;
    int     tile_cursor = 0;
    int64_t data_total  = 0;

    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {

            TileIndex *ti = &slab->tile_index[w * col_stride + h];
            ti->first_tile = tile_cursor;

            int n_tiles = build_tiles_for_rect_size(w, h, sheet_width, sheet_height,
                                                    defects, n_defects,
                                                    interval_scratch, tile_scratch,
                                                    chosen);

            for (int t = 0; t < n_tiles; t++) {
                tile_scratch[t].data_offset = data_total;
                data_total += (int64_t)tile_scratch[t].x_span * tile_scratch[t].y_span;

                int *local_list  = (int *)malloc(n_defects * sizeof(int));
                int  local_count = 0;

                for (int d = 0; d < n_defects; d++) {
                    DefectRef def = defect_at(defects, d);
                    int def_x_lo = defect_x_start(def) - w + 1;
                    int def_x_hi = defect_x_end(def)   - 1;
                    int def_y_lo = defect_y_start(def) - h + 1;
                    int def_y_hi = defect_y_end(def)   - 1;

                    int overlaps_x = !(tile_scratch[t].sheet_x_hi < def_x_lo ||
                                       tile_scratch[t].sheet_x_lo > def_x_hi);
                    int overlaps_y = !(tile_scratch[t].sheet_y_hi < def_y_lo ||
                                       tile_scratch[t].sheet_y_lo > def_y_hi);

                    if (overlaps_x && overlaps_y)
                        local_list[local_count++] = d;
                }

                if (local_count > 0) {
                    tile_scratch[t].local_defect_indices =
                        (int *)realloc(local_list, local_count * sizeof(int));
                    tile_scratch[t].n_local_defects = local_count;
                } else {
                    free(local_list);
                    tile_scratch[t].local_defect_indices = NULL;
                    tile_scratch[t].n_local_defects      = 0;
                }

                slab->tiles[tile_cursor++] = tile_scratch[t];
            }

            ti->tile_count = n_tiles;
        }
    }

    slab->total_data_entries = data_total;
    slab->overflow           = 0;

    /* --- Phase D: allocate flat data array (uint16 deltas) --- */
    slab->data = (uint16_t *)malloc(data_total * sizeof(uint16_t));

    /* --- Phase E: bottom-up DP fill ---
     *
     * For each (w, h) in bottom-up order, iterate over tiles.
     * OpenMP parallelizes (sx, sy) within each tile.
     *
     * Cut candidates use Extended Normal Patterns (Zhang et al. 2023):
     * For each reference point rp in {0} ∪ {relative defect edges},
     * try cuts at rp + z for every normal-pattern position z.
     *   rp=0          → plain normal cuts
     *   rp=edge, z=0  → plain defect-edge cuts
     *   rp=edge, z>0  → extended cuts needed for optimality
     *
     * Boolean masks deduplicate positions so each cut is evaluated once.
     * Stack arrays sized to sheet dimensions are safe for all practical
     * sheet sizes and compatible with OpenMP.
     */
    for (int w = 1; w <= sheet_width; w++) {

        int n_x_cuts   = n_normal_cuts_x[w];
        int x_cut_base = w * max_x_cuts;
        int wh_base    = w * col_stride;

        for (int h = 1; h <= sheet_height; h++) {

            int n_y_cuts   = n_normal_cuts_y[h];
            int y_cut_base = h * max_y_cuts;

            TileIndex *ti       = &slab->tile_index[wh_base + h];
            int32_t    pure_val = F_values[wh_base + h];

            for (int t = 0; t < ti->tile_count; t++) {
                Tile     *tile      = &slab->tiles[ti->first_tile + t];
                uint16_t *tile_data = &slab->data[tile->data_offset];

                #pragma omp parallel for schedule(dynamic, 16) collapse(2)
                for (int sx = tile->sheet_x_lo; sx <= tile->sheet_x_hi; sx++) {
                    for (int sy = tile->sheet_y_lo; sy <= tile->sheet_y_hi; sy++) {

                        /* O(1) purity check via 2D prefix sum.
                         * Pure cell → Fd == pure_val → delta == 0. */
                        int32_t n_defects_here = defect_count_in_rect(
                            defect_count_prefix, col_stride,
                            sx, sy, sx + w, sy + h);

                        if (n_defects_here == 0) {
                            int local_x = sx - tile->sheet_x_lo;
                            int local_y = sy - tile->sheet_y_lo;
                            tile_data[(int64_t)local_x * tile->y_span + local_y] = 0;
                            continue;
                        }

                        int32_t best_value = 0;

                        uint8_t x_mask[sheet_width + 1];
                        uint8_t y_mask[sheet_height + 1];
                        memset(x_mask, 0, (w + 1) * sizeof(uint8_t));
                        memset(y_mask, 0, (h + 1) * sizeof(uint8_t));

                        int rp_x[2 * tile->n_local_defects + 2];
                        int rp_y[2 * tile->n_local_defects + 2];
                        int n_rp_x = 0, n_rp_y = 0;

                        rp_x[n_rp_x++] = 0;
                        rp_y[n_rp_y++] = 0;

                        for (int li = 0; li < tile->n_local_defects; li++) {
                            DefectRef def = defect_at(defects,
                                tile->local_defect_indices[li]);
                            int left  = defect_x_start(def) - sx;
                            int right = defect_x_end(def)   - sx;
                            int bot   = defect_y_start(def) - sy;
                            int top   = defect_y_end(def)   - sy;
                            if (left  >= 0 && left  <= w) rp_x[n_rp_x++] = left;
                            if (right >= 0 && right <= w) rp_x[n_rp_x++] = right;
                            if (bot   >= 0 && bot   <= h) rp_y[n_rp_y++] = bot;
                            if (top   >= 0 && top   <= h) rp_y[n_rp_y++] = top;
                        }

                        for (int ri = 0; ri < n_rp_x; ri++) {
                            int rp = rp_x[ri];
                            if (rp > 0 && rp < w) x_mask[rp] = 1;
                            for (int ci = 0; ci < n_x_cuts; ci++) {
                                int z = rp + normal_cuts_x[x_cut_base + ci];
                                if (z > 0 && z < w) x_mask[z] = 1;
                            }
                        }

                        for (int ri = 0; ri < n_rp_y; ri++) {
                            int rp = rp_y[ri];
                            if (rp > 0 && rp < h) y_mask[rp] = 1;
                            for (int ci = 0; ci < n_y_cuts; ci++) {
                                int z = rp + normal_cuts_y[y_cut_base + ci];
                                if (z > 0 && z < h) y_mask[z] = 1;
                            }
                        }

                        for (int z = 1; z < w; z++) {
                            if (!x_mask[z]) continue;
                            int32_t combined =
                                slab_lookup(slab, F_values, col_stride,
                                            z,     h, sx,     sy)
                              + slab_lookup(slab, F_values, col_stride,
                                            w - z, h, sx + z, sy);
                            if (combined > best_value) best_value = combined;
                        }

                        for (int z = 1; z < h; z++) {
                            if (!y_mask[z]) continue;
                            int32_t combined =
                                slab_lookup(slab, F_values, col_stride,
                                            w, z,     sx, sy)
                              + slab_lookup(slab, F_values, col_stride,
                                            w, h - z, sx, sy + z);
                            if (combined > best_value) best_value = combined;
                        }

                        int local_x = sx - tile->sheet_x_lo;
                        int local_y = sy - tile->sheet_y_lo;

                        /* Store delta = pure_val - best_value.
                         * delta >= 0 always (Fd <= F).
                         * delta may exceed UINT16_MAX for problems with very
                         * large F values (total items area > 65535). We set a
                         * flag; Python will raise rather than silently return
                         * a truncated, incorrect result. */
                        int32_t delta = pure_val - best_value;
                        if (delta > (int32_t)UINT16_MAX) {
                            #pragma omp atomic write
                            slab->overflow = 1;
                        }
                        tile_data[(int64_t)local_x * tile->y_span + local_y] =
                            (uint16_t)delta;
                    }
                }
            }
        }
    }

    free(defects);
    free(interval_scratch);
    free(tile_scratch);
    return slab;
}


SlabEstimate estimate_slab(int sheet_width, int sheet_height,
                           int32_t *defect_array_in, int n_defects) {

    int32_t *defects = (int32_t *)malloc(
        n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    memcpy(defects, defect_array_in,
           n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    if (n_defects > 1)
        qsort(defects, n_defects,
              sizeof(int32_t) * DEFECT_FIELD_COUNT, compare_defects_by_x);

    PositionInterval *interval_scratch =
        (PositionInterval *)malloc(n_defects * sizeof(PositionInterval));
    Tile *tile_scratch =
        (Tile *)malloc(n_defects * sizeof(Tile));

    SlabEstimate est;
    evaluate_strategy(sheet_width, sheet_height, defects, n_defects,
                      interval_scratch, tile_scratch, MERGE_1D_X,
                      &est.tiles_1dx, &est.data_1dx);
    evaluate_strategy(sheet_width, sheet_height, defects, n_defects,
                      interval_scratch, tile_scratch, MERGE_1D_Y,
                      &est.tiles_1dy, &est.data_1dy);
    evaluate_strategy(sheet_width, sheet_height, defects, n_defects,
                      interval_scratch, tile_scratch, MERGE_2D,
                      &est.tiles_2d, &est.data_2d);

    free(defects);
    free(interval_scratch);
    free(tile_scratch);
    return est;
}