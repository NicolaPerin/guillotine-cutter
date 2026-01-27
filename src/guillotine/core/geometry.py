"""Geometry module for handling sheet defects."""

import numpy as np


class SheetGeometry:
    """Handles defects with O(1) purity queries using 2D prefix sums."""
    
    def __init__(self, sheet_sz, defect_sizes, defect_positions):
        """Initialize geometry with sheet size and defects."""
        self.W0, self.H0 = sheet_sz
        
        # Store defects as list of tuples: (x, y, w, h, x_end, y_end)
        self.defects = []
        n_def = len(defect_sizes[0]) if len(defect_sizes) > 0 and len(defect_sizes[0]) > 0 else 0
        
        for i in range(n_def):
            dx = int(defect_positions[0][i])
            dy = int(defect_positions[1][i])
            dw = int(defect_sizes[0][i])
            dh = int(defect_sizes[1][i])
            self.defects.append((dx, dy, dw, dh, dx + dw, dy + dh))
        
        self.n_def = n_def
        self._build_prefix()
    
    def _build_prefix(self):
        """Build 2D prefix sum array for fast defect overlap queries."""
        W0, H0 = self.W0, self.H0
        
        if self.n_def == 0:
            self.prefix = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
            return
        
        # Build defect grid
        grid = np.zeros((W0, H0), dtype=np.int32)
        
        for (dx, dy, dw, dh, dx_end, dy_end) in self.defects:
            x1 = max(dx, 0)
            y1 = max(dy, 0)
            x2 = min(dx_end, W0)
            y2 = min(dy_end, H0)
            
            if x1 < x2 and y1 < y2:
                grid[x1:x2, y1:y2] = 1
        
        # Build prefix sum
        self.prefix = np.zeros((W0 + 1, H0 + 1), dtype=np.int32)
        self.prefix[1:, 1:] = np.cumsum(np.cumsum(grid, axis=0), axis=1)
    
    def is_pure(self, x, y, w, h):
        """Check if rectangle is free of defects (O(1) operation)."""
        if w <= 0 or h <= 0 or self.n_def == 0:
            return True
        
        x0 = max(x, 0)
        y0 = max(y, 0)
        x1 = min(x + w, self.W0)
        y1 = min(y + h, self.H0)
        
        if x0 >= x1 or y0 >= y1:
            return True
        
        # 2D range sum query using prefix array
        defect_count = (self.prefix[x1, y1] - 
                       self.prefix[x0, y1] - 
                       self.prefix[x1, y0] + 
                       self.prefix[x0, y0])
        
        return defect_count == 0