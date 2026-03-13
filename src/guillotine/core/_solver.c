#define PY_SSIZE_T_CLEAN   /* Ensure Py_ssize_t is defined before including Python.h */
#include <Python.h>        /* Python C API header */
#include <stdint.h>        /* for fixed-width integer types like int32_t, int16_t */
#include <stdlib.h>        /* for malloc, calloc, free, qsort, realloc */
#include <string.h>        /* for memcpy */

/* Decision type constants — must match constants.py */
#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5

/* Defect array access macros (each defect entry has NR_DEFECT_FIELDS fields) */
#define NR_DEFECT_FIELDS 6
#define DEF_X(d)     defects[(d)*NR_DEFECT_FIELDS + 0]  /* x start of defect bounding box */
#define DEF_Y(d)     defects[(d)*NR_DEFECT_FIELDS + 1]  /* y start of defect bounding box */
#define DEF_W(d)     defects[(d)*NR_DEFECT_FIELDS + 2]  /* width  of defect bounding box */
#define DEF_H(d)     defects[(d)*NR_DEFECT_FIELDS + 3]  /* height of defect bounding box */
#define DEF_X_END(d) defects[(d)*NR_DEFECT_FIELDS + 4]  /* x end  (exclusive) */
#define DEF_Y_END(d) defects[(d)*NR_DEFECT_FIELDS + 5]  /* y end  (exclusive) */

/* 2D array indexing: arr[i][j] for row-major array with stride stride1 */
#define IDX_2D(arr, stride1, i, j) ((arr)[(i) * (stride1) + (j)])

/* ========================================================================
 * Data structures for multi-tile slab storage
 *
 * The 4D DP table Fd(w, h, x, y) stores optimal values for defected
 * rectangles of size w×h placed at position (x, y) on the sheet. A naive
 * dense array of shape (W0+1)² × (H0+1)² is prohibitively large.
 *
 * Key insight: only positions (x, y) whose rectangle [x, x+w) × [y, y+h)
 * overlaps at least one defect need storage — pure positions fall back to
 * F_values[w, h]. We exploit this by allocating, per (w, h), a set of
 * disjoint rectangular "tiles" covering only the defect-affected regions.
 *
 * Tile construction (per (w, h)):
 *   1. For each defect d, compute the x-interval of affected positions:
 *        x ∈ [DEF_X(d) - w + 1,  DEF_X_END(d) - 1]  (clamped to [0, W0-w])
 *      and the y-interval similarly.
 *   2. Sort intervals by x_lo, then merge overlapping/adjacent x-ranges.
 *      Each merged group gets the union of its constituent y-ranges.
 *   3. Each merged group becomes one Tile with its own dense sub-array.
 *
 * This avoids wasting memory on gaps between spatially separated defects.
 * For example, defects at x=50 and x=350 on a 400-wide sheet produce two
 * narrow tiles instead of one tile spanning the entire width.
 *
 * Lookup: fd_slab_lookup(w, h, x, y) scans the (small) tile list for
 * (w, h); if (x, y) falls in a tile, return the stored value, otherwise
 * return F_values[w, h].
 * ======================================================================== */

/*
 * Tile: one rectangular sub-array of Fd values for a specific (w, h).
 *
 *   x_lo..x_hi, y_lo..y_hi — inclusive bounds in sheet coordinates
 *   x_span, y_span         — dimensions (x_hi - x_lo + 1, etc.)
 *   offset                 — index into the flat FdSlab.data array
 *   local_defs             — indices of defects whose affected region
 *                            overlaps this tile (used for defect-aligned cuts)
 *   n_local_defs           — length of local_defs
 */
typedef struct {
    int x_lo, x_hi, y_lo, y_hi, x_span, y_span;
    int64_t offset;
    int *local_defs;
    int n_local_defs;
} Tile;

/*
 * TileIndex: per-(w, h) index into the flat tile array.
 *   first — index of the first Tile for this (w, h) in FdSlab.tiles
 *   count — number of tiles for this (w, h)
 */
