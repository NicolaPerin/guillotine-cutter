#include "solver_core.h"
#include <omp.h>

extern FdSlab* execute_dp_gpu(int sheet_width, int sheet_height, int col_stride, const int32_t* host_defect_prefix, const int32_t* host_F_values, size_t required_map_capacity);

void fill_g_table(int sheet_width, int sheet_height, int32_t *g_values, int32_t *g_item_index, int32_t *item_widths, int32_t *item_heights, int32_t *item_areas, int n_items) {
    int col_stride = sheet_height + 1;
    #pragma omp parallel for schedule(dynamic)
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
    size_t total_impure_states = 0;
    for (int w = 1; w <= sheet_width; w++) {
        for (int h = 1; h <= sheet_height; h++) {
            for (int sx = 0; sx <= sheet_width - w; sx++) {
                for (int sy = 0; sy <= sheet_height - h; sy++) {
                    if (defect_count_in_rect(defect_count_prefix, col_stride, sx, sy, sx + w, sy + h) > 0) {
                        total_impure_states++;
                    }
                }
            }
        }
    }
    fprintf(stderr, "Launching CUDA DP... (%zu impure states)\n", total_impure_states);
    return execute_dp_gpu(sheet_width, sheet_height, col_stride, defect_count_prefix, F_values, total_impure_states);
}

SlabEstimate estimate_slab(int sheet_width, int sheet_height, int32_t *defect_array_in, int n_defects) {
    SlabEstimate est = {0}; return est;
}