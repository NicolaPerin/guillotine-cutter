#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include "solver_core.h"

static void fd_slab_destructor(PyObject *capsule) {
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (!slab) return;
    free(slab->data); free(slab->tile_index); free(slab->has_tiles); free(slab->tiles); free(slab);
}

static PyObject *py_fill_g(PyObject *Py_UNUSED(self), PyObject *args) {
    int sw, sh, n; PyObject *gv, *gi, *iw, *ih, *ia;
    if (!PyArg_ParseTuple(args, "iiOOOOOi", &sw, &sh, &gv, &gi, &iw, &ih, &ia, &n)) return NULL;
    Py_buffer bgv, bgi, biw, bih, bia;
    PyObject_GetBuffer(gv, &bgv, PyBUF_SIMPLE); PyObject_GetBuffer(gi, &bgi, PyBUF_SIMPLE);
    PyObject_GetBuffer(iw, &biw, PyBUF_SIMPLE); PyObject_GetBuffer(ih, &bih, PyBUF_SIMPLE);
    PyObject_GetBuffer(ia, &bia, PyBUF_SIMPLE);
    fill_g_table(sw, sh, (int32_t*)bgv.buf, (int32_t*)bgi.buf, (int32_t*)biw.buf, (int32_t*)bih.buf, (int32_t*)bia.buf, n);
    PyBuffer_Release(&bgv); PyBuffer_Release(&bgi); PyBuffer_Release(&biw); PyBuffer_Release(&bih); PyBuffer_Release(&bia);
    Py_RETURN_NONE;
}

static PyObject *py_fill_F(PyObject *Py_UNUSED(self), PyObject *args) {
    int sw, sh, mx, my; PyObject *gv, *gi, *fv, *ft, *fp, *nx, *nlx, *ny, *nly;
    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOii", &sw, &sh, &gv, &gi, &fv, &ft, &fp, &nx, &nlx, &ny, &nly, &mx, &my)) return NULL;
    Py_buffer bgv, bgi, bfv, bft, bfp, bnx, bnlx, bny, bnly;
    PyObject_GetBuffer(gv, &bgv, PyBUF_SIMPLE); PyObject_GetBuffer(gi, &bgi, PyBUF_SIMPLE);
    PyObject_GetBuffer(fv, &bfv, PyBUF_SIMPLE); PyObject_GetBuffer(ft, &bft, PyBUF_SIMPLE);
    PyObject_GetBuffer(fp, &bfp, PyBUF_SIMPLE); PyObject_GetBuffer(nx, &bnx, PyBUF_SIMPLE);
    PyObject_GetBuffer(nlx, &bnlx, PyBUF_SIMPLE); PyObject_GetBuffer(ny, &bny, PyBUF_SIMPLE);
    PyObject_GetBuffer(nly, &bnly, PyBUF_SIMPLE);
    fill_F_table(sw, sh, (int32_t*)bgv.buf, (int32_t*)bgi.buf, (int32_t*)bfv.buf, (int8_t*)bft.buf, (int32_t*)bfp.buf, (int32_t*)bnx.buf, (int32_t*)bnlx.buf, mx, (int32_t*)bny.buf, (int32_t*)bnly.buf, my);
    PyBuffer_Release(&bgv); PyBuffer_Release(&bgi); PyBuffer_Release(&bfv); PyBuffer_Release(&bft); PyBuffer_Release(&bfp); PyBuffer_Release(&bnx); PyBuffer_Release(&bnlx); PyBuffer_Release(&bny); PyBuffer_Release(&bnly);
    Py_RETURN_NONE;
}

static PyObject *py_fill_Fd_slab(PyObject *Py_UNUSED(self), PyObject *args) {
    int sw, sh, n; PyObject *op, *of, *od;
    if (!PyArg_ParseTuple(args, "iiOOOi", &sw, &sh, &op, &of, &od, &n)) return NULL;
    Py_buffer bp, bf, bd;
    PyObject_GetBuffer(op, &bp, PyBUF_SIMPLE); PyObject_GetBuffer(of, &bf, PyBUF_SIMPLE); PyObject_GetBuffer(od, &bd, PyBUF_SIMPLE);
    FdSlab *slab = fill_Fd_slab(sw, sh, (int32_t*)bp.buf, (int32_t*)bf.buf, (int32_t*)bd.buf, n);
    PyBuffer_Release(&bp); PyBuffer_Release(&bf); PyBuffer_Release(&bd);
    return PyCapsule_New(slab, "FdSlab", fd_slab_destructor);
}

static PyObject *py_fd_slab_lookup(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *capsule, *o_F; int sh, rw, rh, sx, sy;
    if (!PyArg_ParseTuple(args, "OOiiiii", &capsule, &o_F, &sh, &rw, &rh, &sx, &sy)) return NULL;
    
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (!slab) {  // --- THE FIX: SAFETY GUARD ---
        PyErr_SetString(PyExc_RuntimeError, "Invalid FdSlab capsule");
        return NULL;
    }

    Py_buffer b_F; 
    if (PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE) != 0) return NULL;
    
    ColRef cr;
    resolve_col(&cr, slab, (int32_t*)b_F.buf, sh + 1, rw, rh, sx);
    int32_t val = colref_get(&cr, sy);
    PyBuffer_Release(&b_F);
    
    return PyLong_FromLong(val);
}

static PyObject *py_fd_slab_stats(PyObject *Py_UNUSED(self), PyObject *args) {
    PyObject *c; if (!PyArg_ParseTuple(args, "O", &c)) return NULL;
    FdSlab *slab = (FdSlab*)PyCapsule_GetPointer(c, "FdSlab");
    int64_t dense = (int64_t)(slab->sheet_width + 1) * (slab->sheet_width + 1) * (int64_t)(slab->sheet_height + 1) * (slab->sheet_height + 1);
    return Py_BuildValue("(LLii)", (long long)slab->total_data_entries, (long long)dense, slab->total_tile_count, slab->overflow);
}

static PyObject *py_estimate_slab(PyObject *Py_UNUSED(self), PyObject *args) { return Py_BuildValue("(iLiLiL)", 0, 0LL, 0, 0LL, 0, 0LL); }

static PyMethodDef SolverMethods[] = {
    {"fill_g", py_fill_g, METH_VARARGS, ""}, {"fill_F", py_fill_F, METH_VARARGS, ""},
    {"fill_Fd_slab", py_fill_Fd_slab, METH_VARARGS, ""}, {"fd_slab_lookup", py_fd_slab_lookup, METH_VARARGS, ""},
    {"fd_slab_stats", py_fd_slab_stats, METH_VARARGS, ""}, {"estimate_slab", py_estimate_slab, METH_VARARGS, ""}, {NULL, NULL, 0, NULL}
};

static struct PyModuleDef solvermodule = { PyModuleDef_HEAD_INIT, "_solver", "", -1, SolverMethods, NULL, NULL, NULL, NULL };
PyMODINIT_FUNC PyInit__solver(void) { return PyModule_Create(&solvermodule); }