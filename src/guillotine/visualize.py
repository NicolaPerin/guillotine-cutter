"""Visualization for guillotine cutting solutions."""

import os
import colorsys

import matplotlib
matplotlib.use('Agg')  # Non-interactive backend for saving files
import matplotlib.pyplot as plt
import matplotlib.patches as patches
import matplotlib.colors as mcolors


# Vibrant primary/secondary palette — ordered for maximum contrast between neighbours
_VIBRANT_PALETTE = [
    "#E63946",  # vivid red
    "#2196F3",  # vivid blue
    "#4CAF50",  # vivid green
    "#FF9800",  # vivid orange
    "#9C27B0",  # vivid purple
    "#00BCD4",  # vivid cyan
    "#FFEB3B",  # vivid yellow
    "#F06292",  # vivid pink
    "#009688",  # vivid teal
    "#FF5722",  # vivid deep-orange
    "#3F51B5",  # vivid indigo
    "#8BC34A",  # vivid lime-green
    "#795548",  # warm brown
    "#607D8B",  # blue-grey
    "#E91E63",  # hot pink
    "#00E676",  # neon green
    "#FF1744",  # bright red
    "#2979FF",  # bright blue
    "#FFEA00",  # pure yellow
    "#D500F9",  # neon purple
]


class CuttingVisualizer:
    """Visualizes cutting patterns."""

    def __init__(self, item_sizes, defect_sizes, defect_positions, sheet_size):
        self.item_sizes = item_sizes
        self.defect_sizes = defect_sizes
        self.defect_positions = defect_positions
        self.sheet_size = sheet_size
        self.xticks = []
        self.yticks = []

    def _get_colors(self):
        """Return one vibrant color per item type, cycling if needed."""
        n_items = len(self.item_sizes[0])
        if n_items <= len(_VIBRANT_PALETTE):
            return _VIBRANT_PALETTE[:n_items]
        extra = [
            "#{:02x}{:02x}{:02x}".format(
                int(r * 255), int(g * 255), int(b * 255)
            )
            for r, g, b in (
                colorsys.hsv_to_rgb(i / n_items, 0.85, 0.90)
                for i in range(n_items)
            )
        ]
        return (_VIBRANT_PALETTE + extra)[:n_items]

    def _fill_items(self, ax, w, h, item_w, item_h, color, lw, offset=(0, 0)):
        """Fill rectangle with repeated items."""
        rows = h // item_h
        cols = w // item_w
        ox, oy = offset
        for i in range(rows):
            for j in range(cols):
                rect = patches.Rectangle(
                    (ox + j * item_w, oy + i * item_h),
                    item_w, item_h,
                    linewidth=lw * 0.35,
                    edgecolor='#00000077',
                    facecolor=color,
                )
                ax.add_patch(rect)

    def _draw_cuts(self, ax, sequence, w, h, colors, lw, depth, offset=(0, 0)):
        """Recursively draw cutting sequence."""
        ox, oy = offset

        if isinstance(sequence, str):
            if sequence.startswith("g_"):
                item_idx = int(sequence.split("_")[1])
                item_w = self.item_sizes[0][item_idx]
                item_h = self.item_sizes[1][item_idx]
                self._fill_items(ax, w, h, item_w, item_h, colors[item_idx], lw, offset)
            return

        direction, z, left, right = sequence
        # Cut lines thin out at deeper recursion levels
        cut_lw = max(lw * (0.75 ** depth), 0.15)

        if direction == 'X':
            ax.plot([ox + z, ox + z], [oy, oy + h], color='#111111', linewidth=cut_lw)
            self.xticks.append(ox + z)
            self._draw_cuts(ax, left,  z,     h, colors, lw, depth + 1, (ox,     oy))
            self._draw_cuts(ax, right, w - z, h, colors, lw, depth + 1, (ox + z, oy))
        else:
            ax.plot([ox, ox + w], [oy + z, oy + z], color='#111111', linewidth=cut_lw)
            self.yticks.append(oy + z)
            self._draw_cuts(ax, left,  w, z,     colors, lw, depth + 1, (ox, oy))
            self._draw_cuts(ax, right, w, h - z, colors, lw, depth + 1, (ox, oy + z))

    def plot(self, sequence, output_file):
        """Plot cutting pattern and save as both SVG and PNG."""
        W0, H0 = self.sheet_size

        FIG_W = 12  # inches
        fig, ax = plt.subplots(figsize=(FIG_W, FIG_W * H0 / W0))
        ax.set_xlim(0, W0)
        ax.set_ylim(0, H0)
        ax.set_aspect('equal')

        # Scale line width so it looks consistent regardless of sheet dimensions
        pts_per_unit = FIG_W * 72 / W0
        base_lw = max(pts_per_unit * 0.55, 0.2)

        # Sheet background
        ax.add_patch(plt.Rectangle(
            (0, 0), W0, H0,
            linewidth=base_lw, edgecolor='#111111',
            facecolor='#DDDDDD', hatch='//'
        ))

        # Defects
        for i in range(len(self.defect_sizes[0])):
            ax.add_patch(plt.Rectangle(
                (self.defect_positions[0][i], self.defect_positions[1][i]),
                self.defect_sizes[0][i], self.defect_sizes[1][i],
                linewidth=0, facecolor='#111111',
            ))

        # Cuts + fills
        colors = self._get_colors()
        self.xticks.clear()
        self.yticks.clear()
        self._draw_cuts(ax, sequence, W0, H0, colors, base_lw, depth=0)

        # Ticks: marks at every cut, labels sparsely
        all_xticks = sorted(set([0] + self.xticks + [W0]))
        all_yticks = sorted(set([0] + self.yticks + [H0]))

        def _label_step(positions, target=12):
            span = positions[-1] - positions[0] if len(positions) > 1 else 1
            raw = max(1, span // target)
            for nice in [1, 2, 5, 10, 20, 25, 50, 100, 200]:
                if nice >= raw:
                    return nice
            return raw

        x_step = _label_step(all_xticks)
        y_step = _label_step(all_yticks)
        ax.set_xticks(all_xticks)
        ax.set_yticks(all_yticks)
        ax.set_xticklabels(
            [str(t) if t % x_step == 0 else '' for t in all_xticks], fontsize=7
        )
        ax.set_yticklabels(
            [str(t) if t % y_step == 0 else '' for t in all_yticks], fontsize=7
        )
        ax.tick_params(axis='both', which='major', length=3, width=0.5)

        plt.tight_layout(pad=0.3)

        # Always write both SVG (vector) and PNG (raster)
        base, _ = os.path.splitext(output_file)
        fig.savefig(base + '.svg', format='svg', bbox_inches='tight')
        fig.savefig(base + '.png', dpi=150, bbox_inches='tight')
        plt.close()