#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* Decision type constants */
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

#define IDX_2D(arr, stride1, i, j) ((arr)[(i) * (stride1) + (j)])

/* --- Structs and Prototypes --- */

/*
* The Tile struct represents a rectangular region in the 2D space of the cutting stock problem. It contains the following fields:
* - x_lo, x_hi: The lower and upper bounds of the tile in the x-dimension.
* - y_lo, y_hi: The lower and upper bounds of the tile in the y-dimension.
* - x_span, y_span: The width and height of the tile, calculated as (x_hi - x_lo + 1) and (y_hi - y_lo + 1) respectively.
* - offset: An offset value used for indexing into the data array of the FdSlab structure.
* - local_defs: A pointer to an array of integers representing the indices of local defects that affect this tile.
* - n_local_defs: The number of local defects in the local_defs array.
*/
typedef struct {
    int x_lo, x_hi, y_lo, y_hi, x_span, y_span;
    int64_t offset;
    int *local_defs; 
    int n_local_defs;
} Tile; 

/* The TileIndex struct is a simple structure that holds information about the tiles associated with a specific width and height combination. 
* It contains the following fields:
* - first: The index of the first tile in the tiles array of the FdSlab structure that corresponds to this width and height.
* - count: The number of tiles in the tiles array that correspond to this width and height.
*/ 
typedef struct {
    int first;
    int count;
} TileIndex;

/* The FdSlab struct is a comprehensive structure that encapsulates all the necessary information for handling the cutting stock problem with defects.
* It contains the following fields:
* - data: A pointer to an array of int32_t values that holds precomputed results for specific tile configurations.
* - tiles: A pointer to an array of Tile structures that represent the different tile configurations based on the defects.
* - tile_idx: A pointer to an array of TileIndex structures that provide indexing information for the tiles array based on width and height combinations.
* - total: The total number of precomputed values stored in the data array.
* - n_tiles: The total number of tiles stored in the tiles array.
* - W0, H0: The original width and height of the cutting stock material.
*/
typedef struct {
    int32_t   *data;
    Tile      *tiles;
    TileIndex *tile_idx;
    int64_t    total;
    int        n_tiles;
    int        W0, H0;
} FdSlab;

/* The XInterval struct is a simple structure that represents an interval in the x-dimension along with its corresponding y-dimension bounds.
* It contains the following fields:
* - lo, hi: The lower and upper bounds of the interval in the x-dimension.
* - y_lo, y_hi: The lower and upper bounds of the interval in the y-dimension.
*/
typedef struct {
    int lo, hi;
    int y_lo, y_hi;
} XInterval;

static int cmp_defects_x(const void *a, const void *b) {
    int32_t x_a = *(const int32_t *)a;
    int32_t x_b = *(const int32_t *)b;
    return (x_a > x_b) - (x_a < x_b);
}

static int cmp_intervals(const void *a, const void *b) {
    return ((const XInterval *)a)->lo - ((const XInterval *)b)->lo;
}

/* --- Core Logic Functions --- */

static void fill_g_core(int W0, int H0, int32_t *g_values, int32_t *g_indices,
                        int32_t *item_w, int32_t *item_h, int32_t *item_area, int n_items) {
    int stride = H0 + 1;

    #pragma omp parallel for schedule(dynamic)
    for (int w = 1; w <= W0; w++) {
        int w_stride = w * stride;

        for (int h = 1; h <= H0; h++) {
            int32_t best_val = 0, best_idx = -1;

            for (int i = 0; i < n_items; i++) {
                int nx = w / item_w[i];
                int ny = h / item_h[i];

                if (nx > 0 && ny > 0) {
                    int32_t val = item_area[i] * nx * ny;
                    if (val > best_val) { best_val = val; best_idx = i; }
                }
            }

            g_values[w_stride + h] = best_val;
            g_indices[w_stride + h] = best_idx;
        }
    }
}

static void fill_F_core(int W0, int H0, int32_t *g_values, int32_t *g_indices,
                        int32_t *F_values, int8_t *F_type, int32_t *F_param,
                        int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
                        int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y) {
    int stride = H0 + 1;
    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w], w_stride = w * stride, x_base = w * max_cuts_x, half_w = w >> 1;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h], y_base = h * max_cuts_y, half_h = h >> 1;
            int32_t g_idx = g_indices[w_stride + h], best_val = g_values[w_stride + h];
            int best_type = (g_idx >= 0) ? DECISION_FILL : DECISION_EMPTY;
            int32_t best_param = (g_idx >= 0) ? g_idx : 0;

            for (int i = 0; i < nx; i++) {
                int z = np_x_arr[x_base + i];
                if (z > half_w) break;
                int32_t total = F_values[z * stride + h] + F_values[(w - z) * stride + h];
                if (total > best_val) { best_val = total; best_type = DECISION_CUT_X; best_param = z; }
            }

            for (int i = 0; i < ny; i++) {
                int z = np_y_arr[y_base + i];
                if (z > half_h) break;
                int32_t total = F_values[w_stride + z] + F_values[w_stride + (h - z)];
                if (total > best_val) { best_val = total; best_type = DECISION_CUT_Y; best_param = z; }
            }

            F_values[w_stride + h] = best_val; F_type[w_stride + h] = (int8_t)best_type; F_param[w_stride + h] = best_param;
        }
    }
}

