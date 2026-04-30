# Guillotine Cutter
[![CI](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml/badge.svg)](https://github.com/NicolaPerin/guillotine-cutter/actions/workflows/ci.yml)
[![codecov](https://codecov.io/gh/NicolaPerin/guillotine-cutter/branch/develop/graph/badge.svg)](https://codecov.io/gh/NicolaPerin/guillotine-cutter)

Exact dynamic programming solver for the unconstrained 2D guillotine cutting-stock problem with defects.

## Features

- Exact DP algorithm guaranteeing optimal guillotine cuts
- Sparse slab storage with uint16 delta encoding — typically 10–12% of dense memory
- GPU acceleration via CUDA (auto-detected at build time), with transparent CPU fallback
- Hybrid GPU+CPU execution for problems exceeding VRAM
- OpenMP parallelization on CPU
- Item rotation support (`--allow-rotation`)
- JSON and CSV input
- CLI with `solve` and `plot` subcommands

## Installation

```bash
git clone https://github.com/NicolaPerin/guillotine-cutter.git
cd guillotine-cutter
python3 -m venv .venv
source .venv/bin/activate
pip install -e .[dev]
python setup.py build_ext --inplace
```

`setup.py` auto-detects `nvcc` at build time. On systems with CUDA, the GPU path is compiled and linked automatically. On CPU-only systems the build proceeds unchanged.

## Quick Start

### Python API

```python
from guillotine.core.geometry import SheetGeometry
from guillotine.core.patterns import CutPatternGenerator
from guillotine.core.dp_solver import GuillotineDP

item_sizes = [[5, 10, 12], [5, 10, 12]]
defect_sizes = [[2], [2]]
defect_positions = [[20], [20]]
sheet_size = (100, 100)

geom = SheetGeometry(sheet_size, defect_sizes, defect_positions)
patterns = CutPatternGenerator(item_sizes, geom)
dp = GuillotineDP(item_sizes, geom, patterns)
value, sequence = dp.solve()
print(f"Optimal value: {value}")
```

### CLI

```bash
# Solve from JSON
guillotine solve problem.json

# Solve inline
guillotine solve --sheet 100x100 --items 5x7 7x5 10x12 --defects 9,9,2x2

# Allow item rotation (free performance-wise)
guillotine solve problem.json --allow-rotation

# Preview memory estimates without solving
guillotine solve problem.json --dry-run

# Save with visualization
guillotine solve problem.json --plot result.png

# Plot from a saved solution (no re-solving)
guillotine plot output/solution.json
```

### Input format

```json
{
  "problem": {
    "sheet_size": [50, 50],
    "items": [
      {"id": 0, "width": 5, "height": 7},
      {"id": 1, "width": 10, "height": 12}
    ],
    "defects": [
      {"x": 9, "y": 9, "width": 2, "height": 2}
    ]
  }
}
```

Example solution for a 100×100 sheet with 10 item types and scattered defects (shown in black):

<img src="images/plot.svg" width="800" alt="100x100 solution">

## Performance

### CPU (Ryzen 5 9600X, 6 cores)

| Sheet | Time | Memory |
|---|---|---|
| 100×100 | 0.45s | 80 MB |
| 200×200 | 10.7s | 777 MB |
| 300×300 | 71.7s | 3.8 GB |
| 400×400 | 300s | 11.7 GB |

### GPU (RTX 5070, 12 GB VRAM)

| Sheet | Time | Notes |
|---|---|---|
| 100×100 | 0.20s | ~2.2× vs CPU |
| 200×200 | 1.85s | ~5.8× vs CPU |
| 300×300 | 11.8s | ~6× vs CPU |
| 400×400 | 144s | hybrid GPU+CPU (exceeds available VRAM) |

## Architecture

```
guillotine-cutter/
├── src/guillotine/
│   ├── core/
│   │   ├── geometry.py      # SheetGeometry: defect prefix sums, O(1) purity queries
│   │   ├── patterns.py      # CutPatternGenerator: normal pattern cut positions
│   │   ├── dp_solver.py     # GuillotineDP: three-phase DP solver and reconstruction
│   │   ├── solver_core.c    # C: tiling table, pure table, defect slab fill (CPU)
│   │   ├── solver_core.h    # Shared header: DefectSlab, ColRef, tile structures
│   │   ├── gpu_solver.cu    # CUDA: defect slab fill (GPU, diagonal wavefront)
│   │   ├── _solver.c        # Python C extension bindings
│   │   └── constants.py     # DECISION_* constants
│   ├── io.py                # JSON/CSV I/O, input validation
│   ├── visualize.py         # Matplotlib visualization
│   └── __main__.py          # CLI entry point
├── setup.py                 # Build: auto-detects CUDA, compiles GPU path if available
└── tests/
```

### Algorithm

The solver runs three phases:

**Phase 1 — tiling table:** For each rectangle size `(w, h)`, find the best value achievable by tiling with a single item type. O(W × H × n_items), parallelized with OpenMP.

**Phase 2 — pure table:** Bottom-up DP for defect-free rectangles. Tries single-item tiling and guillotine cuts at normal-pattern positions. O(W × H × (W + H)), sequential.

**Phase 3 — defect slab:** For each `(w, h)` that overlaps at least one defect, compute the optimal value at all affected sheet positions `(sx, sy)`. Evaluates all integer cut positions, guaranteeing optimality.

Most sheet positions are *pure* — no defect overlaps the placed rectangle — so their value equals `pure_values[w][h]` exactly and needs no storage. Only the small fraction of positions that actually touch a defect are stored, grouped into rectangular *tiles* per `(w, h)` pair. Each stored entry is a uint16 *delta*: `pure_values[w][h] - defect_value(w, h, sx, sy)`. Recovering any value costs one binary search over tiles plus one array lookup. This *sparse slab* typically holds 10–12% of what a full dense `(W+1)² × (H+1)²` table would require, while backtracking remains O(1) per lookup.

Rectangles smaller than the smallest item are skipped entirely (min-floor optimization).

The `--dry-run` flag shows estimated slab size before committing to a solve.

### GPU execution

When CUDA is available, Phase 3 runs on the GPU using a diagonal wavefront: all `(w, h)` pairs with `w + h = d` are independent and launched as a single kernel call, with `cudaDeviceSynchronize` between diagonals ensuring that when a cell (w, h) is computed, all the smaller subproblems it reads have already been written. This is true since every cut candidate has a strictly smaller perimeter than (w, h).

If the full slab exceeds available VRAM (with 15% headroom), the GPU fills as many diagonals as fit and transfers the result back. The CPU completes the remaining diagonals transparently. The split point is computed precisely from per-diagonal data counts.

On systems without CUDA the build and execution are CPU-only with no code changes required.

## TODO

- test top-down recursive solver with GPU-precomputed base cases (for problems that don't fit in RAM)
- interactive plot that lets you click through the cut sequence
- proper scalability study across sheet sizes, defect densities, item distributions
- GPU support beyond CUDA — Vulkan, HIP, anything that reduces the NVIDIA dependency
- validate against published paper datasets
- write docs
- qol fixes + refactoring
- find something else to add to this list

## Citation

Inspired by:

H. Zhang, S. Yao, Q. Liu, J. Leng, and L. Wei,
"Exact approaches for the unconstrained two-dimensional cutting problem with defects,"
*Computers & Operations Research*, vol. 160, 106407, 2023.
[https://doi.org/10.1016/j.cor.2023.106407](https://doi.org/10.1016/j.cor.2023.106407)

## License

MIT License — see [LICENSE](LICENSE) for details.