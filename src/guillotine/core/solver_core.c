/* =============================================================================
 * solver_core.c — Guillotine cutting-stock solver: core algorithm
 *
 * Implements the three DP phases declared in solver_core.h.
 * This file has NO Python dependency — it can be compiled standalone
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

/* Sort defect records by their x_start field (first field in each record).
 * Used to ensure defects are processed left-to-right for consistent
 * interval merging in the tile builder. */
static int compare_defects_by_x(const void *a, const void *b) {
    int32_t xa = *(const int32_t *)a;
    int32_t xb = *(const int32_t *)b;
    return (xa > xb) - (xa < xb);
}

/* Sort PositionIntervals by x_lo for the sweep-line merge.
 * After sorting, we can merge overlapping intervals in a single left-to-right pass. */
static int compare_intervals_by_x_lo(const void *a, const void *b) {
    return ((const PositionInterval *)a)->x_lo - ((const PositionInterval *)b)->x_lo;
}

/* Same for y */
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
 * We sort these intervals by x_lo and sweep left-to-right, merging any
 * overlapping or adjacent x-ranges while unioning their y-ranges.
 * Each merged group becomes one Tile.
 *
 * The scratch arrays (scratch_intervals, out_tiles) are caller-provided
 * and reused across all (w, h) iterations to avoid repeated allocation.
 *
 * Returns the number of tiles written to out_tiles[].
 * ============================================================================= */
/* =============================================================================
 * Merge strategy selector
 * ============================================================================= */
typedef enum {
    MERGE_1D_X,   /* original: merge if x-ranges overlap/adjacent, union y */
    MERGE_1D_Y,   /* symmetric: merge if y-ranges overlap/adjacent, union x */
    MERGE_2D,     /* merge only if both x and y genuinely overlap            */
} MergeStrategy;


