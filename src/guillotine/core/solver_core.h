#ifndef SOLVER_CORE_H
#define SOLVER_CORE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

/* =============================================================================
 * Decision codes — stored in the pure_decision_type table to record which
 * choice was optimal for each rectangle size during the pure-table DP.
 * ============================================================================= */
#define DECISION_EMPTY  0   /* no item fits, rectangle is waste            */
#define DECISION_FILL   1   /* tile with copies of one item type           */
#define DECISION_CUT_X  2   /* vertical guillotine cut at pure_decision_param */
#define DECISION_CUT_Y  3   /* horizontal guillotine cut at pure_decision_param */
#define DECISION_DEFECT 4   /* rectangle contains a defect, value is zero  */
#define DECISION_PURE   5   /* rectangle is pure, use pure_values directly */

/* =============================================================================
 * Defect representation
 *
 * Each defect is stored as a flat array of DEFECT_FIELD_COUNT int32 fields.
 * DefectRef is a lightweight view into that array — it holds a pointer to
 * the first field of one defect record and provides named accessors.
 * ============================================================================= */
#define DEFECT_FIELD_COUNT 6

typedef struct { const int32_t *fields; } DefectRef;

static inline DefectRef defect_at(const int32_t *arr, int idx) {
    DefectRef r; r.fields = arr + idx * DEFECT_FIELD_COUNT; return r;
}
static inline int defect_x_start(DefectRef d) { return d.fields[0]; }
static inline int defect_y_start(DefectRef d) { return d.fields[1]; }
static inline int defect_x_end  (DefectRef d) { return d.fields[4]; }
static inline int defect_y_end  (DefectRef d) { return d.fields[5]; }

/* =============================================================================
 * defect_count_in_rect — 2D prefix sum query
 *
 * Returns the number of defects whose bounding box intersects the rectangle
 * [x1, x2) × [y1, y2), using a precomputed 2D prefix sum array.
 * ============================================================================= */
static inline int defect_count_in_rect(const int32_t *prefix, int stride,
                                        int x1, int y1, int x2, int y2) {
    return prefix[x2 * stride + y2] - prefix[x1 * stride + y2]
         - prefix[x2 * stride + y1] + prefix[x1 * stride + y1];
}

/* =============================================================================
 * Tile — a rectangular region of sheet positions (sx, sy) for a fixed (w, h)
 *
 * Represents the set of placements [sheet_x_lo, sheet_x_hi] × [sheet_y_lo,
 * sheet_y_hi] where a rectangle of size w × h overlaps at least one defect.
 * data_offset is the index into DefectSlab.data[] where this tile's delta
 * values begin, laid out in column-major order: [lx * y_span + ly].
 * ============================================================================= */
typedef struct {
    int16_t sheet_x_lo, sheet_x_hi;
    int16_t sheet_y_lo, sheet_y_hi;
    int16_t x_span, y_span;
    int64_t data_offset;
} Tile;

/* =============================================================================
 * TileIndex — per-(w,h) tile directory
 *
 * The first tile is stored inline to avoid a pointer indirection in the common
 * case where a (w, h) pair has only one tile. Additional tiles (overflow) are
 * stored in DefectSlab.tiles[] starting at overflow_start.
 * ============================================================================= */
typedef struct {
    int  tile_count;
    Tile first_tile_inline;
    int  overflow_start;
} TileIndex;

/* =============================================================================
 * DefectSlab — sparse storage for defect-adjusted rectangle values
 *
 * For each rectangle size (w, h) and each sheet position (sx, sy) where
 * placing that rectangle overlaps at least one defect, the slab stores:
 *
 *   delta = pure_values[w][h] - Fd(w, h, sx, sy)
 *
 * as a uint16. Positions outside any tile are implicitly pure (delta = 0),
 * meaning Fd(w, h, sx, sy) == pure_values[w][h].
 *
 * has_tiles[w * col_stride + h]  — nonzero if (w, h) has any tiles
 * tile_index[w * col_stride + h] — TileIndex for (w, h)
 * tiles[]                        — overflow tiles for (w, h) pairs with > 1 tile
 * data[]                         — flat uint16 delta array
 * overflow                       — set to 1 if any delta exceeded uint16 range
 * ============================================================================= */
