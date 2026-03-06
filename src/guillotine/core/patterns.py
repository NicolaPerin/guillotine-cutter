"""Pattern generation for guillotine cuts."""

import numpy as np


def compute_normal_patterns(item_sizes, max_L):
    """Compute reachable cut positions using DP."""
    sizes = sorted(set(int(s) for s in item_sizes if s > 0))
    reachable = [False] * (max_L + 1)
    reachable[0] = True
    
    for size in sizes:
        for z in range(size, max_L + 1):
            if reachable[z - size]:
                reachable[z] = True
    
    patterns = {}
    for L in range(max_L + 1):
        patterns[L] = [z for z in range(1, L) if reachable[z]]
    
    return patterns

def compute_normal_patterns_arrays(patterns_dict, max_L):
    """Convert normal patterns dict to numpy arrays for C interop.
    
    Returns:
        arr: 2D int32 array, shape (max_L+1, max_L), arr[w, i] = i-th cut for width w
        lengths: 1D int32 array, shape (max_L+1,), lengths[w] = number of cuts for width w
    """
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
        
        # Precompute normal patterns for both directions
        self.np_x = compute_normal_patterns(item_sizes[0], self.W0)
        self.np_y = compute_normal_patterns(item_sizes[1], self.H0)

        self.np_x_arr, self.np_x_len = compute_normal_patterns_arrays(self.np_x, self.W0)
        self.np_y_arr, self.np_y_len = compute_normal_patterns_arrays(self.np_y, self.H0)
        
        # Pre-allocate scratch buffers (reused to avoid allocations)
        self._x_mask = [False] * (self.W0 + 1)
        self._y_mask = [False] * (self.H0 + 1)
        self._x_buf = [0] * (self.W0 + 1)
        self._y_buf = [0] * (self.H0 + 1)
    
    def cuts_pure_x(self, w):
        """Get valid X cuts for pure rectangle of width w."""
        return self.np_x.get(w, [])
    
    def cuts_pure_y(self, h):
        """Get valid Y cuts for pure rectangle of height h."""
        return self.np_y.get(h, [])
    
    def _clear_masks(self, w, h):
        """Clear scratch masks for new computation."""
        for i in range(1, w):
            self._x_mask[i] = False
        for i in range(1, h):
            self._y_mask[i] = False

    def _add_normal_cuts(self, w, h):
        """Add normal pattern cuts to masks."""
        for z in self.np_x.get(w, []):
            self._x_mask[z] = True
        for z in self.np_y.get(h, []):
            self._y_mask[z] = True

    def _add_defect_cuts(self, x, y, w, h):
        """Add cuts at defect boundaries to masks.
        
        Geometric idea: To isolate a defect, we need to cut at its edges.
        
        Example:
            Rectangle (x=0, y=0, w=20, h=20) with defect at (10,10) size 5x5
            
            We add cuts at:
            - x=10 (left edge of defect)
            - x=15 (right edge of defect, 10+5)
            - y=10 (bottom edge)
            - y=15 (top edge)
            
            This isolates the defect so we can discard it and use clear pieces.
        """
        x_end = x + w
        y_end = y + h
        
        for (dx, dy, _, _, dx_end, dy_end) in self.geom.defects:
            # Check if defect overlaps this rectangle
            if x >= dx_end or y >= dy_end or dx >= x_end or dy >= y_end:
                continue
            
            # X direction: add cuts at left and right edges of defect
            if w > 1:
                left = dx - x      # Distance from rect origin to defect left edge
                right = dx_end - x  # Distance from rect origin to defect right edge
                
                if 0 < left < w:
                    self._x_mask[left] = True
                if 0 < right < w:
                    self._x_mask[right] = True
            
            # Y direction: add cuts at bottom and top edges of defect
            if h > 1:
                bot = dy - y        # Distance from rect origin to defect bottom edge
                top = dy_end - y    # Distance from rect origin to defect top edge
                
                if 0 < bot < h:
                    self._y_mask[bot] = True
                if 0 < top < h:
                    self._y_mask[top] = True

    def _compact_masks(self, w, h):
        """Convert boolean masks to compact lists.
        
        The masks (_x_mask, _y_mask) are sparse boolean arrays where True
        indicates a valid cut position. This method:
        
        1. Scans through the masks
        2. Collects positions where mask[i] == True
        3. Packs them densely into output buffers
        4. Returns sliced views (avoids copying)
        
        Example:
            _x_mask = [False, False, True, False, True, False, ...]
                                     ^             ^
                                    pos 2         pos 4
            
            Compacts to: _x_buf = [2, 4, ...]
            Returns: [2, 4]
        
        Why not just use list comprehension?
            [i for i in range(1, w) if self._x_mask[i]]
        
        Answer: Performance! This method is called millions of times during DP.
        Reusing preallocated buffers is much faster than creating new lists.
        """
        # X cuts
        nx = 0
        for i in range(1, w):
            if self._x_mask[i]:
                self._x_buf[nx] = i
                nx += 1
        
        # Y cuts
        ny = 0
        for i in range(1, h):
            if self._y_mask[i]:
                self._y_buf[ny] = i
                ny += 1
        
        return self._x_buf[:nx], self._y_buf[:ny]
    
    def cuts_defected(self, x, y, w, h):
        """Generate candidate cut positions for defected rectangle.
        
        This method:
        1. Starts with normal pattern cuts (like pure rectangles)
        2. Adds cuts at defect boundaries to isolate defects
        3. Uses boolean masks for efficient computation
        4. Returns two lists: (x_cuts, y_cuts)
        """
        if w <= 1 and h <= 1:
            return [], []
        
        self._clear_masks(w, h)
        self._add_normal_cuts(w, h)
        self._add_defect_cuts(x, y, w, h)
        return self._compact_masks(w, h)