/* =============================================================================
 * Tile builder — now parameterised by MergeStrategy
 * ============================================================================= */
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

        /* Sort by the primary sweep axis. */
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

        /* Pairwise merge: only merge if both x and y genuinely overlap.
         * Repeated until stable. */
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
 * Builds the sparse FdSlab and fills it with a bottom-up DP.
 *
 * Phases:
 *   A. Sort defects by x for consistent interval merging.
 *   B. Count total tiles (dry-run of tile builder across all (w, h)).
 *   C. Allocate tiles[], compute per-tile data offsets, build local defect
 *      lists for each tile.
 *   D. Allocate flat data[] array.
 *   E. For each (w, h) bottom-up, iterate over tiles:
 *        - Pure positions (prefix-sum check) are pre-filled with F_values[w][h].
 *        - Defected positions try all cut candidates and store the best value.
 *
 * Cut candidates for defected positions:
 *   - Normal-pattern vertical cuts:   split w at each valid position z,
 *     producing children (z, h) at (sx, sy) and (w-z, h) at (sx+z, sy)
 *   - Normal-pattern horizontal cuts: split h at each valid position z,
 *     producing children (w, z) at (sx, sy) and (w, h-z) at (sx, sy+z)
 *   - Defect-aligned vertical cuts:   split at defect_x_start(d)-sx and
 *     defect_x_end(d)-sx for each local defect d — these isolate the defect
 *     so it can be discarded
 *   - Defect-aligned horizontal cuts: similarly for y
 *
 * Each child lookup uses slab_lookup(), which returns the tile value if the
 * child is defected, or F_values[w'][h'] if it is pure. Correctness relies
 * on bottom-up order — all smaller (w, h) are fully resolved before larger.
 *
 * OpenMP parallelizes the (sx, sy) loop within each tile. The schedule
 * is dynamic because defected positions (which do real work) and pure
 * positions (which just pre-fill and skip) are interleaved unpredictably.
 *
 * Note: build_tiles_for_rect_size is called twice per (w, h) — once to count
 * (Phase B), once to emit (Phase C). This avoids storing intermediate results
 * at the cost of repeating the cheap interval merge.
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

    /* Scratch buffers reused across all (w, h) iterations. */
    PositionInterval *interval_scratch = (PositionInterval *)malloc(n_defects * sizeof(PositionInterval));
    Tile             *tile_scratch     = (Tile *)malloc(n_defects * sizeof(Tile));

    /* --- Phase B: evaluate all strategies, pick best by tile count --- */
    const char *strategy_names[] = {"1D-x", "1D-y", "2D"};
    MergeStrategy strategies[]   = {MERGE_1D_X, MERGE_1D_Y, MERGE_2D};
    int     strat_tiles[3];
    int64_t strat_data [3];

    for (int s = 0; s < 3; s++)
        evaluate_strategy(sheet_width, sheet_height, defects, n_defects,
                          interval_scratch, tile_scratch, strategies[s],
                          &strat_tiles[s], &strat_data[s]);

    fprintf(stderr, "Tile strategy comparison:\n");
    for (int s = 0; s < 3; s++)
        fprintf(stderr, "  %-4s  tiles=%d  data=%lld  (%.1f MB)\n",
                strategy_names[s],
                strat_tiles[s],
                (long long)strat_data[s],
                strat_data[s] * 4.0 / 1024.0 / 1024.0);

    /* Pick best strategy.
     *
     * Primary criterion: fewest tiles (drives slab_lookup cost).
     * Constraint: data must not exceed min_data * memory_tolerance.
     * This prevents picking a strategy that saves a few tiles at the
     * cost of a large memory blowup (e.g. sides problem: 1D-y has
     * fewest tiles but 3.4x more memory than 1D-x).
     */
    int64_t min_data = strat_data[0];
    for (int s = 1; s < 3; s++)
        if (strat_data[s] < min_data) min_data = strat_data[s];

    const double memory_tolerance = 1.20;  /* allow up to 20% above minimum */

    int best = -1;
    for (int s = 0; s < 3; s++) {
        if (strat_data[s] > (int64_t)(min_data * memory_tolerance))
            continue;  /* disqualify: too much memory */
        if (best == -1 || strat_tiles[s] < strat_tiles[best])
            best = s;
    }
    /* fallback: if all strategies exceed tolerance (shouldn't happen),
     * just pick minimum memory */
    if (best == -1) {
        best = 0;
        for (int s = 1; s < 3; s++)
            if (strat_data[s] < strat_data[best]) best = s;
    }

    fprintf(stderr, "  -> selected: %s\n", strategy_names[best]);
    MergeStrategy chosen = strategies[best];

    int total_tiles = strat_tiles[best];

    /* --- Phase C: allocate tiles, assign offsets, build local defect lists ---
     *
     * For each tile, we determine which defects are "local" — i.e. their
     * affected position range for this (w, h) overlaps the tile's sheet
     * coordinate range. Only local defects are checked for defect-aligned
     * cuts in the inner DP loop, avoiding redundant checks against distant
     * defects that can never affect positions within this tile. */
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

                /* Build local defect list: defect d is local if its affected
                 * position range for (w, h) overlaps this tile's bounds.
                 *
                 * The affected x-range for defect d at size (w, h) is
                 * [defect_x_start(d) - w + 1,  defect_x_end(d) - 1].
                 * We check overlap with the tile's [sheet_x_lo, sheet_x_hi]
                 * (and similarly for y). */
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

    /* --- Phase D: allocate flat data array --- */
    slab->data = (int32_t *)malloc(data_total * sizeof(int32_t));

    /* --- Phase E: bottom-up DP fill --- */
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
                Tile    *tile      = &slab->tiles[ti->first_tile + t];
                int32_t *tile_data = &slab->data[tile->data_offset];

                #pragma omp parallel for schedule(dynamic, 8) collapse(2)
                for (int sx = tile->sheet_x_lo; sx <= tile->sheet_x_hi; sx++) {
                    for (int sy = tile->sheet_y_lo; sy <= tile->sheet_y_hi; sy++) {

                        /* O(1) purity check via 2D prefix sum. */
                        int32_t n_defects_here = defect_count_in_rect(
                            defect_count_prefix, col_stride,
                            sx, sy, sx + w, sy + h);

                        if (n_defects_here == 0) {
                            /* Pure position — pre-fill with F_values[w][h]. */
                            int local_x = sx - tile->sheet_x_lo;
                            int local_y = sy - tile->sheet_y_lo;
                            tile_data[(int64_t)local_x * tile->y_span + local_y] = pure_val;
                            continue;
                        }

                        int32_t best_value = 0;

                        /* --- Normal-pattern vertical cuts ---
                         * Split width w into z and (w-z) at each valid cut position. */
                        for (int ci = 0; ci < n_x_cuts; ci++) {
                            int cut_pos = normal_cuts_x[x_cut_base + ci];
                            int32_t combined =
                                slab_lookup(slab, F_values, col_stride, cut_pos,     h, sx,           sy)
                              + slab_lookup(slab, F_values, col_stride, w - cut_pos, h, sx + cut_pos, sy);
                            if (combined > best_value) best_value = combined;
                        }

                        /* --- Defect-aligned vertical cuts (local defects only) ---
                         * Cut at the left edge (defect_x_start - sx) and right edge
                         * (defect_x_end - sx) of each local defect to isolate it. */
                        for (int li = 0; li < tile->n_local_defects; li++) {
                            DefectRef def = defect_at(defects, tile->local_defect_indices[li]);
                            int cut_at_left  = defect_x_start(def) - sx;
                            int cut_at_right = defect_x_end(def)   - sx;

                            if (cut_at_left > 0 && cut_at_left < w) {
                                int32_t combined =
                                    slab_lookup(slab, F_values, col_stride, cut_at_left,     h, sx,                sy)
                                  + slab_lookup(slab, F_values, col_stride, w - cut_at_left, h, sx + cut_at_left,  sy);
                                if (combined > best_value) best_value = combined;
                            }
                            if (cut_at_right > 0 && cut_at_right < w) {
                                int32_t combined =
                                    slab_lookup(slab, F_values, col_stride, cut_at_right,     h, sx,                 sy)
                                  + slab_lookup(slab, F_values, col_stride, w - cut_at_right, h, sx + cut_at_right,  sy);
                                if (combined > best_value) best_value = combined;
                            }
                        }

                        /* --- Normal-pattern horizontal cuts ---
                         * Split height h into z and (h-z) at each valid cut position. */
                        for (int ci = 0; ci < n_y_cuts; ci++) {
                            int cut_pos = normal_cuts_y[y_cut_base + ci];
                            int32_t combined =
                                slab_lookup(slab, F_values, col_stride, w, cut_pos,     sx, sy)
                              + slab_lookup(slab, F_values, col_stride, w, h - cut_pos, sx, sy + cut_pos);
                            if (combined > best_value) best_value = combined;
                        }

                        /* --- Defect-aligned horizontal cuts (local defects only) ---
                         * Cut at the bottom edge (defect_y_start - sy) and top edge
                         * (defect_y_end - sy) of each local defect. */
                        for (int li = 0; li < tile->n_local_defects; li++) {
                            DefectRef def = defect_at(defects, tile->local_defect_indices[li]);
                            int cut_at_bot = defect_y_start(def) - sy;
                            int cut_at_top = defect_y_end(def)   - sy;

                            if (cut_at_bot > 0 && cut_at_bot < h) {
                                int32_t combined =
                                    slab_lookup(slab, F_values, col_stride, w, cut_at_bot,     sx, sy)
                                  + slab_lookup(slab, F_values, col_stride, w, h - cut_at_bot, sx, sy + cut_at_bot);
                                if (combined > best_value) best_value = combined;
                            }
                            if (cut_at_top > 0 && cut_at_top < h) {
                                int32_t combined =
                                    slab_lookup(slab, F_values, col_stride, w, cut_at_top,     sx, sy)
                                  + slab_lookup(slab, F_values, col_stride, w, h - cut_at_top, sx, sy + cut_at_top);
                                if (combined > best_value) best_value = combined;
                            }
                        }

                        /* Store the best value found for this defected position. */
                        int local_x = sx - tile->sheet_x_lo;
                        int local_y = sy - tile->sheet_y_lo;
                        tile_data[(int64_t)local_x * tile->y_span + local_y] = best_value;
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
