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
 * Core fill function.
 * All arrays are passed as raw pointers — numpy guarantees contiguous memory.
 *
 * Memory layout vs previous version:
 *   Fd_values: int32 → uint32  (values are always non-negative, doubles max representable value)
 *   Fd_type + Fd_param → Fd_packed uint16  (saves 1 array, 33% less memory for Fd tables)
 */
static void fill_Fd_core(
    int W0, int H0,
    int32_t  *prefix,                                      /* prefix sum: (W0+1, H0+1) */
    int32_t  *F_values,                                    /* pure DP table: (W0+1, H0+1) */
    uint32_t *Fd_values,                                   /* defected values: (W0+1, H0+1, W0+1, H0+1) */
    uint16_t *Fd_packed,                                   /* packed type+param: (W0+1, H0+1, W0+1, H0+1) */
    int32_t  *np_x_arr, int32_t *np_x_len, int max_cuts_x, /* normal patterns x: (W0+1, max_cuts_x) */
    int32_t  *np_y_arr, int32_t *np_y_len, int max_cuts_y, /* normal patterns y: (H0+1, max_cuts_y) */
    int32_t  *defects, int n_def                           /* defects: (n_def, 6) — dx,dy,dw,dh,dx_end,dy_end */
) {
    /* strides for 2D arrays */
    int stride_p = H0 + 1;   /* prefix[x, y]   = prefix[x*stride_p + y]   */
    int stride_F = H0 + 1;   /* F_values[w, h] = F_values[w*stride_F + h] */

    /* strides for 4D arrays, layout (w, h, x, y), y varies fastest */
    int stride2 = H0 + 1;              /* x stride:              H0+1        */
    int stride1 = (W0 + 1) * stride2;  /* h stride: (W0+1) *    (H0+1)      */
    int stride0 = (H0 + 1) * stride1;  /* w stride: (H0+1) * (W0+1)*(H0+1) */

    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w];  /* number of normal pattern X cuts for width w */
        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h];  /* number of normal pattern Y cuts for height h */

            /* parallelize over all (x, y) positions for this fixed (w, h) */
            #pragma omp parallel for schedule(dynamic, 16) collapse(2)
            for (int x = 0; x <= W0 - w; x++) {
                for (int y = 0; y <= H0 - h; y++) {

                    /* purity check: O(1) via prefix sum */
                    int32_t defect_count =
                        IDX_2D(prefix, stride_p, x+w, y+h)
                      - IDX_2D(prefix, stride_p, x,   y+h)
                      - IDX_2D(prefix, stride_p, x+w, y  )
                      + IDX_2D(prefix, stride_p, x,   y  );

                    if (defect_count == 0) {
                        /* pure rectangle: reuse F_values directly, no cuts needed */
                        IDX_4D(Fd_values, stride0, stride1, stride2, w, h, x, y) = (uint32_t)IDX_2D(F_values, stride_F, w, h);
                        IDX_4D(Fd_packed, stride0, stride1, stride2, w, h, x, y) = PACK_FD(DECISION_PURE, 0);
                        continue;
                    }

                    /* defected rectangle: try all cuts, keep best */
                    uint32_t best_val   = 0;
                    int      best_type  = DECISION_DEFECT;
                    int      best_param = 0;

                    /* --- vertical cuts (X direction) --- */

                    /* normal pattern cuts in X */
                    for (int i = 0; i < nx; i++) {
                        int z = np_x_arr[w * max_cuts_x + i];
                        uint32_t lv    = IDX_4D(Fd_values, stride0, stride1, stride2, z,   h, x,   y);
                        uint32_t rv    = IDX_4D(Fd_values, stride0, stride1, stride2, w-z, h, x+z, y);
                        uint32_t total = lv + rv;
                        if (total > best_val) {
                            best_val   = total;
                            best_type  = DECISION_CUT_X;
                            best_param = z;
                        }
                    }

                    /* defect boundary cuts in X */
                    for (int d = 0; d < n_def; d++) {
                        int dx     = defects[d*6 + 0];
                        int dy     = defects[d*6 + 1];
                        int dx_end = defects[d*6 + 4];
                        int dy_end = defects[d*6 + 5];

                        /* skip defects that don't overlap this rectangle */
                        if (x >= dx_end || y >= dy_end || dx >= x+w || dy >= y+h)
                            continue;

                        /* try cutting at left and right edges of this defect */
                        int cuts[2] = { dx - x, dx_end - x };
                        for (int ci = 0; ci < 2; ci++) {
                            int z = cuts[ci];
                            if (z <= 0 || z >= w) continue;
                            uint32_t lv    = IDX_4D(Fd_values, stride0, stride1, stride2, z,   h, x,   y);
                            uint32_t rv    = IDX_4D(Fd_values, stride0, stride1, stride2, w-z, h, x+z, y);
                            uint32_t total = lv + rv;
                            if (total > best_val) {
                                best_val   = total;
                                best_type  = DECISION_CUT_X;
                                best_param = z;
                            }
                        }
                    }

                    /* --- horizontal cuts (Y direction) --- */

                    /* normal pattern cuts in Y */
                    for (int i = 0; i < ny; i++) {
                        int z = np_y_arr[h * max_cuts_y + i];
                        uint32_t bv    = IDX_4D(Fd_values, stride0, stride1, stride2, w, z,   x, y  );
                        uint32_t tv    = IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z, x, y+z);
                        uint32_t total = bv + tv;
                        if (total > best_val) {
                            best_val   = total;
                            best_type  = DECISION_CUT_Y;
                            best_param = z;
                        }
                    }

                    /* defect boundary cuts in Y */
                    for (int d = 0; d < n_def; d++) {
                        int dx     = defects[d*6 + 0];
                        int dy     = defects[d*6 + 1];
                        int dx_end = defects[d*6 + 4];
                        int dy_end = defects[d*6 + 5];

                        if (x >= dx_end || y >= dy_end || dx >= x+w || dy >= y+h)
                            continue;

                        /* try cutting at bottom and top edges of this defect */
                        int cuts[2] = { dy - y, dy_end - y };
                        for (int ci = 0; ci < 2; ci++) {
                            int z = cuts[ci];
                            if (z <= 0 || z >= h) continue;
                            uint32_t bv    = IDX_4D(Fd_values, stride0, stride1, stride2, w, z,   x, y  );
                            uint32_t tv    = IDX_4D(Fd_values, stride0, stride1, stride2, w, h-z, x, y+z);
                            uint32_t total = bv + tv;
                            if (total > best_val) {
                                best_val   = total;
                                best_type  = DECISION_CUT_Y;
                                best_param = z;
                            }
                        }
                    }

                    IDX_4D(Fd_values, stride0, stride1, stride2, w, h, x, y) = best_val;
                    IDX_4D(Fd_packed, stride0, stride1, stride2, w, h, x, y) = PACK_FD(best_type, best_param);

                } /* y */
            } /* x */
        } /* h */
    } /* w */
}

