"""Tests for I/O module."""

import pytest
import json
from guillotine.io import save_problem_json, validate_problem


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

def test_save_solution_with_simple_sequences(tmp_path):
    """Test serializing simple string sequences like 'empty' and 'defect'."""
    from guillotine.io import save_solution_json
    
    filepath = tmp_path / "solution_empty.json"
    
    # Test with "empty" sequence
    save_solution_json(str(filepath), 0, "empty", (10, 10), 0)
    
    with open(filepath) as f:
        data = json.load(f)
    
    assert data["solution"]["cut_sequence"] == {"type": "empty"}
    
    # Test with "defect" sequence
    filepath2 = tmp_path / "solution_defect.json"
    save_solution_json(str(filepath2), 0, "defect", (10, 10), 100)
    
    with open(filepath2) as f:
        data = json.load(f)
    
    assert data["solution"]["cut_sequence"] == {"type": "defect"}

def test_load_problem_json_no_defects(tmp_path):
    """Test loading problem from JSON when no defects present."""
    from guillotine.io import save_problem_json, load_problem_json
    
    filepath = tmp_path / "problem_no_defects.json"
    
    # Save problem with no defects
    item_sizes = [[5, 10], [5, 10]]
    defect_sizes = [[], []]
    defect_positions = [[], []]
    sheet_size = (20, 20)
    
    save_problem_json(
        str(filepath),
        item_sizes,
        defect_sizes,
        defect_positions,
        sheet_size
    )
    
    # Load it back
    loaded = load_problem_json(str(filepath))
    
    # Should have empty defects
    assert loaded["defect_sizes"] == [[], []]
    assert loaded["defect_positions"] == [[], []]


VALID = {
    "item_sizes": [[5, 10], [5, 10]],
    "defect_sizes": [[2], [2]],
    "defect_positions": [[9], [9]],
    "sheet_size": (27, 27),
}

def valid(**overrides):
    return {**VALID, **overrides}


# --- sheet ---
@pytest.mark.parametrize("sheet_size", [(0, 27), (27, 0), (-1, 27), (0, 0)])
def test_invalid_sheet(sheet_size):
    with pytest.raises(ValueError, match="Sheet"):
        validate_problem(**valid(sheet_size=sheet_size))


# --- items ---
def test_no_items():
    with pytest.raises(ValueError, match="least one item"):
        validate_problem(**valid(item_sizes=[[], []]))

@pytest.mark.parametrize("item_sizes", [
    [[0, 10], [5, 10]],   # zero width
    [[5, 10], [0, 10]],   # zero height
    [[-1, 10], [5, 10]],  # negative
])
def test_item_nonpositive_dimensions(item_sizes):
    with pytest.raises(ValueError, match="Item"):
        validate_problem(**valid(item_sizes=item_sizes))

def test_item_larger_than_sheet():
    with pytest.raises(ValueError, match="larger than the sheet"):
        validate_problem(**valid(item_sizes=[[30], [5]], sheet_size=(27, 27)))


# --- defects ---
@pytest.mark.parametrize("defect_sizes", [
    [[0], [2]],   # zero width
    [[2], [0]],   # zero height
])
def test_defect_nonpositive_dimensions(defect_sizes):
    with pytest.raises(ValueError, match="Defect"):
        validate_problem(**valid(defect_sizes=defect_sizes))

@pytest.mark.parametrize("defect_positions", [
    [[-1], [9]],  # negative x
    [[9], [-1]],  # negative y
])
def test_defect_negative_position(defect_positions):
    with pytest.raises(ValueError, match="Defect"):
        validate_problem(**valid(defect_positions=defect_positions))

def test_defect_outside_sheet():
    with pytest.raises(ValueError, match="outside the sheet"):
        validate_problem(**valid(defect_positions=[[26], [26]], defect_sizes=[[5], [5]]))


# --- valid inputs pass ---
def test_valid_problem_passes():
    validate_problem(**VALID)

def test_valid_no_defects_passes():
    validate_problem(
        item_sizes=[[5], [5]],
        defect_sizes=[[], []],
        defect_positions=[[], []],
        sheet_size=(27, 27),
    )