static int build_tiles_for_wh(int w, int h, int W0, int H0, int32_t *defects, int n_def, XInterval *intervals, Tile *out_tiles) {
    int n_iv = 0;
    const int max_x = W0 - w, max_y = H0 - h;

    for (int d = 0; d < n_def; d++) {
        int x_lo = (DEF_X(d) - w + 1 < 0) ? 0 : DEF_X(d) - w + 1;
        int x_hi = (DEF_X_END(d) - 1 > max_x) ? max_x : DEF_X_END(d) - 1;
        int y_lo = (DEF_Y(d) - h + 1 < 0) ? 0 : DEF_Y(d) - h + 1;
        int y_hi = (DEF_Y_END(d) - 1 > max_y) ? max_y : DEF_Y_END(d) - 1;

        if (x_lo <= x_hi && y_lo <= y_hi) {
            intervals[n_iv].lo = x_lo; intervals[n_iv].hi = x_hi;
            intervals[n_iv].y_lo = y_lo; intervals[n_iv].y_hi = y_hi;
            n_iv++;
        }
    }

    if (n_iv == 0) return 0;
    qsort(intervals, n_iv, sizeof(XInterval), cmp_intervals);
    int n_tiles = 0;
    XInterval cur = intervals[0];

    for (int i = 1; i < n_iv; i++) {

        if (intervals[i].lo <= cur.hi + 1) {
            if (intervals[i].hi > cur.hi) cur.hi = intervals[i].hi;
            if (intervals[i].y_lo < cur.y_lo) cur.y_lo = intervals[i].y_lo;
            if (intervals[i].y_hi > cur.y_hi) cur.y_hi = intervals[i].y_hi;
        } else {
            out_tiles[n_tiles].x_lo = cur.lo; out_tiles[n_tiles].x_hi = cur.hi;
            out_tiles[n_tiles].y_lo = cur.y_lo; out_tiles[n_tiles].y_hi = cur.y_hi;
            out_tiles[n_tiles].x_span = cur.hi - cur.lo + 1; out_tiles[n_tiles].y_span = cur.y_hi - cur.y_lo + 1;
            n_tiles++; cur = intervals[i];
        }
    }

    out_tiles[n_tiles].x_lo = cur.lo; out_tiles[n_tiles].x_hi = cur.hi;
    out_tiles[n_tiles].y_lo = cur.y_lo; out_tiles[n_tiles].y_hi = cur.y_hi;
    out_tiles[n_tiles].x_span = cur.hi - cur.lo + 1; out_tiles[n_tiles].y_span = cur.y_hi - cur.y_lo + 1;

    return n_tiles + 1;
}

static inline int32_t fd_slab_lookup(const FdSlab *slab, const int32_t *F_values, int stride_F, int w, int h, int x, int y) {
    const TileIndex *ti = &slab->tile_idx[w * (slab->H0 + 1) + h];

    for (int t = 0; t < ti->count; t++) {
        const Tile *tile = &slab->tiles[ti->first + t];

        if (x >= tile->x_lo && x <= tile->x_hi && y >= tile->y_lo && y <= tile->y_hi)
            return slab->data[tile->offset + (int64_t)(x - tile->x_lo) * tile->y_span + (y - tile->y_lo)];
    }

    return F_values[w * stride_F + h];
}

/* --- Slab Generator --- */

