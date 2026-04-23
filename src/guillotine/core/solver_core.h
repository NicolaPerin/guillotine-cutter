#ifndef SOLVER_CORE_H
#define SOLVER_CORE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>

#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5

#define DEFECT_FIELD_COUNT 6
typedef struct { const int32_t *fields; } DefectRef;

static inline DefectRef defect_at(const int32_t *arr, int idx) {
    DefectRef r; r.fields = arr + idx * DEFECT_FIELD_COUNT; return r;
}
static inline int defect_x_start(DefectRef d) { return d.fields[0]; }
static inline int defect_y_start(DefectRef d) { return d.fields[1]; }
static inline int defect_x_end  (DefectRef d) { return d.fields[4]; }
static inline int defect_y_end  (DefectRef d) { return d.fields[5]; }

static inline int defect_count_in_rect(const int32_t *prefix, int stride, int x1, int y1, int x2, int y2) {
    return prefix[x2 * stride + y2] - prefix[x1 * stride + y2] - prefix[x2 * stride + y1] + prefix[x1 * stride + y1];
}

// Restored Original Dense Tile Structures
typedef struct {
    int16_t sheet_x_lo, sheet_x_hi, sheet_y_lo, sheet_y_hi;
    int16_t x_span, y_span;
    int64_t data_offset;
} Tile;

typedef struct {
    int tile_count;
    Tile first_tile_inline;
    int overflow_start;
} TileIndex;

typedef struct {
    int sheet_width, sheet_height;
    uint8_t *has_tiles;
    TileIndex *tile_index;
    Tile *tiles;
    int total_tile_count;
    uint16_t *data;
    int64_t total_data_entries;
    int overflow;
} FdSlab;

typedef struct {
    int32_t pure_val;
    bool is_pure;
    int16_t y_span;
    int16_t sheet_y_lo;
    const uint16_t *tdata;
} ColRef;

// CPU Resolve helper for backtracking
static inline void resolve_col(ColRef *cr, FdSlab *slab, int32_t *F_values, int col_stride, int w, int h, int sx) {
    int wh_idx = w * col_stride + h;
    cr->pure_val = F_values[wh_idx];
    cr->is_pure = true;
    if (!slab->has_tiles[wh_idx]) return;
    TileIndex *ti = &slab->tile_index[wh_idx];
    if (ti->tile_count == 0) return;

    int L = 0, R = ti->tile_count - 1;
    while (L <= R) {
        int M = L + (R - L) / 2;
        Tile *t = (M == 0) ? &ti->first_tile_inline : &slab->tiles[ti->overflow_start + M - 1];
        if (sx < t->sheet_x_lo) R = M - 1;
        else if (sx > t->sheet_x_hi) L = M + 1;
        else {
            cr->is_pure = false;
            cr->y_span = t->y_span;
            cr->sheet_y_lo = t->sheet_y_lo;
            cr->tdata = &slab->data[t->data_offset + (sx - t->sheet_x_lo) * t->y_span];
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

typedef struct { int tiles_1dx, tiles_1dy, tiles_2d; int64_t data_1dx, data_1dy, data_2d; } SlabEstimate;

void fill_g_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *item_widths, int32_t *item_heights, int32_t *item_areas, int n_items);
void fill_F_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *F_values, int8_t *F_decision_type, int32_t *F_decision_param, int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts, int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts);
FdSlab *fill_Fd_slab(int sheet_width, int sheet_height, int32_t *defect_count_prefix, int32_t *F_values, int32_t *defect_array_in, int n_defects);
SlabEstimate estimate_slab(int sheet_width, int sheet_height, int32_t *defect_array_in, int n_defects);

// GPU execution hook
void execute_phase_e_gpu_wavefront(FdSlab* slab, int sheet_width, int sheet_height, int col_stride, const int32_t* host_defect_prefix, const int32_t* host_F_values);

#endif