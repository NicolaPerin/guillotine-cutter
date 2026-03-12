#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Decision type constants - must match constants.py */
#define DECISION_EMPTY  0
#define DECISION_FILL   1
#define DECISION_CUT_X  2
#define DECISION_CUT_Y  3
#define DECISION_DEFECT 4
#define DECISION_PURE   5

/* Defect array access macros */
#define NR_DEFECT_FIELDS 6
#define DEF_X(d)     defects[(d)*NR_DEFECT_FIELDS + 0]
#define DEF_Y(d)     defects[(d)*NR_DEFECT_FIELDS + 1]
#define DEF_W(d)     defects[(d)*NR_DEFECT_FIELDS + 2]
#define DEF_H(d)     defects[(d)*NR_DEFECT_FIELDS + 3]
#define DEF_X_END(d) defects[(d)*NR_DEFECT_FIELDS + 4]
#define DEF_Y_END(d) defects[(d)*NR_DEFECT_FIELDS + 5]

/* Bit packing for Fd decisions (int16_t) */
#define PACK_FD(type, param)   (((int16_t)(type) << 13) | ((int16_t)(param) & 0x1FFF))
#define UNPACK_TYPE(packed)    (((packed) >> 13) & 0x7)
#define UNPACK_PARAM(packed)   ((packed) & 0x1FFF)

/* 2D array indexing */
#define IDX_2D(arr, stride1, i, j) ((arr)[(i) * (stride1) + (j)])

/* ========================================================================
 * fill_g_core — unchanged
 * ======================================================================== */
