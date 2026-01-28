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
