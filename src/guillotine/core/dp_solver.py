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
        self.F_type = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int8)
        self.F_param = np.zeros((self.W0 + 1, self.H0 + 1), dtype=np.int32)
        
        # Defected rectangles: dict (4D space too sparse for array)
        # Stores (value, decision_type, decision_param)
        self.cache = {}
        
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
    
    def F(self, w, h):
        """Get pure rectangle value."""
        if w <= 0 or h <= 0:
            return 0
        return int(self.F_values[w, h])
    
    def F_d(self, x, y, w, h):
        """DP for defected rectangles with inlined child lookups."""
        if w <= 0 or h <= 0:
            return 0
        
        key = (x, y, w, h)
        cached = self.cache.get(key)
        if cached is not None:
            return cached[0]
        
        # Check purity
        if self.geom.is_pure(x, y, w, h):
            val = int(self.F_values[w, h])
            self.cache[key] = (val, DECISION_PURE, 0)
            return val
        
        best_val = 0
        best_type = DECISION_DEFECT
        best_param = 0
        
        # Local refs for speed
        cache = self.cache
        is_pure = self.geom.is_pure
        F_values = self.F_values
        
        X_cuts, Y_cuts = self.patterns.cuts_defected(x, y, w, h)
        
        # Vertical cuts with inlined lookups
        for z in X_cuts:
            # Left child
            lw, lh = z, h
            if lw > 0:
                lkey = (x, y, lw, lh)
                lc = cache.get(lkey)
                if lc is not None:
                    lv = lc[0]
                elif is_pure(x, y, lw, lh):
                    lv = int(F_values[lw, lh])
                    cache[lkey] = (lv, DECISION_PURE, 0)
                else:
                    lv = self.F_d(x, y, lw, lh)
            else:
                lv = 0
            
            # Right child
            rx, rw, rh = x + z, w - z, h
            if rw > 0:
                rkey = (rx, y, rw, rh)
                rc = cache.get(rkey)
                if rc is not None:
                    rv = rc[0]
                elif is_pure(rx, y, rw, rh):
                    rv = int(F_values[rw, rh])
                    cache[rkey] = (rv, DECISION_PURE, 0)
                else:
                    rv = self.F_d(rx, y, rw, rh)
            else:
                rv = 0
            
            total = lv + rv
            if total > best_val:
                best_val = total
                best_type = DECISION_CUT_X
                best_param = z
        
        # Horizontal cuts with inlined lookups
        for z in Y_cuts:
            # Bottom child
            bw, bh = w, z
            if bh > 0:
                bkey = (x, y, bw, bh)
                bc = cache.get(bkey)
                if bc is not None:
                    bv = bc[0]
                elif is_pure(x, y, bw, bh):
                    bv = int(F_values[bw, bh])
                    cache[bkey] = (bv, DECISION_PURE, 0)
                else:
                    bv = self.F_d(x, y, bw, bh)
            else:
                bv = 0
            
            # Top child
            ty, tw, th = y + z, w, h - z
            if th > 0:
                tkey = (x, ty, tw, th)
                tc = cache.get(tkey)
                if tc is not None:
                    tv = tc[0]
                elif is_pure(x, ty, tw, th):
                    tv = int(F_values[tw, th])
                    cache[tkey] = (tv, DECISION_PURE, 0)
                else:
                    tv = self.F_d(x, ty, tw, th)
            else:
                tv = 0
            
            total = bv + tv
            if total > best_val:
                best_val = total
                best_type = DECISION_CUT_Y
                best_param = z
        
        cache[key] = (best_val, best_type, best_param)
        return best_val
    
    def solve(self):
        """Solve and return (value, sequence)."""
        # Fast path: entirely pure sheet
        if self.geom.is_pure(0, 0, self.W0, self.H0):
            val = int(self.F_values[self.W0, self.H0])
            seq = self._reconstruct_F(self.W0, self.H0)
            return val, seq
        
        val = self.F_d(0, 0, self.W0, self.H0)
        seq = self._reconstruct_Fd(0, 0, self.W0, self.H0)
        return int(val), seq
    
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
        
        c = self.cache.get((x, y, w, h))
        if c is None:
            return 'empty'
        
        _, t, p = c
        p = int(p)
        
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