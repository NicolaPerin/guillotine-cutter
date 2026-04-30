"""DP solver for guillotine cutting."""

import numpy as np
from guillotine.core.constants import (
    DECISION_EMPTY,
    DECISION_FILL,
    DECISION_CUT_X,
    DECISION_CUT_Y,
)


class GuillotineDP:
    """DP solver for the guillotine cutting-stock problem.

    Phase 1 (tiling table)  precomputes the best single-item tiling value
                            for each rectangle size.
    Phase 2 (pure table)    bottom-up DP for defect-free rectangles, using
                            normal-pattern cut candidates.
    Phase 3 (defect slab)   bottom-up DP for defect-affected rectangles,
                            stored in a sparse slab with uint16 delta encoding.
                            All integer cut positions are evaluated, guaranteeing
                            optimality without precomputed candidate sets.
    """

    def __init__(self, item_sizes, geometry, patterns):
        self.geom     = geometry
        self.patterns = patterns
        self.W0       = geometry.W0
        self.H0       = geometry.H0

        self.item_w    = np.array(item_sizes[0], dtype=np.int32)
        self.item_h    = np.array(item_sizes[1], dtype=np.int32)
        self.item_area = self.item_w * self.item_h
        self.n_items   = len(self.item_w)

        # Pure-rectangle tables, shape (W0+1, H0+1)
        self.pure_values = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)
        self.pure_type   = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int8)
        self.pure_param  = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)

        # DefectSlab PyCapsule (set during _fill_defect_slab)
        self._defect_slab = None

        self._precompute_tiling()
        self._precompute_pure()

    def _precompute_tiling(self):
        """Precompute best single-item tiling value and item index for each rectangle size."""
        W0, H0 = self.W0, self.H0

        tiling_values      = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        tiling_item_index  = np.full((W0 + 1, H0 + 1), -1, dtype=np.int32)

        try:
            from guillotine.core import _solver
            _solver.fill_tiling(
                W0, H0,
                tiling_values, tiling_item_index,
                self.item_w, self.item_h, self.item_area,
                self.n_items,
            )
        except ImportError as e:
            raise RuntimeError(
                "The guillotine C extension (_solver) is required but could not be imported."
            ) from e

        self.tiling_values     = tiling_values
        self.tiling_item_index = tiling_item_index

    def _precompute_pure(self):
        """Bottom-up DP for defect-free rectangles."""
        W0, H0 = self.W0, self.H0

        try:
            from guillotine.core import _solver
            _solver.fill_pure(
                W0, H0,
                self.tiling_values, self.tiling_item_index,
                self.pure_values, self.pure_type, self.pure_param,
                self.patterns.np_x_arr, self.patterns.np_x_len,
                self.patterns.np_y_arr, self.patterns.np_y_len,
                int(self.patterns.np_x_arr.shape[1]),
                int(self.patterns.np_y_arr.shape[1]),
            )
        except ImportError as e:
            raise RuntimeError(
                "The guillotine C extension (_solver) is required but could not be imported."
            ) from e

    def _defect_value(self, w, h, x, y):
        """Look up the defect-adjusted value at placement (w, h, x, y)."""
        from guillotine.core import _solver
        return _solver.defect_slab_lookup(
            self._defect_slab, self.pure_values, self.H0, w, h, x, y)

    def _fill_defect_slab(self):
        """Build and fill the sparse defect slab for Phase 3."""
        try:
            from guillotine.core import _solver
            min_w = int(self.item_w.min())
            min_h = int(self.item_h.min())
            self._defect_slab = _solver.fill_defect_slab(
                self.W0, self.H0,
                self.geom.prefix, self.pure_values,
                self.geom.defects_arr,
                self.geom.n_def,
                min_w, min_h,
            )
            slab_entries, dense_entries, n_tiles, overflow = \
                _solver.defect_slab_stats(self._defect_slab)

            if overflow:
                raise RuntimeError(
                    "Defect slab overflow: a delta value exceeded UINT16_MAX (65535). "
                    "The slab stores pure_values[w][h] - defect_value(w,h,sx,sy) as "
                    "uint16. To fix this, change the slab data[] type from uint16_t to "
                    "uint32_t in solver_core.h and solver_core.c, then rebuild."
                )

            slab_bytes  = slab_entries * 2   # uint16
            dense_bytes = dense_entries * 4  # int32 if fully materialised

            print(
                f"Fd slab: {slab_entries:,} deltas "
                f"({slab_bytes / 1024 / 1024:.1f} MB uint16) "
                f"in {n_tiles:,} tiles, "
                f"vs dense {dense_entries:,} "
                f"({dense_bytes / 1024 / 1024:.1f} MB int32), "
                f"ratio {slab_bytes / max(dense_bytes, 1) * 100:.2f}%"
            )

        except ImportError as e:
            raise RuntimeError(
                "The guillotine C extension (_solver) is required but could not be imported."
            ) from e

    def _reconstruct_pure(self, w, h):
        """Reconstruct the cut sequence for a defect-free rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'

        t = int(self.pure_type[w, h])
        p = int(self.pure_param[w, h])

        if t == DECISION_EMPTY:
            return 'empty'
        if t == DECISION_FILL:
            return f'g_{p}'
        if t == DECISION_CUT_X:
            return ('X', p, self._reconstruct_pure(p, h), self._reconstruct_pure(w - p, h))
        if t == DECISION_CUT_Y:
            return ('Y', p, self._reconstruct_pure(w, p), self._reconstruct_pure(w, h - p))
        return 'empty'

    def _reconstruct_defect(self, x, y, w, h):
        """Reconstruct the cut sequence for a defect-affected rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'

        target_val = self._defect_value(w, h, x, y)

        if target_val == 0:
            return 'defect' if not self.geom.is_pure(x, y, w, h) else 'empty'

        if self.geom.is_pure(x, y, w, h):
            return self._reconstruct_pure(w, h)

        for z in range(1, w):
            if (self._defect_value(z, h, x, y) +
                    self._defect_value(w - z, h, x + z, y) == target_val):
                return ('X', z,
                        self._reconstruct_defect(x, y, z, h),
                        self._reconstruct_defect(x + z, y, w - z, h))

        for z in range(1, h):
            if (self._defect_value(w, z, x, y) +
                    self._defect_value(w, h - z, x, y + z) == target_val):
                return ('Y', z,
                        self._reconstruct_defect(x, y, w, z),
                        self._reconstruct_defect(x, y + z, w, h - z))

        return 'defect'

    def solve(self):
        if self.geom.is_pure(0, 0, self.W0, self.H0):
            return (int(self.pure_values[self.W0, self.H0]),
                    self._reconstruct_pure(self.W0, self.H0))

        self._fill_defect_slab()
        val = self._defect_value(self.W0, self.H0, 0, 0)
        seq = self._reconstruct_defect(0, 0, self.W0, self.H0)
        return val, seq