"""Geometry module for handling sheet defects."""

import numpy as np


class SheetGeometry:
    """Handles defects with O(1) purity queries using 2D prefix sums."""
    
    def __init__(self, sheet_sz, defect_sizes, defect_positions):
        """Initialize geometry with sheet size and defects."""
        self.W0, self.H0 = sheet_sz
        
        # Count defects
        n_def = len(defect_sizes[0]) if len(defect_sizes) > 0 and len(defect_sizes[0]) > 0 else 0
        self.n_def = n_def
