#define PY_SSIZE_T_CLEAN // Ensure Py_ssize_t is defined before including Python.h
#include <Python.h> // Python C API header
#include <stdint.h> // for fixed-width integer types like uint32_t, uint16_t

/* Decision type constants - must match constants.py */
#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5

/*
 * Bit layout for Fd_packed (uint16_t):
 *   bits 15-13 : decision type (DECISION_* constants, values 0-5, fits in 3 bits)
 *   bits 12-0  : cut parameter (cut position z, max value 8191, covers any practical sheet)
 *
 * Pack:   packed = ((uint16_t)type << 13) | ((uint16_t)param & 0x1FFF)
 * Unpack: type   = (packed >> 13) & 0x7
 *         param  = packed & 0x1FFF
 */
#define PACK_FD(type, param)   (((uint16_t)(type) << 13) | ((uint16_t)(param) & 0x1FFF))
#define UNPACK_TYPE(packed)    (((packed) >> 13) & 0x7)
#define UNPACK_PARAM(packed)   ((packed) & 0x1FFF)

/*
 * Array index macros.
 * prefix, F_values are 2D: shape (W0+1, H0+1)
 * Fd_values is 4D uint32_t: shape (W0+1, H0+1, W0+1, H0+1)
 * Fd_packed is 4D uint16_t: shape (W0+1, H0+1, W0+1, H0+1), same layout
 */
#define IDX_2D(arr, stride1, i, j) ((arr)[(i) * (stride1) + (j)])
#define IDX_4D(arr, stride0, stride1, stride2, w, h, x, y) ((arr)[(w) * (stride0) + (h) * (stride1) + (x) * (stride2) + (y)])

/*
 * Precompute g: best single-item tiling value and item index for each rectangle size.
 * g_values[w,h] = best area achievable by tiling w×h with copies of one item type
 * g_indices[w,h] = index of that item type, or -1 if no item fits
 *
 * Arrays: g_values, g_indices are 2D (W0+1, H0+1)
 *         item_w, item_h, item_area are 1D (n_items,)
 */
static void fill_g_core(
    int W0, int H0,
    int32_t *g_values,   /* output: (W0+1, H0+1) */
    int32_t *g_indices,  /* output: (W0+1, H0+1) */
    int32_t *item_w,     /* (n_items,) */
    int32_t *item_h,     /* (n_items,) */
    int32_t *item_area,  /* (n_items,) */
    int n_items
) {
    int stride = H0 + 1;

    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            int32_t best_val = 0;
            int32_t best_idx = -1;

            for (int i = 0; i < n_items; i++) {
                int nx = w / item_w[i];
                int ny = h / item_h[i];
                if (nx > 0 && ny > 0) {
                    int32_t val = item_area[i] * nx * ny;
                    if (val > best_val) {
                        best_val = val;
                        best_idx = i;
                    }
                }
            }

            g_values [w * stride + h] = best_val;
            g_indices[w * stride + h] = best_idx;
        }
    }
}

/*
 * Bottom-up DP for pure rectangles (no defects).
 * F_values[w,h] = optimal value for pure w×h rectangle
 * F_type[w,h]   = decision type (DECISION_EMPTY/FILL/CUT_X/CUT_Y)
 * F_param[w,h]  = decision parameter (item index for FILL, cut position for CUT)
 *
 * Uses symmetry: only tries cuts up to half the dimension.
 * Normal pattern arrays encode which cut positions are valid.
 */
