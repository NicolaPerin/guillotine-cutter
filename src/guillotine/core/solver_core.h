/* =============================================================================
 * solver_core.h — Public interface for the guillotine cutting-stock solver
 *
 * Contains all type definitions, constants, inline helper functions, and
 * declarations for the three core DP phases. This header has no Python
 * dependency — it can be included from pure-C test harnesses or alternative
 * language bindings.
 *
 * The solver operates in three phases:
 *   Phase 1 (g-table):  Best single-item tiling value for each rectangle size
 *   Phase 2 (F-table):  Optimal value for pure (defect-free) rectangles
 *   Phase 3 (Fd-table): Optimal value for defected rectangles, stored in a
 *                        sparse "multi-tile slab" to avoid allocating the full
 *                        (W+1)^2 × (H+1)^2 dense array
 * ============================================================================= */

#ifndef SOLVER_CORE_H
#define SOLVER_CORE_H

#include <stdint.h>

/* =============================================================================
 * Decision type constants — must match constants.py
 * ============================================================================= */
#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5


/* =============================================================================
 * Defect record layout
 *
 * The defect array is a flat int32_t buffer. Each defect occupies
 * DEFECT_FIELD_COUNT consecutive entries in this order:
 *   [0] x_start  — left edge of the bounding box
 *   [1] y_start  — bottom edge
 *   [2] width
 *   [3] height
 *   [4] x_end    — right edge  (exclusive, i.e. x_start + width)
 *   [5] y_end    — top edge    (exclusive, i.e. y_start + height)
 *
 * Use the DefectRef helper below instead of indexing the raw array directly.
 * ============================================================================= */
#define DEFECT_FIELD_COUNT 6

/* Lightweight view into one defect's fields — no allocation, just pointer
 * arithmetic. The compiler inlines all accessor calls completely. */
typedef struct {
    const int32_t *fields;   /* points to defect_array[d * DEFECT_FIELD_COUNT] */
} DefectRef;

static inline DefectRef defect_at(const int32_t *defect_array, int index) {
    DefectRef ref;
    ref.fields = defect_array + index * DEFECT_FIELD_COUNT;
    return ref;
}

static inline int defect_x_start(DefectRef d) { return d.fields[0]; }
static inline int defect_y_start(DefectRef d) { return d.fields[1]; }
static inline int defect_width  (DefectRef d) { return d.fields[2]; }
static inline int defect_height (DefectRef d) { return d.fields[3]; }
static inline int defect_x_end  (DefectRef d) { return d.fields[4]; }  /* exclusive */
static inline int defect_y_end  (DefectRef d) { return d.fields[5]; }  /* exclusive */


/* =============================================================================
 * 2D prefix-sum query
 *
 * prefix is a (sheet_width+1) × (sheet_height+1) array in row-major order.
 * Returns the number of defect pixels in the rectangle [x0, x1) × [y0, y1).
 *
 * This is the standard inclusion-exclusion formula for 2D prefix sums:
 *   count = P[x1][y1] - P[x0][y1] - P[x1][y0] + P[x0][y0]
 * ============================================================================= */
static inline int32_t defect_count_in_rect(const int32_t *prefix, int stride,
                                           int x0, int y0, int x1, int y1) {
    return prefix[x1 * stride + y1]
         - prefix[x0 * stride + y1]
         - prefix[x1 * stride + y0]
         + prefix[x0 * stride + y0];
}


/* =============================================================================
 * Sparse Fd storage — multi-tile slab
 *
 * The 4D table Fd(w, h, x, y) gives the optimal value for a defected
 * rectangle of size w×h whose bottom-left corner is at sheet position (x, y).
 *
 * A naive dense array of shape (W+1)^2 × (H+1)^2 is prohibitively large.
 * Key observation: only positions (x, y) where the rectangle [x, x+w) × [y, y+h)
 * overlaps at least one defect need to be stored — all other positions equal
 * F_values[w][h] (the pure-rectangle answer).
 *
 * We represent the affected region for each (w, h) as a set of disjoint
 * rectangular "tiles" covering only the defect-affected positions. All tile
 * data is packed into one flat int32_t array. A TileIndex entry tells us
 * which slice of the tiles[] array belongs to a given (w, h).
 *
 * Tile construction (per (w, h)):
 *   1. For each defect, compute the x-interval of positions whose rectangle
 *      overlaps it:  x ∈ [defect.x_start - w + 1,  defect.x_end - 1]
 *      (clamped to [0, sheet_width - w]), and similarly for y.
 *   2. Sort intervals by x_lo, then merge overlapping or adjacent x-ranges,
 *      taking the union of their y-ranges.
 *   3. Each merged group becomes one Tile with its own dense sub-array.
 *
 * This avoids wasting memory on gaps between spatially separated defects —
 * e.g. defects at x=50 and x=350 on a 400-wide sheet produce two narrow
 * tiles instead of one tile spanning the entire width.
 *
 * Lookup: scan the (small) tile list for (w, h); if (x, y) falls in a tile,
 * return the stored value; otherwise return F_values[w][h].
 * ============================================================================= */