typedef struct {
    int        sheet_width, sheet_height;
    uint8_t   *has_tiles;
    TileIndex *tile_index;
    Tile      *tiles;
    int        total_tile_count;
    uint16_t  *data;
    int64_t    total_data_entries;
    int        overflow;
} DefectSlab;

/* =============================================================================
 * ColRef — resolved column reference used during backtracking and CPU fill
 *
 * resolve_col() populates a ColRef for a given (w, h, sx). If sx falls inside
 * a tile, is_pure is false and tdata points to the column's delta values.
 * colref_get() then returns pure_val - tdata[ly] for impure positions, or
 * pure_val directly for pure ones.
 * ============================================================================= */
typedef struct {
    int32_t         pure_val;
    bool            is_pure;
    int16_t         y_span;
    int16_t         sheet_y_lo;
    const uint16_t *tdata;
} ColRef;

static inline void resolve_col(ColRef *cr, DefectSlab *slab,
                                int32_t *pure_values, int col_stride,
                                int w, int h, int sx) {
    int wh_idx = w * col_stride + h;
    cr->pure_val = pure_values[wh_idx];
    cr->is_pure  = true;

    if (!slab->has_tiles[wh_idx]) return;
    TileIndex *ti = &slab->tile_index[wh_idx];
    if (ti->tile_count == 0) return;

    /* Binary search over x-sorted, x-disjoint tiles. Disjointness is
     * guaranteed by the MERGE_1D_X strategy used during slab construction,
     * which is required for the binary search to be correct. */
    int L = 0, R = ti->tile_count - 1;
    while (L <= R) {
        int M = L + (R - L) / 2;
        Tile *t = (M == 0) ? &ti->first_tile_inline
                           : &slab->tiles[ti->overflow_start + M - 1];
        if      (sx < t->sheet_x_lo) R = M - 1;
        else if (sx > t->sheet_x_hi) L = M + 1;
        else {
            cr->is_pure    = false;
            cr->y_span     = t->y_span;
            cr->sheet_y_lo = t->sheet_y_lo;
            cr->tdata      = &slab->data[t->data_offset
                             + (sx - t->sheet_x_lo) * t->y_span];
            return;
        }
    }
}

static inline int32_t colref_get(ColRef *cr, int sy) {
    if (cr->is_pure) return cr->pure_val;
    int ly = sy - cr->sheet_y_lo;
    if (ly >= 0 && ly < cr->y_span) return cr->pure_val - cr->tdata[ly];
    return cr->pure_val;
}

/* =============================================================================
 * Public API
 * ============================================================================= */

/* Phase 1: for each rectangle size (w, h), compute the best value achievable
 * by tiling with a single item type and record which item type achieved it. */
void fill_tiling_table(int sheet_width, int sheet_height,
                       int32_t *tiling_values, int32_t *tiling_item_index,
                       int32_t *item_widths, int32_t *item_heights,
                       int32_t *item_areas, int n_items);

/* Phase 2: bottom-up DP for defect-free rectangles. For each (w, h), find the
 * best value over all item tilings and guillotine cuts, using normal-pattern
 * candidate sets. Records the optimal decision for backtracking. */
void fill_pure_table(int sheet_width, int sheet_height,
                     int32_t *tiling_values, int32_t *tiling_item_index,
                     int32_t *pure_values, int8_t *pure_decision_type,
                     int32_t *pure_decision_param,
                     int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts,
                     int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts);

/* Phase 3: build and fill the sparse DefectSlab. Rectangles smaller than
 * min_w × min_h cannot contain any item and are skipped. The fill is
 * dispatched to the GPU if available, otherwise falls back to CPU. */
DefectSlab *fill_defect_slab(int sheet_width, int sheet_height,
                              int32_t *defect_count_prefix, int32_t *pure_values,
                              int32_t *defect_array_in, int n_defects,
                              int min_w, int min_h);

/* CPU implementation of the defect slab fill (OpenMP, sequential wh order). */
void fill_defect_slab_cpu(DefectSlab *slab, int sheet_width, int sheet_height,
                           int col_stride, int32_t *defect_count_prefix,
                           int32_t *pure_values);

/* GPU implementation of the defect slab fill (CUDA diagonal wavefront). */
#ifdef HAVE_CUDA
int fill_defect_slab_gpu(DefectSlab *slab, int sheet_width, int sheet_height,
                           int col_stride, const int32_t *defect_count_prefix,
                           const int32_t *pure_values);
#endif
#endif