"""DP solver for guillotine cutting."""

import numpy as np
from guillotine.core.constants import (
    DECISION_EMPTY,
    DECISION_FILL,
    DECISION_CUT_X,
    DECISION_CUT_Y,
)


class GuillotineDP:
    """Optimized DP solver with multi-tile slab-based defected-rectangle storage."""

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

        # Legacy dense array (only used if slab is unavailable)
        self.Fd_values = None

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
        except ImportError:
            item_w, item_h, item_area = self.item_w, self.item_h, self.item_area
            n_items = self.n_items
            for w in range(1, W0 + 1):
                for h in range(1, H0 + 1):
                    best_val = 0
                    best_idx = -1
                    for i in range(n_items):
                        nx = w // item_w[i]
                        ny = h // item_h[i]
                        if nx > 0 and ny > 0:
                            val = item_area[i] * nx * ny
                            if val > best_val:
                                best_val = val
                                best_idx = i
                    g_values[w, h]  = best_val
                    g_indices[w, h] = best_idx

        self.g_values  = g_values
        self.g_indices = g_indices

    def _precompute_F(self):
        """Bottom-up DP for pure rectangles (no defects)."""
        W0, H0    = self.W0, self.H0
        F_values  = self.F_values
        F_type    = self.F_type
        F_param   = self.F_param

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
            return
        except ImportError:
            pass

        patterns  = self.patterns
        g_values  = self.g_values
        g_indices = self.g_indices

        for w in range(1, W0 + 1):
            for h in range(1, H0 + 1):
                best_val   = int(g_values[w, h])
                best_type  = DECISION_FILL if g_indices[w, h] >= 0 else DECISION_EMPTY
                best_param = int(g_indices[w, h]) if g_indices[w, h] >= 0 else 0

                half_w = w >> 1
                for z in patterns.cuts_pure_x(w):
                    if z > half_w:
                        break
                    total = F_values[z, h] + F_values[w - z, h]
                    if total > best_val:
                        best_val   = total
                        best_type  = DECISION_CUT_X
                        best_param = z

                half_h = h >> 1
                for z in patterns.cuts_pure_y(h):
                    if z > half_h:
                        break
                    total = F_values[w, z] + F_values[w, h - z]
                    if total > best_val:
                        best_val   = total
                        best_type  = DECISION_CUT_Y
                        best_param = z

                F_values[w, h] = best_val
                F_type[w, h]   = best_type
                F_param[w, h]  = best_param

    def _fd_value(self, w, h, x, y):
        """Look up Fd value using slab (preferred) or dense array (legacy)."""
        if self._fd_slab is not None:
            from guillotine.core import _solver
            return _solver.fd_slab_lookup(self._fd_slab, self.F_values, self.H0, w, h, x, y)
        else:
            return int(self.Fd_values[w, h, x, y])

    def _fill_Fd(self):
        """Fill defected rectangle DP table using multi-tile slab storage."""
        try:
            from guillotine.core import _solver
            self._fd_slab = _solver.fill_Fd_slab(
                self.W0, self.H0,
                self.geom.prefix, self.F_values,
                self.patterns.np_x_arr, self.patterns.np_x_len,
                self.patterns.np_y_arr, self.patterns.np_y_len,
                self.geom.defects_arr,
                int(self.patterns.np_x_arr.shape[1]),
                int(self.patterns.np_y_arr.shape[1]),
                self.geom.n_def,
            )
            slab_entries, dense_entries, n_tiles = _solver.fd_slab_stats(self._fd_slab)
            print(f"Fd slab: {slab_entries:,} entries ({slab_entries * 4 / 1024 / 1024:.1f} MB) "
                  f"in {n_tiles:,} tiles, "
                  f"vs dense {dense_entries:,} ({dense_entries * 4 / 1024 / 1024:.1f} MB), "
                  f"ratio {slab_entries / max(dense_entries, 1) * 100:.2f}%")
            return
        except ImportError:
            pass

        # Legacy dense fallback
        shape = (self.W0 + 1, self.H0 + 1, self.W0 + 1, self.H0 + 1)
        self.Fd_values = np.zeros(shape, dtype=np.int32)

        try:
            from guillotine.core import _solver
            _solver.fill_Fd(
                self.W0, self.H0, self.geom.prefix, self.F_values, self.Fd_values,
                self.patterns.np_x_arr, self.patterns.np_x_len,
                self.patterns.np_y_arr, self.patterns.np_y_len,
                self.geom.defects_arr,
                int(self.patterns.np_x_arr.shape[1]),
                int(self.patterns.np_y_arr.shape[1]),
                self.geom.n_def
            )
        except ImportError:
            pass

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
        """Finds the optimal cut by checking which candidate cut produces the target value."""
        if w <= 0 or h <= 0:
            return 'empty'

        target_val = self._fd_value(w, h, x, y)

        if target_val == 0:
            if self.geom.prefix[x+w, y+h] - self.geom.prefix[x, y+h] - self.geom.prefix[x+w, y] + self.geom.prefix[x, y] > 0:
                return 'defect'
            return 'empty'

        # If pure, delegate to 2D reconstruction
        if self.geom.prefix[x+w, y+h] - self.geom.prefix[x, y+h] - self.geom.prefix[x+w, y] + self.geom.prefix[x, y] == 0:
            return self._reconstruct_F(w, h)

        X_cuts, Y_cuts = self.patterns.cuts_defected(x, y, w, h)

        for z in X_cuts:
            if self._fd_value(z, h, x, y) + self._fd_value(w - z, h, x + z, y) == target_val:
                return ('X', z, self._reconstruct_Fd(x, y, z, h), self._reconstruct_Fd(x + z, y, w - z, h))

        for z in Y_cuts:
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