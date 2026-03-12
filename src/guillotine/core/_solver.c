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
            int32_t best_val = 0, best_idx = -1;
            for (int i = 0; i < n_items; i++) {
                int nx = nx_table[i];
                int ny = h / item_h[i];
                if (nx > 0 && ny > 0) {
                    int32_t val = item_area[i] * nx * ny;
                    if (val > best_val) { best_val = val; best_idx = i; }
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
                if (total > best_val) { best_val = total; best_type = DECISION_CUT_X; best_param = z; }
            }
            for (int i = 0; i < ny; i++) {
                int z = np_y_arr[y_cut_base + i];
                if (z > half_h) break;
                int32_t total = F_values[w_stride + z] + F_values[w_stride + (h - z)];
                if (total > best_val) { best_val = total; best_type = DECISION_CUT_Y; best_param = z; }
            }
            F_values[w_stride + h] = best_val;
            F_type  [w_stride + h] = (int8_t)best_type;
            F_param [w_stride + h] = best_param;
        }
    }
}

/* ========================================================================
 * Multi-tile slab for Fd
 *
 * Instead of one bounding box per (w,h), we build multiple disjoint tiles
 * by merging overlapping defect x-ranges, then for each x-group taking
 * the union y-range. This avoids wasting memory on the gap between
 * spatially separated defects.
 *
 * Example: defects at x=50 and x=350 on a 400-wide sheet.
 * Old single-BB approach: one tile spanning x=[0,400], y=[0,200] => 80K positions
 * New multi-tile: two tiles, x=[0,100] and x=[300,400], each with their y-range
 *   => maybe 20K + 20K = 40K positions (50% savings)
 *
 * Data structures:
 *   - Tile: {x_lo, x_hi, y_lo, y_hi, x_span, y_span, offset}
 *   - Per (w,h): n_tiles, pointer to first tile in a flat tile array
 *   - TileMeta[w*(H0+1)+h] = {first_tile_index, n_tiles}
 * ======================================================================== */

#define MAX_TILES_PER_WH 256  /* generous upper bound for merging */

typedef struct {
    int x_lo, x_hi;  /* inclusive */
    int y_lo, y_hi;  /* inclusive */
    int x_span, y_span;
    int64_t offset;   /* into flat data array */
} Tile;

typedef struct {
    int first;   /* index into tile array */
    int count;   /* number of tiles for this (w,h) */
} TileIndex;

typedef struct {
    int32_t   *data;       /* flat array of all Fd values */
    Tile      *tiles;      /* flat array of all tiles */
    TileIndex *tile_idx;   /* (W0+1)*(H0+1) entries */
    int64_t    total;      /* total entries in data */
    int        n_tiles;    /* total number of tiles */
    int        W0, H0;
} FdSlab;

/* Interval for x-range merging */
typedef struct {
    int lo, hi;     /* x range (inclusive) */
    int y_lo, y_hi; /* y range for this defect group */
} XInterval;

static int cmp_intervals(const void *a, const void *b) {
    return ((const XInterval *)a)->lo - ((const XInterval *)b)->lo;
}

/*
 * For a given (w,h), compute merged tiles from defect intervals.
 * Returns number of tiles written to out_tiles.
 */
static int build_tiles_for_wh(
    int w, int h, int W0, int H0,
    int32_t *defects, int n_def,
    XInterval *intervals,  /* scratch, size >= n_def */
    Tile *out_tiles        /* scratch, size >= n_def */
) {
    int n_iv = 0;

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

        intervals[n_iv].lo   = x_lo;
        intervals[n_iv].hi   = x_hi;
        intervals[n_iv].y_lo = y_lo;
        intervals[n_iv].y_hi = y_hi;
        n_iv++;
    }

    if (n_iv == 0) return 0;

    /* Sort by x_lo */
    qsort(intervals, n_iv, sizeof(XInterval), cmp_intervals);

    /* Merge overlapping x-ranges, union y-ranges within each group */
    int n_tiles = 0;
    int cur_xlo = intervals[0].lo;
    int cur_xhi = intervals[0].hi;
    int cur_ylo = intervals[0].y_lo;
    int cur_yhi = intervals[0].y_hi;

    for (int i = 1; i < n_iv; i++) {
        if (intervals[i].lo <= cur_xhi + 1) {
            /* Overlapping or adjacent in x — merge */
            if (intervals[i].hi > cur_xhi) cur_xhi = intervals[i].hi;
            if (intervals[i].y_lo < cur_ylo) cur_ylo = intervals[i].y_lo;
            if (intervals[i].y_hi > cur_yhi) cur_yhi = intervals[i].y_hi;
        } else {
            /* Gap in x — emit current tile, start new one */
            out_tiles[n_tiles].x_lo   = cur_xlo;
            out_tiles[n_tiles].x_hi   = cur_xhi;
            out_tiles[n_tiles].y_lo   = cur_ylo;
            out_tiles[n_tiles].y_hi   = cur_yhi;
            out_tiles[n_tiles].x_span = cur_xhi - cur_xlo + 1;
            out_tiles[n_tiles].y_span = cur_yhi - cur_ylo + 1;
            n_tiles++;

            cur_xlo = intervals[i].lo;
            cur_xhi = intervals[i].hi;
            cur_ylo = intervals[i].y_lo;
            cur_yhi = intervals[i].y_hi;
        }
    }
    /* Emit last tile */
    out_tiles[n_tiles].x_lo   = cur_xlo;
    out_tiles[n_tiles].x_hi   = cur_xhi;
    out_tiles[n_tiles].y_lo   = cur_ylo;
    out_tiles[n_tiles].y_hi   = cur_yhi;
    out_tiles[n_tiles].x_span = cur_xhi - cur_xlo + 1;
    out_tiles[n_tiles].y_span = cur_yhi - cur_ylo + 1;
    n_tiles++;

    return n_tiles;
}