static void fill_F_core(
    int W0, int H0,
    int32_t *g_values,   /* (W0+1, H0+1) */
    int32_t *g_indices,  /* (W0+1, H0+1) */
    int32_t *F_values,   /* output: (W0+1, H0+1) */
    int8_t  *F_type,     /* output: (W0+1, H0+1) */
    int32_t *F_param,    /* output: (W0+1, H0+1) */
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y
) {
    int stride = H0 + 1;

    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w];
        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h];

            int32_t best_val   = g_values[w * stride + h];
            int     best_type  = (g_indices[w * stride + h] >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (g_indices[w * stride + h] >= 0) ? g_indices[w * stride + h] : 0;

            /* Vertical cuts — symmetry: only z <= w/2 */
            int half_w = w >> 1;
            for (int i = 0; i < nx; i++) {
                int z = np_x_arr[w * max_cuts_x + i];
                if (z > half_w) break;
                int32_t total = F_values[z * stride + h] + F_values[(w - z) * stride + h];
                if (total > best_val) {
                    best_val   = total;
                    best_type  = DECISION_CUT_X;
                    best_param = z;
                }
            }

            /* Horizontal cuts — symmetry: only z <= h/2 */
            int half_h = h >> 1;
            for (int i = 0; i < ny; i++) {
                int z = np_y_arr[h * max_cuts_y + i];
                if (z > half_h) break;
                int32_t total = F_values[w * stride + z] + F_values[w * stride + (h - z)];
                if (total > best_val) {
                    best_val   = total;
                    best_type  = DECISION_CUT_Y;
                    best_param = z;
                }
            }

            F_values[w * stride + h] = best_val;
            F_type  [w * stride + h] = (int8_t)best_type;
            F_param [w * stride + h] = best_param;
        }
    }
}

/*
 * Core fill function for defected rectangles.
 * All arrays are passed as raw pointers — numpy guarantees contiguous memory.
 *
 * Memory layout vs previous version:
 *   Fd_values: int32 → uint32  (values are always non-negative, doubles max representable value)
 *   Fd_type + Fd_param → Fd_packed uint16  (saves 1 array, 33% less memory for Fd tables)
 */
static void fill_Fd_core(
    int W0, int H0,
    int32_t  *prefix,
    int32_t  *F_values,
    uint32_t *Fd_values,
    int32_t  *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t  *np_y_arr, int32_t *np_y_len, int max_cuts_y,
    int32_t  *defects, int n_def
) {
    int stride_p = H0 + 1;
    int stride_F = H0 + 1;
    int stride2 = H0 + 1;
    int stride1 = (W0 + 1) * stride2;
    int stride0 = (H0 + 1) * stride1;

    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w];
        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h];

            #pragma omp parallel for schedule(dynamic, 16) collapse(2)
            for (int x = 0; x <= W0 - w; x++) {
                for (int y = 0; y <= H0 - h; y++) {

                    int32_t defect_count =
                        IDX_2D(prefix, stride_p, x+w, y+h)
                      - IDX_2D(prefix, stride_p, x,   y+h)
                      - IDX_2D(prefix, stride_p, x+w, y  )
                      + IDX_2D(prefix, stride_p, x,   y  );

                    if (defect_count == 0) {
                        IDX_4D(Fd_values, stride0, stride1, stride2, w, h, x, y) = (uint32_t)IDX_2D(F_values, stride_F, w, h);
                        continue;
                    }

                    uint32_t best_val = 0;

                    /* X cuts */
                    for (int i = 0; i < nx; i++) {
                        int z = np_x_arr[w * max_cuts_x + i];
                        uint32_t total = IDX_4D(Fd_values, stride0, stride1, stride2, z, h, x, y) + 
                                         IDX_4D(Fd_values, stride0, stride1, stride2, w-z, h, x+z, y);
                        if (total > best_val) best_val = total;
                    }

                    for (int d = 0; d < n_def; d++) {
                        int dx = defects[d*6 + 0], dx_end = defects[d*6 + 4];
                        if (x >= dx_end || dx >= x+w) continue;
                        int z1 = dx - x;
                        int z2 = dx_end - x;
                        int cuts[2] = { z1, z2 };
                        for (int ci = 0; ci < 2; ci++) {
                            int z = cuts[ci];
                            if (z <= 0 || z >= w) continue;
                            uint32_t total = IDX_4D(Fd_values, stride0, stride1, stride2, z, h, x, y) + 
                                             IDX_4D(Fd_values, stride0, stride1, stride2, w-z, h, x+z, y);
                            if (total > best_val) best_val = total;
                        }
                    }

                    /* Y cuts */
                    for (int i = 0; i < ny; i++) {
                        int z = np_y_arr[h * max_cuts_y + i];
                        uint32_t total = IDX_4D(Fd_values, stride0, stride1, stride2, w, z, x, y) + 
                                         IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z, x, y+z);
                        if (total > best_val) best_val = total;
                    }

                    for (int d = 0; d < n_def; d++) {
                        int dy = defects[d*6 + 1], dy_end = defects[d*6 + 5];
                        if (y >= dy_end || dy >= y+h) continue;
                        int z1 = dy - y;
                        int z2 = dy_end - y;
                        int cuts[2] = { z1, z2 };
                        for (int ci = 0; ci < 2; ci++) {
                            int z = cuts[ci];
                            if (z <= 0 || z >= h) continue;
                            uint32_t total = IDX_4D(Fd_values, stride0, stride1, stride2, w, z, x, y) + 
                                             IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z, x, y+z);
                            if (total > best_val) best_val = total;
                        }
                    }

                    IDX_4D(Fd_values, stride0, stride1, stride2, w, h, x, y) = best_val;
                }
            }
        }
    }
}