static void fill_g_core(
    int W0, int H0,
    int32_t *g_values, int32_t *g_indices,
    int32_t *item_w, int32_t *item_h, int32_t *item_area,
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

/* ========================================================================
 * fill_F_core — unchanged
 * ======================================================================== */
static void fill_F_core(
    int W0, int H0,
    int32_t *g_values, int32_t *g_indices,
    int32_t *F_values, int8_t *F_type, int32_t *F_param,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y
) {
    int stride = H0 + 1;
    for (int w = 1; w <= W0; w++) {
        int nx       = np_x_len[w];
        int w_stride = w * stride;
        int x_cut_base = w * max_cuts_x;
        int half_w   = w >> 1;
        for (int h = 1; h <= H0; h++) {
            int ny       = np_y_len[h];
            int y_cut_base = h * max_cuts_y;
            int half_h   = h >> 1;

            int32_t g_idx    = g_indices[w_stride + h];
            int32_t best_val = g_values [w_stride + h];
            int     best_type  = (g_idx >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (g_idx >= 0) ? g_idx : 0;

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

/* ========================================================================
 * Slab-based Fd storage
 *
 * Key insight: for each (w,h), only (x,y) positions whose rectangle
 * [x,x+w) x [y,y+h) overlaps a defect need storage. We compute the
 * bounding box of all such (x,y) per (w,h) and allocate a dense sub-array
 * ("slab tile") covering just that range.
 *
 * Lookup: bounds-check (x,y) against the tile; if outside → return F_values[w,h].
 * Inside the tile, some positions may still be pure (the BB is a superset of
 * the true defected set), so we pre-fill those with F_values[w,h].
 *
 * This gives O(1) lookup (bounds check + flat index), OpenMP-friendly fill,
 * and memory proportional to the defect footprint rather than the full sheet.
 * ======================================================================== */

typedef struct {
    int x_lo, x_hi;   /* inclusive bounds of slab tile in x */
    int y_lo, y_hi;   /* inclusive bounds of slab tile in y */
    int x_span;       /* x_hi - x_lo + 1, or 0 if empty */
    int y_span;       /* y_hi - y_lo + 1, or 0 if empty */
    int64_t offset;   /* byte offset into the flat data array */
} SlabMeta;

typedef struct {
    int32_t  *data;
    SlabMeta *meta;   /* (W0+1) * (H0+1) entries */
    int64_t   total;  /* total int32_t entries in data */
    int       W0, H0;
} FdSlab;

static inline int32_t fd_slab_lookup(
    const FdSlab *slab, const int32_t *F_values, int stride_F,
    int w, int h, int x, int y
) {
    const SlabMeta *m = &slab->meta[w * (slab->H0 + 1) + h];
    if (m->x_span > 0 &&
        x >= m->x_lo && x <= m->x_hi &&
        y >= m->y_lo && y <= m->y_hi)
    {
        return slab->data[m->offset + (int64_t)(x - m->x_lo) * m->y_span + (y - m->y_lo)];
    }
    return F_values[w * stride_F + h];
}

static inline void fd_slab_set(
    FdSlab *slab, int w, int h, int x, int y, int32_t value
) {
    SlabMeta *m = &slab->meta[w * (slab->H0 + 1) + h];
    slab->data[m->offset + (int64_t)(x - m->x_lo) * m->y_span + (y - m->y_lo)] = value;
}

static FdSlab *fill_Fd_slab(
    int W0, int H0,
    int32_t *prefix,
    int32_t *F_values,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y,
    int32_t *defects, int n_def
) {
    int stride_p = H0 + 1;
    int stride_F = H0 + 1;
    int meta_count = (W0 + 1) * (H0 + 1);

    FdSlab *slab = (FdSlab *)malloc(sizeof(FdSlab));
    if (!slab) return NULL;
    slab->W0 = W0;
    slab->H0 = H0;
    slab->data = NULL;
    slab->meta = NULL;

    SlabMeta *meta = (SlabMeta *)calloc(meta_count, sizeof(SlabMeta));
    if (!meta) { free(slab); return NULL; }
    slab->meta = meta;

    /* ---- Phase 1: per-(w,h) bounding boxes ---- */
    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            int gx_lo = W0 + 1, gx_hi = -1;
            int gy_lo = H0 + 1, gy_hi = -1;

            for (int d = 0; d < n_def; d++) {
                int x_lo = DEF_X(d) - w + 1;
                if (x_lo < 0) x_lo = 0;
                int x_hi = DEF_X_END(d) - 1;
                if (x_hi > W0 - w) x_hi = W0 - w;

                int y_lo = DEF_Y(d) - h + 1;
                if (y_lo < 0) y_lo = 0;
                int y_hi = DEF_Y_END(d) - 1;
                if (y_hi > H0 - h) y_hi = H0 - h;

                if (x_lo > x_hi || y_lo > y_hi) continue;

                if (x_lo < gx_lo) gx_lo = x_lo;
                if (x_hi > gx_hi) gx_hi = x_hi;
                if (y_lo < gy_lo) gy_lo = y_lo;
                if (y_hi > gy_hi) gy_hi = y_hi;
            }

            SlabMeta *m = &meta[w * (H0 + 1) + h];
            if (gx_hi >= 0 && gy_hi >= 0) {
                m->x_lo = gx_lo; m->x_hi = gx_hi;
                m->y_lo = gy_lo; m->y_hi = gy_hi;
                m->x_span = gx_hi - gx_lo + 1;
                m->y_span = gy_hi - gy_lo + 1;
            }
            /* else: x_span = y_span = 0 from calloc */
        }
    }

    /* ---- Phase 2: compute offsets ---- */
    int64_t total = 0;
    for (int wh = 0; wh < meta_count; wh++) {
        meta[wh].offset = total;
        total += (int64_t)meta[wh].x_span * meta[wh].y_span;
    }
    slab->total = total;

    /* ---- Phase 3: allocate data ---- */
    if (total > 0) {
        slab->data = (int32_t *)malloc(total * sizeof(int32_t));
        if (!slab->data) {
            free(meta); free(slab);
            return NULL;
        }
    }

    /* ---- Phase 4: pre-fill every slab tile with F_values[w,h] ----
     * This ensures that pure positions inside the bounding box
     * already contain the correct value, so the DP loop can skip them
     * and lookups always return the right answer. */
    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            SlabMeta *m = &meta[w * (H0 + 1) + h];
            if (m->x_span == 0) continue;
            int32_t fval = F_values[w * stride_F + h];
            int64_t tile_size = (int64_t)m->x_span * m->y_span;
            int32_t *tile = &slab->data[m->offset];
            for (int64_t i = 0; i < tile_size; i++)
                tile[i] = fval;
        }
    }

    /* ---- Phase 5: DP fill — only iterate slab ranges ---- */
    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w];
        int w_max_cuts_x = w * max_cuts_x;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h];
            int h_max_cuts_y = h * max_cuts_y;
            SlabMeta *m = &meta[w * (H0 + 1) + h];

            if (m->x_span == 0) continue;

            #pragma omp parallel for schedule(dynamic, 4) collapse(2)
            for (int x = m->x_lo; x <= m->x_hi; x++) {
                for (int y = m->y_lo; y <= m->y_hi; y++) {

                    /* Check if actually defected via prefix sum */
                    int32_t dc =
                        IDX_2D(prefix, stride_p, x+w, y+h)
                      - IDX_2D(prefix, stride_p, x,   y+h)
                      - IDX_2D(prefix, stride_p, x+w, y  )
                      + IDX_2D(prefix, stride_p, x,   y  );

                    if (dc == 0) {
                        /* Pure — already pre-filled with F_values[w,h], skip */
                        continue;
                    }

                    int32_t best_val = 0;

                    /* X cuts — normal pattern positions */
                    for (int i = 0; i < nx; i++) {
                        int z = np_x_arr[w_max_cuts_x + i];
                        int32_t left  = fd_slab_lookup(slab, F_values, stride_F, z,   h, x,   y);
                        int32_t right = fd_slab_lookup(slab, F_values, stride_F, w-z, h, x+z, y);
                        int32_t t = left + right;
                        if (t > best_val) best_val = t;
                    }

                    /* X cuts — defect-aligned */
                    for (int d = 0; d < n_def; d++) {
                        if (x >= DEF_X_END(d) || DEF_X(d) >= x + w) continue;
                        int z1 = DEF_X(d) - x;
                        int z2 = DEF_X_END(d) - x;
                        if (z1 > 0 && z1 < w) {
                            int32_t t = fd_slab_lookup(slab, F_values, stride_F, z1,   h, x,    y)
                                      + fd_slab_lookup(slab, F_values, stride_F, w-z1, h, x+z1, y);
                            if (t > best_val) best_val = t;
                        }
                        if (z2 > 0 && z2 < w) {
                            int32_t t = fd_slab_lookup(slab, F_values, stride_F, z2,   h, x,    y)
                                      + fd_slab_lookup(slab, F_values, stride_F, w-z2, h, x+z2, y);
                            if (t > best_val) best_val = t;
                        }
                    }

                    /* Y cuts — normal pattern positions */
                    for (int i = 0; i < ny; i++) {
                        int z = np_y_arr[h_max_cuts_y + i];
                        int32_t top    = fd_slab_lookup(slab, F_values, stride_F, w, z,   x, y);
                        int32_t bottom = fd_slab_lookup(slab, F_values, stride_F, w, h-z, x, y+z);
                        int32_t t = top + bottom;
                        if (t > best_val) best_val = t;
                    }

                    /* Y cuts — defect-aligned */
                    for (int d = 0; d < n_def; d++) {
                        if (y >= DEF_Y_END(d) || DEF_Y(d) >= y + h) continue;
                        int z1 = DEF_Y(d) - y;
                        int z2 = DEF_Y_END(d) - y;
                        if (z1 > 0 && z1 < h) {
                            int32_t t = fd_slab_lookup(slab, F_values, stride_F, w, z1,   x, y)
                                      + fd_slab_lookup(slab, F_values, stride_F, w, h-z1, x, y+z1);
                            if (t > best_val) best_val = t;
                        }
                        if (z2 > 0 && z2 < h) {
                            int32_t t = fd_slab_lookup(slab, F_values, stride_F, w, z2,   x, y)
                                      + fd_slab_lookup(slab, F_values, stride_F, w, h-z2, x, y+z2);
                            if (t > best_val) best_val = t;
                        }
                    }

                    fd_slab_set(slab, w, h, x, y, best_val);
                }
            }
        }
    }

    return slab;
}

