"""I/O operations for guillotine cutting."""

import json

def validate_problem(item_sizes, defect_sizes, defect_positions, sheet_size):
    """Validate problem inputs, raising ValueError with a clear message on failure."""
    W, H = sheet_size

    # Sheet
    if W <= 0 or H <= 0:
        raise ValueError(f"Sheet dimensions must be positive, got {W}x{H}")

    # Items
    if not item_sizes[0]:
        raise ValueError("At least one item must be provided")
    for i, (w, h) in enumerate(zip(item_sizes[0], item_sizes[1])):
        if w <= 0 or h <= 0:
            raise ValueError(f"Item {i} dimensions must be positive, got {w}x{h}")
        if w > W or h > H:
            raise ValueError(f"Item {i} ({w}x{h}) is larger than the sheet ({W}x{H})")

    # Defects
    for i, (x, y, w, h) in enumerate(zip(
        defect_positions[0], defect_positions[1],
        defect_sizes[0], defect_sizes[1]
    )):
        if w <= 0 or h <= 0:
            raise ValueError(f"Defect {i} dimensions must be positive, got {w}x{h}")
        if x < 0 or y < 0:
            raise ValueError(f"Defect {i} position must be non-negative, got ({x},{y})")
        if x + w > W or y + h > H:
            raise ValueError(f"Defect {i} at ({x},{y}) size {w}x{h} extends outside the sheet")


def save_problem_json(filepath, item_sizes, defect_sizes, defect_positions, sheet_size):
    """Save problem definition to JSON file."""
    problem = _build_problem_dict(item_sizes, defect_sizes, defect_positions, sheet_size)

    with open(filepath, 'w') as f:
        json.dump({"problem": problem}, f, indent=2)


def _build_problem_dict(item_sizes, defect_sizes, defect_positions, sheet_size):
    """Build the problem dict used in both problem and solution JSON files."""
    return {
        "sheet_size": list(sheet_size),
        "items": [
            {"id": i, "width": item_sizes[0][i], "height": item_sizes[1][i]}
            for i in range(len(item_sizes[0]))
        ],
        "defects": [
            {
                "x": defect_positions[0][i],
                "y": defect_positions[1][i],
                "width": defect_sizes[0][i],
                "height": defect_sizes[1][i]
            }
            for i in range(len(defect_sizes[0]))
        ]
    }


def _parse_problem_dict(problem):
    """Parse a problem dict back into the (item_sizes, defect_sizes,
    defect_positions, sheet_size) tuple format used throughout the solver."""
    items = problem["items"]
    item_sizes = [
        [item["width"] for item in items],
        [item["height"] for item in items]
    ]

    defects = problem.get("defects", [])
    if defects:
        defect_sizes = [
            [d["width"] for d in defects],
            [d["height"] for d in defects]
        ]
        defect_positions = [
            [d["x"] for d in defects],
            [d["y"] for d in defects]
        ]
    else:
        defect_sizes = [[], []]
        defect_positions = [[], []]

    sheet_size = tuple(problem["sheet_size"])

    return item_sizes, defect_sizes, defect_positions, sheet_size


def load_problem_json(filepath):
    """Load problem definition from JSON file."""
    with open(filepath, 'r') as f:
        data = json.load(f)

    item_sizes, defect_sizes, defect_positions, sheet_size = _parse_problem_dict(data["problem"])
    validate_problem(item_sizes, defect_sizes, defect_positions, sheet_size)

    return {
        "item_sizes": item_sizes,
        "defect_sizes": defect_sizes,
        "defect_positions": defect_positions,
        "sheet_size": sheet_size
    }


