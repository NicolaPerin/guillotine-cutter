/* =============================================================================
 * _solver.c — Python C extension bindings for the guillotine cutting solver.
 *
 * This file is the ONLY file that depends on the Python C API. It provides
 * thin wrappers that:
 *   1. Parse Python arguments (PyArg_ParseTuple)
 *   2. Acquire buffer views on numpy arrays (PyObject_GetBuffer)
 *   3. Call the core C functions declared in solver_core.h
 *   4. Release buffer views
 *   5. Return Python objects
 *
 * All algorithmic logic lives in solver_core.c.
 * ============================================================================= */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "solver_core.h"

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

/* Python wrapper for fill_Fd_core */
static PyObject* py_fill_Fd(PyObject *self, PyObject *args) {
    int W0, H0, max_cuts_x, max_cuts_y, n_def;
    PyObject *o_pre, *o_F, *o_Fd, *o_nx, *o_nlx, *o_ny, *o_nly, *o_def;

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

    fill_Fd_core(W0, H0, (int32_t*)b_pre.buf, (int32_t*)b_F.buf, (int32_t*)b_Fd.buf,
                 (int32_t*)b_nx.buf, (int32_t*)b_nlx.buf, max_cuts_x,
                 (int32_t*)b_ny.buf, (int32_t*)b_nly.buf, max_cuts_y,
                 (int32_t*)b_def.buf, n_def);

    PyBuffer_Release(&b_pre); PyBuffer_Release(&b_F); PyBuffer_Release(&b_Fd);
    PyBuffer_Release(&b_nx); PyBuffer_Release(&b_nlx); PyBuffer_Release(&b_ny);
    PyBuffer_Release(&b_nly); PyBuffer_Release(&b_def);
    Py_RETURN_NONE;
}

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
