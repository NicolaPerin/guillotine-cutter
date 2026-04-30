"""Visualization for guillotine cutting solutions."""

import os
import math
import colorsys

import matplotlib
matplotlib.use('Agg')  # Non-interactive backend for saving files
import matplotlib.pyplot as plt
import matplotlib.patches as patches


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


def _is_leaf(node):
    """Check if a cutting sequence node is a leaf (no further cuts).

    Leaves are either:
      - A string like "g_3" (item fill) or "empty"
      - None or any non-tuple type
    """
    return not isinstance(node, tuple)


def _select_ticks(all_positions, sheet_dim, max_ticks=20):
    """Select up to max_ticks cut positions with guaranteed minimum spacing.

    Strategy:
      1. Always include 0 as the first tick.
      2. Walk through the sorted cut coordinates; accept a tick only if it
         is at least `min_gap` units away from the last accepted tick.
         min_gap = sheet_dim / max_ticks.
      3. Always include sheet_dim as the final tick (even if it's close to
         the previous one — the endpoint is too important to skip).

    This prevents label overlap regardless of how the cuts cluster.

    Returns a sorted list of tick positions (ints).
    """
    positions = sorted(set(all_positions))

    if len(positions) == 0:
        return [0, sheet_dim]

    min_gap = sheet_dim / max_ticks

    # Always start with 0
    selected = [0]
    last_accepted = 0

    for pos in positions:
        if pos <= 0 or pos >= sheet_dim:
            continue
        if pos - last_accepted >= min_gap:
            selected.append(pos)
            last_accepted = pos

    # Always end with sheet_dim
    selected.append(sheet_dim)

    return selected


class CuttingVisualizer:
    """Visualizes cutting patterns."""

    def __init__(self, item_sizes, defect_sizes, defect_positions, sheet_size, n_items_orig=None):
        self.item_sizes = item_sizes
        self.defect_sizes = defect_sizes
        self.defect_positions = defect_positions
        self.sheet_size = sheet_size
        self.n_items_orig = n_items_orig if n_items_orig is not None else len(item_sizes[0])
        self.xticks = []
        self.yticks = []

    def _get_colors(self):
        n_items = self.n_items_orig
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

    def _draw_cuts(self, ax, sequence, w, h, colors, lw, offset=(0, 0)):
        """Recursively draw cutting sequence.

        Cut lines use exactly two thickness levels:
          - Structural cuts (at least one child has further cuts): thick
          - Terminal cuts (both children are leaves / item fills): thin

        This makes it visually clear which cuts subdivide the sheet
        versus which cuts separate final item placements.
        """
        ox, oy = offset

        if isinstance(sequence, str):
            if sequence.startswith("g_"):
                item_idx = int(sequence.split("_")[1])
                color_idx = item_idx % self.n_items_orig
                item_w = self.item_sizes[0][item_idx]
                item_h = self.item_sizes[1][item_idx]
                self._fill_items(ax, w, h, item_w, item_h, colors[color_idx], lw, offset)
            return

        direction, z, left, right = sequence

        # Two-level thickness: thin if both children are leaves, thick otherwise
        is_terminal = _is_leaf(left) and _is_leaf(right)
        cut_lw = lw * 0.4 if is_terminal else lw

        if direction == 'X':
            ax.plot([ox + z, ox + z], [oy, oy + h], color='#111111', linewidth=cut_lw)
            self.xticks.append(ox + z)
            self._draw_cuts(ax, left,  z,     h, colors, lw, (ox,     oy))
            self._draw_cuts(ax, right, w - z, h, colors, lw, (ox + z, oy))
        else:
            ax.plot([ox, ox + w], [oy + z, oy + z], color='#111111', linewidth=cut_lw)
            self.yticks.append(oy + z)
            self._draw_cuts(ax, left,  w, z,     colors, lw, (ox, oy))
            self._draw_cuts(ax, right, w, h - z, colors, lw, (ox, oy + z))

    def plot(self, sequence, output_file):
        """Plot cutting pattern and save as both SVG and PNG."""
        W0, H0 = self.sheet_size

        FIG_W = 12  # inches
        fig_h = FIG_W * H0 / W0
        fig, ax = plt.subplots(figsize=(FIG_W, fig_h))
        ax.set_xlim(0, W0)
        ax.set_ylim(0, H0)
        ax.set_aspect('equal')

        # Line width scaling via power law.
        #
        # ppu (points per unit) measures how many typographic points one
        # sheet-unit occupies on screen. We use the geometric mean of the
        # figure's width and height (in inches) to handle rectangular sheets
        # fairly — neither axis dominates.
        #
        # A power law base_lw = 0.40 * ppu^0.75 was calibrated against:
        #   40×40   → ~4.0 pt   (small sheet, thick lines)
        #   100×100 → ~2.0 pt   (reference)
        #   150×150 → ~1.5 pt
        #   300×150 → ~0.7 pt
        #   300×300 → ~0.9 pt
        #   450×200 → ~0.5 pt   (large sheet, thin lines)
        #
        # The exponent 0.75 makes lines thin out faster than linear (1.0)
        # but slower than sqrt (0.5), which matches visual expectations:
        # larger sheets have more cuts, so lines must shrink to avoid clutter.
        geo_fig_dim = math.sqrt(FIG_W * fig_h)
        ppu = geo_fig_dim * 72 / max(W0, H0)
        base_lw = max(0.40 * ppu ** 0.75, 0.2)

        # Font size: scale with the shorter figure dimension so labels
        # never look oversized on rectangular sheets. Clamp to [7, 12].
        min_fig_dim = min(FIG_W, fig_h)
        tick_font = max(7, min(12, min_fig_dim * 1.5))

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
        self._draw_cuts(ax, sequence, W0, H0, colors, base_lw)

        # Select ticks with guaranteed minimum spacing to prevent overlap
        selected_xticks = _select_ticks(self.xticks, W0, max_ticks=20)
        selected_yticks = _select_ticks(self.yticks, H0, max_ticks=20)

        ax.set_xticks(selected_xticks)
        ax.set_yticks(selected_yticks)
        ax.set_xticklabels([str(t) for t in selected_xticks], fontsize=tick_font)
        ax.set_yticklabels([str(t) for t in selected_yticks], fontsize=tick_font)
        ax.tick_params(axis='both', which='major', length=3, width=0.5)

        plt.tight_layout(pad=0.3)

        # Always write both SVG (vector) and PNG (raster)
        base, _ = os.path.splitext(output_file)
        fig.savefig(base + '.svg', format='svg', bbox_inches='tight')
        fig.savefig(base + '.png', dpi=150, bbox_inches='tight')
        plt.close()