def save_solution_json(filepath, value, sequence,
                       item_sizes, defect_sizes, defect_positions, sheet_size):
    """Save solution to JSON file with all metrics and the full problem definition.

    The problem definition is embedded so that the solution file is
    self-contained — the plot command can reconstruct the visualization
    from this file alone without needing the original problem file.
    """
    W, H = sheet_size
    total_area = W * H
    defect_area = sum(
        defect_sizes[0][i] * defect_sizes[1][i]
        for i in range(len(defect_sizes[0]))
    )
    usable_area = total_area - defect_area

    utilization_pct = (value / total_area) * 100
    defect_loss_pct = (defect_area / total_area) * 100
    efficiency_pct = (value / usable_area) * 100 if usable_area > 0 else 0

    output = {
        "problem": _build_problem_dict(item_sizes, defect_sizes, defect_positions, sheet_size),
        "solution": {
            "cut_area": value,
            "total_area": total_area,
            "defect_area": defect_area,
            "usable_area": usable_area,
            "utilization": f"{value}/{total_area} ({utilization_pct:.1f}%)",
            "defect_loss": f"{defect_area}/{total_area} ({defect_loss_pct:.1f}%)",
            "efficiency": f"{value}/{usable_area} ({efficiency_pct:.1f}%)",
            "cut_sequence": _serialize_sequence(sequence)
        }
    }

    with open(filepath, 'w') as f:
        json.dump(output, f, indent=2)


def _serialize_sequence(seq):
    """Convert nested tuple sequence to JSON-serializable dict."""
    if isinstance(seq, str):
        if seq.startswith("g_"):
            return {"type": "fill", "item_id": int(seq.split("_")[1])}
        return {"type": seq}

    direction, z, left, right = seq
    return {
        "type": direction,
        "position": int(z),
        "left": _serialize_sequence(left),
        "right": _serialize_sequence(right)
    }


def _deserialize_sequence(data):
    """Convert JSON dict back to the nested tuple/string sequence format.

    Inverse of _serialize_sequence. Terminal nodes become strings
    ("empty", "defect", "g_0", etc.), recursive nodes become
    ("X"|"Y", position, left, right) tuples.
    """
    node_type = data["type"]

    if node_type == "fill":
        return f"g_{data['item_id']}"
    if node_type in ("empty", "defect"):
        return node_type

    return (
        node_type,
        data["position"],
        _deserialize_sequence(data["left"]),
        _deserialize_sequence(data["right"])
    )


def load_solution_json(filepath):
    """Load a solution JSON file.

    Returns a dict with:
        "item_sizes": [[widths], [heights]]
        "defect_sizes": [[widths], [heights]]
        "defect_positions": [[xs], [ys]]
        "sheet_size": (W, H)
        "cut_area": int
        "total_area": int
        "defect_area": int
        "sequence": nested tuple/string cut sequence
    """
    with open(filepath, 'r') as f:
        data = json.load(f)

    sol = data["solution"]
    item_sizes, defect_sizes, defect_positions, sheet_size = _parse_problem_dict(data["problem"])

    return {
        "item_sizes": item_sizes,
        "defect_sizes": defect_sizes,
        "defect_positions": defect_positions,
        "sheet_size": sheet_size,
        "cut_area": sol["cut_area"],
        "total_area": sol["total_area"],
        "defect_area": sol["defect_area"],
        "sequence": _deserialize_sequence(sol["cut_sequence"])
    }


def load_items_csv(filepath):
    """Load items from CSV file.

    Expected CSV format:
        width,height
        5,5
        10,10
    """
    import csv

    widths = []
    heights = []

    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            widths.append(int(row['width']))
            heights.append(int(row['height']))

    return [widths, heights]


def load_defects_csv(filepath):
    """Load defects from CSV file.

    Expected CSV format:
        x,y,width,height
        9,9,2,2
        15,15,3,3

    Returns:
        Tuple of (defect_sizes, defect_positions)
    """
    import csv

    widths = []
    heights = []
    x_positions = []
    y_positions = []

    with open(filepath, 'r') as f:
        reader = csv.DictReader(f)
        for row in reader:
            x_positions.append(int(row['x']))
            y_positions.append(int(row['y']))
            widths.append(int(row['width']))
            heights.append(int(row['height']))

    defect_sizes = [widths, heights]
    defect_positions = [x_positions, y_positions]

    return defect_sizes, defect_positions