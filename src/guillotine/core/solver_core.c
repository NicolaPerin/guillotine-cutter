/* =============================================================================
 * solver_core.c — Pure C implementation of the guillotine cutting DP algorithm.
 *
 * No Python dependency. All algorithmic logic lives here.
 * ============================================================================= */

#include "solver_core.h"
#include <stdlib.h>
#include <string.h>

void fill_g_core(
    int W0, int H0,
    int32_t *g_values,
    int32_t *g_indices,
    int32_t *item_w,
    int32_t *item_h,
    int32_t *item_area,
    int n_items
) {
    int stride = H0 + 1;

    for (int w = 1; w <= W0; w++) {

        int w_stride = w * stride;
        int nx_table[n_items];

        for (int i = 0; i < n_items; i++)
            nx_table[i] = w / item_w[i];

        for (int h = 1; h <= H0; h++) {

            int32_t best_val = 0;
            int32_t best_idx = -1;

            for (int i = 0; i < n_items; i++) {
                int nx = nx_table[i];
                int ny = h / item_h[i];

                if (nx > 0 && ny > 0) {
                    int32_t val = item_area[i] * nx * ny;

                    if (val > best_val) {
                        best_val = val;
                        best_idx = i;
                    }
                }
            }

            g_values [w_stride + h] = best_val;
            g_indices[w_stride + h] = best_idx;
        }
    }
}

void fill_F_core(
    int W0, int H0,
    int32_t *g_values,
    int32_t *g_indices,
    int32_t *F_values,
    int8_t  *F_type,
    int32_t *F_param,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y
) {
    int stride = H0 + 1;

    for (int w = 1; w <= W0; w++) {
        int nx         = np_x_len[w];
        int w_stride   = w * stride;
        int x_cut_base = w * max_cuts_x;
        int half_w     = w >> 1;

        for (int h = 1; h <= H0; h++) {
            int ny         = np_y_len[h];
            int y_cut_base = h * max_cuts_y;
            int half_h     = h >> 1;

            int32_t g_idx    = g_indices[w_stride + h];
            int32_t best_val = g_values [w_stride + h];
            int     best_type  = (g_idx >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (g_idx >= 0) ? g_idx : 0;

            /* Vertical cuts — symmetry: only z <= w/2 */
            for (int i = 0; i < nx; i++) {
                int z = np_x_arr[x_cut_base + i];
                if (z > half_w) break;
                int32_t total = F_values[z * stride + h] + F_values[w_stride - z * stride + h];
                if (total > best_val) {
                    best_val   = total;
                    best_type  = DECISION_CUT_X;
                    best_param = z;
                }
            }

            /* Horizontal cuts — symmetry: only z <= h/2 */
            for (int i = 0; i < ny; i++) {
                int z = np_y_arr[y_cut_base + i];
                if (z > half_h) break;
                int32_t total = F_values[w_stride + z] + F_values[w_stride + (h - z)];
                if (total > best_val) {
                    best_val   = total;
                    best_type  = DECISION_CUT_Y;
                    best_param = z;
                }
            }

            F_values[w_stride + h] = best_val;
            F_type  [w_stride + h] = (int8_t)best_type;
            F_param [w_stride + h] = best_param;
        }
    }
}

void fill_Fd_core(
    int W0, int H0,
    int32_t *prefix,
    int32_t *F_values,
    int32_t *Fd_values,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y,
    int32_t *defects, int n_def
) {
    int stride_p = H0 + 1;
    int stride_F = H0 + 1;
    int64_t stride2 = H0 + 1;
    int64_t stride1 = (int64_t)(W0 + 1) * stride2;
    int64_t stride0 = (int64_t)(H0 + 1) * stride1;

    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w];
        int w_max_cuts_x = w * max_cuts_x;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h];
            int h_max_cuts_y = h * max_cuts_y;

            #pragma omp parallel for schedule(dynamic, 16) collapse(2)
            for (int x = 0; x <= W0 - w; x++) {

                for (int y = 0; y <= H0 - h; y++) {

                    int32_t defect_count =
                        IDX_2D(prefix, stride_p, x+w, y+h)
                      - IDX_2D(prefix, stride_p, x,   y+h)
                      - IDX_2D(prefix, stride_p, x+w, y  )
                      + IDX_2D(prefix, stride_p, x,   y  );

                    if (defect_count == 0) {
                        IDX_4D(Fd_values, stride0, stride1, stride2, w, h, x, y) =
                            IDX_2D(F_values, stride_F, w, h);
                        continue;
                    }

                    int32_t best_val = 0;

                    /* X cuts — normal pattern positions */
                    for (int i = 0; i < nx; i++) {
                        int z = np_x_arr[w_max_cuts_x + i];
                        int32_t total =
                            IDX_4D(Fd_values, stride0, stride1, stride2, z, h, x, y) +
                            IDX_4D(Fd_values, stride0, stride1, stride2, w-z, h, x+z, y);
                        if (total > best_val) best_val = total;
                    }

                    /* X cuts — defect-aligned positions */
                    for (int d = 0; d < n_def; d++) {
                        if (x >= DEF_X_END(d) || DEF_X(d) >= x + w) continue;
                        int z1 = DEF_X(d) - x;
                        int z2 = DEF_X_END(d) - x;
                        if (z1 > 0 && z1 < w) {
                            int32_t total =
                                IDX_4D(Fd_values, stride0, stride1, stride2, z1, h, x, y) +
                                IDX_4D(Fd_values, stride0, stride1, stride2, w-z1, h, x+z1, y);
                            if (total > best_val) best_val = total;
                        }
                        if (z2 > 0 && z2 < w) {
                            int32_t total =
                                IDX_4D(Fd_values, stride0, stride1, stride2, z2, h, x, y) +
                                IDX_4D(Fd_values, stride0, stride1, stride2, w-z2, h, x+z2, y);
                            if (total > best_val) best_val = total;
                        }
                    }

                    /* Y cuts — normal pattern positions */
                    for (int i = 0; i < ny; i++) {
                        int z = np_y_arr[h_max_cuts_y + i];
                        int32_t total =
                            IDX_4D(Fd_values, stride0, stride1, stride2, w, z, x, y) +
                            IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z, x, y+z);
                        if (total > best_val) best_val = total;
                    }

                    /* Y cuts — defect-aligned positions */
                    for (int d = 0; d < n_def; d++) {
                        if (y >= DEF_Y_END(d) || DEF_Y(d) >= y + h) continue;
                        int z1 = DEF_Y(d) - y;
                        int z2 = DEF_Y_END(d) - y;
                        if (z1 > 0 && z1 < h) {
                            int32_t total =
                                IDX_4D(Fd_values, stride0, stride1, stride2, w, z1, x, y) +
                                IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z1, x, y+z1);
                            if (total > best_val) best_val = total;
                        }
                        if (z2 > 0 && z2 < h) {
                            int32_t total =
                                IDX_4D(Fd_values, stride0, stride1, stride2, w, z2, x, y) +
                                IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z2, x, y+z2);
                            if (total > best_val) best_val = total;
                        }
                    }

                    IDX_4D(Fd_values, stride0, stride1, stride2, w, h, x, y) = best_val;
                }
            }
        }
    }
}