static FdSlab *fill_Fd_slab(int W0, int H0, int32_t *prefix, int32_t *F_values, int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x, int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y, int32_t *defects_raw, int n_def) {
    int stride_F = H0 + 1, stride_p = H0 + 1;
    
    // Here we are dynamically allocating memory for the defects array, which will hold the defect information. 
    // The size of this array is determined by the number of defects (n_def) multiplied by the number of fields per defect (NR_DEFECT_FIELDS) and the size of an int32_t.
    int32_t *defects = (int32_t*)malloc(n_def * NR_DEFECT_FIELDS * sizeof(int32_t));

    // We copy the defect data from the input array (defects_raw) to our newly allocated array (defects) using memcpy. 
    // This is done to ensure that we have our own copy of the defect data, which we can modify if needed without affecting the original data passed from Python.
    memcpy(defects, defects_raw, n_def * NR_DEFECT_FIELDS * sizeof(int32_t));

    if (n_def > 1) qsort(defects, n_def, sizeof(int32_t) * NR_DEFECT_FIELDS, cmp_defects_x);

    FdSlab *slab = (FdSlab *)calloc(1, sizeof(FdSlab));
    slab->W0 = W0; slab->H0 = H0;
    slab->tile_idx = (TileIndex *)calloc((W0 + 1) * (H0 + 1), sizeof(TileIndex));
    
    XInterval *iv_scr = (XInterval *)malloc(n_def * sizeof(XInterval));
    Tile *t_scr = (Tile *)malloc(n_def * sizeof(Tile));

    int total_tiles = 0;

    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            total_tiles += build_tiles_for_wh(w, h, W0, H0, defects, n_def, iv_scr, t_scr);
        }
    }

    slab->tiles = (Tile *)calloc(total_tiles, sizeof(Tile));
    slab->n_tiles = total_tiles;
    int tile_cursor = 0; int64_t data_total = 0;

    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            TileIndex *ti = &slab->tile_idx[w * stride_F + h];
            ti->first = tile_cursor;
            int nt = build_tiles_for_wh(w, h, W0, H0, defects, n_def, iv_scr, t_scr);

            for (int t = 0; t < nt; t++) {
                t_scr[t].offset = data_total;
                data_total += (int64_t)t_scr[t].x_span * t_scr[t].y_span;
                
                int *l_list = (int *)malloc(n_def * sizeof(int)), l_count = 0;
                for (int d = 0; d < n_def; d++) {
                    if (!(t_scr[t].x_hi < (DEF_X(d) - w + 1) || t_scr[t].x_lo > (DEF_X_END(d) - 1) ||
                          t_scr[t].y_hi < (DEF_Y(d) - h + 1) || t_scr[t].y_lo > (DEF_Y_END(d) - 1)))
                        l_list[l_count++] = d;
                }

                if (l_count > 0) {
                    t_scr[t].n_local_defs = l_count;
                    t_scr[t].local_defs = (int *)realloc(l_list, l_count * sizeof(int));
                } else { 
                    free(l_list); t_scr[t].local_defs = NULL; t_scr[t].n_local_defs = 0; 
                }

                slab->tiles[tile_cursor++] = t_scr[t];
            }
            ti->count = nt;
        }
    }

    slab->total = data_total;
    slab->data = (int32_t *)malloc(data_total * sizeof(int32_t));

    // Pre-fill and DP
    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w], w_max = w * max_cuts_x;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h], h_max = h * max_cuts_y;
            TileIndex *ti = &slab->tile_idx[w * stride_F + h];
            int32_t base_f = F_values[w * stride_F + h];

            for (int t = 0; t < ti->count; t++) {
                Tile *tile = &slab->tiles[ti->first + t];
                int32_t *t_ptr = &slab->data[tile->offset];

                #pragma omp parallel for schedule(dynamic, 8) collapse(2)
                for (int x = tile->x_lo; x <= tile->x_hi; x++) {
                    for (int y = tile->y_lo; y <= tile->y_hi; y++) {
                        int32_t dc = IDX_2D(prefix, stride_p, x+w, y+h) - IDX_2D(prefix, stride_p, x, y+h)
                                   - IDX_2D(prefix, stride_p, x+w, y)   + IDX_2D(prefix, stride_p, x, y);
                        if (dc == 0) { 
                            t_ptr[(int64_t)(x - tile->x_lo) * tile->y_span + (y - tile->y_lo)] = base_f; continue; 
                        }

                        int32_t best = 0;
                        for (int i = 0; i < nx; i++) {
                            int z = np_x_arr[w_max + i];
                            int32_t s = fd_slab_lookup(slab, F_values, stride_F, z, h, x, y) + fd_slab_lookup(slab, F_values, stride_F, w-z, h, x+z, y);
                            if (s > best) best = s;
                        }

                        for (int i = 0; i < tile->n_local_defs; i++) {
                            int d = tile->local_defs[i];
                            int z1 = DEF_X(d)-x, z2 = DEF_X_END(d)-x;
                            if (z1 > 0 && z1 < w) { 
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, z1, h, x, y) + fd_slab_lookup(slab, F_values, stride_F, w-z1, h, x+z1, y); 
                                if (s > best) best = s; 
                            }
                            if (z2 > 0 && z2 < w) { 
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, z2, h, x, y) + fd_slab_lookup(slab, F_values, stride_F, w-z2, h, x+z2, y); 
                                if (s > best) best = s; 
                            }
                        }

                        for (int i = 0; i < ny; i++) {
                            int z = np_y_arr[h_max + i];
                            int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z, x, y) + fd_slab_lookup(slab, F_values, stride_F, w, h-z, x, y+z);
                            if (s > best) best = s;
                        }

                        for (int i = 0; i < tile->n_local_defs; i++) {
                            int d = tile->local_defs[i];
                            int z1 = DEF_Y(d)-y, z2 = DEF_Y_END(d)-y;
                            if (z1 > 0 && z1 < h) { 
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z1, x, y) + fd_slab_lookup(slab, F_values, stride_F, w, h-z1, x, y+z1); 
                                if (s > best) best = s; 
                            }
                            if (z2 > 0 && z2 < h) { 
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z2, x, y) + fd_slab_lookup(slab, F_values, stride_F, w, h-z2, x, y+z2); 
                                if (s > best) best = s; 
                            }
                        }

                        t_ptr[(int64_t)(x - tile->x_lo) * tile->y_span + (y - tile->y_lo)] = best;
                    }
                }
            }
        }
    }
    free(defects); free(iv_scr); free(t_scr);
    return slab;
}

