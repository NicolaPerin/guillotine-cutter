"""Tests for pattern generation module."""

import pytest
from guillotine.core.patterns import compute_normal_patterns


def test_compute_patterns_single_size():
    """With single item size, only multiples should be reachable."""
    patterns = compute_normal_patterns([4], 20)
    
    # Length 0 should have no cuts
    assert len(patterns[0]) == 0
    
    # Position 4 should be reachable at length 8 (cut at 4)
    assert 4 in patterns[8]
    
    # Positions 4 and 8 should be reachable at length 12
    assert 4 in patterns[12]
    assert 8 in patterns[12]


def test_compute_patterns_two_sizes():
    """With two sizes, combinations should be reachable."""
    patterns = compute_normal_patterns([3, 5], 20)
    
    # Size 3 positions
    assert 3 in patterns[6]
    assert 3 in patterns[9]
    
    # Size 5 positions
    assert 5 in patterns[10]
    
    # Combination: 3+5=8
    assert 3 in patterns[8]
    assert 5 in patterns[8]
    
    # Length 1 or 2 not reachable (too small for any item)
    assert len(patterns[1]) == 0
    assert len(patterns[2]) == 0

def test_compute_patterns_empty_items():
    """With no items, no positions should be reachable."""
    patterns = compute_normal_patterns([], 10)
    
    # All lengths should have no cuts
    assert len(patterns[5]) == 0
    assert len(patterns[10]) == 0

def test_pattern_generator_init():
    """Test CutPatternGenerator initialization."""
    from guillotine.core.geometry import SheetGeometry
    from guillotine.core.patterns import CutPatternGenerator
    
    geom = SheetGeometry((20, 20), [[], []], [[], []])
    gen = CutPatternGenerator([[3, 4], [3, 4]], geom)
    
    assert gen.W0 == 20
    assert gen.H0 == 20
    assert gen.geom == geom