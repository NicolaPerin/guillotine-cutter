"""Tests for DP solver module."""

import pytest
from guillotine.core.dp_solver import GuillotineDP
from guillotine.core.geometry import SheetGeometry
from guillotine.core.patterns import CutPatternGenerator


def test_dp_solver_init():
    """Test GuillotineDP initialization."""
    item_sizes = [[3, 4], [3, 4]]
    geom = SheetGeometry((20, 20), [[], []], [[], []])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    
    assert dp.W0 == 20
    assert dp.H0 == 20
    assert dp.geom == geom
    assert dp.patterns == patterns

def test_dp_solver_solve_returns_tuple():
    """Test that solve() returns (value, sequence) tuple."""
    item_sizes = [[3], [3]]
    geom = SheetGeometry((10, 10), [[], []], [[], []])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    result = dp.solve()
    
    # Should return tuple of (value, sequence)
    assert isinstance(result, tuple)
    assert len(result) == 2
    
    value, sequence = result
    assert isinstance(value, int)

def test_dp_solver_simple_no_defects():
    """Test DP solver with simple case, no defects."""
    # Single item 3x3, sheet 9x9 → should fit 9 items perfectly
    item_sizes = [[3], [3]]
    geom = SheetGeometry((9, 9), [[], []], [[], []])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # 9x9 sheet with 3x3 items → 9 items = 81 area
    assert value == 81
    
    # Sequence should not be just "empty"
    assert sequence != "empty"