/* Python wrapper for fill_Fd_core */
static PyObject* py_fill_Fd(PyObject *self, PyObject *args)
{
    int W0, H0, max_cuts_x, max_cuts_y, n_def;

    /* numpy array objects */
    PyObject *o_prefix, *o_F_values;
    PyObject *o_Fd_values, *o_Fd_packed;   /* Fd_type + Fd_param merged into Fd_packed */
    PyObject *o_np_x_arr, *o_np_x_len;
    PyObject *o_np_y_arr, *o_np_y_len;
    PyObject *o_defects;

    /* format string: 2 ints, 9 objects, 3 ints = "iiOOOOOOOOOiii" */
    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOiii",
            &W0, &H0,
            &o_prefix,
            &o_F_values,
            &o_Fd_values, &o_Fd_packed,
            &o_np_x_arr, &o_np_x_len,
            &o_np_y_arr, &o_np_y_len,
            &o_defects,
            &max_cuts_x, &max_cuts_y, &n_def))
        return NULL;

    Py_buffer buf_prefix,   buf_F_values;
    Py_buffer buf_Fd_values, buf_Fd_packed;
    Py_buffer buf_np_x_arr, buf_np_x_len;
    Py_buffer buf_np_y_arr, buf_np_y_len;
    Py_buffer buf_defects;

    #define GET_BUF(buf, obj) \
        if (PyObject_GetBuffer(obj, &buf, PyBUF_SIMPLE | PyBUF_C_CONTIGUOUS) < 0) \
            return NULL;

    GET_BUF(buf_prefix,    o_prefix)
    GET_BUF(buf_F_values,  o_F_values)
    GET_BUF(buf_Fd_values, o_Fd_values)
    GET_BUF(buf_Fd_packed, o_Fd_packed)
    GET_BUF(buf_np_x_arr,  o_np_x_arr)
    GET_BUF(buf_np_x_len,  o_np_x_len)
    GET_BUF(buf_np_y_arr,  o_np_y_arr)
    GET_BUF(buf_np_y_len,  o_np_y_len)
    GET_BUF(buf_defects,   o_defects)

    fill_Fd_core(
        W0, H0,
        (int32_t *)  buf_prefix.buf,
        (int32_t *)  buf_F_values.buf,
        (uint32_t *) buf_Fd_values.buf,
        (uint16_t *) buf_Fd_packed.buf,
        (int32_t *)  buf_np_x_arr.buf, (int32_t *) buf_np_x_len.buf, max_cuts_x,
        (int32_t *)  buf_np_y_arr.buf, (int32_t *) buf_np_y_len.buf, max_cuts_y,
        (int32_t *)  buf_defects.buf,  n_def
    );

    /* release buffers */
    PyBuffer_Release(&buf_prefix);
    PyBuffer_Release(&buf_F_values);
    PyBuffer_Release(&buf_Fd_values);
    PyBuffer_Release(&buf_Fd_packed);
    PyBuffer_Release(&buf_np_x_arr);
    PyBuffer_Release(&buf_np_x_len);
    PyBuffer_Release(&buf_np_y_arr);
    PyBuffer_Release(&buf_np_y_len);
    PyBuffer_Release(&buf_defects);

    Py_RETURN_NONE;
}

/* method table */
static PyMethodDef SolverMethods[] = {
    {"fill_Fd", py_fill_Fd, METH_VARARGS, "Fill defected DP tables (bottom-up)."},
    {NULL, NULL, 0, NULL}
};

/* module definition */
static struct PyModuleDef solvermodule = {
    PyModuleDef_HEAD_INIT,
    "_solver",   /* module name */
    NULL,        /* docstring */
    -1,
    SolverMethods
};

/* module init function — called by Python on import */
PyMODINIT_FUNC PyInit__solver(void)
{
    return PyModule_Create(&solvermodule);
}