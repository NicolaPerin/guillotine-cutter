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

// --- GPU Data Structures ---
typedef struct {
    uint64_t key;
    uint16_t delta;
} HashEntry;

typedef struct {
    HashEntry *entries;
    size_t capacity;
    size_t mask;
    int64_t count;
} GPUMap;

typedef struct {
    int sheet_width;
    int sheet_height;
    GPUMap *map;
    int overflow;
} FdSlab;

typedef struct {
    int tiles_1dx, tiles_1dy, tiles_2d;
    int64_t data_1dx, data_1dy, data_2d;
} SlabEstimate;

void fill_g_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *item_widths, int32_t *item_heights, int32_t *item_areas, int n_items);
void fill_F_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *F_values, int8_t *F_decision_type, int32_t *F_decision_param, int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts, int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts);
FdSlab *fill_Fd_slab(int sheet_width, int sheet_height, int32_t *defect_count_prefix, int32_t *F_values, int32_t *defect_array_in, int n_defects);
SlabEstimate estimate_slab(int sheet_width, int sheet_height, int32_t *defect_array_in, int n_defects);

#endif