/*
 * Tile — one rectangular sub-array of Fd values for a specific (w, h).
 *
 *   sheet_x_lo .. sheet_x_hi   inclusive position bounds on the sheet
 *   sheet_y_lo .. sheet_y_hi
 *   x_span, y_span             dimensions (hi - lo + 1)
 *   data_offset                index of this tile's first entry in FdSlab.data[]
 *   local_defect_indices       indices of defects whose affected region overlaps
 *                              this tile — used to generate defect-aligned cut
 *                              candidates without scanning all defects
 *   n_local_defects            length of local_defect_indices
 */
typedef struct {
    int     sheet_x_lo, sheet_x_hi;
    int     sheet_y_lo, sheet_y_hi;
    int     x_span, y_span;
    int64_t data_offset;
    int    *local_defect_indices;
    int     n_local_defects;
} Tile;

/*
 * TileIndex — locates all tiles for a given (rect_width, rect_height) pair
 * in the flat FdSlab.tiles[] array.
 */
typedef struct {
    int first_tile;   /* index into FdSlab.tiles[] */
    int tile_count;
} TileIndex;

/*
 * FdSlab — the complete sparse Fd storage, returned to Python as a PyCapsule.
 *
 *   data[]               flat array of all stored int32_t values
 *   tiles[]              flat array of all Tile structs across all (w, h)
 *   tile_index[]         (sheet_width+1)*(sheet_height+1) TileIndex entries,
 *                        indexed by rect_width*(sheet_height+1) + rect_height
 *   total_data_entries   total number of int32_t entries in data[]
 *   total_tile_count     total number of Tile structs in tiles[]
 *   sheet_width, sheet_height
 */
typedef struct {
    int32_t   *data;
    Tile      *tiles;
    TileIndex *tile_index;
    int64_t    total_data_entries;
    int        total_tile_count;
    int        sheet_width, sheet_height;
} FdSlab;


/* =============================================================================
 * Slab lookup — retrieve Fd(rect_width, rect_height, sheet_x, sheet_y)
 *
 * Scans the tile list for (rect_width, rect_height). If (sheet_x, sheet_y)
 * falls inside a tile, returns the stored value. Otherwise the position is
 * pure and returns F_values[rect_width][rect_height].
 *
 * The tile list is typically 1–5 entries for ~20 defects, so the linear
 * scan is cheap compared to the DP work per cell.
 *
 * This function is defined inline in the header because it is called from
 * both solver_core.c (the hot DP inner loop — billions of calls) and
 * _solver.c (the Python reconstruction wrapper — few calls). Keeping it
 * in the header lets the compiler inline it in the hot path without
 * requiring link-time optimization.
 * ============================================================================= */
static inline int32_t slab_lookup(const FdSlab  *slab,
                                  const int32_t *F_values,
                                  int            col_stride,
                                  int rect_width, int rect_height,
                                  int sheet_x,    int sheet_y) {

    const TileIndex *ti = &slab->tile_index[rect_width * (slab->sheet_height + 1) + rect_height];

    for (int t = 0; t < ti->tile_count; t++) {
        const Tile *tile = &slab->tiles[ti->first_tile + t];

        if (sheet_x >= tile->sheet_x_lo && sheet_x <= tile->sheet_x_hi &&
            sheet_y >= tile->sheet_y_lo && sheet_y <= tile->sheet_y_hi) {
            int local_x = sheet_x - tile->sheet_x_lo;
            int local_y = sheet_y - tile->sheet_y_lo;
            return slab->data[tile->data_offset + (int64_t)local_x * tile->y_span + local_y];
        }
    }

    return F_values[rect_width * col_stride + rect_height];
}


/* =============================================================================
 * Core function declarations
 * ============================================================================= */

/*
 * Phase 1 — g-table: best single-item tiling
 *
 * For every rectangle size (w × h), compute:
 *   g_values[w][h]     = max area coverable by tiling with copies of one item type
 *   g_item_index[w][h] = which item type achieves that, or -1 if no item fits
 *
 * Both arrays are (sheet_width+1) × (sheet_height+1) in row-major order.
 */
void fill_g_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *item_widths, int32_t *item_heights,
                  int32_t *item_areas, int n_items);

/*
 * Phase 2 — F-table: optimal value for pure (defect-free) rectangles
 *
 * Bottom-up DP. For each (w, h), tries single-item tiling, vertical cuts,
 * and horizontal cuts. Only cut positions in the normal-pattern arrays are
 * tried, and only up to half the dimension (symmetry).
 *
 * Output arrays F_values, F_decision_type, F_decision_param are all
 * (sheet_width+1) × (sheet_height+1) in row-major order.
 */
void fill_F_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *F_values, int8_t *F_decision_type, int32_t *F_decision_param,
                  int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                  int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts);

/*
 * Phase 3 — Fd-table: optimal value for defected rectangles
 *
 * Builds and returns a sparse FdSlab. The caller owns the returned pointer
 * and is responsible for freeing it (in practice, Python does this via the
 * PyCapsule destructor).
 *
 * The slab stores only positions where the rectangle overlaps a defect;
 * all other positions implicitly equal F_values[w][h].
 */
FdSlab *fill_Fd_slab(int sheet_width, int sheet_height,
                     int32_t *defect_count_prefix, int32_t *F_values,
                     int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                     int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts,
                     int32_t *defect_array_in, int n_defects);

#endif /* SOLVER_CORE_H */
