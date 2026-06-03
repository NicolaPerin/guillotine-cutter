# gdcut
[![CI](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml/badge.svg)](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/NicolaPerin/guillotine-cutter/branch/main/graph/badge.svg)](https://codecov.io/gh/NicolaPerin/guillotine-cutter)
![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)
![Language: C++17](https://img.shields.io/badge/language-C%2B%2B17-blue.svg)

Freely inspired by:
H. Zhang, S. Yao, Q. Liu, J. Leng, and L. Wei,
"Exact approaches for the unconstrained two-dimensional cutting problem with defects,"
*Computers & Operations Research*, vol. 160, 106407, 2023.
[https://doi.org/10.1016/j.cor.2023.106407](https://doi.org/10.1016/j.cor.2023.106407)

Exact solver for the unconstrained 2D guillotine cutting-stock problem with defects.
Maximizes the total area of items placed on a rectangular sheet subject to guillotine
cuts and rectangular defect regions.

> **Work in progress.** The solver is functional and tested but the codebase is under active development.

## Build

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j$(nproc)
```

Requires a C++17 compiler and OpenMP.

## Input format

```json
{
  "problem": {
    "sheet_size": [100, 100],
    "items": [
      {"width": 10, "height": 10},
      {"width": 15, "height": 20}
    ],
    "defects": [
      {"x": 30, "y": 30, "width": 5, "height": 5}
    ]
  }
}
```

## Backends

gdcut selects a solver backend automatically based on raster point density:

- **Iterative**: bottom-up DP with sparse delta encoding and OpenMP parallelism.
  Efficient on dense problems (many small items, many defects).
- **Recursive**: top-down memoized DP with extended raster points (DPR) per Zhang
  et al. Efficient on sparse problems (large sheet, few defects). Always
  single-threaded.

Auto-dispatch threshold is tunable via `--sparse-threshold` (default: 0.5).
Override with `--solver iterative|recursive|auto`.

## Usage

### Command line

```bash
./build/gdcut solve problem.json
./build/gdcut solve problem.json solution.json     # custom output path
./build/gdcut solve problem.json -t 8              # 8 OpenMP threads
./build/gdcut solve problem.json --solver recursive
./build/gdcut solve problem.json --sparse-threshold 0.6
```

### Web interface

```bash
./build/gdcut serve
```

Opens a local web interface at `http://localhost:8080`. The interface allows
interactive problem definition (sheet dimensions, items, defects), solver
selection, and step-by-step visualization of the cut sequence.

> **Work in progress.** The visualizer is functional but still under active development.

Options:

```bash
./build/gdcut serve --port 9090
```

## Tests

```bash
cmake --build build -j$(nproc)
./build/tests/test_all
```

## License

MIT — see [LICENSE](LICENSE) for details.