/* --- Python Wrappers --- */

static void fd_slab_destructor(PyObject *capsule) {
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(capsule, "FdSlab");

    if (slab) {
        if (slab->data) free(slab->data);
        if (slab->tile_idx) free(slab->tile_idx);
        if (slab->tiles) {

            for (int i = 0; i < slab->n_tiles; i++) if (slab->tiles[i].local_defs) free(slab->tiles[i].local_defs);
            free(slab->tiles);
        }
        free(slab);
    }
}

static PyObject* py_fill_Fd_slab(PyObject *self, PyObject *args) {
    int W0, H0, mcx, mcy, n_def;
    PyObject *o_pre, *o_F, *o_nx, *o_nlx, *o_ny, *o_nly, *o_def;
    if (!PyArg_ParseTuple(args, "iiOOOOOOOiii", &W0, &H0, &o_pre, &o_F, &o_nx, &o_nlx, &o_ny, &o_nly, &o_def, &mcx, &mcy, &n_def)) return NULL;

    Py_buffer b_pre, b_F, b_nx, b_nlx, b_ny, b_nly, b_def;
    if (PyObject_GetBuffer(o_pre, &b_pre, PyBUF_SIMPLE)<0 ||
        PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE)<0 || 
        PyObject_GetBuffer(o_nx, &b_nx, PyBUF_SIMPLE)<0 || 
        PyObject_GetBuffer(o_nlx, &b_nlx, PyBUF_SIMPLE)<0 || 
        PyObject_GetBuffer(o_ny, &b_ny, PyBUF_SIMPLE)<0 || 
        PyObject_GetBuffer(o_nly, &b_nly, PyBUF_SIMPLE)<0 || 
        PyObject_GetBuffer(o_def, &b_def, PyBUF_SIMPLE)<0) 
        return NULL;

    FdSlab *slab = fill_Fd_slab(W0, H0, (int32_t*)b_pre.buf, (int32_t*)b_F.buf, (int32_t*)b_nx.buf, (int32_t*)b_nlx.buf, mcx, (int32_t*)b_ny.buf, (int32_t*)b_nly.buf, mcy, (int32_t*)b_def.buf, n_def);
    PyBuffer_Release(&b_pre); PyBuffer_Release(&b_F); PyBuffer_Release(&b_nx); PyBuffer_Release(&b_nlx); PyBuffer_Release(&b_ny); PyBuffer_Release(&b_nly); PyBuffer_Release(&b_def);
    return PyCapsule_New(slab, "FdSlab", fd_slab_destructor);
}

static PyObject* py_fd_slab_lookup(PyObject *self, PyObject *args) {
    PyObject *cap, *o_F; int H0, w, h, x, y;
    if (!PyArg_ParseTuple(args, "OOiiiii", &cap, &o_F, &H0, &w, &h, &x, &y)) return NULL;
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(cap, "FdSlab");
    Py_buffer b_F; PyObject_GetBuffer(o_F, &b_F, PyBUF_SIMPLE);
    int32_t val = fd_slab_lookup(slab, (int32_t*)b_F.buf, H0 + 1, w, h, x, y);
    PyBuffer_Release(&b_F); return PyLong_FromLong(val);
}

