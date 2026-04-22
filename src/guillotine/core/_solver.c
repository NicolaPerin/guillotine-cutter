/* =============================================================================
 * _solver.c — Python C extension bindings for the guillotine cutting solver
 *
 * This file is the ONLY file that depends on the Python C API. It provides wrappers that:
 *   1. Parse Python arguments (PyArg_ParseTuple)
 *   2. Acquire buffer views on numpy arrays (PyObject_GetBuffer)
 *   3. Call the core C functions declared in solver_core.h
 *   4. Release buffer views
 *   5. Return Python objects
 *
 * All algorithmic logic lives in solver_core.c, which has no Python
 * dependency and can be compiled/tested independently.
 *
 * The FdSlab returned by fill_Fd_slab() is wrapped in a PyCapsule.
 * Python holds the capsule; when the capsule is garbage-collected,
 * the destructor frees all slab memory (data[], tiles[], tile_index[],
 * and each tile's local_defect_indices[]).
 * ============================================================================= */

#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdlib.h>
#include "solver_core.h"


/* =============================================================================
 * PyCapsule destructor — frees the FdSlab when Python GC collects it
 *
 * Ownership model:
 *   - fill_Fd_slab() allocates: slab, slab->data, slab->tiles,
 *     slab->tile_index, and each tile's local_defect_indices[]
 *   - py_fill_Fd_slab() wraps the slab pointer in a PyCapsule
 *   - When the capsule is collected, this destructor frees everything
 *
 * free(NULL) is safe per the C standard, so no NULL checks needed for
 * individual tile->local_defect_indices entries.
 * ============================================================================= */
static void fd_slab_destructor(PyObject *capsule) {
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (!slab) return;
    free(slab->data);
    free(slab->tile_index);
    free(slab->has_tiles);
    free(slab->tiles);
    free(slab);
}


/* =============================================================================
 * py_fill_g — Python wrapper for Phase 1 (g-table)
 *
 * Args from Python:
 *   (sheet_width, sheet_height,
 *    g_values,       — numpy int32 array, shape (W+1, H+1), output
 *    g_item_index,   — numpy int32 array, shape (W+1, H+1), output
 *    item_widths,    — numpy int32 array, shape (n_items,)
 *    item_heights,   — numpy int32 array, shape (n_items,)
 *    item_areas,     — numpy int32 array, shape (n_items,)
 *    n_items)
 *
 * Returns: None (modifies g_values and g_item_index in-place)
 * ============================================================================= */
static PyObject *py_fill_g(PyObject *Py_UNUSED(self), PyObject *args) {
    int sheet_width, sheet_height, n_items;
    PyObject *o_gv, *o_gi, *o_iw, *o_ih, *o_ia;

    if (!PyArg_ParseTuple(args, "iiOOOOOi",
            &sheet_width, &sheet_height,
            &o_gv, &o_gi, &o_iw, &o_ih, &o_ia,
            &n_items))
        return NULL;

    Py_buffer b_gv, b_gi, b_iw, b_ih, b_ia;
    PyObject_GetBuffer(o_gv, &b_gv, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_gi, &b_gi, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_iw, &b_iw, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ih, &b_ih, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ia, &b_ia, PyBUF_SIMPLE);

    fill_g_table(sheet_width, sheet_height,
        (int32_t *)b_gv.buf, (int32_t *)b_gi.buf,
        (int32_t *)b_iw.buf, (int32_t *)b_ih.buf, (int32_t *)b_ia.buf,
        n_items);

    PyBuffer_Release(&b_gv); PyBuffer_Release(&b_gi);
    PyBuffer_Release(&b_iw); PyBuffer_Release(&b_ih); PyBuffer_Release(&b_ia);

    Py_RETURN_NONE;
}


/* =============================================================================
 * py_fill_F — Python wrapper for Phase 2 (F-table)
 *
 * Args from Python:
 *   (sheet_width, sheet_height,
 *    g_values,          — numpy int32 array, shape (W+1, H+1), input
 *    g_item_index,      — numpy int32 array, shape (W+1, H+1), input
 *    F_values,          — numpy int32 array, shape (W+1, H+1), output
 *    F_decision_type,   — numpy int8  array, shape (W+1, H+1), output
 *    F_decision_param,  — numpy int32 array, shape (W+1, H+1), output
 *    normal_cuts_x,     — numpy int32 array, shape (W+1, max_x_cuts), input
 *    n_normal_cuts_x,   — numpy int32 array, shape (W+1,), input
 *    normal_cuts_y,     — numpy int32 array, shape (H+1, max_y_cuts), input
 *    n_normal_cuts_y,   — numpy int32 array, shape (H+1,), input
 *    max_x_cuts, max_y_cuts)
 *
 * Returns: None (modifies F_values, F_decision_type, F_decision_param in-place)
 * ============================================================================= */
