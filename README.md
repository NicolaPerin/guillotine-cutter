# Guillotine Cutter

[![CI](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml/badge.svg)](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/NicolaPerin/guillotine-cutter/branch/develop/graph/badge.svg)](https://codecov.io/gh/NicolaPerin/guillotine-cutter)

## Features

- Fast dynamic programming algorithm  
- Handles defects and irregular regions
- JSON and CSV input support
- Command-line interface

## Status

🚧 Work in progress - migrating from Jupyter notebook to production package.

## Installation

**Development version (from source):**
```bash
git clone https://github.com/NicolaPerin/guillotine-cutter.git
cd guillotine-cutter
pip install -e .
```

## Quick Start

### Python API
```python
from guillotine.core.geometry import SheetGeometry
from guillotine.core.patterns import CutPatternGenerator
from guillotine.core.dp_solver import GuillotineDP

# Define problem
item_sizes = [[5, 10, 12], [5, 10, 12]]
defect_sizes = [[2], [2]]
defect_positions = [[9], [9]]
sheet_size = (27, 27)

# Solve
geom = SheetGeometry(sheet_size, defect_sizes, defect_positions)
patterns = CutPatternGenerator(item_sizes, geom)
dp = GuillotineDP(item_sizes, geom, patterns)

value, sequence = dp.solve()
print(f"Optimal value: {value}")
```

### CLI Usage
```bash
# Run benchmark with visualization
python -m guillotine --benchmark --plot

# From JSON file with custom output
python -m guillotine problem.json --output solution.json --plot result.png

# With profiling
python -m guillotine --benchmark --profile profile.txt
```
### Input Formats

**JSON format** (`problem.json`):
```json
{
  "problem": {
    "sheet_size": [27, 27],
    "items": [
      {"id": 0, "width": 5, "height": 5},
      {"id": 1, "width": 10, "height": 10}
    ],
    "defects": [
      {"x": 9, "y": 9, "width": 2, "height": 2}
    ]
  }
}
```

**CSV format**:
```bash
# items.csv
width,height
5,5
10,10

# defects.csv
x,y,width,height
9,9,2,2
```

## Output

The solver generates:
- **JSON solution** with metrics (utilization, efficiency, cut sequence)
- **PNG visualization** (optional) showing the cutting pattern
- **Profile data** (optional) for performance analysis


## Development
```bash
# Install in development mode
pip install -e .

# Run all tests
pytest tests/ -v

# Run tests with coverage
pytest tests/ --cov=guillotine --cov-report=term
```

## Architecture
```
guillotine-cutter/
├── src/guillotine/
│   ├── core/             # Core algorithm
│   │   ├── geometry.py   # Defect handling with O(1) queries
│   │   ├── patterns.py   # Cut position generation
│   │   └── dp_solver.py  # Dynamic programming solver
│   ├── io.py             # JSON/CSV input/output
│   ├── visualize.py      # Matplotlib visualization
│   └── __main__.py       # CLI entry point
└── tests/                # Comprehensive test suite
```

## Citation

This project is inspired by the guillotine cutting approach for 2D cutting stock problems with defects described in:

H. Zhang, S. Yao, Q. Liu, J. Leng, and L. Wei,  
“Exact approaches for the unconstrained two-dimensional cutting problem with defects,”  
*Computers & Operations Research*, vol. 160, 106407, 2023.  
[https://doi.org/10.1016/j.cor.2023.106407](https://doi.org/10.1016/j.cor.2023.106407)

## License

MIT License - see [LICENSE](LICENSE) file for details.