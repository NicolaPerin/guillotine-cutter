"""Visualization for guillotine cutting solutions."""


class CuttingVisualizer:
    """Visualizes cutting patterns."""
    
    def __init__(self, item_sizes, defect_sizes, defect_positions, sheet_size):
        """Initialize visualizer."""
        self.item_sizes = item_sizes
        self.defect_sizes = defect_sizes
        self.defect_positions = defect_positions
        self.sheet_size = sheet_size