/*
 * Lookup: scan tiles for (w,h), check if (x,y) falls in any tile.
 * Returns the Fd value, or F_values[w,h] if not in any tile.
 */
static inline int32_t fd_slab_lookup(
    const FdSlab *slab, const int32_t *F_values, int stride_F,
    int w, int h, int x, int y
) {
    const TileIndex *ti = &slab->tile_idx[w * (slab->H0 + 1) + h];
    const Tile *tiles = &slab->tiles[ti->first];
    int n = ti->count;

    for (int t = 0; t < n; t++) {
        if (x >= tiles[t].x_lo && x <= tiles[t].x_hi &&
            y >= tiles[t].y_lo && y <= tiles[t].y_hi)
        {
            return slab->data[tiles[t].offset +
                   (int64_t)(x - tiles[t].x_lo) * tiles[t].y_span +
                   (y - tiles[t].y_lo)];
        }
    }
    return F_values[w * stride_F + h];
}

static inline void fd_slab_set(
    FdSlab *slab, int w, int h, int x, int y, int32_t value
) {
    const TileIndex *ti = &slab->tile_idx[w * (slab->H0 + 1) + h];
    const Tile *tiles = &slab->tiles[ti->first];
    int n = ti->count;

    for (int t = 0; t < n; t++) {
        if (x >= tiles[t].x_lo && x <= tiles[t].x_hi &&
            y >= tiles[t].y_lo && y <= tiles[t].y_hi)
        {
            slab->data[tiles[t].offset +
                   (int64_t)(x - tiles[t].x_lo) * tiles[t].y_span +
                   (y - tiles[t].y_lo)] = value;
            return;
        }
    }
}

