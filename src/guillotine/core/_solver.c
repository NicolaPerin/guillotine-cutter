/* =============================================================================
 * _solver.c — Python C extension bindings for the guillotine cutting solver
 *
 * This file is the ONLY file that depends on the Python C API. It provides
 * wrappers that:
 *   1. Parse Python arguments (PyArg_ParseTuple)
 *   2. Acquire buffer views on numpy arrays (PyObject_GetBuffer)
 *   3. Call the core C functions declared in solver_core.h
 *   4. Release buffer views
 *   5. Return Python objects
 *
 * All algorithmic logic lives in solver_core.c, which has no Python
 * dependency and can be compiled/tested independently.
 *
 * The DefectSlab returned by fill_defect_slab() is wrapped in a PyCapsule.
 * Python holds the capsule; when it is garbage-collected, the destructor
 * frees all slab memory (data[], tiles[], tile_index[], has_tiles[]).
 * ============================================================================= */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include "solver_core.h"


/* =============================================================================
 * PyCapsule destructor — frees the DefectSlab when Python GC collects it
 * ============================================================================= */
static void defect_slab_destructor(PyObject *capsule) {
    DefectSlab *slab = (DefectSlab *)PyCapsule_GetPointer(capsule, "DefectSlab");
    if (!slab) return;
    free(slab->data);
    free(slab->tile_index);
    free(slab->has_tiles);
    free(slab->tiles);
    free(slab);
}


/* =============================================================================
 * py_fill_tiling — Python wrapper for Phase 1 (tiling table)
 *
 * Args from Python:
 *   (sheet_width, sheet_height,
 *    tiling_values,      — numpy int32 array, shape (W+1, H+1), output
 *    tiling_item_index,  — numpy int32 array, shape (W+1, H+1), output
 *    item_widths,        — numpy int32 array, shape (n_items,)
 *    item_heights,       — numpy int32 array, shape (n_items,)
 *    item_areas,         — numpy int32 array, shape (n_items,)
 *    n_items)
 *
 * Returns: None (modifies tiling_values and tiling_item_index in-place)
 * ============================================================================= */
static PyObject *py_fill_tiling(PyObject *Py_UNUSED(self), PyObject *args) {
    int sheet_width, sheet_height, n_items;
    PyObject *o_tv, *o_ti, *o_iw, *o_ih, *o_ia;

    if (!PyArg_ParseTuple(args, "iiOOOOOi",
            &sheet_width, &sheet_height,
            &o_tv, &o_ti, &o_iw, &o_ih, &o_ia,
            &n_items))
        return NULL;

    Py_buffer b_tv, b_ti, b_iw, b_ih, b_ia;
    PyObject_GetBuffer(o_tv, &b_tv, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ti, &b_ti, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_iw, &b_iw, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ih, &b_ih, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ia, &b_ia, PyBUF_SIMPLE);

    fill_tiling_table(sheet_width, sheet_height,
        (int32_t *)b_tv.buf, (int32_t *)b_ti.buf,
        (int32_t *)b_iw.buf, (int32_t *)b_ih.buf, (int32_t *)b_ia.buf,
        n_items);

    PyBuffer_Release(&b_tv); PyBuffer_Release(&b_ti);
    PyBuffer_Release(&b_iw); PyBuffer_Release(&b_ih); PyBuffer_Release(&b_ia);

    Py_RETURN_NONE;
}


/* =============================================================================
 * py_fill_pure — Python wrapper for Phase 2 (pure table)
 *
 * Args from Python:
 *   (sheet_width, sheet_height,
 *    tiling_values,       — numpy int32 array, shape (W+1, H+1), input
 *    tiling_item_index,   — numpy int32 array, shape (W+1, H+1), input
 *    pure_values,         — numpy int32 array, shape (W+1, H+1), output
 *    pure_decision_type,  — numpy int8  array, shape (W+1, H+1), output
 *    pure_decision_param, — numpy int32 array, shape (W+1, H+1), output
 *    normal_cuts_x,       — numpy int32 array, shape (W+1, max_x_cuts), input
 *    n_normal_cuts_x,     — numpy int32 array, shape (W+1,), input
 *    normal_cuts_y,       — numpy int32 array, shape (H+1, max_y_cuts), input
 *    n_normal_cuts_y,     — numpy int32 array, shape (H+1,), input
 *    max_x_cuts, max_y_cuts)
 *
 * Returns: None (modifies pure_values, pure_decision_type,
 *                pure_decision_param in-place)
 * ============================================================================= */
