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

    Phase 1 (g-table)  precomputes the best single-item tiling value for
                       each rectangle size.
    Phase 2 (F-table)  bottom-up DP for defect-free rectangles, using
                       normal-pattern cut candidates.
    Phase 3 (Fd-table) bottom-up DP for defect-affected rectangles, stored
                       in a sparse slab with uint16 delta encoding.  All
                       integer cut positions are evaluated, guaranteeing
                       optimality without precomputed candidate sets.
    """

    def __init__(self, item_sizes, geometry, patterns):
        self.geom = geometry
        self.patterns = patterns
        self.W0 = geometry.W0
        self.H0 = geometry.H0

        self.item_w    = np.array(item_sizes[0], dtype=np.int32)
        self.item_h    = np.array(item_sizes[1], dtype=np.int32)
        self.item_area = self.item_w * self.item_h
        self.n_items   = len(self.item_w)

        # Pure rectangles: 2D tables, shape (W0+1, H0+1)
        self.F_values = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)
        self.F_type   = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int8)
        self.F_param  = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)

        # Slab Fd: PyCapsule wrapping C FdSlab (set during _fill_Fd)
        self._fd_slab = None

        self._precompute_g()
        self._precompute_F()

    def _precompute_g(self):
        """Precompute best single-item tiling value and item index for each rectangle size."""
        W0, H0 = self.W0, self.H0

        g_values  = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        g_indices = np.full((W0 + 1, H0 + 1), -1, dtype=np.int32)

        try:
            from guillotine.core import _solver
            _solver.fill_g(
                W0, H0,
                g_values, g_indices,
                self.item_w, self.item_h, self.item_area,
                self.n_items
            )
        except ImportError as e:
            raise RuntimeError(
                "The guillotine C extension (_solver) is required but could not be imported. "
            ) from e

        self.g_values  = g_values
        self.g_indices = g_indices

    def _precompute_F(self):
        """Bottom-up DP for pure rectangles (no defects)."""
        W0, H0   = self.W0, self.H0
        F_values = self.F_values
        F_type   = self.F_type
        F_param  = self.F_param

        try:
            from guillotine.core import _solver
            _solver.fill_F(
                W0, H0,
                self.g_values, self.g_indices,
                F_values, F_type, F_param,
                self.patterns.np_x_arr, self.patterns.np_x_len,
                self.patterns.np_y_arr, self.patterns.np_y_len,
                int(self.patterns.np_x_arr.shape[1]),
                int(self.patterns.np_y_arr.shape[1]),
            )
        except ImportError as e:
            raise RuntimeError(
                "The guillotine C extension (_solver) is required but could not be imported. "
            ) from e

    def _fd_value(self, w, h, x, y):
        """Look up Fd(w, h, x, y) from the slab."""
        from guillotine.core import _solver
        return _solver.fd_slab_lookup(self._fd_slab, self.F_values, self.H0, w, h, x, y)

    def _fill_Fd(self):
        """Fill defected rectangle DP table using multi-tile slab storage."""
        try:
            from guillotine.core import _solver
            min_w = int(self.item_w.min())
            min_h = int(self.item_h.min())
            self._fd_slab = _solver.fill_Fd_slab(
                self.W0, self.H0,
                self.geom.prefix, self.F_values,
                self.geom.defects_arr,
                self.geom.n_def,
                min_w, min_h,
            )
            slab_entries, dense_entries, n_tiles, overflow = _solver.fd_slab_stats(self._fd_slab)
            if overflow:
                raise RuntimeError(
                    "Fd slab overflow: a delta value exceeded UINT16_MAX (65535). "
                    "The slab stores F[w][h] - Fd(w,h,sx,sy) as uint16, so this "
                    "requires F[w][h] > 65535 for some (w,h). To fix this, change "
                    "the slab data[] type from uint16_t to uint32_t in solver_core.h "
                    "and solver_core.c (search for uint16), then rebuild."
                )
            
            slab_bytes  = slab_entries * 2    # uint16
            dense_bytes = dense_entries * 4   # would be int32 if materialized

            print(f"Fd slab: {slab_entries:,} deltas ({slab_bytes / 1024 / 1024:.1f} MB uint16) "
                f"in {n_tiles:,} tiles, "
                f"vs dense {dense_entries:,} ({dense_bytes / 1024 / 1024:.1f} MB int32), "
                f"ratio {slab_bytes / max(dense_bytes, 1) * 100:.2f}%")
            
        except ImportError as e:
            raise RuntimeError(
                "The guillotine C extension (_solver) is required but could not be imported. "
            ) from e

    def _reconstruct_F(self, w, h):
        """Reconstruct cut sequence for a pure rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'

        t = int(self.F_type[w, h])
        p = int(self.F_param[w, h])

        if t == DECISION_EMPTY:
            return 'empty'
        if t == DECISION_FILL:
            return f'g_{p}'
        if t == DECISION_CUT_X:
            return ('X', p, self._reconstruct_F(p, h), self._reconstruct_F(w - p, h))
        if t == DECISION_CUT_Y:
            return ('Y', p, self._reconstruct_F(w, p), self._reconstruct_F(w, h - p))
        return 'empty'

    def _reconstruct_Fd(self, x, y, w, h):
        """Reconstruct cut sequence for a defect-affected rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'

        target_val = self._fd_value(w, h, x, y)

        if target_val == 0:
            return 'defect' if not self.geom.is_pure(x, y, w, h) else 'empty'

        if self.geom.is_pure(x, y, w, h):
            return self._reconstruct_F(w, h)

        for z in range(1, w):
            if self._fd_value(z, h, x, y) + self._fd_value(w - z, h, x + z, y) == target_val:
                return ('X', z, self._reconstruct_Fd(x, y, z, h), self._reconstruct_Fd(x + z, y, w - z, h))

        for z in range(1, h):
            if self._fd_value(w, z, x, y) + self._fd_value(w, h - z, x, y + z) == target_val:
                return ('Y', z, self._reconstruct_Fd(x, y, w, z), self._reconstruct_Fd(x, y + z, w, h - z))

        return 'defect'

    def solve(self):
        if self.geom.is_pure(0, 0, self.W0, self.H0):
            return int(self.F_values[self.W0, self.H0]), self._reconstruct_F(self.W0, self.H0)

        self._fill_Fd()
        val = self._fd_value(self.W0, self.H0, 0, 0)
        seq = self._reconstruct_Fd(0, 0, self.W0, self.H0)
        return val, seq