typedef struct {
    int first;
    int count;
} TileIndex;

/*
 * FdSlab: the complete sparse Fd storage structure, returned to Python
 * as a PyCapsule.
 *   data     — flat array holding all tile values (int32_t)
 *   tiles    — flat array of all Tile structs across all (w, h)
 *   tile_idx — (W0+1)*(H0+1) TileIndex entries, indexed by w*(H0+1)+h
 *   total    — total number of int32_t entries in data
 *   n_tiles  — total number of Tile structs
 *   W0, H0   — sheet dimensions
 */
typedef struct {
    int32_t   *data;
    Tile      *tiles;
    TileIndex *tile_idx;
    int64_t    total;
    int        n_tiles;
    int        W0, H0;
} FdSlab;

/*
 * XInterval: scratch struct for the tile-building merge algorithm.
 *   lo, hi     — x-range of one defect's affected positions (inclusive)
 *   y_lo, y_hi — corresponding y-range
 */
typedef struct {
    int lo, hi;
    int y_lo, y_hi;
} XInterval;

/* qsort comparator: sort defects by x coordinate (first field) */
static int cmp_defects_x(const void *a, const void *b) {
    int32_t x_a = *(const int32_t *)a;
    int32_t x_b = *(const int32_t *)b;
    return (x_a > x_b) - (x_a < x_b);
}

/* qsort comparator: sort XIntervals by x_lo */
static int cmp_intervals(const void *a, const void *b) {
    return ((const XInterval *)a)->lo - ((const XInterval *)b)->lo;
}

/* ========================================================================
 * Precompute g: best single-item tiling value and item index for each
 * rectangle size.
 *
 * g_values[w, h]  = best area achievable by tiling w×h with copies of
 *                   one item type
 * g_indices[w, h] = index of that item type, or -1 if no item fits
 *
 * Arrays: g_values, g_indices are 2D (W0+1, H0+1)
 *         item_w, item_h, item_area are 1D (n_items,)
 * ======================================================================== */
static void fill_g_core(int W0, int H0, int32_t *g_values, int32_t *g_indices,
                        int32_t *item_w, int32_t *item_h, int32_t *item_area, int n_items) {
    int stride = H0 + 1;

    #pragma omp parallel for schedule(dynamic)
    for (int w = 1; w <= W0; w++) {
        int w_stride = w * stride;

        for (int h = 1; h <= H0; h++) {
            int32_t best_val = 0, best_idx = -1;

            for (int i = 0; i < n_items; i++) {
                int nx = w / item_w[i];
                int ny = h / item_h[i];

                if (nx > 0 && ny > 0) {
                    int32_t val = item_area[i] * nx * ny;
                    if (val > best_val) { best_val = val; best_idx = i; }
                }
            }

            g_values[w_stride + h] = best_val;
            g_indices[w_stride + h] = best_idx;
        }
    }
}

/* ========================================================================
 * Bottom-up DP for pure rectangles (no defects).
 *
 * F_values[w, h] = optimal value for pure w×h rectangle
 * F_type[w, h]   = decision type (DECISION_EMPTY/FILL/CUT_X/CUT_Y)
 * F_param[w, h]  = decision parameter (item index for FILL, cut position
 *                  for CUT)
 *
 * Uses symmetry: only tries cuts up to half the dimension.
 * Normal pattern arrays encode which cut positions are valid.
 * ======================================================================== */
