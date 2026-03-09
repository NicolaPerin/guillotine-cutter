"""DP solver for guillotine cutting."""

import numpy as np
from guillotine.core.constants import (
    DECISION_EMPTY,
    DECISION_FILL,
    DECISION_CUT_X,
    DECISION_CUT_Y,
    DECISION_DEFECT,
    DECISION_PURE,
)

# Bit layout for Fd_packed (uint16):
#   bits 15-13 : decision type (DECISION_* constants, values 0-5)
#   bits 12-0  : cut parameter (cut position z, max 8191)
_PACK_SHIFT = 13
_PACK_MASK  = 0x1FFF


class GuillotineDP:
    """Optimized DP solver."""

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

        # Defected rectangles: allocated lazily in _fill_Fd() to avoid
        # wasting (W0+1)^4 × 6 bytes when the sheet is pure
        self.Fd_values = None
        self.Fd_packed = None

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

                # Vertical cuts — exploit symmetry, only try z <= w/2
                half_w = w >> 1
                for z in patterns.cuts_pure_x(w):
                    if z > half_w:
                        break
                    total = F_values[z, h] + F_values[w - z, h]
                    if total > best_val:
                        best_val   = total
                        best_type  = DECISION_CUT_X
                        best_param = z

                # Horizontal cuts — exploit symmetry, only try z <= h/2
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

    def _fill_Fd(self):
        """Bottom-up iterative DP for defected rectangles.

        Tries the C extension first; falls back to pure Python if unavailable.
        Arrays use layout [w, h, x, y] for cache locality in the hot loop.
        """
        # Allocate 4D tables on first use — avoids wasting memory for pure sheets
        shape = (self.W0+1, self.H0+1, self.W0+1, self.H0+1)
        self.Fd_values = np.zeros(shape, dtype=np.uint32)
        self.Fd_packed = np.zeros(shape, dtype=np.uint16)

        try:
            from guillotine.core import _solver
            _solver.fill_Fd(
                self.W0, self.H0,
                self.geom.prefix,
                self.F_values,
                self.Fd_values, self.Fd_packed,         # uint32 values + uint16 packed type/param
                self.patterns.np_x_arr, self.patterns.np_x_len,
                self.patterns.np_y_arr, self.patterns.np_y_len,
                self.geom.defects_arr,
                int(self.patterns.np_x_arr.shape[1]),
                int(self.patterns.np_y_arr.shape[1]),
                self.geom.n_def
            )
            return
        except ImportError:
            pass

        # Python fallback — same logic as C extension, used when .so is not compiled
        W0, H0    = self.W0, self.H0
        F_values  = self.F_values
        Fd_values = self.Fd_values
        Fd_packed = self.Fd_packed
        prefix    = self.geom.prefix

        for w in range(1, W0 + 1):
            for h in range(1, H0 + 1):
                for x in range(0, W0 - w + 1):
                    for y in range(0, H0 - h + 1):

                        # Purity check via prefix sum — O(1)
                        if prefix[x+w, y+h] - prefix[x, y+h] - prefix[x+w, y] + prefix[x, y] == 0:
                            Fd_values[w, h, x, y] = F_values[w, h]
                            Fd_packed[w, h, x, y] = DECISION_PURE << _PACK_SHIFT
                            continue

                        best_val   = 0
                        best_type  = DECISION_DEFECT
                        best_param = 0

                        X_cuts, Y_cuts = self.patterns.cuts_defected(x, y, w, h)

                        for z in X_cuts:
                            lv    = int(Fd_values[z,     h, x,   y])
                            rv    = int(Fd_values[w - z, h, x+z, y])
                            total = lv + rv
                            if total > best_val:
                                best_val   = total
                                best_type  = DECISION_CUT_X
                                best_param = z

                        for z in Y_cuts:
                            bv    = int(Fd_values[w, z,     x, y  ])
                            tv    = int(Fd_values[w, h - z, x, y+z])
                            total = bv + tv
                            if total > best_val:
                                best_val   = total
                                best_type  = DECISION_CUT_Y
                                best_param = z

                        Fd_values[w, h, x, y] = best_val
                        Fd_packed[w, h, x, y] = (best_type << _PACK_SHIFT) | (best_param & _PACK_MASK)

    def solve(self):
        """Solve and return (optimal_value, cut_sequence)."""
        # Fast path: pure sheet — skip Fd entirely
        if self.geom.is_pure(0, 0, self.W0, self.H0):
            val = int(self.F_values[self.W0, self.H0])
            seq = self._reconstruct_F(self.W0, self.H0)
            return val, seq

        self._fill_Fd()
        val = int(self.Fd_values[self.W0, self.H0, 0, 0])
        seq = self._reconstruct_Fd(0, 0, self.W0, self.H0)
        return val, seq

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
        """Reconstruct cut sequence for a defected rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'

        # Unpack type and param from single uint16 field
        packed = int(self.Fd_packed[w, h, x, y])
        t = (packed >> _PACK_SHIFT) & 0x7
        p = packed & _PACK_MASK

        if t == DECISION_PURE:
            return self._reconstruct_F(w, h)
        if t == DECISION_EMPTY:
            return 'empty'
        if t == DECISION_DEFECT:
            return 'defect'
        if t == DECISION_CUT_X:
            return ('X', p, self._reconstruct_Fd(x, y, p, h), self._reconstruct_Fd(x + p, y, w - p, h))
        if t == DECISION_CUT_Y:
            return ('Y', p, self._reconstruct_Fd(x, y, w, p), self._reconstruct_Fd(x, y + p, w, h - p))
        return 'empty'