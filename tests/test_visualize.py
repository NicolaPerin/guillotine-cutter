"""Tests for visualization module."""

import pytest  # noqa: F401
import os


def test_visualizer_init():
    """Test CuttingVisualizer initialization."""
    from guillotine.visualize import CuttingVisualizer
    
    item_sizes = [[3, 4], [3, 4]]
    defect_sizes = [[2], [2]]
    defect_positions = [[5], [5]]
    sheet_size = (20, 20)
    
    viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
    
    assert viz.sheet_size == sheet_size
    assert viz.item_sizes == item_sizes