static void fill_F_core(int W0, int H0, int32_t *g_values, int32_t *g_indices,
                        int32_t *F_values, int8_t *F_type, int32_t *F_param,
                        int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
                        int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y) {
    int stride = H0 + 1;
    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w], w_stride = w * stride, x_base = w * max_cuts_x, half_w = w >> 1;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h], y_base = h * max_cuts_y, half_h = h >> 1;
            int32_t g_idx = g_indices[w_stride + h], best_val = g_values[w_stride + h];
            int best_type = (g_idx >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (g_idx >= 0) ? g_idx : 0;

            /* Vertical cuts — symmetry: only z <= w/2 */
            for (int i = 0; i < nx; i++) {
                int z = np_x_arr[x_base + i];
                if (z > half_w) break;
                int32_t total = F_values[z * stride + h] + F_values[(w - z) * stride + h];
                if (total > best_val) { best_val = total; best_type = DECISION_CUT_X; best_param = z; }
            }

            /* Horizontal cuts — symmetry: only z <= h/2 */
            for (int i = 0; i < ny; i++) {
                int z = np_y_arr[y_base + i];
                if (z > half_h) break;
                int32_t total = F_values[w_stride + z] + F_values[w_stride + (h - z)];
                if (total > best_val) { best_val = total; best_type = DECISION_CUT_Y; best_param = z; }
            }

            F_values[w_stride + h] = best_val;
            F_type[w_stride + h] = (int8_t)best_type;
            F_param[w_stride + h] = best_param;
        }
    }
}

/* ========================================================================
 * Build merged tiles for a single (w, h) pair.
 *
 * For each defect d, the set of positions (x, y) such that rectangle
 * [x, x+w) × [y, y+h) overlaps defect d is:
 *   x ∈ [DEF_X(d) - w + 1,  DEF_X_END(d) - 1]   clamped to [0, W0-w]
 *   y ∈ [DEF_Y(d) - h + 1,  DEF_Y_END(d) - 1]   clamped to [0, H0-h]
 *
 * We sort these intervals by x_lo and merge overlapping/adjacent ones.
 * Each merged group becomes a tile with the union y-range.
 *
 * Returns number of tiles written to out_tiles.
 * ======================================================================== */
static int build_tiles_for_wh(int w, int h, int W0, int H0, int32_t *defects, int n_def,
                              XInterval *intervals, Tile *out_tiles) {
    int n_iv = 0;
    const int max_x = W0 - w, max_y = H0 - h;

    /* Compute affected (x, y) interval for each defect */
    for (int d = 0; d < n_def; d++) {
        int x_lo = (DEF_X(d) - w + 1 < 0) ? 0 : DEF_X(d) - w + 1;
        int x_hi = (DEF_X_END(d) - 1 > max_x) ? max_x : DEF_X_END(d) - 1;
        int y_lo = (DEF_Y(d) - h + 1 < 0) ? 0 : DEF_Y(d) - h + 1;
        int y_hi = (DEF_Y_END(d) - 1 > max_y) ? max_y : DEF_Y_END(d) - 1;

        if (x_lo <= x_hi && y_lo <= y_hi) {
            intervals[n_iv].lo = x_lo;   intervals[n_iv].hi = x_hi;
            intervals[n_iv].y_lo = y_lo; intervals[n_iv].y_hi = y_hi;
            n_iv++;
        }
    }

    if (n_iv == 0) return 0;

    /* Sort by x_lo for sweep-line merge */
    qsort(intervals, n_iv, sizeof(XInterval), cmp_intervals);

    /* Merge overlapping/adjacent x-ranges; union their y-ranges */
    int n_tiles = 0;
    XInterval cur = intervals[0];

    for (int i = 1; i < n_iv; i++) {
        if (intervals[i].lo <= cur.hi + 1) {
            /* Overlapping or adjacent — extend current group */
            if (intervals[i].hi > cur.hi)     cur.hi = intervals[i].hi;
            if (intervals[i].y_lo < cur.y_lo) cur.y_lo = intervals[i].y_lo;
            if (intervals[i].y_hi > cur.y_hi) cur.y_hi = intervals[i].y_hi;
        } else {
            /* Gap in x — emit current tile, start new group */
            out_tiles[n_tiles].x_lo = cur.lo;    out_tiles[n_tiles].x_hi = cur.hi;
            out_tiles[n_tiles].y_lo = cur.y_lo;  out_tiles[n_tiles].y_hi = cur.y_hi;
            out_tiles[n_tiles].x_span = cur.hi - cur.lo + 1;
            out_tiles[n_tiles].y_span = cur.y_hi - cur.y_lo + 1;
            n_tiles++;
            cur = intervals[i];
        }
    }

    /* Emit last tile */
    out_tiles[n_tiles].x_lo = cur.lo;    out_tiles[n_tiles].x_hi = cur.hi;
    out_tiles[n_tiles].y_lo = cur.y_lo;  out_tiles[n_tiles].y_hi = cur.y_hi;
    out_tiles[n_tiles].x_span = cur.hi - cur.lo + 1;
    out_tiles[n_tiles].y_span = cur.y_hi - cur.y_lo + 1;

    return n_tiles + 1;
}

