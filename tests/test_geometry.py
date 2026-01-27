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
