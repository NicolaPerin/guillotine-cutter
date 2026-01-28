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
