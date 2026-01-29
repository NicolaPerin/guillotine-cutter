"""DP solver for guillotine cutting."""

import numpy as np
from guillotine.core.constants import (
    NOT_COMPUTED,
    DECISION_EMPTY,
    DECISION_FILL,
    DECISION_CUT_X,
    DECISION_CUT_Y,
    DECISION_DEFECT,
    DECISION_PURE,
)


class GuillotineDP:
    """Optimized DP solver with bottom-up pure computation and sparse defect cache."""
    
    def __init__(self, item_sizes, geometry, patterns):
        """Initialize solver and precompute pure rectangle values."""
        self.geom = geometry
        self.patterns = patterns
        self.W0 = geometry.W0
        self.H0 = geometry.H0
        
        # Convert items to numpy for fast g computation
        self.item_w = np.array(item_sizes[0], dtype=np.int32)
        self.item_h = np.array(item_sizes[1], dtype=np.int32)
        self.item_area = self.item_w * self.item_h
        self.n_items = len(self.item_w)
        
        # Precompute g(w,h) for all dimensions
        # g_values[w,h] = best area achievable by tiling with single item type
        # g_indices[w,h] = which item achieves that (for reconstruction)
        self._precompute_g()
        
        # Precompute F(w,h) bottom-up for all pure rectangles
        # This eliminates ALL recursion for pure rectangles
        self._precompute_F()
        
        # Sparse cache for defected rectangles: (x,y,w,h) -> (value, type, param)
        # Much more memory-efficient than dense 4D array
        self.cache_Fd = {}
    
    def _precompute_g(self):
        """Precompute best single-item-type tiling for all rectangle sizes.
        
        For each (w,h), find which item type gives maximum coverage.
        
        Time: O(W * H * n_items)
        Space: O(W * H)
        """
        W0, H0 = self.W0, self.H0
        n_items = self.n_items
        item_w = self.item_w
        item_h = self.item_h
        item_area = self.item_area
        
        # Arrays to store results
        g_values = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        g_indices = np.full((W0 + 1, H0 + 1), -1, dtype=np.int32)
        
        # For each rectangle size
        for w in range(1, W0 + 1):
            for h in range(1, H0 + 1):
                best_val = 0
                best_idx = -1
                
                # Try each item type
                for i in range(n_items):
                    # How many items fit in each direction?
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
        """Precompute optimal values for all pure rectangles bottom-up.
        
        F(w,h) = max of:
          - g(w,h): tile with single item type
          - max over z: F(z,h) + F(w-z,h)  [vertical cut]
          - max over z: F(w,z) + F(w,h-z)  [horizontal cut]
        
        By processing in order of increasing w and h, all subproblems
        are solved before we need them.
        
        Time: O(W * H * max_cuts)
        Space: O(W * H)
        """
        W0, H0 = self.W0, self.H0
        patterns = self.patterns
        g_values = self.g_values
        g_indices = self.g_indices
        
        # Arrays for F values and decisions
        F_values = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        F_type = np.zeros((W0 + 1, H0 + 1), dtype=np.int8)
        F_param = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        
        # Process in order of increasing dimensions
        for w in range(1, W0 + 1):
            for h in range(1, H0 + 1):
                # Start with tiling option
                best_val = g_values[w, h]
                best_type = DECISION_FILL if g_indices[w, h] >= 0 else DECISION_EMPTY
                best_param = g_indices[w, h] if g_indices[w, h] >= 0 else 0
                
                # Try vertical cuts (exploit symmetry: only z <= w/2)
                half_w = w >> 1
                for z in patterns.cuts_pure_x(w):
                    if z > half_w:
                        break
                    total = F_values[z, h] + F_values[w - z, h]
                    if total > best_val:
                        best_val = total
                        best_type = DECISION_CUT_X
                        best_param = z
                
                # Try horizontal cuts (exploit symmetry: only z <= h/2)
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
        
        self.F_values = F_values
        self.F_type = F_type
        self.F_param = F_param
    
    def F(self, w, h):
        """Get precomputed pure rectangle value. O(1)."""
        if w <= 0 or h <= 0:
            return 0
        return int(self.F_values[w, h])
    
    def F_d(self, x, y, w, h):
        """Compute optimal value for potentially defected rectangle.
        
        Uses recursion only for defected regions (sparse).
        Pure regions use precomputed F values directly.
        """
        if w <= 0 or h <= 0:
            return 0
        
        # Check cache
        key = (x, y, w, h)
        cached = self.cache_Fd.get(key)
        if cached is not None:
            return cached[0]
        
        # If pure, use precomputed value
        if self.geom.is_pure(x, y, w, h):
            val = self.F_values[w, h]
            self.cache_Fd[key] = (int(val), DECISION_PURE, 0)
            return int(val)
        
        # Defected: must cut around defects
        best_val = 0
        best_type = DECISION_DEFECT
        best_param = 0
        
        # Get cuts including defect boundaries
        X_cuts, Y_cuts = self.patterns.cuts_defected(x, y, w, h)
        
        # Try vertical cuts
        for z in X_cuts:
            total = self.F_d(x, y, z, h) + self.F_d(x + z, y, w - z, h)
            if total > best_val:
                best_val = total
                best_type = DECISION_CUT_X
                best_param = z
        
        # Try horizontal cuts  
        for z in Y_cuts:
            total = self.F_d(x, y, w, z) + self.F_d(x, y + z, w, h - z)
            if total > best_val:
                best_val = total
                best_type = DECISION_CUT_Y
                best_param = z
        
        self.cache_Fd[key] = (best_val, best_type, best_param)
        return best_val
    
    def solve(self):
        """Solve the cutting problem and return (value, sequence)."""
        value = self.F_d(0, 0, self.W0, self.H0)
        sequence = self._reconstruct_Fd(0, 0, self.W0, self.H0)
        return value, sequence
    
    def _reconstruct_F(self, w, h):
        """Reconstruct cutting sequence for pure rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'
        
        dec_type = self.F_type[w, h]
        dec_param = self.F_param[w, h]
        
        if dec_type == DECISION_EMPTY:
            return 'empty'
        
        if dec_type == DECISION_FILL:
            return f'g_{dec_param}'
        
        if dec_type == DECISION_CUT_X:
            z = int(dec_param)
            return ('X', z, self._reconstruct_F(z, h), self._reconstruct_F(w - z, h))
        
        if dec_type == DECISION_CUT_Y:
            z = int(dec_param)
            return ('Y', z, self._reconstruct_F(w, z), self._reconstruct_F(w, h - z))
        
        return 'empty'
    
    def _reconstruct_Fd(self, x, y, w, h):
        """Reconstruct cutting sequence for potentially defected rectangle."""
        if w <= 0 or h <= 0:
            return 'empty'
        
        cached = self.cache_Fd.get((x, y, w, h))
        if cached is None:
            # Should not happen if solve() was called
            return 'empty'
        
        _, dec_type, dec_param = cached
        
        if dec_type == DECISION_PURE:
            return self._reconstruct_F(w, h)
        
        if dec_type == DECISION_EMPTY:
            return 'empty'
        
        if dec_type == DECISION_DEFECT:
            return 'defect'
        
        if dec_type == DECISION_FILL:
            return f'g_{dec_param}'
        
        if dec_type == DECISION_CUT_X:
            z = int(dec_param)
            left = self._reconstruct_Fd(x, y, z, h)
            right = self._reconstruct_Fd(x + z, y, w - z, h)
            return ('X', z, left, right)
        
        if dec_type == DECISION_CUT_Y:
            z = int(dec_param)
            bot = self._reconstruct_Fd(x, y, w, z)
            top = self._reconstruct_Fd(x, y + z, w, h - z)
            return ('Y', z, bot, top)
        
        return 'empty'
