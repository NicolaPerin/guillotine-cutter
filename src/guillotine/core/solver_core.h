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
 *                        sparse "multi-tile slab" with uint16 delta encoding
 *                        (stored = F[w][h] - Fd) to halve the slab memory
 *                        vs the raw-int32 design.
 * ============================================================================= */

#ifndef SOLVER_CORE_H
#define SOLVER_CORE_H

#include <stdint.h>
#include <stdlib.h>


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
 * The defect array is a flat int32_t buffer.  Each defect occupies
 * DEFECT_FIELD_COUNT consecutive entries in this order:
 *   [0] x_start  — left edge of the bounding box
 *   [1] y_start  — bottom edge
 *   [2] width
 *   [3] height
 *   [4] x_end    — right edge  (exclusive: x_start + width)
 *   [5] y_end    — top edge    (exclusive: y_start + height)
 *
 * Use the DefectRef helpers below instead of indexing the raw array directly.
 * ============================================================================= */
#define DEFECT_FIELD_COUNT 6

typedef struct { const int32_t *fields; } DefectRef;

static inline DefectRef defect_at(const int32_t *arr, int idx) {
    DefectRef r; r.fields = arr + idx * DEFECT_FIELD_COUNT; return r;
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
 * Standard inclusion-exclusion: P[x1][y1] - P[x0][y1] - P[x1][y0] + P[x0][y0]
 * ============================================================================= */
static inline int32_t defect_count_in_rect(const int32_t *prefix, int stride,
                                           int x0, int y0, int x1, int y1) {
    return prefix[x1 * stride + y1]
         - prefix[x0 * stride + y1]
         - prefix[x1 * stride + y0]
         + prefix[x0 * stride + y0];
}


/* =============================================================================
 * Sparse Fd storage — multi-tile slab with uint16 delta encoding
 *
 * The 4D table Fd(w, h, x, y) gives the optimal value for a defected
 * rectangle of size w×h placed at sheet position (x, y).
 *
 * Only positions where the rectangle overlaps at least one defect need
 * explicit storage — all other positions equal F_values[w][h] and are
 * recovered without a table lookup.
 *
 * TILES
 *   For each (w, h) pair, the affected region is represented as a small
 *   set of disjoint rectangular tiles.  Each tile covers a contiguous
 *   sub-range of (x, y) positions and owns a slice of the flat data[]
 *   array.  The tile index (TileIndex) for each (w, h) is stored in a
 *   flat array of size (sheet_width+1) × (sheet_height+1).
 *
 * DELTA ENCODING
 *   data[i]  =  F_values[w][h] - Fd(w, h, x, y)   as uint16.
 *   Since Fd <= F always, delta >= 0.  FdSlab.overflow is set if any
 *   delta exceeds UINT16_MAX; callers should treat the slab as invalid.
 *
 * LOOKUP OPTIMIZATIONS
 *   has_tiles[] — checked first; pure (w,h) pairs return F_values[w][h]
 *   without touching tile_index[] or data[].
 *
 *   Inlined first tile — TileIndex embeds one Tile directly.  Single-tile
 *   (w,h) pairs (the common case) need only one indirection to reach
 *   tile bounds, then one more for data[].  Overflow tiles (tile_count > 1)
 *   are stored in the separate FdSlab.tiles[] overflow array.
 * ============================================================================= */

/* Tile — one rectangular region of affected (x, y) positions for a
 * given (w, h) pair.
 *
 * Phase E fills data[data_offset .. data_offset + x_span*y_span - 1]
 * with uint16 deltas for every position in the tile.  Positions within
 * the tile that are defect-free get delta = 0 (Fd == F).
 */
typedef struct {
    int      sheet_x_lo, sheet_x_hi;
    int      sheet_y_lo, sheet_y_hi;
    int      x_span, y_span;
    int64_t  data_offset;
} Tile;

/* TileIndex — one entry per (w, h) pair.
 *
 * tile_count == 0  → pure pair; slab_lookup returns F_values[w][h].
 * tile_count == 1  → only first_tile_inline used; overflow_start ignored.
 * tile_count >  1  → first_tile_inline is tile 0;
 *                    tiles[overflow_start .. overflow_start + tile_count - 2]
 *                    are tiles 1..N-1.
 */
typedef struct {
    int  tile_count;
    Tile first_tile_inline;
    int  overflow_start;
} TileIndex;

typedef struct {
    uint16_t  *data;               /* delta = F[w][h] - Fd, as uint16      */
    Tile      *tiles;              /* overflow tiles (index >= 1 per wh)   */
    TileIndex *tile_index;
    uint8_t   *has_tiles;          /* 1 iff tile_count >= 1 for this (w,h) */
    int64_t    total_data_entries;
    int        total_tile_count;   /* entries used in tiles[] (overflow)   */
    int        sheet_width, sheet_height;
    int        overflow;           /* set if any delta exceeded UINT16_MAX */
} FdSlab;


/* =============================================================================
 * slab_lookup — retrieve Fd(rect_width, rect_height, sheet_x, sheet_y)
 *
 * Fast path (pure pair):     has_tiles[] == 0  →  return F_values[w][h].
 * Common path (1 tile):      check first_tile_inline (inlined in TileIndex).
 * Rare path (>1 tiles):      scan overflow tiles[overflow_start ..].
 *
 * Defined inline so it can be used both in solver_core.c (Phase E, billions
 * of calls) and in _solver.c (reconstruction, few calls) without LTO.
 *
 * NOTE: slab_lookup does NOT access defect_pool.  The pool is used only
 * during Phase E fill (building cut candidates), not during lookup.
 * ============================================================================= */
static inline int32_t slab_lookup(const FdSlab  *slab,
                                  const int32_t *F_values,
                                  int            col_stride,
                                  int rect_width, int rect_height,
                                  int sheet_x,    int sheet_y) {

    int wh_idx = rect_width * (slab->sheet_height + 1) + rect_height;
    int32_t F_wh = F_values[rect_width * col_stride + rect_height];

    if (!slab->has_tiles[wh_idx])
        return F_wh;

    const TileIndex *ti   = &slab->tile_index[wh_idx];
    const Tile      *tile = &ti->first_tile_inline;

    if (sheet_x >= tile->sheet_x_lo && sheet_x <= tile->sheet_x_hi &&
        sheet_y >= tile->sheet_y_lo && sheet_y <= tile->sheet_y_hi) {
        uint16_t delta = slab->data[
            tile->data_offset
            + (int64_t)(sheet_x - tile->sheet_x_lo) * tile->y_span
            + (sheet_y - tile->sheet_y_lo)];
        return F_wh - (int32_t)delta;
    }

    for (int t = 0; t < ti->tile_count - 1; t++) {
        tile = &slab->tiles[ti->overflow_start + t];
        if (sheet_x >= tile->sheet_x_lo && sheet_x <= tile->sheet_x_hi &&
            sheet_y >= tile->sheet_y_lo && sheet_y <= tile->sheet_y_hi) {
            uint16_t delta = slab->data[
                tile->data_offset
                + (int64_t)(sheet_x - tile->sheet_x_lo) * tile->y_span
                + (sheet_y - tile->sheet_y_lo)];
            return F_wh - (int32_t)delta;
        }
    }

    return F_wh;
}


/* =============================================================================
 * SlabEstimate — returned by estimate_slab() for dry-run memory reporting
 * ============================================================================= */
typedef struct {
    int     tiles_1dx, tiles_1dy, tiles_2d;
    int64_t data_1dx,  data_1dy,  data_2d;
} SlabEstimate;


/* =============================================================================
 * Function declarations
 * ============================================================================= */

void fill_g_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *item_widths, int32_t *item_heights,
                  int32_t *item_areas, int n_items);

void fill_F_table(int sheet_width, int sheet_height,
                  int32_t *g_values, int32_t *g_item_index,
                  int32_t *F_values, int8_t *F_decision_type,
                  int32_t *F_decision_param,
                  int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                  int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts);

FdSlab *fill_Fd_slab(int sheet_width, int sheet_height,
                     int32_t *defect_count_prefix, int32_t *F_values,
                     int32_t *defect_array_in, int n_defects);

SlabEstimate estimate_slab(int sheet_width, int sheet_height,
                           int32_t *defect_array, int n_defects);

#endif