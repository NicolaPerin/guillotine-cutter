#include "solver_core.h"
#include <stdlib.h>
#include <string.h>

extern void execute_phase_e_gpu_wavefront(FdSlab* slab, int sheet_width, int sheet_height, int col_stride, const int32_t* host_defect_prefix, const int32_t* host_F_values);

typedef struct { int x_lo; int x_hi; int y_lo; int y_hi; } PositionInterval;
typedef enum { MERGE_1D_X } MergeStrategy;

static int cmp_defects_x(const void *a, const void *b) {
    int32_t xa = *(const int32_t *)a, xb = *(const int32_t *)b;
    return (xa > xb) - (xa < xb);
}

static int cmp_iv_x(const void *a, const void *b) { 
    return ((const PositionInterval *)a)->x_lo - ((const PositionInterval *)b)->x_lo; 
}

static int build_tiles(int rw, int rh, int sw, int sh, const int32_t *defects, int n_defects, PositionInterval *scratch, Tile *out, MergeStrategy strategy) {
    (void)strategy; 
    int max_sx = sw - rw;
    int max_sy = sh - rh;
    int n = 0;
    
    for (int d = 0; d < n_defects; d++) {
        DefectRef df = defect_at(defects, d);
        int xl = defect_x_start(df) - rw + 1; if (xl < 0) xl = 0;
        int xh = defect_x_end(df) - 1; if (xh > max_sx) xh = max_sx;
        int yl = defect_y_start(df) - rh + 1; if (yl < 0) yl = 0;
        int yh = defect_y_end(df) - 1; if (yh > max_sy) yh = max_sy;
        if (xl <= xh && yl <= yh) {
            scratch[n].x_lo = xl; scratch[n].x_hi = xh;
            scratch[n].y_lo = yl; scratch[n].y_hi = yh;
            n++;
        }
    }
    if (n == 0) return 0;
    
    qsort(scratch, n, sizeof(PositionInterval), cmp_iv_x);
    int n_tiles = 0;
    PositionInterval cur = scratch[0];
    
    for (int i = 1; i < n; i++) {
        if (scratch[i].x_lo <= cur.x_hi + 1) { 
            if (scratch[i].x_hi > cur.x_hi) cur.x_hi = scratch[i].x_hi;
            if (scratch[i].y_hi > cur.y_hi) cur.y_hi = scratch[i].y_hi;
            if (scratch[i].x_lo < cur.x_lo) cur.x_lo = scratch[i].x_lo;
            if (scratch[i].y_lo < cur.y_lo) cur.y_lo = scratch[i].y_lo;
        } else { 
            out[n_tiles].sheet_x_lo = cur.x_lo; out[n_tiles].sheet_x_hi = cur.x_hi;
            out[n_tiles].sheet_y_lo = cur.y_lo; out[n_tiles].sheet_y_hi = cur.y_hi;
            out[n_tiles].x_span = cur.x_hi - cur.x_lo + 1; out[n_tiles].y_span = cur.y_hi - cur.y_lo + 1;
            n_tiles++;
            cur = scratch[i];
        }
    }
    
    out[n_tiles].sheet_x_lo = cur.x_lo; out[n_tiles].sheet_x_hi = cur.x_hi;
    out[n_tiles].sheet_y_lo = cur.y_lo; out[n_tiles].sheet_y_hi = cur.y_hi;
    out[n_tiles].x_span = cur.x_hi - cur.x_lo + 1; out[n_tiles].y_span = cur.y_hi - cur.y_lo + 1;
    return n_tiles + 1;
}

void fill_g_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *item_widths, int32_t *item_heights, int32_t *item_areas, int n_items) {
    int col_stride = sheet_height + 1;
    for (int rw = 1; rw <= sheet_width; rw++) {
        int base = rw * col_stride;
        for (int rh = 1; rh <= sheet_height; rh++) {
            int32_t best_value = 0, best_item = -1;
            for (int i = 0; i < n_items; i++) {
                int cx = rw / item_widths[i], cy = rh / item_heights[i];
                if (cx > 0 && cy > 0) {
                    int32_t area = item_areas[i] * cx * cy;
                    if (area > best_value) { best_value = area; best_item = i; }
                }
            }
            g_values[base + rh] = best_value;
            g_item_index[base + rh] = best_item;
        }
    }
}

