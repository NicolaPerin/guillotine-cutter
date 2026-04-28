/* =============================================================================
 * solver_core.h — Public interface for the guillotine cutting-stock solver
 * ============================================================================= */

#ifndef SOLVER_CORE_H
#define SOLVER_CORE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>


/* =============================================================================
 * Decision type constants
 * ============================================================================= */
#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5


/* =============================================================================
 * Defect layout + helpers
 * ============================================================================= */
#define DEFECT_FIELD_COUNT 6

typedef struct {
    const int32_t *fields;
} DefectRef;

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
 * Prefix sum helper
 * ============================================================================= */
static inline int32_t defect_count_in_rect(const int32_t *prefix, int stride,
                                           int x0, int y0, int x1, int y1) {
    return prefix[x1 * stride + y1]
         - prefix[x0 * stride + y1]
         - prefix[x1 * stride + y0]
         + prefix[x0 * stride + y0];
}


/* =============================================================================
 * Sparse slab structures
 * ============================================================================= */

typedef struct {
    int      sheet_x_lo, sheet_x_hi;
    int      sheet_y_lo, sheet_y_hi;
    int      x_span, y_span;
    int64_t  data_offset;
} Tile;

typedef struct {
    int  tile_count;
    Tile first_tile_inline;
    int  overflow_start;
} TileIndex;

typedef struct {
    uint16_t  *data;
    Tile      *tiles;
    TileIndex *tile_index;
    uint8_t   *has_tiles;
    int64_t    total_data_entries;
    int        total_tile_count;
    int        sheet_width, sheet_height;
    int        overflow;
} FdSlab;


/* =============================================================================
 * Column resolver
 * ============================================================================= */

#define MAX_COL_SEGS 64

typedef struct {
    int32_t  pure_val;
    int      n_segs;
    struct {
        int       y_lo, y_hi;
        uint16_t *delta_col;
    } segs[MAX_COL_SEGS];
} ColRef;

static inline void resolve_col(ColRef *out, const FdSlab *slab, const int32_t *F_values,
                                int col_stride, int wp, int hp, int sx) {
    int wh = wp * col_stride + hp;
    out->pure_val = F_values[wh];
    out->n_segs   = 0;

    if (!slab->has_tiles[wh]) return;

    TileIndex *ti = &slab->tile_index[wh];

    for (int t = 0; t < ti->tile_count; t++) {
        Tile *tp = (t == 0)
            ? &ti->first_tile_inline
            : &slab->tiles[ti->overflow_start + t - 1];

        if (sx >= tp->sheet_x_lo && sx <= tp->sheet_x_hi) {
            if (out->n_segs >= MAX_COL_SEGS) {
                fprintf(stderr,
                        "ERROR: MAX_COL_SEGS (%d) exceeded for "
                        "(w=%d, h=%d, sx=%d)\n",
                        MAX_COL_SEGS, wp, hp, sx);
                abort();
            }

            int lx = sx - tp->sheet_x_lo;
            int s  = out->n_segs++;

            out->segs[s].y_lo = tp->sheet_y_lo;
            out->segs[s].y_hi = tp->sheet_y_hi;
            out->segs[s].delta_col =
                &slab->data[tp->data_offset + (int64_t)lx * tp->y_span];
        }
    }
}

static inline int32_t colref_get(const ColRef *r, int sy) {
    for (int s = 0; s < r->n_segs; s++) {
        if (sy >= r->segs[s].y_lo && sy <= r->segs[s].y_hi) {
            return r->pure_val
                 - r->segs[s].delta_col[sy - r->segs[s].y_lo];
        }
    }
    return r->pure_val;
}


/* =============================================================================
 * SlabEstimate
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
                     int32_t *defect_array_in, int n_defects,
                     int min_w, int min_h);

SlabEstimate estimate_slab(int sheet_width, int sheet_height,
                           int32_t *defect_array, int n_defects);

#endif