/* PyCapsule destructor */
static void fd_slab_destructor(PyObject *capsule) {
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (slab) {
        free(slab->data);
        free(slab->meta);
        free(slab);
    }
}

/* ---- Python wrappers ---- */

static PyObject* py_fill_Fd_slab(PyObject *self, PyObject *args) {
    int W0, H0, max_cuts_x, max_cuts_y, n_def;
    PyObject *o_pre, *o_F, *o_nx, *o_nlx, *o_ny, *o_nly, *o_def;

    if (!PyArg_ParseTuple(args, "iiOOOOOOOiii",
            &W0, &H0, &o_pre, &o_F,
            &o_nx, &o_nlx, &o_ny, &o_nly, &o_def,
            &max_cuts_x, &max_cuts_y, &n_def))
        return NULL;

    Py_buffer b_pre={0}, b_F={0}, b_nx={0}, b_nlx={0}, b_ny={0}, b_nly={0}, b_def={0};
    if (PyObject_GetBuffer(o_pre, &b_pre, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;
    if (PyObject_GetBuffer(o_F,   &b_F,   PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;
    if (PyObject_GetBuffer(o_nx,  &b_nx,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;
    if (PyObject_GetBuffer(o_nlx, &b_nlx, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;
    if (PyObject_GetBuffer(o_ny,  &b_ny,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;
    if (PyObject_GetBuffer(o_nly, &b_nly, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;
    if (PyObject_GetBuffer(o_def, &b_def, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fail;

    FdSlab *slab = fill_Fd_slab(W0, H0,
        (int32_t*)b_pre.buf, (int32_t*)b_F.buf,
        (int32_t*)b_nx.buf, (int32_t*)b_nlx.buf, max_cuts_x,
        (int32_t*)b_ny.buf, (int32_t*)b_nly.buf, max_cuts_y,
        (int32_t*)b_def.buf, n_def);

    PyBuffer_Release(&b_pre); PyBuffer_Release(&b_F);
    PyBuffer_Release(&b_nx);  PyBuffer_Release(&b_nlx);
    PyBuffer_Release(&b_ny);  PyBuffer_Release(&b_nly);
    PyBuffer_Release(&b_def);

    if (!slab) {
        PyErr_SetString(PyExc_MemoryError, "Failed to allocate FdSlab");
        return NULL;
    }

    PyObject *capsule = PyCapsule_New(slab, "FdSlab", fd_slab_destructor);
    if (!capsule) {
        free(slab->data); free(slab->meta); free(slab);
        return NULL;
    }
    return capsule;

fail:
    PyBuffer_Release(&b_pre); PyBuffer_Release(&b_F);
    PyBuffer_Release(&b_nx);  PyBuffer_Release(&b_nlx);
    PyBuffer_Release(&b_ny);  PyBuffer_Release(&b_nly);
    PyBuffer_Release(&b_def);
    return NULL;
}

static PyObject* py_fd_slab_lookup(PyObject *self, PyObject *args) {
    PyObject *capsule, *o_F;
    int H0, w, h, x, y;
    if (!PyArg_ParseTuple(args, "OOiiiii", &capsule, &o_F, &H0, &w, &h, &x, &y))
        return NULL;

    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (!slab) return NULL;

    Py_buffer b_F = {0};
    if (PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) return NULL;

    int32_t val = fd_slab_lookup(slab, (int32_t*)b_F.buf, H0 + 1, w, h, x, y);
    PyBuffer_Release(&b_F);
    return PyLong_FromLong(val);
}

static PyObject* py_fd_slab_stats(PyObject *self, PyObject *args) {
    PyObject *capsule;
    if (!PyArg_ParseTuple(args, "O", &capsule)) return NULL;
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");
    if (!slab) return NULL;

    int64_t dense = (int64_t)(slab->W0+1) * (slab->W0+1) *
                    (int64_t)(slab->H0+1) * (slab->H0+1);
    return Py_BuildValue("(LL)", (long long)slab->total, (long long)dense);
}

/* ========================================================================
 * Legacy dense fill_Fd — kept for backward compat, int64_t strides fixed
 * ======================================================================== */
static void fill_Fd_core(
    int W0, int H0,
    int32_t *prefix, int32_t *F_values, int32_t *Fd_values,
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
                    int32_t dc =
                        IDX_2D(prefix, stride_p, x+w, y+h)
                      - IDX_2D(prefix, stride_p, x,   y+h)
                      - IDX_2D(prefix, stride_p, x+w, y  )
                      + IDX_2D(prefix, stride_p, x,   y  );
                    if (dc == 0) {
                        Fd_values[(int64_t)w*stride0 + (int64_t)h*stride1 + (int64_t)x*stride2 + y] = IDX_2D(F_values, stride_F, w, h);
                        continue;
                    }
                    int32_t best_val = 0;
                    for (int i = 0; i < nx; i++) {
                        int z = np_x_arr[w_max_cuts_x + i];
                        int32_t total = Fd_values[(int64_t)z*stride0 + (int64_t)h*stride1 + (int64_t)x*stride2 + y]
                                      + Fd_values[(int64_t)(w-z)*stride0 + (int64_t)h*stride1 + (int64_t)(x+z)*stride2 + y];
                        if (total > best_val) best_val = total;
                    }
                    for (int d = 0; d < n_def; d++) {
                        if (x >= DEF_X_END(d) || DEF_X(d) >= x + w) continue;
                        int z1 = DEF_X(d) - x;
                        int z2 = DEF_X_END(d) - x;
                        if (z1 > 0 && z1 < w) {
                            int32_t total = Fd_values[(int64_t)z1*stride0 + (int64_t)h*stride1 + (int64_t)x*stride2 + y]
                                          + Fd_values[(int64_t)(w-z1)*stride0 + (int64_t)h*stride1 + (int64_t)(x+z1)*stride2 + y];
                            if (total > best_val) best_val = total;
                        }
                        if (z2 > 0 && z2 < w) {
                            int32_t total = Fd_values[(int64_t)z2*stride0 + (int64_t)h*stride1 + (int64_t)x*stride2 + y]
                                          + Fd_values[(int64_t)(w-z2)*stride0 + (int64_t)h*stride1 + (int64_t)(x+z2)*stride2 + y];
                            if (total > best_val) best_val = total;
                        }
                    }
                    for (int i = 0; i < ny; i++) {
                        int z = np_y_arr[h_max_cuts_y + i];
                        int32_t total = Fd_values[(int64_t)w*stride0 + (int64_t)z*stride1 + (int64_t)x*stride2 + y]
                                      + Fd_values[(int64_t)w*stride0 + (int64_t)(h-z)*stride1 + (int64_t)x*stride2 + (y+z)];
                        if (total > best_val) best_val = total;
                    }
                    for (int d = 0; d < n_def; d++) {
                        if (y >= DEF_Y_END(d) || DEF_Y(d) >= y + h) continue;
                        int z1 = DEF_Y(d) - y;
                        int z2 = DEF_Y_END(d) - y;
                        if (z1 > 0 && z1 < h) {
                            int32_t total = Fd_values[(int64_t)w*stride0 + (int64_t)z1*stride1 + (int64_t)x*stride2 + y]
                                          + Fd_values[(int64_t)w*stride0 + (int64_t)(h-z1)*stride1 + (int64_t)x*stride2 + (y+z1)];
                            if (total > best_val) best_val = total;
                        }
                        if (z2 > 0 && z2 < h) {
                            int32_t total = Fd_values[(int64_t)w*stride0 + (int64_t)z2*stride1 + (int64_t)x*stride2 + y]
                                          + Fd_values[(int64_t)w*stride0 + (int64_t)(h-z2)*stride1 + (int64_t)x*stride2 + (y+z2)];
                            if (total > best_val) best_val = total;
                        }
                    }
                    Fd_values[(int64_t)w*stride0 + (int64_t)h*stride1 + (int64_t)x*stride2 + y] = best_val;
                }
            }
        }
    }
}

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

/* fill_g / fill_F wrappers — unchanged */
static PyObject* py_fill_g(PyObject *self, PyObject *args) {
    int W0, H0, n_items;
    PyObject *o_g_values, *o_g_indices, *o_item_w, *o_item_h, *o_item_area;
    if (!PyArg_ParseTuple(args, "iiOOOOOi",
            &W0, &H0, &o_g_values, &o_g_indices,
            &o_item_w, &o_item_h, &o_item_area, &n_items))
        return NULL;
    Py_buffer bv={0},bi={0},bw={0},bh={0},ba={0};
    if (PyObject_GetBuffer(o_g_values,  &bv, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fg;
    if (PyObject_GetBuffer(o_g_indices, &bi, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fg;
    if (PyObject_GetBuffer(o_item_w,    &bw, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fg;
    if (PyObject_GetBuffer(o_item_h,    &bh, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fg;
    if (PyObject_GetBuffer(o_item_area, &ba, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS) < 0) goto fg;
    fill_g_core(W0,H0,(int32_t*)bv.buf,(int32_t*)bi.buf,
        (int32_t*)bw.buf,(int32_t*)bh.buf,(int32_t*)ba.buf,n_items);
    PyBuffer_Release(&bv);PyBuffer_Release(&bi);
    PyBuffer_Release(&bw);PyBuffer_Release(&bh);PyBuffer_Release(&ba);
    Py_RETURN_NONE;
fg:
    PyBuffer_Release(&bv);PyBuffer_Release(&bi);
    PyBuffer_Release(&bw);PyBuffer_Release(&bh);PyBuffer_Release(&ba);
    return NULL;
}

static PyObject* py_fill_F(PyObject *self, PyObject *args) {
    int W0,H0,mcx,mcy;
    PyObject *ogv,*ogi,*ofv,*oft,*ofp,*onx,*onlx,*ony,*only;
    if (!PyArg_ParseTuple(args,"iiOOOOOOOOOii",
            &W0,&H0,&ogv,&ogi,&ofv,&oft,&ofp,&onx,&onlx,&ony,&only,&mcx,&mcy))
        return NULL;
    Py_buffer bgv={0},bgi={0},bfv={0},bft={0},bfp={0},bnx={0},bnlx={0},bny={0},bnly={0};
    if (PyObject_GetBuffer(ogv,  &bgv,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ogi,  &bgi,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ofv,  &bfv,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(oft,  &bft,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ofp,  &bfp,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(onx,  &bnx,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(onlx, &bnlx, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ony,  &bny,  PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(only, &bnly, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    fill_F_core(W0,H0,
        (int32_t*)bgv.buf,(int32_t*)bgi.buf,
        (int32_t*)bfv.buf,(int8_t*)bft.buf,(int32_t*)bfp.buf,
        (int32_t*)bnx.buf,(int32_t*)bnlx.buf,mcx,
        (int32_t*)bny.buf,(int32_t*)bnly.buf,mcy);
    PyBuffer_Release(&bgv);PyBuffer_Release(&bgi);
    PyBuffer_Release(&bfv);PyBuffer_Release(&bft);PyBuffer_Release(&bfp);
    PyBuffer_Release(&bnx);PyBuffer_Release(&bnlx);
    PyBuffer_Release(&bny);PyBuffer_Release(&bnly);
    Py_RETURN_NONE;
fF:
    PyBuffer_Release(&bgv);PyBuffer_Release(&bgi);
    PyBuffer_Release(&bfv);PyBuffer_Release(&bft);PyBuffer_Release(&bfp);
    PyBuffer_Release(&bnx);PyBuffer_Release(&bnlx);
    PyBuffer_Release(&bny);PyBuffer_Release(&bnly);
    return NULL;
}

/* method table */
static PyMethodDef SolverMethods[] = {
    {"fill_g",         py_fill_g,         METH_VARARGS, "Precompute g tables."},
    {"fill_F",         py_fill_F,         METH_VARARGS, "Bottom-up DP for pure rectangles."},
    {"fill_Fd",        py_fill_Fd,        METH_VARARGS, "Dense Fd fill (legacy)."},
    {"fill_Fd_slab",   py_fill_Fd_slab,   METH_VARARGS, "Slab-based Fd fill (memory efficient)."},
    {"fd_slab_lookup", py_fd_slab_lookup, METH_VARARGS, "Lookup Fd value from slab."},
    {"fd_slab_stats",  py_fd_slab_stats,  METH_VARARGS, "Return (slab_entries, dense_equivalent)."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef solvermodule = {
    PyModuleDef_HEAD_INIT, "_solver", NULL, -1, SolverMethods
};

PyMODINIT_FUNC PyInit__solver(void) {
    return PyModule_Create(&solvermodule);
}