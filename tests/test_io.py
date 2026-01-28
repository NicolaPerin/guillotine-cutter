"""Tests for I/O module."""

import pytest
import json
import os
from guillotine.io import save_problem_json


def test_save_problem_json(tmp_path):
    """Test saving problem to JSON file."""
    # tmp_path is pytest fixture for temporary directory
    filepath = tmp_path / "problem.json"
    
    item_sizes = [[5, 10], [5, 10]]
    defect_sizes = [[2], [2]]
    defect_positions = [[9], [9]]
    sheet_size = (27, 27)
    
    save_problem_json(
        str(filepath),
        item_sizes,
        defect_sizes,
        defect_positions,
        sheet_size
    )
    
    # File should exist
    assert filepath.exists()
    
    # Should be valid JSON
    with open(filepath) as f:
        data = json.load(f)
    
    # Should have problem key
    assert "problem" in data
    assert data["problem"]["sheet_size"] == [27, 27]
    assert len(data["problem"]["items"]) == 2
