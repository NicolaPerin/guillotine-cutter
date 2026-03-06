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


class GuillotineDP:
    """Optimized DP solver v7."""
    
    def __init__(self, item_sizes, geometry, patterns):
        self.geom = geometry
        self.patterns = patterns
        self.W0 = geometry.W0
        self.H0 = geometry.H0
        
        self.item_w = np.array(item_sizes[0], dtype=np.int32)
        self.item_h = np.array(item_sizes[1], dtype=np.int32)
        self.item_area = self.item_w * self.item_h
        self.n_items = len(self.item_w)
        
        # Pure rectangles: numpy arrays for O(1) access (no hashing)
        self.F_values = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)
        self.F_type   = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)
        self.F_param  = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)

        # Defected rectangles: dense 4D arrays (will replace cache)
        self.Fd_values = np.zeros((self.W0 + 1, self.H0 + 1, self.W0 + 1, self.H0 + 1), dtype=np.int32)
        self.Fd_type   = np.zeros((self.W0 + 1, self.H0 + 1, self.W0 + 1, self.H0 + 1), dtype=np.int32)
        self.Fd_param  = np.zeros((self.W0 + 1, self.H0 + 1, self.W0 + 1, self.H0 + 1), dtype=np.int32)
        
        self._precompute_g()
        self._precompute_F()
    
    def _precompute_g(self):
        """Precompute best tiling for each size."""
        W0, H0 = self.W0, self.H0
        item_w, item_h, item_area = self.item_w, self.item_h, self.item_area
        n_items = self.n_items
        
        g_values = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        g_indices = np.full((W0 + 1, H0 + 1), -1, dtype=np.int32)
        
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
                g_values[w, h] = best_val
                g_indices[w, h] = best_idx
        
        self.g_values = g_values
        self.g_indices = g_indices
    
    def _precompute_F(self):
        """Bottom-up DP for pure rectangles."""
        W0, H0 = self.W0, self.H0
        patterns = self.patterns
        g_values = self.g_values
        g_indices = self.g_indices
        F_values = self.F_values
        F_type = self.F_type
        F_param = self.F_param
        
        for w in range(1, W0 + 1):
            for h in range(1, H0 + 1):
                best_val = int(g_values[w, h])
                best_type = DECISION_FILL if g_indices[w, h] >= 0 else DECISION_EMPTY
                best_param = int(g_indices[w, h]) if g_indices[w, h] >= 0 else 0
                
                # Vertical cuts (use symmetry)
                half_w = w >> 1
                for z in patterns.cuts_pure_x(w):
                    if z > half_w:
                        break
                    total = F_values[z, h] + F_values[w - z, h]
                    if total > best_val:
                        best_val = total
                        best_type = DECISION_CUT_X
                        best_param = z
                
                # Horizontal cuts (use symmetry)
                half_h = h >> 1
                for z in patterns.cuts_pure_y(h):
                    if z > half_h:
                        break
                    total = F_values[w, z] + F_values[w, h - z]
                    if total > best_val:
                        best_val = total
                        best_type = DECISION_CUT_Y
                        best_param = z
                
                F_values[w, h] = best_val
                F_type[w, h] = best_type
                F_param[w, h] = best_param
    
    def _fill_Fd(self):
        """Bottom-up iterative DP for defected rectangles."""
        W0, H0 = self.W0, self.H0
        F_values = self.F_values
        Fd_values = self.Fd_values
        Fd_type   = self.Fd_type
        Fd_param  = self.Fd_param
        prefix   = self.geom.prefix

        for w in range(1, W0 + 1):
            for h in range(1, H0 + 1):
                for x in range(0, W0 - w + 1):
                    for y in range(0, H0 - h + 1):

                        if prefix[x+w, y+h] - prefix[x, y+h] - prefix[x+w, y] + prefix[x, y] == 0:
                            Fd_values[x, y, w, h] = F_values[w, h]
                            Fd_type[x, y, w, h]   = DECISION_PURE
                            Fd_param[x, y, w, h]  = 0
                            continue

                        best_val  = 0
                        best_type = DECISION_DEFECT
                        best_param = 0

                        X_cuts, Y_cuts = self.patterns.cuts_defected(x, y, w, h)

                        for z in X_cuts:
                            lv = Fd_values[x,     y, z,     h]
                            rv = Fd_values[x + z, y, w - z, h]
                            total = int(lv) + int(rv)
                            if total > best_val:
                                best_val   = total
                                best_type  = DECISION_CUT_X
                                best_param = z

                        for z in Y_cuts:
                            bv = Fd_values[x, y,     w, z    ]
                            tv = Fd_values[x, y + z, w, h - z]
                            total = int(bv) + int(tv)
                            if total > best_val:
                                best_val   = total
                                best_type  = DECISION_CUT_Y
                                best_param = z

                        Fd_values[x, y, w, h] = best_val
                        Fd_type[x, y, w, h]   = best_type
                        Fd_param[x, y, w, h]  = best_param
    
    def solve(self):
        """Solve and return (value, sequence)."""
        if self.geom.is_pure(0, 0, self.W0, self.H0):
            val = int(self.F_values[self.W0, self.H0])
            seq = self._reconstruct_F(self.W0, self.H0)
            return val, seq

        self._fill_Fd()
        val = int(self.Fd_values[0, 0, self.W0, self.H0])
        seq = self._reconstruct_Fd(0, 0, self.W0, self.H0)
        return val, seq
    
    def _reconstruct_F(self, w, h):
        """Reconstruct sequence for pure rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'
        
        t = self.F_type[w, h]
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
        """Reconstruct sequence for defected rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'
        
        t = int(self.Fd_type[x, y, w, h])
        p = int(self.Fd_param[x, y, w, h])
        
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