"""Pattern generation for guillotine cuts."""

import numpy as np


def compute_normal_patterns(item_sizes, max_L):
    """Compute reachable cut positions using DP.
    
    Maintained for backward compatibility with unit tests.
    """
    reachable = _compute_reachability_array(item_sizes, max_L)
    patterns = {}
    for L in range(max_L + 1):
        patterns[L] = [z for z in range(1, L) if reachable[z]]
    return patterns

def _compute_reachability_array(item_sizes, max_L):
    """Compute boolean reachability array for item size sums."""
    sizes = sorted(set(int(s) for s in item_sizes if s > 0))
    reachable = [False] * (max_L + 1)
    reachable[0] = True
    for size in sizes:
        for z in range(size, max_L + 1):
            if reachable[z - size]:
                reachable[z] = True
    return reachable

def compute_normal_patterns_arrays(patterns_dict, max_L):
    """Convert normal patterns dict to numpy arrays for C interop."""
    lengths = np.zeros(max_L + 1, dtype=np.int32)
    for L in range(max_L + 1):
        lengths[L] = len(patterns_dict.get(L, []))
    
    max_cuts = int(lengths.max()) if lengths.max() > 0 else 0
    arr = np.zeros((max_L + 1, max_cuts), dtype=np.int32)
    
    for L in range(max_L + 1):
        cuts = patterns_dict.get(L, [])
        for i, z in enumerate(cuts):
            arr[L, i] = z
    
    return arr, lengths

class CutPatternGenerator:
    """Generates candidate cut positions."""
    
    def __init__(self, item_sizes, geometry):
        """Initialize pattern generator."""
        self.geom = geometry
        self.W0, self.H0 = geometry.W0, geometry.H0
        
        # 1. Precompute normal patterns (dict for Python reconstruction)
        self.np_x = compute_normal_patterns(item_sizes[0], self.W0)
        self.np_y = compute_normal_patterns(item_sizes[1], self.H0)

        # 2. Precompute reachability (arrays for "Extended" pattern logic)
        self.reachable_x = _compute_reachability_array(item_sizes[0], self.W0)
        self.reachable_y = _compute_reachability_array(item_sizes[1], self.H0)

        # 3. Precompute arrays for C interop
        self.np_x_arr, self.np_x_len = compute_normal_patterns_arrays(self.np_x, self.W0)
        self.np_y_arr, self.np_y_len = compute_normal_patterns_arrays(self.np_y, self.H0)
        
        # Pre-allocate scratch buffers
        self._x_mask = [False] * (self.W0 + 1)
        self._y_mask = [False] * (self.H0 + 1)
        self._x_buf = [0] * (self.W0 + 1)
        self._y_buf = [0] * (self.H0 + 1)
    
    def cuts_pure_x(self, w):
        return self.np_x.get(w, [])
    
    def cuts_pure_y(self, h):
        return self.np_y.get(h, [])
    
    def _clear_masks(self, w, h):
        for i in range(1, w + 1):
            self._x_mask[i] = False
        for i in range(1, h + 1):
            self._y_mask[i] = False

    def _compact_masks(self, w, h):
        nx = 0
        for i in range(1, w):
            if self._x_mask[i]:
                self._x_buf[nx] = i
                nx += 1
        
        ny = 0
        for i in range(1, h):
            if self._y_mask[i]:
                self._y_buf[ny] = i
                ny += 1
        
        return self._x_buf[:nx], self._y_buf[:ny]