/* ========================================================================
 * Slab lookup: given (w, h, x, y), find the value in the appropriate tile.
 *
 * Scans the tile list for (w, h). If (x, y) falls inside a tile, returns
 * the stored value. Otherwise returns F_values[w, h] (pure rectangle).
 *
 * The tile list is typically 1–5 entries for ~20 defects, so the linear
 * scan is cheap compared to the DP work per cell.
 * ======================================================================== */
static inline int32_t fd_slab_lookup(const FdSlab *slab, const int32_t *F_values, int stride_F,
                                     int w, int h, int x, int y) {
    const TileIndex *ti = &slab->tile_idx[w * (slab->H0 + 1) + h];

    for (int t = 0; t < ti->count; t++) {
        const Tile *tile = &slab->tiles[ti->first + t];

        if (x >= tile->x_lo && x <= tile->x_hi && y >= tile->y_lo && y <= tile->y_hi)
            return slab->data[tile->offset + (int64_t)(x - tile->x_lo) * tile->y_span + (y - tile->y_lo)];
    }

    return F_values[w * stride_F + h];
}

/* ========================================================================
 * Core fill function for defected rectangles using multi-tile slab storage.
 *
 * Phases:
 *   1. Sort defects by x coordinate for efficient interval merging.
 *   2. Count total tiles across all (w, h) pairs.
 *   3. Allocate tile array and compute per-tile data offsets.
 *      For each tile, determine which defects are "local" — i.e. their
 *      affected region for this (w, h) overlaps the tile's (x, y) range.
 *      This lets the inner DP loop only check relevant defects for
 *      defect-aligned cuts.
 *   4. Allocate flat data array.
 *   5. Bottom-up DP fill: for each (w, h), iterate only over tile regions.
 *      Pure positions (defect_count == 0 via prefix sum) are pre-filled
 *      with F_values[w, h]. Defected positions try all candidate cuts
 *      (normal patterns + defect-aligned) and store the best value.
 *
 * Memory layout:
 *   All tile data is packed into one contiguous int32_t array (slab->data).
 *   Tile t's sub-array starts at slab->data[tile.offset] and has shape
 *   tile.x_span × tile.y_span in row-major order.
 *
 * Note: build_tiles_for_wh is called twice per (w, h) — once to count
 * tiles (phase 2), once to emit them (phase 3). This avoids storing
 * intermediate results at the cost of repeating the cheap interval merge.
 * ======================================================================== */
