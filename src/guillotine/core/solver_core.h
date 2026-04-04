#ifndef SOLVER_CORE_H
#define SOLVER_CORE_H

#include <stdint.h>

/* Decision type constants — must match constants.py */
#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5

/* Defect array access macros (each defect entry has NR_DEFECT_FIELDS fields) */
#define NR_DEFECT_FIELDS 6
#define DEF_X(d)     defects[(d)*NR_DEFECT_FIELDS + 0]
#define DEF_Y(d)     defects[(d)*NR_DEFECT_FIELDS + 1]
#define DEF_W(d)     defects[(d)*NR_DEFECT_FIELDS + 2]
#define DEF_H(d)     defects[(d)*NR_DEFECT_FIELDS + 3]
#define DEF_X_END(d) defects[(d)*NR_DEFECT_FIELDS + 4]
#define DEF_Y_END(d) defects[(d)*NR_DEFECT_FIELDS + 5]

/*
 * Bit layout for Fd_packed (int16_t):
 *   bits 15-13 : decision type
 *   bits 12-0  : cut parameter
 */
#define PACK_FD(type, param)   (((int16_t)(type) << 13) | ((int16_t)(param) & 0x1FFF))
#define UNPACK_TYPE(packed)    (((packed) >> 13) & 0x7)
#define UNPACK_PARAM(packed)   ((packed) & 0x1FFF)

/* Array index macros */
#define IDX_2D(arr, stride1, i, j) ((arr)[(i) * (stride1) + (j)])
#define IDX_4D(arr, stride0, stride1, stride2, w, h, x, y) \
    ((arr)[(int64_t)(w) * (stride0) + (int64_t)(h) * (stride1) + (int64_t)(x) * (stride2) + (y)])

/* Function declarations */
void fill_g_core(
    int W0, int H0,
    int32_t *g_values,
    int32_t *g_indices,
    int32_t *item_w,
    int32_t *item_h,
    int32_t *item_area,
    int n_items
);

void fill_F_core(
    int W0, int H0,
    int32_t *g_values,
    int32_t *g_indices,
    int32_t *F_values,
    int8_t  *F_type,
    int32_t *F_param,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y
);

void fill_Fd_core(
    int W0, int H0,
    int32_t *prefix,
    int32_t *F_values,
    int32_t *Fd_values,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y,
    int32_t *defects, int n_def
);

#endif /* SOLVER_CORE_H */