/* Python wrapper for fill_Fd_core */
static PyObject* py_fill_Fd(PyObject *self, PyObject *args) {
    int W0, H0, max_cuts_x, max_cuts_y, n_def;
    PyObject *o_pre, *o_F, *o_Fd, *o_nx, *o_nlx, *o_ny, *o_nly, *o_def;

    /* FIXED: Format string "iiOOOOOOOOiii" now correctly counts 8 objects and 5 ints */
    if (!PyArg_ParseTuple(args, "iiOOOOOOOOiii", &W0, &H0, &o_pre, &o_F, &o_Fd, 
                             &o_nx, &o_nlx, &o_ny, &o_nly, &o_def, 
                             &max_cuts_x, &max_cuts_y, &n_def)) return NULL;

    Py_buffer b_pre, b_F, b_Fd, b_nx, b_nlx, b_ny, b_nly, b_def;
    PyObject_GetBuffer(o_pre, &b_pre, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_Fd, &b_Fd, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nx, &b_nx, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nlx, &b_nlx, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ny, &b_ny, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nly, &b_nly, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_def, &b_def, PyBUF_SIMPLE);

    fill_Fd_core(W0, H0, (int32_t*)b_pre.buf, (int32_t*)b_F.buf, (uint32_t*)b_Fd.buf, 
                 (int32_t*)b_nx.buf, (int32_t*)b_nlx.buf, max_cuts_x, 
                 (int32_t*)b_ny.buf, (int32_t*)b_nly.buf, max_cuts_y, (int32_t*)b_def.buf, n_def);

    PyBuffer_Release(&b_pre); PyBuffer_Release(&b_F); PyBuffer_Release(&b_Fd);
    PyBuffer_Release(&b_nx); PyBuffer_Release(&b_nlx); PyBuffer_Release(&b_ny);
    PyBuffer_Release(&b_nly); PyBuffer_Release(&b_def);
    Py_RETURN_NONE;
}