static FdSlab *fill_Fd_slab(int W0, int H0, int32_t *prefix, int32_t *F_values,
                            int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
                            int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y,
                            int32_t *defects_raw, int n_def) {
    int stride_F = H0 + 1, stride_p = H0 + 1;

    /* Copy and sort defects by x for consistent interval merging */
    int32_t *defects = (int32_t *)malloc(n_def * NR_DEFECT_FIELDS * sizeof(int32_t));
    memcpy(defects, defects_raw, n_def * NR_DEFECT_FIELDS * sizeof(int32_t));
    if (n_def > 1) qsort(defects, n_def, sizeof(int32_t) * NR_DEFECT_FIELDS, cmp_defects_x);

    FdSlab *slab = (FdSlab *)calloc(1, sizeof(FdSlab));
    slab->W0 = W0;
    slab->H0 = H0;
    slab->tile_idx = (TileIndex *)calloc((W0 + 1) * (H0 + 1), sizeof(TileIndex));

    /* Scratch buffers reused across all (w, h) iterations */
    XInterval *iv_scr = (XInterval *)malloc(n_def * sizeof(XInterval));
    Tile *t_scr = (Tile *)malloc(n_def * sizeof(Tile));

    /* ---- Phase 1: count total tiles across all (w, h) ---- */
    int total_tiles = 0;
    for (int w = 1; w <= W0; w++)
        for (int h = 1; h <= H0; h++)
            total_tiles += build_tiles_for_wh(w, h, W0, H0, defects, n_def, iv_scr, t_scr);

    /* ---- Phase 2: allocate tiles, compute offsets, build local defect lists ---- */
    slab->tiles = (Tile *)calloc(total_tiles, sizeof(Tile));
    slab->n_tiles = total_tiles;
    int tile_cursor = 0;
    int64_t data_total = 0;

    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            TileIndex *ti = &slab->tile_idx[w * stride_F + h];
            ti->first = tile_cursor;
            int nt = build_tiles_for_wh(w, h, W0, H0, defects, n_def, iv_scr, t_scr);

            for (int t = 0; t < nt; t++) {
                t_scr[t].offset = data_total;
                data_total += (int64_t)t_scr[t].x_span * t_scr[t].y_span;

                /* Build local defect list for this tile:
                 * defect d is local if its affected (x, y) range for (w, h)
                 * overlaps the tile's [x_lo..x_hi] × [y_lo..y_hi] */
                int *l_list = (int *)malloc(n_def * sizeof(int));
                int l_count = 0;
                for (int d = 0; d < n_def; d++) {
                    if (!(t_scr[t].x_hi < (DEF_X(d) - w + 1) || t_scr[t].x_lo > (DEF_X_END(d) - 1) ||
                          t_scr[t].y_hi < (DEF_Y(d) - h + 1) || t_scr[t].y_lo > (DEF_Y_END(d) - 1)))
                        l_list[l_count++] = d;
                }

                if (l_count > 0) {
                    t_scr[t].n_local_defs = l_count;
                    t_scr[t].local_defs = (int *)realloc(l_list, l_count * sizeof(int));
                } else {
                    free(l_list);
                    t_scr[t].local_defs = NULL;
                    t_scr[t].n_local_defs = 0;
                }

                slab->tiles[tile_cursor++] = t_scr[t];
            }
            ti->count = nt;
        }
    }

    slab->total = data_total;

    /* ---- Phase 3: allocate flat data array ---- */
    slab->data = (int32_t *)malloc(data_total * sizeof(int32_t));

    /* ---- Phase 4: pre-fill tiles and run bottom-up DP ----
     *
     * Pre-filling with F_values[w, h] ensures that pure positions inside a
     * tile's bounding box (where defect_count == 0) already hold the correct
     * value. The DP loop skips them with `continue`.
     *
     * For defected positions, we try all candidate cuts:
     *   - Normal pattern X cuts: split w into z and w-z
     *   - Normal pattern Y cuts: split h into z and h-z
     *   - Defect-aligned X cuts: split at DEF_X(d)-x and DEF_X_END(d)-x
     *   - Defect-aligned Y cuts: split at DEF_Y(d)-y and DEF_Y_END(d)-y
     *
     * Each child lookup uses fd_slab_lookup, which returns the tile value if
     * the child is stored, or F_values[w', h'] if it's pure. This is correct
     * because smaller (w, h) have already been filled in bottom-up order.
     */
    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w], w_max = w * max_cuts_x;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h], h_max = h * max_cuts_y;
            TileIndex *ti = &slab->tile_idx[w * stride_F + h];
            int32_t base_f = F_values[w * stride_F + h];

            for (int t = 0; t < ti->count; t++) {
                Tile *tile = &slab->tiles[ti->first + t];
                int32_t *t_ptr = &slab->data[tile->offset];

                #pragma omp parallel for schedule(dynamic, 8) collapse(2)
                for (int x = tile->x_lo; x <= tile->x_hi; x++) {
                    for (int y = tile->y_lo; y <= tile->y_hi; y++) {

                        /* O(1) purity check via 2D prefix sum */
                        int32_t dc = IDX_2D(prefix, stride_p, x+w, y+h) - IDX_2D(prefix, stride_p, x, y+h)
                                   - IDX_2D(prefix, stride_p, x+w, y)   + IDX_2D(prefix, stride_p, x, y);
                        if (dc == 0) {
                            /* Pure — pre-fill with F_values[w, h] */
                            t_ptr[(int64_t)(x - tile->x_lo) * tile->y_span + (y - tile->y_lo)] = base_f;
                            continue;
                        }

                        int32_t best = 0;

                        /* X cuts — normal pattern positions */
                        for (int i = 0; i < nx; i++) {
                            int z = np_x_arr[w_max + i];
                            int32_t s = fd_slab_lookup(slab, F_values, stride_F, z, h, x, y)
                                      + fd_slab_lookup(slab, F_values, stride_F, w-z, h, x+z, y);
                            if (s > best) best = s;
                        }

                        /* X cuts — defect-aligned positions (only local defects) */
                        for (int i = 0; i < tile->n_local_defs; i++) {
                            int d = tile->local_defs[i];
                            int z1 = DEF_X(d) - x, z2 = DEF_X_END(d) - x;
                            if (z1 > 0 && z1 < w) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, z1, h, x, y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w-z1, h, x+z1, y);
                                if (s > best) best = s;
                            }
                            if (z2 > 0 && z2 < w) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, z2, h, x, y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w-z2, h, x+z2, y);
                                if (s > best) best = s;
                            }
                        }

                        /* Y cuts — normal pattern positions */
                        for (int i = 0; i < ny; i++) {
                            int z = np_y_arr[h_max + i];
                            int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z, x, y)
                                      + fd_slab_lookup(slab, F_values, stride_F, w, h-z, x, y+z);
                            if (s > best) best = s;
                        }

                        /* Y cuts — defect-aligned positions (only local defects) */
                        for (int i = 0; i < tile->n_local_defs; i++) {
                            int d = tile->local_defs[i];
                            int z1 = DEF_Y(d) - y, z2 = DEF_Y_END(d) - y;
                            if (z1 > 0 && z1 < h) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z1, x, y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w, h-z1, x, y+z1);
                                if (s > best) best = s;
                            }
                            if (z2 > 0 && z2 < h) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z2, x, y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w, h-z2, x, y+z2);
                                if (s > best) best = s;
                            }
                        }

                        t_ptr[(int64_t)(x - tile->x_lo) * tile->y_span + (y - tile->y_lo)] = best;
                    }
                }
            }
        }
    }

    free(defects);
    free(iv_scr);
    free(t_scr);
    return slab;
}

