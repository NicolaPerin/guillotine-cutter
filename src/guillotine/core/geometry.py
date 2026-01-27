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
