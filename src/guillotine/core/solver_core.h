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

typedef struct {
    const int32_t *fields;
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
static inline int defect_x_end  (DefectRef d) { return d.fields[4]; }
static inline int defect_y_end  (DefectRef d) { return d.fields[5]; }


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
 * ============================================================================= */

typedef struct {
    int     sheet_x_lo, sheet_x_hi;
    int     sheet_y_lo, sheet_y_hi;
    int     x_span, y_span;
    int64_t data_offset;
    int    *local_defect_indices;
    int     n_local_defects;
} Tile;

typedef struct {
    int first_tile;
    int tile_count;
} TileIndex;

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
 * Defined inline so the compiler can inline it into both solver_core.c
 * (hot DP inner loop — billions of calls) and _solver.c (reconstruction
 * wrapper — few calls) without requiring link-time optimization.
 * ============================================================================= */
static inline int32_t slab_lookup(const FdSlab  *slab,
                                  const int32_t *F_values,
                                  int            col_stride,
                                  int rect_width, int rect_height,
                                  int sheet_x,    int sheet_y) {

    const TileIndex *ti = &slab->tile_index[
        rect_width * (slab->sheet_height + 1) + rect_height];

    for (int t = 0; t < ti->tile_count; t++) {
        const Tile *tile = &slab->tiles[ti->first_tile + t];
        if (sheet_x >= tile->sheet_x_lo && sheet_x <= tile->sheet_x_hi &&
            sheet_y >= tile->sheet_y_lo && sheet_y <= tile->sheet_y_hi) {
            int local_x = sheet_x - tile->sheet_x_lo;
            int local_y = sheet_y - tile->sheet_y_lo;
            return slab->data[
                tile->data_offset + (int64_t)local_x * tile->y_span + local_y];
        }
    }

    return F_values[rect_width * col_stride + rect_height];
}


/* =============================================================================
 * SlabEstimate — returned by estimate_slab() for dry-run memory reporting
 * ============================================================================= */
typedef struct {
    int     tiles_1dx;
    int     tiles_1dy;
    int     tiles_2d;
    int64_t data_1dx;
    int64_t data_1dy;
    int64_t data_2d;
} SlabEstimate;


/* =============================================================================
 * Function declarations
 * ============================================================================= */

/* Phase 1 — g-table: best single-item tiling */
void fill_g_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *item_widths, int32_t *item_heights,
                  int32_t *item_areas, int n_items);

/* Phase 2 — F-table: optimal value for pure (defect-free) rectangles */
void fill_F_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *F_values, int8_t *F_decision_type,
                  int32_t *F_decision_param,
                  int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                  int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts);

/* Phase 3 — Fd-table via bottom-up slab */
FdSlab *fill_Fd_slab(int sheet_width, int sheet_height,
                     int32_t *defect_count_prefix, int32_t *F_values,
                     int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                     int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts,
                     int32_t *defect_array_in, int n_defects);

/* Dry-run: estimate slab memory for all three merge strategies */
SlabEstimate estimate_slab(int sheet_width, int sheet_height,
                           int32_t *defect_array, int n_defects);

#endif