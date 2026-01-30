"""Visualization for guillotine cutting solutions."""

import matplotlib
matplotlib.use('Agg')  # Non-interactive backend for saving files
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import matplotlib.colors as mcolors


class CuttingVisualizer:
    """Visualizes cutting patterns."""
    
    def __init__(self, item_sizes, defect_sizes, defect_positions, sheet_size):
        """Initialize visualizer."""
        self.item_sizes = item_sizes
        self.defect_sizes = defect_sizes
        self.defect_positions = defect_positions
        self.sheet_size = sheet_size
        self.xticks = []
        self.yticks = []
    
    def _get_colors(self):
        """Generate distinct colors for each item type."""
        # Use tableau colors for better distinction
        colors = list(mcolors.TABLEAU_COLORS.values())
        n_items = len(self.item_sizes[0])
        return colors[:n_items]
    
    def _fill_items(self, ax, w, h, item_w, item_h, color, offset=(0, 0)):
        """Fill rectangle with repeated items."""
        rows = h // item_h
        cols = w // item_w
        ox, oy = offset
        
        for i in range(rows):
            for j in range(cols):
                rect = patches.Rectangle(
                    (ox + j * item_w, oy + i * item_h),
                    item_w, item_h,
                    linewidth=0.8, edgecolor='k', facecolor=color
                )
                ax.add_patch(rect)
    
    def _draw_cuts(self, ax, sequence, w, h, colors, offset=(0, 0)):
        """Recursively draw cutting sequence."""
        ox, oy = offset
        
        # Base case: terminal node
        if isinstance(sequence, str):
            if sequence.startswith("g_"):
                item_idx = int(sequence.split("_")[1])
                item_w = self.item_sizes[0][item_idx]
                item_h = self.item_sizes[1][item_idx]
                self._fill_items(ax, w, h, item_w, item_h, colors[item_idx], offset)
            # else: "empty" or "defect" - do nothing
            return
        
        # Recursive case: cut
        direction, z, left, right = sequence
        
        if direction == 'X':
            # Vertical cut at position z
            ax.plot([ox + z, ox + z], [oy, oy + h], 'k', linewidth=1.5)
            self.xticks.append(ox + z)
            self._draw_cuts(ax, left, z, h, colors, (ox, oy))
            self._draw_cuts(ax, right, w - z, h, colors, (ox + z, oy))
        else:  # 'Y'
            # Horizontal cut at position z
            ax.plot([ox, ox + w], [oy + z, oy + z], 'k', linewidth=1.5)
            self.yticks.append(oy + z)
            self._draw_cuts(ax, left, w, z, colors, (ox, oy))
            self._draw_cuts(ax, right, w, h - z, colors, (ox, oy + z))
    
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
        
        # Draw cutting sequence
        colors = self._get_colors()
        self.xticks.clear()
        self.yticks.clear()
        self._draw_cuts(ax, sequence, W0, H0, colors, (0, 0))
        
        # Add tick marks at cut positions
        all_xticks = [0] + sorted(set(self.xticks)) + [W0]
        all_yticks = [0] + sorted(set(self.yticks)) + [H0]
        ax.set_xticks(all_xticks)
        ax.set_yticks(all_yticks)
        
        # Save
        plt.savefig(output_file, dpi=150, bbox_inches='tight')
        plt.close()