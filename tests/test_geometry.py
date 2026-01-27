"""Tests for SheetGeometry module."""

import pytest
from guillotine.core.geometry import SheetGeometry


def test_init_no_defects():
    """Test initialization with no defects."""
    geom = SheetGeometry(
        sheet_sz=(20, 20),
        defect_sizes=[[], []],
        defect_positions=[[], []]
    )
    
    assert geom.W0 == 20
    assert geom.H0 == 20
    assert geom.n_def == 0

def test_init_with_single_defect():
    """Test initialization with one defect."""
    geom = SheetGeometry(
        sheet_sz=(20, 20),
        defect_sizes=[[2], [2]],
        defect_positions=[[5], [5]]
    )
    
    assert geom.W0 == 20
    assert geom.H0 == 20
    assert geom.n_def == 1
    assert len(geom.defects) == 1
    
    # Check defect tuple: (x, y, w, h, x_end, y_end)
    dx, dy, dw, dh, dx_end, dy_end = geom.defects[0]
    assert dx == 5
    assert dy == 5
    assert dw == 2
    assert dh == 2
    assert dx_end == 7
    assert dy_end == 7

def test_is_pure_no_defects():
    """With no defects, all rectangles should be pure."""
    geom = SheetGeometry(
        sheet_sz=(20, 20),
        defect_sizes=[[], []],
        defect_positions=[[], []]
    )
    
    assert geom.is_pure(0, 0, 10, 10) is True
    assert geom.is_pure(5, 5, 5, 5) is True
    assert geom.is_pure(0, 0, 20, 20) is True

def test_is_pure_detects_overlap():
    """Rectangle overlapping defect should NOT be pure."""
    geom = SheetGeometry(
        sheet_sz=(20, 20),
        defect_sizes=[[2], [2]],
        defect_positions=[[10], [10]]  # Defect at (10,10) size 2x2
    )
    
    # Exact defect location
    assert geom.is_pure(10, 10, 2, 2) is False
    
    # Overlapping defect
    assert geom.is_pure(9, 9, 3, 3) is False

def test_is_pure_clear_area():
    """Areas without defects should be pure."""
    geom = SheetGeometry(
        sheet_sz=(20, 20),
        defect_sizes=[[2], [2]],
        defect_positions=[[10], [10]]  # Defect at (10,10)
    )
    
    # Before defect - should be pure
    assert geom.is_pure(0, 0, 5, 5) is True
    
    # After defect - should be pure  
    assert geom.is_pure(15, 15, 5, 5) is True

def test_is_pure_zero_dimensions():
    """Rectangles with zero or negative dimensions should be pure."""
    geom = SheetGeometry(
        sheet_sz=(20, 20),
        defect_sizes=[[2], [2]],
        defect_positions=[[10], [10]]
    )
    
    # Zero dimensions
    assert geom.is_pure(5, 5, 0, 10) is True
    assert geom.is_pure(5, 5, 10, 0) is True
    assert geom.is_pure(5, 5, 0, 0) is True
    
    # Negative dimensions
    assert geom.is_pure(5, 5, -5, 10) is True
    assert geom.is_pure(5, 5, 10, -5) is True

def test_multiple_defects():
    """Test with multiple defects."""
    geom = SheetGeometry(
        sheet_sz=(30, 30),
        defect_sizes=[[2, 2, 3], [2, 2, 1]],
        defect_positions=[[5, 12, 18], [5, 7, 10]]
    )
    
    assert geom.n_def == 3
    assert len(geom.defects) == 3
    
    # Between defects should be pure
    assert geom.is_pure(10, 10, 2, 2) is True
    
    # Overlapping first defect should not be pure
    assert geom.is_pure(4, 4, 3, 3) is False
    
    # Overlapping second defect should not be pure
    assert geom.is_pure(11, 6, 3, 3) is False