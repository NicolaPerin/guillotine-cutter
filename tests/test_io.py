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

def test_load_problem_json(tmp_path):
    """Test loading problem from JSON file."""
    from guillotine.io import save_problem_json, load_problem_json
    
    # First save a problem
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
    
    # Now load it back
    loaded = load_problem_json(str(filepath))
    
    # Should match original
    assert loaded["item_sizes"] == item_sizes
    assert loaded["defect_sizes"] == defect_sizes
    assert loaded["defect_positions"] == defect_positions
    assert loaded["sheet_size"] == sheet_size

def test_save_solution_json(tmp_path):
    """Test saving solution to JSON file."""
    from guillotine.io import save_solution_json
    
    filepath = tmp_path / "solution.json"
    
    value = 644
    sequence = ("X", 5, "g_0", "g_1")
    sheet_size = (27, 27)
    defect_area = 4
    
    save_solution_json(
        str(filepath),
        value,
        sequence,
        sheet_size,
        defect_area
    )
    
    # File should exist
    assert filepath.exists()
    
    # Load and check structure
    with open(filepath) as f:
        data = json.load(f)
    
    assert "solution" in data
    sol = data["solution"]
    
    # Check all required fields
    assert sol["cut_area"] == 644
    assert sol["total_area"] == 729
    assert sol["defect_area"] == 4
    assert "utilization" in sol
    assert "efficiency" in sol
    assert "cut_sequence" in sol

def test_load_items_csv(tmp_path):
    """Test loading items from CSV file."""
    from guillotine.io import load_items_csv
    
    # Create CSV file
    csv_file = tmp_path / "items.csv"
    csv_file.write_text("width,height\n5,5\n10,10\n12,12\n")
    
    # Load it
    item_sizes = load_items_csv(str(csv_file))
    
    # Should match
    assert item_sizes == [[5, 10, 12], [5, 10, 12]]

def test_load_defects_csv(tmp_path):
    """Test loading defects from CSV file."""
    from guillotine.io import load_defects_csv
    
    # Create CSV file
    csv_file = tmp_path / "defects.csv"
    csv_file.write_text("x,y,width,height\n9,9,2,2\n15,15,3,3\n")
    
    # Load it
    defect_sizes, defect_positions = load_defects_csv(str(csv_file))
    
    # Should match
    assert defect_sizes == [[2, 3], [2, 3]]
    assert defect_positions == [[9, 15], [9, 15]]