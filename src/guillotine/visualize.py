"""Visualization for guillotine cutting solutions."""

import matplotlib
matplotlib.use('Agg')  # Non-interactive backend for saving files
import matplotlib.pyplot as plt
import matplotlib.patches as patches


"""Visualization for guillotine cutting solutions."""

import matplotlib
matplotlib.use('Agg')  # Non-interactive backend for saving files
import matplotlib.pyplot as plt
import matplotlib.patches as patches


class CuttingVisualizer:
    """Visualizes cutting patterns."""
    
    def __init__(self, item_sizes, defect_sizes, defect_positions, sheet_size):
        """Initialize visualizer."""
        self.item_sizes = item_sizes
        self.defect_sizes = defect_sizes
        self.defect_positions = defect_positions
        self.sheet_size = sheet_size
    
    def plot(self, sequence, output_file):
        """Plot cutting pattern and save to file."""
        W0, H0 = self.sheet_size
        
        # Create figure
        fig, ax = plt.subplots(figsize=(8, 8 * H0 / W0))
        ax.set_xlim(-1, W0 + 1)
        ax.set_ylim(-1, H0 + 1)
        ax.set_aspect('equal')
        
        # Draw sheet background
        ax.add_patch(plt.Rectangle(
            (0, 0), W0, H0,
            linewidth=2, edgecolor='k', facecolor='lightgrey', hatch='//'
        ))
        
        # Draw defects (black rectangles)
        for i in range(len(self.defect_sizes[0])):
            ax.add_patch(plt.Rectangle(
                (self.defect_positions[0][i], self.defect_positions[1][i]),
                self.defect_sizes[0][i], self.defect_sizes[1][i],
                linewidth=1, edgecolor='k', facecolor='k'
            ))
        
        # TODO: Draw cutting sequence
        
        # Save
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        plt.close()