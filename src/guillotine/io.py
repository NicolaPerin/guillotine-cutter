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