static PyObject* py_fd_slab_stats(PyObject *self, PyObject *args) {
    PyObject *cap; if (!PyArg_ParseTuple(args, "O", &cap)) return NULL;
    FdSlab *slab = (FdSlab *)PyCapsule_GetPointer(cap, "FdSlab");
    return Py_BuildValue("(LLi)", (long long)slab->total, (long long)(slab->W0+1)*(slab->W0+1)*(slab->H0+1)*(slab->H0+1), slab->n_tiles);
}

static PyObject* py_fill_g(PyObject *self, PyObject *args) {
    int W0, H0, n; PyObject *o_gv, *o_gi, *o_iw, *o_ih, *o_ia;
    if (!PyArg_ParseTuple(args, "iiOOOOOi", &W0, &H0, &o_gv, &o_gi, &o_iw, &o_ih, &o_ia, &n)) return NULL;
    Py_buffer bv, bi, bw, bh, ba;
    PyObject_GetBuffer(o_gv, &bv, PyBUF_SIMPLE); PyObject_GetBuffer(o_gi, &bi, PyBUF_SIMPLE);
    PyObject_GetBuffer(o_iw, &bw, PyBUF_SIMPLE); PyObject_GetBuffer(o_ih, &bh, PyBUF_SIMPLE); PyObject_GetBuffer(o_ia, &ba, PyBUF_SIMPLE);
    fill_g_core(W0, H0, (int32_t*)bv.buf, (int32_t*)bi.buf, (int32_t*)bw.buf, (int32_t*)bh.buf, (int32_t*)ba.buf, n);
    PyBuffer_Release(&bv); PyBuffer_Release(&bi); PyBuffer_Release(&bw); PyBuffer_Release(&bh); PyBuffer_Release(&ba);
    Py_RETURN_NONE;
}

static PyObject* py_fill_F(PyObject *self, PyObject *args) {
    int W0, H0, mcx, mcy; PyObject *ogv, *ogi, *ofv, *oft, *ofp, *onx, *onlx, *ony, *only;
    if (!PyArg_ParseTuple(args, "iiOOOOOOOOOii", &W0, &H0, &ogv, &ogi, &ofv, &oft, &ofp, &onx, &onlx, &ony, &only, &mcx, &mcy)) return NULL;
    Py_buffer bgv, bgi, bfv, bft, bfp, bnx, bnlx, bny, bnly;
    PyObject_GetBuffer(ogv, &bgv, PyBUF_SIMPLE); PyObject_GetBuffer(ogi, &bgi, PyBUF_SIMPLE); PyObject_GetBuffer(ofv, &bfv, PyBUF_SIMPLE); PyObject_GetBuffer(oft, &bft, PyBUF_SIMPLE); PyObject_GetBuffer(ofp, &bfp, PyBUF_SIMPLE); PyObject_GetBuffer(onx, &bnx, PyBUF_SIMPLE); PyObject_GetBuffer(onlx, &bnlx, PyBUF_SIMPLE); PyObject_GetBuffer(ony, &bny, PyBUF_SIMPLE); PyObject_GetBuffer(only, &bnly, PyBUF_SIMPLE);
    fill_F_core(W0, H0, (int32_t*)bgv.buf, (int32_t*)bgi.buf, (int32_t*)bfv.buf, (int8_t*)bft.buf, (int32_t*)bfp.buf, (int32_t*)bnx.buf, (int32_t*)bnlx.buf, mcx, (int32_t*)bny.buf, (int32_t*)bnly.buf, mcy);
    PyBuffer_Release(&bgv); PyBuffer_Release(&bgi); PyBuffer_Release(&bfv); PyBuffer_Release(&bft); PyBuffer_Release(&bfp); PyBuffer_Release(&bnx); PyBuffer_Release(&bnlx); PyBuffer_Release(&bny); PyBuffer_Release(&bnly);
    Py_RETURN_NONE;
}

static PyMethodDef SolverMethods[] = {
    {"fill_g", py_fill_g, METH_VARARGS, ""}, 
    {"fill_F", py_fill_F, METH_VARARGS, ""},
    {"fill_Fd_slab", py_fill_Fd_slab, METH_VARARGS, ""}, 
    {"fd_slab_lookup", py_fd_slab_lookup, METH_VARARGS, ""},
    {"fd_slab_stats", py_fd_slab_stats, METH_VARARGS, ""}, 
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef solvermodule = { 
    PyModuleDef_HEAD_INIT, 
    "_solver", NULL, 
    -1, 
    SolverMethods 
};

PyMODINIT_FUNC PyInit__solver(void) { 
    return PyModule_Create(&solvermodule); 
}