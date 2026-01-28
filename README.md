# Guillotine Cutter

[![CI](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml/badge.svg)](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml)
[![Coverage](https://img.shields.io/badge/coverage-97%25-brightgreen)](https://github.com/NicolaPerin/guillotine-cutter)

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
```python
from guillotine import GuillotineCutter

# Create solver
solver = GuillotineCutter(
    item_sizes=[[3, 4, 5], [3, 4, 5]],
    defect_sizes=[[2], [2]],
    defect_positions=[[5], [5]],
    sheet_size=(30, 30)
)

# Solve
value, sequence = solver.solve()
print(f"Optimal value: {value}")
```

## CLI Usage
```bash
# Run benchmark
python -m guillotine --benchmark

# From JSON file  
python -m guillotine problem.json --output solution.json
```

## Development
```bash
# Run tests
pytest tests/ -v --cov
```

## CI/CD

- **GitHub Actions**: Automated testing on push
- **Jenkins**: Local CI with polling

## License

MIT