void fill_F_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *F_values, int8_t *F_decision_type, int32_t *F_decision_param, int32_t *normal_cuts_x, int32_t *n_normal_cuts_x, int max_x_cuts, int32_t *normal_cuts_y, int32_t *n_normal_cuts_y, int max_y_cuts) {
    int col_stride = sheet_height + 1;
    for (int rw = 1; rw <= sheet_width; rw++) {
        int base = rw * col_stride;
        int nxc = n_normal_cuts_x[rw], xcb = rw * max_x_cuts, hw = rw >> 1;
        for (int rh = 1; rh <= sheet_height; rh++) {
            int nyc = n_normal_cuts_y[rh], ycb = rh * max_y_cuts, hh = rh >> 1;
            int32_t best_item = g_item_index[base + rh], best_value = g_values[base + rh];
            int8_t best_type = (best_item >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (best_item >= 0) ? best_item : 0;

            for (int ci = 0; ci < nxc; ci++) {
                int z = normal_cuts_x[xcb + ci];
                if (z > hw) break;
                int32_t v = F_values[z * col_stride + rh] + F_values[(rw - z) * col_stride + rh];
                if (v > best_value) { best_value = v; best_type = DECISION_CUT_X; best_param = z; }
            }
            for (int ci = 0; ci < nyc; ci++) {
                int z = normal_cuts_y[ycb + ci];
                if (z > hh) break;
                int32_t v = F_values[base + z] + F_values[base + (rh - z)];
                if (v > best_value) { best_value = v; best_type = DECISION_CUT_Y; best_param = z; }
            }
            F_values[base + rh] = best_value;
            F_decision_type[base + rh] = best_type;
            F_decision_param[base + rh] = best_param;
        }
    }
}

FdSlab *fill_Fd_slab(int sheet_width, int sheet_height, int32_t *defect_count_prefix, int32_t *F_values, int32_t *defect_array_in, int n_defects) {
    int col_stride = sheet_height + 1;
    int wh_count = (sheet_width + 1) * col_stride;
    
    FdSlab *slab = calloc(1, sizeof(FdSlab));
    slab->sheet_width = sheet_width;
    slab->sheet_height = sheet_height;
    slab->has_tiles = calloc(wh_count, sizeof(uint8_t));
    slab->tile_index = calloc(wh_count, sizeof(TileIndex));
    
    if (n_defects == 0) {
        slab->tiles = NULL;
        slab->data = NULL;
        slab->total_tile_count = 0;
        slab->total_data_entries = 0;
        return slab;
    }

    int32_t *defects = malloc(n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    memcpy(defects, defect_array_in, n_defects * DEFECT_FIELD_COUNT * sizeof(int32_t));
    if (n_defects > 1) qsort(defects, n_defects, sizeof(int32_t) * DEFECT_FIELD_COUNT, cmp_defects_x);

    PositionInterval *iv = malloc(n_defects * sizeof(PositionInterval));
    Tile *temp_tiles = malloc(wh_count * n_defects * sizeof(Tile));
    int t_count = 0;
    int64_t data_count = 0;

    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {
            int wh_idx = w * col_stride + h;
            int local_t_start = t_count;
            
            int local_tiles = build_tiles(w, h, sheet_width, sheet_height, defects, n_defects, iv, &temp_tiles[t_count], MERGE_1D_X);
            
            if (local_tiles > 0) {
                slab->has_tiles[wh_idx] = 1;
                slab->tile_index[wh_idx].tile_count = local_tiles;
                
                // --- THE FIX: Calculate offsets FIRST ---
                for (int i = 0; i < local_tiles; i++) {
                    temp_tiles[local_t_start + i].data_offset = data_count;
                    data_count += (int64_t)temp_tiles[local_t_start + i].x_span * temp_tiles[local_t_start + i].y_span;
                }
                
                // --- NOW assign to the inline tile with the correct offset ---
                slab->tile_index[wh_idx].first_tile_inline = temp_tiles[local_t_start];
                if (local_tiles > 1) slab->tile_index[wh_idx].overflow_start = local_t_start + 1;
                
                t_count += local_tiles;
            }
        }
    }

    slab->total_tile_count = t_count;
    slab->total_data_entries = data_count;
    slab->tiles = temp_tiles; 
    
    if (data_count > 0) {
        slab->data = malloc(data_count * sizeof(uint16_t));
        execute_phase_e_gpu_wavefront(slab, sheet_width, sheet_height, col_stride, defect_count_prefix, F_values);
    } else {
        slab->data = NULL;
    }
    
    free(defects);
    free(iv);
    return slab;
}

SlabEstimate estimate_slab(int sheet_width, int sheet_height, int32_t *defect_array_in, int n_defects) {
    SlabEstimate est = {0}; return est;
}