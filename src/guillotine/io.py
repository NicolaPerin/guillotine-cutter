"""I/O operations for guillotine cutting."""

import json

def save_problem_json(filepath, item_sizes, defect_sizes, defect_positions, sheet_size):
    """Save problem definition to JSON file."""
    problem = {
        "problem": {
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
    }
    
    with open(filepath, 'w') as f:
        json.dump(problem, f, indent=2)


def load_problem_json(filepath):
    """Load problem definition from JSON file."""
    with open(filepath, 'r') as f:
        data = json.load(f)
    
    problem = data["problem"]
    
    # Parse items
    items = problem["items"]
    item_sizes = [
        [item["width"] for item in items],
        [item["height"] for item in items]
    ]
    
    # Parse defects
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
    
    return {
        "item_sizes": item_sizes,
        "defect_sizes": defect_sizes,
        "defect_positions": defect_positions,
        "sheet_size": sheet_size
    }

def save_solution_json(filepath, value, sequence, sheet_size, defect_area):
    """Save solution to JSON file with all metrics."""
    W, H = sheet_size
    total_area = W * H
    usable_area = total_area - defect_area
    
    # Calculate metrics
    utilization_pct = (value / total_area) * 100
    defect_loss_pct = (defect_area / total_area) * 100
    efficiency_pct = (value / usable_area) * 100 if usable_area > 0 else 0
    
    solution = {
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
        json.dump(solution, f, indent=2)

def _serialize_sequence(seq):
    """Convert nested tuple sequence to JSON-serializable dict."""
    if isinstance(seq, str):
        # Terminal node: "empty", "defect", "g_0", etc.
        if seq.startswith("g_"):
            return {"type": "fill", "item_id": int(seq.split("_")[1])}
        return {"type": seq}
    
    # Recursive node: ("X"|"Y", position, left, right)
    direction, z, left, right = seq
    return {
        "type": direction,
        "position": int(z),
        "left": _serialize_sequence(left),
        "right": _serialize_sequence(right)
    }