/* ========================================================================
 * PyCapsule destructor — frees the slab and all owned memory when Python
 * garbage-collects the capsule object.
 * ======================================================================== */
static void fd_slab_destructor(PyObject *capsule) {
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (slab) {
        free(slab->data);
        free(slab->tile_idx);
        if (slab->tiles) {
            for (int i = 0; i < slab->n_tiles; i++)
                free(slab->tiles[i].local_defs);  /* NULL-safe per C standard */
            free(slab->tiles);
        }
        free(slab);
    }
}

/* ========================================================================
 * Python wrappers
 *
 * All wrappers follow the same pattern:
 *   1. Parse arguments with PyArg_ParseTuple
 *   2. Acquire buffer views on numpy arrays
 *   3. Call the C core function
 *   4. Release buffer views
 *   5. Return result (Py_RETURN_NONE or a PyObject)
 *
 * The `self` parameter is unused (required by METH_VARARGS signature)
 * and suppressed via Py_UNUSED to avoid -Wunused-parameter warnings.
 * ======================================================================== */

/* Python wrapper for fill_Fd_slab.
 * Args: (W0, H0, prefix, F_values, np_x_arr, np_x_len, np_y_arr, np_y_len,
 *        defects_arr, max_cuts_x, max_cuts_y, n_def)
 * Returns: PyCapsule wrapping the FdSlab (freed when capsule is GC'd) */