static PyObject *py_fill_pure(PyObject *Py_UNUSED(self), PyObject *args) {
    int sheet_width, sheet_height, max_x_cuts, max_y_cuts;
    PyObject *o_tv, *o_ti, *o_pv, *o_pt, *o_pp, *o_nx, *o_nlx, *o_ny, *o_nly;

    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOii",
            &sheet_width, &sheet_height,
            &o_tv, &o_ti,
            &o_pv, &o_pt, &o_pp,
            &o_nx, &o_nlx,
            &o_ny, &o_nly,
            &max_x_cuts, &max_y_cuts))
        return NULL;

    Py_buffer b_tv, b_ti, b_pv, b_pt, b_pp, b_nx, b_nlx, b_ny, b_nly;
    PyObject_GetBuffer(o_tv,  &b_tv,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ti,  &b_ti,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_pv,  &b_pv,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_pt,  &b_pt,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_pp,  &b_pp,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nx,  &b_nx,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nlx, &b_nlx, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ny,  &b_ny,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nly, &b_nly, PyBUF_SIMPLE);

    fill_pure_table(sheet_width, sheet_height,
        (int32_t *)b_tv.buf,  (int32_t *)b_ti.buf,
        (int32_t *)b_pv.buf,  (int8_t  *)b_pt.buf, (int32_t *)b_pp.buf,
        (int32_t *)b_nx.buf,  (int32_t *)b_nlx.buf, max_x_cuts,
        (int32_t *)b_ny.buf,  (int32_t *)b_nly.buf, max_y_cuts);

    PyBuffer_Release(&b_tv);  PyBuffer_Release(&b_ti);
    PyBuffer_Release(&b_pv);  PyBuffer_Release(&b_pt);  PyBuffer_Release(&b_pp);
    PyBuffer_Release(&b_nx);  PyBuffer_Release(&b_nlx);
    PyBuffer_Release(&b_ny);  PyBuffer_Release(&b_nly);

    Py_RETURN_NONE;
}


/* =============================================================================
 * py_fill_defect_slab — Python wrapper for Phase 3 (defect slab)
 *
 * Args from Python:
 *   (sheet_width, sheet_height,
 *    defect_count_prefix, — numpy int32 array, shape (W+1, H+1), input
 *    pure_values,         — numpy int32 array, shape (W+1, H+1), input
 *    defect_array,        — numpy int32 array, shape (n_defects, 6), input
 *    n_defects,
 *    min_w, min_h)        — minimum item dimensions; smaller rectangles skipped
 *
 * Returns: PyCapsule wrapping the DefectSlab pointer.
 *          The capsule destructor frees all slab memory when collected.
 * ============================================================================= */
static PyObject *py_fill_defect_slab(PyObject *Py_UNUSED(self), PyObject *args) {
    int sw, sh, n, min_w, min_h;
    PyObject *o_prefix, *o_pure, *o_defects;

    if (!PyArg_ParseTuple(args, "iiOOOiii",
            &sw, &sh, &o_prefix, &o_pure, &o_defects, &n, &min_w, &min_h))
        return NULL;

    Py_buffer b_prefix, b_pure, b_defects;
    PyObject_GetBuffer(o_prefix,  &b_prefix,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_pure,    &b_pure,    PyBUF_SIMPLE);
    PyObject_GetBuffer(o_defects, &b_defects, PyBUF_SIMPLE);

    DefectSlab *slab = fill_defect_slab(
        sw, sh,
        (int32_t *)b_prefix.buf,
        (int32_t *)b_pure.buf,
        (int32_t *)b_defects.buf,
        n, min_w, min_h);

    PyBuffer_Release(&b_prefix);
    PyBuffer_Release(&b_pure);
    PyBuffer_Release(&b_defects);

    return PyCapsule_New(slab, "DefectSlab", defect_slab_destructor);
}


/* =============================================================================
 * py_defect_slab_lookup — single-point lookup for solution reconstruction
 *
 * During backtracking, the Python code queries the optimal value for a
 * specific defect-affected placement. Uses resolve_col()/colref_get()
 * from solver_core.h.
 *
 * Args from Python:
 *   (capsule,        — PyCapsule wrapping the DefectSlab
 *    pure_values,    — numpy int32 array, shape (W+1, H+1), input
 *    sheet_height,   — int (H0), needed to compute col_stride = H0+1
 *    rect_width, rect_height, sheet_x, sheet_y)
 *
 * Returns: int (the defect-adjusted value at that placement)
 * ============================================================================= */
