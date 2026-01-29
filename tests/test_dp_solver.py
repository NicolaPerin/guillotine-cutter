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

def test_dp_solver_with_defect():
    """Test DP solver with defect present."""
    # Sheet 10x10 with defect at (5,5) size 2x2
    # Item 3x3
    item_sizes = [[3], [3]]
    geom = SheetGeometry((10, 10), [[2], [2]], [[5], [5]])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # With defect, can't achieve 100 (perfect)
    # Should get something less than 100
    assert value < 100
    assert value > 0  # But should get something
    
    # Sequence should involve cuts (not just "defect")
    assert sequence != "defect"

def test_dp_solver_defect_covers_all():
    """Test when defect covers entire sheet."""
    # Sheet 10x10 with defect covering all
    item_sizes = [[3], [3]]
    geom = SheetGeometry((10, 10), [[10], [10]], [[0], [0]])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # No usable area
    assert value == 0
    assert sequence == "defect"

def test_dp_solver_multiple_defects():
    """Test with multiple defects."""
    # Sheet 20x20 with two defects
    item_sizes = [[3], [3]]
    geom = SheetGeometry(
        (20, 20),
        [[2, 2], [2, 2]],
        [[5, 15], [5, 15]]
    )
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # Should get something but less than perfect (400)
    assert 0 < value < 400

def test_dp_solver_zero_dimensions():
    """Test with zero or negative dimensions via F and F_d methods."""
    item_sizes = [[3], [3]]
    geom = SheetGeometry((10, 10), [[], []], [[], []])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    
    # Test F (pure rectangle) with zero dimensions
    assert dp.F(0, 5) == 0
    assert dp.F(5, 0) == 0
    assert dp.F(0, 0) == 0
    
    # Test F with negative dimensions
    assert dp.F(-5, 5) == 0
    assert dp.F(5, -5) == 0
    
    # Test F_d (defected rectangle) with zero dimensions
    assert dp.F_d(0, 0, 0, 5) == 0
    assert dp.F_d(0, 0, 5, 0) == 0
    
    # Test F_d with negative dimensions
    assert dp.F_d(0, 0, -5, 5) == 0
    assert dp.F_d(0, 0, 5, -5) == 0

def test_dp_solver_paper_benchmark():
    """Test benchmark case from original paper.
    
    Square sheet 27x27
    Items: 5x5, 10x10, 12x12, 15x15
    Defect: 2x2 at (9,9)
    Expected optimal: 644 out of 729
    """
    item_sizes = [[5, 10, 12, 15], [5, 10, 12, 15]]
    geom = SheetGeometry((27, 27), [[2], [2]], [[9], [9]])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # Should achieve optimal value from paper
    assert value == 644
    
    # Should not be trivial
    assert sequence != "empty"
    assert sequence != "defect"
    
    # Total sheet area
    assert 27 * 27 == 729
    
    # Utilization
    utilization = value / 729
    assert utilization > 0.88  # 644/729 ≈ 88.3%

def test_dp_solver_items_too_large():
    """Test when all items are larger than sheet."""
    # Sheet 5x5, but all items are 10x10 or larger
    item_sizes = [[10, 15], [10, 15]]
    geom = SheetGeometry((5, 5), [[], []], [[], []])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # Nothing fits
    assert value == 0
    assert sequence == "empty"

def test_dp_solver_perfect_single_item():
    """Test when single item fills sheet perfectly."""
    # Sheet 15x15, item 15x15 - perfect match
    item_sizes = [[15], [15]]
    geom = SheetGeometry((15, 15), [[], []], [[], []])
    patterns = CutPatternGenerator(item_sizes, geom)
    
    dp = GuillotineDP(item_sizes, geom, patterns)
    value, sequence = dp.solve()
    
    # Perfect utilization
    assert value == 225  # 15*15
    assert sequence == "g_0"  # Filled with item 0, no cuts