static FdSlab *fill_Fd_slab(
    int W0, int H0,
    int32_t *prefix, int32_t *F_values,
    int32_t *np_x_arr, int32_t *np_x_len, int max_cuts_x,
    int32_t *np_y_arr, int32_t *np_y_len, int max_cuts_y,
    int32_t *defects, int n_def
) {
    int stride_p = H0 + 1;
    int stride_F = H0 + 1;
    int meta_count = (W0 + 1) * (H0 + 1);

    FdSlab *slab = (FdSlab *)calloc(1, sizeof(FdSlab));
    if (!slab) return NULL;
    slab->W0 = W0;
    slab->H0 = H0;

    TileIndex *tile_idx = (TileIndex *)calloc(meta_count, sizeof(TileIndex));
    if (!tile_idx) { free(slab); return NULL; }
    slab->tile_idx = tile_idx;

    /* Scratch buffers for tile building */
    XInterval *iv_scratch = (XInterval *)malloc(n_def * sizeof(XInterval));
    Tile *tile_scratch = (Tile *)malloc(n_def * sizeof(Tile));
    if (!iv_scratch || !tile_scratch) {
        free(iv_scratch); free(tile_scratch);
        free(tile_idx); free(slab);
        return NULL;
    }

    /* ---- Phase 1: count total tiles across all (w,h) ---- */
    int total_tiles = 0;
    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            int nt = build_tiles_for_wh(w, h, W0, H0, defects, n_def,
                                        iv_scratch, tile_scratch);
            tile_idx[w * (H0 + 1) + h].count = nt;
            total_tiles += nt;
        }
    }

    /* ---- Phase 2: allocate tile array, assign offsets ---- */
    Tile *all_tiles = (Tile *)malloc(total_tiles * sizeof(Tile));
    if (!all_tiles) {
        free(iv_scratch); free(tile_scratch);
        free(tile_idx); free(slab);
        return NULL;
    }
    slab->tiles = all_tiles;
    slab->n_tiles = total_tiles;

    int tile_cursor = 0;
    int64_t data_total = 0;

    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            TileIndex *ti = &tile_idx[w * (H0 + 1) + h];
            ti->first = tile_cursor;

            int nt = build_tiles_for_wh(w, h, W0, H0, defects, n_def,
                                        iv_scratch, tile_scratch);

            for (int t = 0; t < nt; t++) {
                tile_scratch[t].offset = data_total;
                data_total += (int64_t)tile_scratch[t].x_span * tile_scratch[t].y_span;
                all_tiles[tile_cursor++] = tile_scratch[t];
            }
        }
    }
    slab->total = data_total;

    free(iv_scratch);
    free(tile_scratch);

    /* ---- Phase 3: allocate and pre-fill data ---- */
    slab->data = (int32_t *)malloc(data_total * sizeof(int32_t));
    if (!slab->data) {
        free(all_tiles); free(tile_idx); free(slab);
        return NULL;
    }

    /* Pre-fill each tile with F_values[w,h] */
    for (int w = 1; w <= W0; w++) {
        for (int h = 1; h <= H0; h++) {
            TileIndex *ti = &tile_idx[w * (H0 + 1) + h];
            int32_t fval = F_values[w * stride_F + h];
            for (int t = 0; t < ti->count; t++) {
                Tile *tile = &all_tiles[ti->first + t];
                int64_t sz = (int64_t)tile->x_span * tile->y_span;
                int32_t *p = &slab->data[tile->offset];
                for (int64_t i = 0; i < sz; i++) p[i] = fval;
            }
        }
    }

    /* ---- Phase 4: DP fill ---- */
    for (int w = 1; w <= W0; w++) {
        int nx = np_x_len[w];
        int w_max_cuts_x = w * max_cuts_x;

        for (int h = 1; h <= H0; h++) {
            int ny = np_y_len[h];
            int h_max_cuts_y = h * max_cuts_y;
            TileIndex *ti = &tile_idx[w * (H0 + 1) + h];

            if (ti->count == 0) continue;

            /* Iterate over each tile's (x,y) range */
            for (int t = 0; t < ti->count; t++) {
                Tile *tile = &all_tiles[ti->first + t];

                #pragma omp parallel for schedule(dynamic, 8) collapse(2)
                for (int x = tile->x_lo; x <= tile->x_hi; x++) {
                    for (int y = tile->y_lo; y <= tile->y_hi; y++) {

                        int32_t dc =
                            IDX_2D(prefix, stride_p, x+w, y+h)
                          - IDX_2D(prefix, stride_p, x,   y+h)
                          - IDX_2D(prefix, stride_p, x+w, y  )
                          + IDX_2D(prefix, stride_p, x,   y  );

                        if (dc == 0) continue; /* pure, already pre-filled */

                        int32_t best_val = 0;

                        /* X cuts — normal patterns */
                        for (int i = 0; i < nx; i++) {
                            int z = np_x_arr[w_max_cuts_x + i];
                            int32_t lv = fd_slab_lookup(slab, F_values, stride_F, z,   h, x,   y);
                            int32_t rv = fd_slab_lookup(slab, F_values, stride_F, w-z, h, x+z, y);
                            int32_t s = lv + rv;
                            if (s > best_val) best_val = s;
                        }

                        /* X cuts — defect-aligned */
                        for (int d = 0; d < n_def; d++) {
                            if (x >= DEF_X_END(d) || DEF_X(d) >= x + w) continue;
                            int z1 = DEF_X(d) - x;
                            int z2 = DEF_X_END(d) - x;
                            if (z1 > 0 && z1 < w) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, z1,   h, x,    y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w-z1, h, x+z1, y);
                                if (s > best_val) best_val = s;
                            }
                            if (z2 > 0 && z2 < w) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, z2,   h, x,    y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w-z2, h, x+z2, y);
                                if (s > best_val) best_val = s;
                            }
                        }

                        /* Y cuts — normal patterns */
                        for (int i = 0; i < ny; i++) {
                            int z = np_y_arr[h_max_cuts_y + i];
                            int32_t tv = fd_slab_lookup(slab, F_values, stride_F, w, z,   x, y);
                            int32_t bv = fd_slab_lookup(slab, F_values, stride_F, w, h-z, x, y+z);
                            int32_t s = tv + bv;
                            if (s > best_val) best_val = s;
                        }

                        /* Y cuts — defect-aligned */
                        for (int d = 0; d < n_def; d++) {
                            if (y >= DEF_Y_END(d) || DEF_Y(d) >= y + h) continue;
                            int z1 = DEF_Y(d) - y;
                            int z2 = DEF_Y_END(d) - y;
                            if (z1 > 0 && z1 < h) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z1,   x, y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w, h-z1, x, y+z1);
                                if (s > best_val) best_val = s;
                            }
                            if (z2 > 0 && z2 < h) {
                                int32_t s = fd_slab_lookup(slab, F_values, stride_F, w, z2,   x, y)
                                          + fd_slab_lookup(slab, F_values, stride_F, w, h-z2, x, y+z2);
                                if (s > best_val) best_val = s;
                            }
                        }

                        fd_slab_set(slab, w, h, x, y, best_val);
                    }
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
        free(slab->tiles);
        free(slab->tile_idx);
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
        free(slab->data); free(slab->tiles);
        free(slab->tile_idx); free(slab);
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
    return Py_BuildValue("(LLi)", (long long)slab->total, (long long)dense, slab->n_tiles);
}