/* Python wrapper for fill_g_core */
static PyObject* py_fill_g(PyObject *self, PyObject *args)
{
    int W0, H0, n_items;
    PyObject *o_g_values, *o_g_indices, *o_item_w, *o_item_h, *o_item_area;

    if (!PyArg_ParseTuple(args, "iiOOOOOi",
            &W0, &H0,
            &o_g_values, &o_g_indices,
            &o_item_w, &o_item_h, &o_item_area,
            &n_items))
        return NULL;

    Py_buffer buf_g_values = {0}, buf_g_indices = {0};
    Py_buffer buf_item_w = {0}, buf_item_h = {0}, buf_item_area = {0};

    if (PyObject_GetBuffer(o_g_values,  &buf_g_values,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_g;
    if (PyObject_GetBuffer(o_g_indices, &buf_g_indices, PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_g;
    if (PyObject_GetBuffer(o_item_w,    &buf_item_w,    PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_g;
    if (PyObject_GetBuffer(o_item_h,    &buf_item_h,    PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_g;
    if (PyObject_GetBuffer(o_item_area, &buf_item_area, PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_g;

    fill_g_core(
        W0, H0,
        (int32_t *) buf_g_values.buf,
        (int32_t *) buf_g_indices.buf,
        (int32_t *) buf_item_w.buf,
        (int32_t *) buf_item_h.buf,
        (int32_t *) buf_item_area.buf,
        n_items
    );

    PyBuffer_Release(&buf_g_values);
    PyBuffer_Release(&buf_g_indices);
    PyBuffer_Release(&buf_item_w);
    PyBuffer_Release(&buf_item_h);
    PyBuffer_Release(&buf_item_area);
    Py_RETURN_NONE;

fail_g:
    PyBuffer_Release(&buf_g_values);
    PyBuffer_Release(&buf_g_indices);
    PyBuffer_Release(&buf_item_w);
    PyBuffer_Release(&buf_item_h);
    PyBuffer_Release(&buf_item_area);
    return NULL;
}

/* Python wrapper for fill_F_core */
static PyObject* py_fill_F(PyObject *self, PyObject *args)
{
    int W0, H0, max_cuts_x, max_cuts_y;
    PyObject *o_g_values, *o_g_indices;
    PyObject *o_F_values, *o_F_type, *o_F_param;
    PyObject *o_np_x_arr, *o_np_x_len;
    PyObject *o_np_y_arr, *o_np_y_len;

    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOii",
            &W0, &H0,
            &o_g_values, &o_g_indices,
            &o_F_values, &o_F_type, &o_F_param,
            &o_np_x_arr, &o_np_x_len,
            &o_np_y_arr, &o_np_y_len,
            &max_cuts_x, &max_cuts_y))
        return NULL;

    Py_buffer buf_g_values = {0}, buf_g_indices = {0};
    Py_buffer buf_F_values = {0}, buf_F_type = {0}, buf_F_param = {0};
    Py_buffer buf_np_x_arr = {0}, buf_np_x_len = {0};
    Py_buffer buf_np_y_arr = {0}, buf_np_y_len = {0};

    if (PyObject_GetBuffer(o_g_values,  &buf_g_values,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_g_indices, &buf_g_indices, PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_F_values,  &buf_F_values,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_F_type,    &buf_F_type,    PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_F_param,   &buf_F_param,   PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_np_x_arr,  &buf_np_x_arr,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_np_x_len,  &buf_np_x_len,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_np_y_arr,  &buf_np_y_arr,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;
    if (PyObject_GetBuffer(o_np_y_len,  &buf_np_y_len,  PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) goto fail_F;

    fill_F_core(
        W0, H0,
        (int32_t *) buf_g_values.buf,
        (int32_t *) buf_g_indices.buf,
        (int32_t *) buf_F_values.buf,
        (int8_t  *) buf_F_type.buf,
        (int32_t *) buf_F_param.buf,
        (int32_t *) buf_np_x_arr.buf, (int32_t *) buf_np_x_len.buf, max_cuts_x,
        (int32_t *) buf_np_y_arr.buf, (int32_t *) buf_np_y_len.buf, max_cuts_y
    );

    PyBuffer_Release(&buf_g_values);
    PyBuffer_Release(&buf_g_indices);
    PyBuffer_Release(&buf_F_values);
    PyBuffer_Release(&buf_F_type);
    PyBuffer_Release(&buf_F_param);
    PyBuffer_Release(&buf_np_x_arr);
    PyBuffer_Release(&buf_np_x_len);
    PyBuffer_Release(&buf_np_y_arr);
    PyBuffer_Release(&buf_np_y_len);
    Py_RETURN_NONE;

fail_F:
    PyBuffer_Release(&buf_g_values);
    PyBuffer_Release(&buf_g_indices);
    PyBuffer_Release(&buf_F_values);
    PyBuffer_Release(&buf_F_type);
    PyBuffer_Release(&buf_F_param);
    PyBuffer_Release(&buf_np_x_arr);
    PyBuffer_Release(&buf_np_x_len);
    PyBuffer_Release(&buf_np_y_arr);
    PyBuffer_Release(&buf_np_y_len);
    return NULL;
}

/* method table */
/* method table */
static PyMethodDef SolverMethods[] = {
    {"fill_g",  py_fill_g,  METH_VARARGS, "Precompute best single-item tiling (g tables)."},
    {"fill_F",  py_fill_F,  METH_VARARGS, "Bottom-up DP for pure rectangles."},
    {"fill_Fd", py_fill_Fd, METH_VARARGS, "Fill Fd_values for defected rectangles."},
    {NULL, NULL, 0, NULL}
};

/* module definition */
static struct PyModuleDef solvermodule = {
    PyModuleDef_HEAD_INIT, "_solver", NULL, -1, SolverMethods
};

/* module init function — called by Python on import */
PyMODINIT_FUNC PyInit__solver(void) {
    return PyModule_Create(&solvermodule);
}