static PyObject *py_fill_F(PyObject *Py_UNUSED(self), PyObject *args) {
    int sheet_width, sheet_height, max_x_cuts, max_y_cuts;
    PyObject *o_gv, *o_gi, *o_fv, *o_ft, *o_fp, *o_nx, *o_nlx, *o_ny, *o_nly;

    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOii",
            &sheet_width, &sheet_height,
            &o_gv, &o_gi,
            &o_fv, &o_ft, &o_fp,
            &o_nx, &o_nlx,
            &o_ny, &o_nly,
            &max_x_cuts, &max_y_cuts))
        return NULL;

    Py_buffer b_gv, b_gi, b_fv, b_ft, b_fp, b_nx, b_nlx, b_ny, b_nly;
    PyObject_GetBuffer(o_gv,  &b_gv,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_gi,  &b_gi,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_fv,  &b_fv,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ft,  &b_ft,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_fp,  &b_fp,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nx,  &b_nx,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nlx, &b_nlx, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_ny,  &b_ny,  PyBUF_SIMPLE);
    PyObject_GetBuffer(o_nly, &b_nly, PyBUF_SIMPLE);

    fill_F_table(sheet_width, sheet_height,
        (int32_t *)b_gv.buf,  (int32_t *)b_gi.buf,
        (int32_t *)b_fv.buf,  (int8_t  *)b_ft.buf, (int32_t *)b_fp.buf,
        (int32_t *)b_nx.buf,  (int32_t *)b_nlx.buf, max_x_cuts,
        (int32_t *)b_ny.buf,  (int32_t *)b_nly.buf, max_y_cuts);

    PyBuffer_Release(&b_gv);  PyBuffer_Release(&b_gi);
    PyBuffer_Release(&b_fv);  PyBuffer_Release(&b_ft);  PyBuffer_Release(&b_fp);
    PyBuffer_Release(&b_nx);  PyBuffer_Release(&b_nlx);
    PyBuffer_Release(&b_ny);  PyBuffer_Release(&b_nly);

    Py_RETURN_NONE;
}


/* =============================================================================
 * py_fill_Fd_slab — Python wrapper for Phase 3 (Fd-table)
 *
 * Args from Python:
 *   (sheet_width, sheet_height,
 *    defect_count_prefix, — numpy int32 array, shape (W+1, H+1), input
 *    F_values,            — numpy int32 array, shape (W+1, H+1), input
 *    defect_array,        — numpy int32 array, shape (n_defects, 6), input
 *    n_defects)
 *
 * Returns: PyCapsule wrapping the FdSlab pointer.
 *          The capsule destructor frees all slab memory when collected.
 * ============================================================================= */
static PyObject *py_fill_Fd_slab(PyObject *Py_UNUSED(self), PyObject *args) {
    int sheet_width, sheet_height, n_defects;
    PyObject *o_prefix, *o_F, *o_defects;

    if (!PyArg_ParseTuple(args, "iiOOOi",
            &sheet_width, &sheet_height,
            &o_prefix, &o_F,
            &o_defects,
            &n_defects))
        return NULL;

    Py_buffer b_prefix, b_F, b_defects;
    if (PyObject_GetBuffer(o_prefix,  &b_prefix,  PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_F,       &b_F,       PyBUF_SIMPLE) < 0 ||
        PyObject_GetBuffer(o_defects, &b_defects, PyBUF_SIMPLE) < 0)
        return NULL;

    FdSlab *slab = fill_Fd_slab(
        sheet_width, sheet_height,
        (int32_t *)b_prefix.buf,
        (int32_t *)b_F.buf,
        (int32_t *)b_defects.buf, n_defects);

    PyBuffer_Release(&b_prefix);
    PyBuffer_Release(&b_F);
    PyBuffer_Release(&b_defects);

    return PyCapsule_New(slab, "FdSlab", fd_slab_destructor);
}


/* =============================================================================
 * py_fd_slab_lookup — single-point lookup, used during solution reconstruction
 *
 * During backtracking, the Python code needs to query Fd(w, h, x, y) to
 * determine which cut was optimal at each sub-rectangle. This function
 * provides that query via resolve_col()/colref_get() from solver_core.h.
 *
 * Args from Python:
 *   (capsule,        — PyCapsule wrapping the FdSlab
 *    F_values,       — numpy int32 array, shape (W+1, H+1), input
 *    sheet_height,   — int (H0), needed to compute col_stride = H0+1
 *    rect_width, rect_height, sheet_x, sheet_y)
 *
 * Returns: int (the Fd value at that position)
 * ============================================================================= */