/* ========================================================================
 * Legacy dense fill_Fd — kept for backward compat, int64_t strides
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
                        int z1 = DEF_X(d) - x; int z2 = DEF_X_END(d) - x;
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
                        int z1 = DEF_Y(d) - y; int z2 = DEF_Y_END(d) - y;
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

/* fill_g / fill_F wrappers */
static PyObject* py_fill_g(PyObject *self, PyObject *args) {
    int W0, H0, n_items;
    PyObject *o_gv, *o_gi, *o_iw, *o_ih, *o_ia;
    if (!PyArg_ParseTuple(args, "iiOOOOOi",
            &W0, &H0, &o_gv, &o_gi, &o_iw, &o_ih, &o_ia, &n_items))
        return NULL;
    Py_buffer bv={0},bi={0},bw={0},bh={0},ba={0};
    if (PyObject_GetBuffer(o_gv, &bv, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fg;
    if (PyObject_GetBuffer(o_gi, &bi, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fg;
    if (PyObject_GetBuffer(o_iw, &bw, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fg;
    if (PyObject_GetBuffer(o_ih, &bh, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fg;
    if (PyObject_GetBuffer(o_ia, &ba, PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fg;
    fill_g_core(W0,H0,(int32_t*)bv.buf,(int32_t*)bi.buf,
        (int32_t*)bw.buf,(int32_t*)bh.buf,(int32_t*)ba.buf,n_items);
    PyBuffer_Release(&bv);PyBuffer_Release(&bi);
    PyBuffer_Release(&bw);PyBuffer_Release(&bh);PyBuffer_Release(&ba);
    Py_RETURN_NONE;
fg: PyBuffer_Release(&bv);PyBuffer_Release(&bi);
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
    if (PyObject_GetBuffer(ogv,&bgv,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ogi,&bgi,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ofv,&bfv,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(oft,&bft,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ofp,&bfp,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(onx,&bnx,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(onlx,&bnlx,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(ony,&bny,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
    if (PyObject_GetBuffer(only,&bnly,PyBUF_SIMPLE|PyBUF_C_CONTIGUOUS)<0) goto fF;
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
fF: PyBuffer_Release(&bgv);PyBuffer_Release(&bgi);
    PyBuffer_Release(&bfv);PyBuffer_Release(&bft);PyBuffer_Release(&bfp);
    PyBuffer_Release(&bnx);PyBuffer_Release(&bnlx);
    PyBuffer_Release(&bny);PyBuffer_Release(&bnly);
    return NULL;
}

static PyMethodDef SolverMethods[] = {
    {"fill_g",         py_fill_g,         METH_VARARGS, "Precompute g tables."},
    {"fill_F",         py_fill_F,         METH_VARARGS, "Bottom-up DP for pure rectangles."},
    {"fill_Fd",        py_fill_Fd,        METH_VARARGS, "Dense Fd fill (legacy)."},
    {"fill_Fd_slab",   py_fill_Fd_slab,   METH_VARARGS, "Multi-tile slab Fd fill."},
    {"fd_slab_lookup", py_fd_slab_lookup, METH_VARARGS, "Lookup Fd value from slab."},
    {"fd_slab_stats",  py_fd_slab_stats,  METH_VARARGS, "Return (slab_entries, dense_equivalent, n_tiles)."},
    {NULL, NULL, 0, NULL}
};

static struct PyModuleDef solvermodule = {
    PyModuleDef_HEAD_INIT, "_solver", NULL, -1, SolverMethods
};

PyMODINIT_FUNC PyInit__solver(void) {
    return PyModule_Create(&solvermodule);
}