static PyObject* py_fill_Fd_slab(PyObject *Py_UNUSED(self), PyObject *args) {
    int W0, H0, mcx, mcy, n_def;
    PyObject *o_pre, *o_F, *o_nx, *o_nlx, *o_ny, *o_nly, *o_def;
    if (!PyArg_ParseTuple(args, "iiOOOOOOOiii",
            &W0, &H0, &o_pre, &o_F, &o_nx, &o_nlx, &o_ny, &o_nly, &o_def,
            &mcx, &mcy, &n_def))
        return NULL;

    Py_buffer b_pre, b_F, b_nx, b_nlx, b_ny, b_nly, b_def;
    if (PyObject_GetBuffer(o_pre, &b_pre, PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_F,   &b_F,   PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_nx,  &b_nx,  PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_nlx, &b_nlx, PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_ny,  &b_ny,  PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_nly, &b_nly, PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_def, &b_def, PyBUF_SIMPLE) < 0)
        return NULL;

    FdSlab *slab = fill_Fd_slab(W0, H0,
        (int32_t *)b_pre.buf, (int32_t *)b_F.buf,
        (int32_t *)b_nx.buf,  (int32_t *)b_nlx.buf, mcx,
        (int32_t *)b_ny.buf,  (int32_t *)b_nly.buf, mcy,
        (int32_t *)b_def.buf, n_def);

    PyBuffer_Release(&b_pre); PyBuffer_Release(&b_F);
    PyBuffer_Release(&b_nx);  PyBuffer_Release(&b_nlx);
    PyBuffer_Release(&b_ny);  PyBuffer_Release(&b_nly);
    PyBuffer_Release(&b_def);

    return PyCapsule_New(slab, "FdSlab", fd_slab_destructor);
}

/* Python wrapper for fd_slab_lookup — single point query for reconstruction.
 * Args: (capsule, F_values, H0, w, h, x, y)
 * Returns: int (the Fd value) */
static PyObject* py_fd_slab_lookup(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *cap, *o_F;
    int H0, w, h, x, y;
    if (!PyArg_ParseTuple(args, "OOiiiii", &cap, &o_F, &H0, &w, &h, &x, &y))
        return NULL;

    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(cap, "FdSlab");
    Py_buffer b_F;
    PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE);
    int32_t val = fd_slab_lookup(slab, (int32_t *)b_F.buf, H0 + 1, w, h, x, y);
    PyBuffer_Release(&b_F);

    return PyLong_FromLong(val);
}

/* Python wrapper for fd_slab_stats — diagnostics.
 * Args: (capsule,)
 * Returns: (slab_entries, dense_equivalent, n_tiles) */
static PyObject* py_fd_slab_stats(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *cap;
    if (!PyArg_ParseTuple(args, "O", &cap)) return NULL;

    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(cap, "FdSlab");
    int64_t dense = (int64_t)(slab->W0 + 1) * (slab->W0 + 1) *
                    (int64_t)(slab->H0 + 1) * (slab->H0 + 1);

    return Py_BuildValue("(LLi)", (long long)slab->total, (long long)dense, slab->n_tiles);
}

/* Python wrapper for fill_g_core */
static PyObject* py_fill_g(PyObject *Py_UNUSED(self), PyObject *args) {
    int W0, H0, n;
    PyObject *o_gv, *o_gi, *o_iw, *o_ih, *o_ia;
    if (!PyArg_ParseTuple(args, "iiOOOOOi",
            &W0, &H0, &o_gv, &o_gi, &o_iw, &o_ih, &o_ia, &n))
        return NULL;

    Py_buffer bv, bi, bw, bh, ba;
    PyObject_GetBuffer(o_gv, &bv, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_gi, &bi, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_iw, &bw, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ih, &bh, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ia, &ba, PyBUF_SIMPLE);

    fill_g_core(W0, H0,
        (int32_t *)bv.buf, (int32_t *)bi.buf,
        (int32_t *)bw.buf, (int32_t *)bh.buf, (int32_t *)ba.buf, n);

    PyBuffer_Release(&bv); PyBuffer_Release(&bi);
    PyBuffer_Release(&bw); PyBuffer_Release(&bh); PyBuffer_Release(&ba);
    Py_RETURN_NONE;
}