static PyObject *py_fd_slab_lookup(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *capsule, *o_F;
    int sheet_height, rect_width, rect_height, sheet_x, sheet_y;
    if (!PyArg_ParseTuple(args, "OOiiiii",
                          &capsule, &o_F, &sheet_height,
                          &rect_width, &rect_height, &sheet_x, &sheet_y))
        return NULL;

    FdSlab  *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    Py_buffer b_F;
    PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE);

    int col_stride = sheet_height + 1;
    int32_t *F_values = (int32_t *)b_F.buf;

    ColRef cr;
    resolve_col(&cr, slab, F_values, col_stride, rect_width, rect_height, sheet_x);
    
    int32_t value = colref_get(&cr, sheet_y);

    PyBuffer_Release(&b_F);
    return PyLong_FromLong(value);
}


/* =============================================================================
 * py_fd_slab_stats — diagnostics for memory usage analysis
 *
 * Returns a 4-tuple:
 *   (slab_entries,      — actual number of uint16_t values stored in the slab
 *    dense_equivalent,  — number of entries a full dense 4D array would need
 *    n_tiles,           — total number of tiles across all (w, h) pairs
 *    overflow)          — 1 if any delta exceeded UINT16_MAX during fill,
 *                         otherwise 0. Callers should raise on overflow.
 *
 * The ratio (slab_entries * 2) / (dense_equivalent * 4) shows the effective
 * memory savings from both sparse tiling and uint16 delta encoding.
 *
 * Args from Python:
 *   (capsule,)  — PyCapsule wrapping the FdSlab
 * ============================================================================= */
static PyObject *py_fd_slab_stats(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule))
        return NULL;

    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");

    /* Dense equivalent: (W+1)^2 * (H+1)^2 entries.
     * Uses int64_t to avoid overflow for large sheets. */
    int64_t dense = (int64_t)(slab->sheet_width  + 1) * (slab->sheet_width  + 1)
                  * (int64_t)(slab->sheet_height + 1) * (slab->sheet_height + 1);

    return Py_BuildValue("(LLii)",
        (long long)slab->total_data_entries,
        (long long)dense,
        slab->total_tile_count,
        slab->overflow);
}


static PyObject *py_estimate_slab(PyObject *Py_UNUSED(self), PyObject *args) {
    int sheet_width, sheet_height, n_defects;
    PyObject *o_defects;

    if (!PyArg_ParseTuple(args, "iiOi",
            &sheet_width, &sheet_height, &o_defects, &n_defects))
        return NULL;

    Py_buffer b_defects;
    if (PyObject_GetBuffer(o_defects, &b_defects, PyBUF_SIMPLE) < 0)
        return NULL;

    SlabEstimate est = estimate_slab(
        sheet_width, sheet_height,
        (int32_t *)b_defects.buf, n_defects);

    PyBuffer_Release(&b_defects);

    return Py_BuildValue("(iLiLiL)",
        est.tiles_1dx, (long long)est.data_1dx,
        est.tiles_1dy, (long long)est.data_1dy,
        est.tiles_2d,  (long long)est.data_2d);
}


/* =============================================================================
 * Module definition
 *
 * These five functions are the complete Python-visible API of the _solver
 * extension module. They map directly to the three DP phases plus the
 * two slab query functions:
 *
 *   fill_g         → Phase 1: g-table (single-item tiling)
 *   fill_F         → Phase 2: F-table (pure rectangle DP)
 *   fill_Fd_slab   → Phase 3: Fd-table (defected rectangle DP, returns capsule)
 *   fd_slab_lookup → Query Fd(w,h,x,y) from the slab (for reconstruction)
 *   fd_slab_stats  → Diagnostics (slab_entries, dense_equivalent, n_tiles)
 * ============================================================================= */

static PyMethodDef SolverMethods[] = {
    {"fill_g",         py_fill_g,          METH_VARARGS,
     "Phase 1: precompute best single-item tiling (g table)."},

    {"fill_F",         py_fill_F,          METH_VARARGS,
     "Phase 2: bottom-up DP for pure rectangles (F table)."},

    {"fill_Fd_slab",   py_fill_Fd_slab,    METH_VARARGS,
     "Phase 3: fill Fd values using multi-tile slab storage. Returns PyCapsule."},

    {"fd_slab_lookup", py_fd_slab_lookup,  METH_VARARGS,
     "Lookup a single Fd(w, h, x, y) value from the slab capsule."},

    {"fd_slab_stats",  py_fd_slab_stats,   METH_VARARGS,
     "Return (slab_entries, dense_equivalent, n_tiles, overflow) for memory diagnostics."},

    {"estimate_slab", py_estimate_slab, METH_VARARGS,
     "Estimate slab size for all three merge strategies. Returns (tiles_1dx, data_1dx, tiles_1dy, data_1dy, tiles_2d, data_2d)."},

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

/* Module initialization — called by Python on `import guillotine.core._solver`. */
PyMODINIT_FUNC PyInit__solver(void) {
    return PyModule_Create(&solvermodule);
}