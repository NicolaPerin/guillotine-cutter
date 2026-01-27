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