/* Python wrapper for fill_F_core */
static PyObject* py_fill_F(PyObject *Py_UNUSED(self), PyObject *args) {
    int W0, H0, mcx, mcy;
    PyObject *ogv, *ogi, *ofv, *oft, *ofp, *onx, *onlx, *ony, *only;
    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOii",
            &W0, &H0, &ogv, &ogi, &ofv, &oft, &ofp, &onx, &onlx, &ony, &only,
            &mcx, &mcy))
        return NULL;

    Py_buffer bgv, bgi, bfv, bft, bfp, bnx, bnlx, bny, bnly;
    PyObject_GetBuffer(ogv,  &bgv,  PyBUF_SIMPLE);
    PyObject_GetBuffer(ogi,  &bgi,  PyBUF_SIMPLE);
    PyObject_GetBuffer(ofv,  &bfv,  PyBUF_SIMPLE);
    PyObject_GetBuffer(oft,  &bft,  PyBUF_SIMPLE);
    PyObject_GetBuffer(ofp,  &bfp,  PyBUF_SIMPLE);
    PyObject_GetBuffer(onx,  &bnx,  PyBUF_SIMPLE);
    PyObject_GetBuffer(onlx, &bnlx, PyBUF_SIMPLE);
    PyObject_GetBuffer(ony,  &bny,  PyBUF_SIMPLE);
    PyObject_GetBuffer(only, &bnly, PyBUF_SIMPLE);

    fill_F_core(W0, H0,
        (int32_t *)bgv.buf, (int32_t *)bgi.buf,
        (int32_t *)bfv.buf, (int8_t *)bft.buf, (int32_t *)bfp.buf,
        (int32_t *)bnx.buf, (int32_t *)bnlx.buf, mcx,
        (int32_t *)bny.buf, (int32_t *)bnly.buf, mcy);

    PyBuffer_Release(&bgv);  PyBuffer_Release(&bgi);
    PyBuffer_Release(&bfv);  PyBuffer_Release(&bft);  PyBuffer_Release(&bfp);
    PyBuffer_Release(&bnx);  PyBuffer_Release(&bnlx);
    PyBuffer_Release(&bny);  PyBuffer_Release(&bnly);
    Py_RETURN_NONE;
}

/* method table */
static PyMethodDef SolverMethods[] = {
    {"fill_g",         py_fill_g,         METH_VARARGS, "Precompute best single-item tiling (g tables)."},
    {"fill_F",         py_fill_F,         METH_VARARGS, "Bottom-up DP for pure rectangles."},
    {"fill_Fd_slab",   py_fill_Fd_slab,   METH_VARARGS, "Fill Fd values using multi-tile slab storage."},
    {"fd_slab_lookup", py_fd_slab_lookup,  METH_VARARGS, "Lookup a single Fd value from the slab."},
    {"fd_slab_stats",  py_fd_slab_stats,   METH_VARARGS, "Return (slab_entries, dense_equivalent, n_tiles)."},
    {NULL, NULL, 0, NULL}
};

/* module definition */
static struct PyModuleDef solvermodule = {
    PyModuleDef_HEAD_INIT,
    "_solver",       /* m_name */
    NULL,            /* m_doc */
    -1,              /* m_size */
    SolverMethods,   /* m_methods */
    NULL,            /* m_slots */
    NULL,            /* m_traverse */
    NULL,            /* m_clear */
    NULL             /* m_free */
};

/* module init function — called by Python on import */
PyMODINIT_FUNC PyInit__solver(void) {
    return PyModule_Create(&solvermodule);
}