static PyObject *py_defect_slab_lookup(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *capsule, *o_pure;
    int sheet_height, rect_width, rect_height, sheet_x, sheet_y;

    if (!PyArg_ParseTuple(args, "OOiiiii",
            &capsule, &o_pure, &sheet_height,
            &rect_width, &rect_height, &sheet_x, &sheet_y))
        return NULL;

    DefectSlab *slab = (DefectSlab *)PyCapsule_GetPointer(capsule, "DefectSlab");
    if (!slab) {
        PyErr_SetString(PyExc_RuntimeError, "Invalid DefectSlab capsule");
        return NULL;
    }

    Py_buffer b_pure;
    if (PyObject_GetBuffer(o_pure, &b_pure, PyBUF_SIMPLE) != 0)
        return NULL;

    int col_stride = sheet_height + 1;
    ColRef cr;
    resolve_col(&cr, slab, (int32_t *)b_pure.buf, col_stride,
                rect_width, rect_height, sheet_x);
    int32_t value = colref_get(&cr, sheet_y);

    PyBuffer_Release(&b_pure);
    return PyLong_FromLong(value);
}


/* =============================================================================
 * py_defect_slab_stats — diagnostics for memory usage analysis
 *
 * Returns a 4-tuple:
 *   (slab_entries,      — actual number of uint16 values stored
 *    dense_equivalent,  — entries a full dense 4D table would need
 *    n_tiles,           — total tiles across all (w, h) pairs
 *    overflow)          — 1 if any delta exceeded uint16 range, else 0
 *
 * Args from Python:
 *   (capsule,)  — PyCapsule wrapping the DefectSlab
 * ============================================================================= */
static PyObject *py_defect_slab_stats(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule))
        return NULL;

    DefectSlab *slab = (DefectSlab *)PyCapsule_GetPointer(capsule, "DefectSlab");

    int64_t dense = (int64_t)(slab->sheet_width  + 1) * (slab->sheet_width  + 1)
                  * (int64_t)(slab->sheet_height + 1) * (slab->sheet_height + 1);

    return Py_BuildValue("(LLii)",
        (long long)slab->total_data_entries,
        (long long)dense,
        slab->total_tile_count,
        slab->overflow);
}


/* =============================================================================
 * Module definition
 *
 *   fill_tiling        → Phase 1: tiling table (best single-item tiling)
 *   fill_pure          → Phase 2: pure table (defect-free rectangle DP)
 *   fill_defect_slab   → Phase 3: defect slab (returns PyCapsule)
 *   defect_slab_lookup → Query defect-adjusted value at (w, h, x, y)
 *   defect_slab_stats  → Memory diagnostics
 * ============================================================================= */
static PyMethodDef SolverMethods[] = {
    {"fill_tiling",        py_fill_tiling,        METH_VARARGS,
     "Phase 1: precompute best single-item tiling values."},

    {"fill_pure",          py_fill_pure,          METH_VARARGS,
     "Phase 2: bottom-up DP for defect-free rectangles."},

    {"fill_defect_slab",   py_fill_defect_slab,   METH_VARARGS,
     "Phase 3: fill defect-adjusted values using sparse slab storage. Returns PyCapsule."},

    {"defect_slab_lookup", py_defect_slab_lookup, METH_VARARGS,
     "Look up the defect-adjusted value at a specific placement."},

    {"defect_slab_stats",  py_defect_slab_stats,  METH_VARARGS,
     "Return (slab_entries, dense_equivalent, n_tiles, overflow) for memory diagnostics."},

    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef solvermodule = {
    PyModuleDef_HEAD_INIT,
    "_solver",                                      /* m_name     */
    "C extension for guillotine cutting-stock DP",  /* m_doc      */
    -1,                                             /* m_size     */
    SolverMethods,                                  /* m_methods  */
    NULL,                                           /* m_slots    */
    NULL,                                           /* m_traverse */
    NULL,                                           /* m_clear    */
    NULL                                            /* m_free     */
};

PyMODINIT_FUNC PyInit__solver(void) {
    return PyModule_Create(&solvermodule);
}