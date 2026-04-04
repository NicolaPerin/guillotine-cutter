"""CLI entry point for guillotine cutting solver."""

import os
import argparse
import time
import cProfile
import pstats
from guillotine.core.geometry import SheetGeometry
from guillotine.core.patterns import CutPatternGenerator
from guillotine.core.dp_solver import GuillotineDP
from guillotine.io import save_solution_json, load_problem_json, load_solution_json, validate_problem


def ensure_output_path(filepath):
    """
    Ensure outputs are saved inside ./output unless
    user explicitly provides a directory.
    """
    directory = os.path.dirname(filepath)

    # If user provided no directory, force into ./output
    if not directory:
        directory = "output"
        filepath = os.path.join(directory, filepath)

    os.makedirs(directory, exist_ok=True)
    return filepath


def format_bytes(n_bytes):
    """Format a byte count as a human-readable string."""
    if n_bytes < 1024:
        return f"{n_bytes} B"
    elif n_bytes < 1024 ** 2:
        return f"{n_bytes / 1024:.1f} KB"
    elif n_bytes < 1024 ** 3:
        return f"{n_bytes / 1024**2:.1f} MB"
    else:
        return f"{n_bytes / 1024**3:.2f} GB"


# -------------------------
# CLI parsing helpers
# -------------------------
def parse_items_cli(items_list):
    """Parse items from CLI inline format WxH."""
    widths, heights = [], []
    for item in items_list:
        try:
            w, h = map(int, item.lower().split("x"))
        except Exception:
            raise ValueError(f"Invalid item format: {item}. Use WxH (e.g., 5x5)")
        widths.append(w)
        heights.append(h)
    return [widths, heights]


def parse_defects_cli(defects_list):
    """Parse defects from CLI inline format X,Y,WxH."""
    widths, heights, xs, ys = [], [], [], []
    for defect in defects_list:
        try:
            pos_part, size_part = defect.rsplit(",", 1)
            x, y = map(int, pos_part.split(","))
            w, h = map(int, size_part.lower().split("x"))
        except Exception:
            raise ValueError(
                f"Invalid defect format: {defect}. Use X,Y,WxH (e.g., 9,9,2x2)"
            )
        xs.append(x)
        ys.append(y)
        widths.append(w)
        heights.append(h)
    return [widths, heights], [xs, ys]


def estimate_memory(sheet_size, n_defects):
    """Estimate memory usage for the solver.

    Returns a dict with estimated byte counts for each major allocation:
      - f_tables:  F_values + F_type + F_param (2D, always allocated)
      - fd_dense:  Fd_values as dense 4D array (if no slab)
      - prefix:    2D prefix sum array
      - total:     sum of all

    The Fd table dominates memory. With the current dense approach,
    Fd_values is (W0+1)^2 * (H0+1)^2 * 4 bytes (int32).
    """
    W0, H0 = sheet_size

    # 2D tables: F_values (int32), F_type (int8), F_param (int32)
    cells_2d = (W0 + 1) * (H0 + 1)
    f_tables = cells_2d * (4 + 1 + 4)

    # g tables: g_values (int32), g_indices (int32)
    g_tables = cells_2d * (4 + 4)

    # Prefix sum: (W0+1) * (H0+1) * int32
    prefix = cells_2d * 4

    # Fd dense 4D: (W0+1)^2 * (H0+1)^2 * int32
    cells_4d = (W0 + 1) ** 2 * (H0 + 1) ** 2
    fd_dense = cells_4d * 4

    total = f_tables + g_tables + prefix + fd_dense

    return {
        "f_tables": f_tables,
        "g_tables": g_tables,
        "prefix": prefix,
        "fd_dense": fd_dense,
        "fd_cells": cells_4d,
        "total": total,
        "has_defects": n_defects > 0,
    }


def print_dry_run(item_sizes, defect_sizes, defect_positions, sheet_size):
    """Print problem summary and estimated resource usage without solving."""
    W0, H0 = sheet_size
    n_items = len(item_sizes[0])
    n_defects = len(defect_sizes[0])
    total_area = W0 * H0
    defect_area = sum(
        defect_sizes[0][i] * defect_sizes[1][i] for i in range(n_defects)
    )

    print("=" * 60)
    print("DRY RUN — Problem Summary")
    print("=" * 60)
    print()

    print(f"  Sheet size:    {W0} x {H0}  (area: {total_area:,})")
    print()

    print(f"  Item types:    {n_items}")
    for i in range(n_items):
        w, h = item_sizes[0][i], item_sizes[1][i]
        print(f"    [{i}]  {w} x {h}  (area: {w*h})")
    print()

    print(f"  Defects:       {n_defects}")
    if n_defects > 0:
        for i in range(n_defects):
            x, y = defect_positions[0][i], defect_positions[1][i]
            w, h = defect_sizes[0][i], defect_sizes[1][i]
            print(f"    [{i}]  at ({x}, {y})  size {w} x {h}  (area: {w*h})")
        print(f"  Total defect area: {defect_area:,}  ({defect_area/total_area*100:.1f}% of sheet)")
    print()

    mem = estimate_memory(sheet_size, n_defects)

    print("  Estimated memory usage:")
    print(f"    g tables (values + indices):      {format_bytes(mem['g_tables'])}")
    print(f"    F tables (values + type + param): {format_bytes(mem['f_tables'])}")
    print(f"    Prefix sum array:                 {format_bytes(mem['prefix'])}")

    if mem["has_defects"]:
        print(f"    Fd dense 4D array:                {format_bytes(mem['fd_dense'])}  ({mem['fd_cells']:,} cells)")
        print(f"    ----------------------------------------")
        print(f"    Total estimated:                  {format_bytes(mem['total'])}")
    else:
        total_pure = mem["f_tables"] + mem["g_tables"] + mem["prefix"]
        print(f"    Fd table:                         not needed (pure sheet)")
        print(f"    ----------------------------------------")
        print(f"    Total estimated:                  {format_bytes(total_pure)}")

    print()
    print("=" * 60)


# -------------------------
# CLI parser
# -------------------------
def parse_args(argv=None):
    parser = argparse.ArgumentParser(
        prog="guillotine",
        description="2D guillotine cutting optimizer with defect handling.",
        epilog=(
            "Examples:\n"
            "  guillotine benchmark\n"
            "  guillotine solve problem.json\n"
            "  guillotine solve --sheet 27x27 --items 5x5 10x10 --defects 9,9,2x2\n"
            "  guillotine solve problem.json --dry-run\n"
            "  guillotine solve problem.json --plot\n"
            "  guillotine plot output/solution.json\n"
            "Run 'guillotine <command> --help' for more information."
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter
    )

    subparsers = parser.add_subparsers(dest="command", required=True)

    # ---- benchmark command ----
    bench_parser = subparsers.add_parser(
        "benchmark",
        help="Run built-in 27x27 benchmark case",
        description="Run the built-in benchmark problem from the reference paper."
    )
    bench_parser.add_argument(
        "--profile",
        nargs="?",
        const="profiling.txt",
        metavar="PROFILE_FILE",
        help="Enable profiling (default: profiling.txt)"
    )
    bench_parser.add_argument(
        "--plot",
        nargs="?",
        const="plot.png",
        metavar="IMAGE_FILE",
        help="Generate visualization (default: plot.png)"
    )
    bench_parser.add_argument(
        "-o", "--output",
        default="solution.json",
        help="Output JSON file"
    )
    bench_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print problem summary and memory estimates without solving"
    )

    # ---- solve command ----
    solve_parser = subparsers.add_parser(
        "solve",
        help="Solve a cutting problem",
        description=(
            "Solve a cutting problem.\n\n"
            "Input modes:\n"
            "  JSON: guillotine solve problem.json\n"
            "  Inline: guillotine solve --sheet 27x27 --items 5x5 10x10 --defects 9,9,2x2\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    solve_parser.add_argument("input", nargs="?", help="Input JSON file")
    solve_parser.add_argument("--sheet", help="Sheet size (e.g., 27x27)")
    solve_parser.add_argument(
        "--items",
        nargs="+",
        metavar="WxH",
        help="Items as WxH (e.g., 5x5 10x10)"
    )
    solve_parser.add_argument(
        "--defects",
        nargs="+",
        metavar="X,Y,WxH",
        help="Defects as X,Y,WxH (e.g., 9,9,2x2)"
    )
    solve_parser.add_argument(
        "--profile",
        nargs="?",
        const="profiling.txt",
        metavar="PROFILE_FILE",
        help="Enable profiling (default: profiling.txt)"
    )
    solve_parser.add_argument(
        "--plot",
        nargs="?",
        const="plot.png",
        metavar="IMAGE_FILE",
        help="Generate visualization (default: plot.png)"
    )
    solve_parser.add_argument(
        "-o", "--output",
        default="solution.json",
        help="Output JSON file"
    )
    solve_parser.add_argument(
        "--dry-run",
        action="store_true",
        help="Print problem summary and memory estimates without solving"
    )

    # ---- plot command ----
    plot_parser = subparsers.add_parser(
        "plot",
        help="Generate visualization from an existing solution",
        description=(
            "Plot a cutting solution without re-solving.\n\n"
            "The solution JSON file contains the embedded problem definition\n"
            "(item sizes, defects, sheet size) along with the cut sequence,\n"
            "so no additional input files are needed.\n\n"
            "Example:\n"
            "  guillotine plot output/solution.json\n"
            "  guillotine plot output/solution.json -o my_plot.png\n"
        ),
        formatter_class=argparse.RawDescriptionHelpFormatter
    )
    plot_parser.add_argument(
        "solution",
        help="Solution JSON file (output of a previous solve)"
    )
    plot_parser.add_argument(
        "-o", "--output",
        default="plot.png",
        metavar="IMAGE_FILE",
        help="Output image file (default: plot.png)"
    )

    return parser.parse_args(argv)


# -------------------------
# Solver functions
# -------------------------
def run_solver(item_sizes, defect_sizes, defect_positions, sheet_size,
               output_file, profile_file=None, plot_file=None):
    """Run the solver, save results, and optionally plot.

    Plotting is done AFTER saving — the solution JSON is loaded back from
    disk so that all solver memory (geometry, patterns, DP tables, Fd arrays)
    can be garbage collected before matplotlib is imported. This avoids
    holding ~GB of solver data alongside matplotlib's ~60 MB footprint.
    """

    def solve():
        geom = SheetGeometry(sheet_size, defect_sizes, defect_positions)
        patterns = CutPatternGenerator(item_sizes, geom)
        dp = GuillotineDP(item_sizes, geom, patterns)
        start = time.time()
        value, sequence = dp.solve()
        solve_time = time.time() - start
        return value, sequence, solve_time

    if profile_file:
        print(f"Profiling enabled, output: {profile_file}")
        profiler = cProfile.Profile()
        profiler.enable()
        value, sequence, solve_time = solve()
        profiler.disable()
        profile_file = ensure_output_path(profile_file)
        with open(profile_file, 'w') as f:
            stats = pstats.Stats(profiler, stream=f)
            stats.sort_stats('cumulative')
            stats.print_stats(50)
        print(f"Profile saved to: {profile_file}")
    else:
        value, sequence, solve_time = solve()

    output_file = ensure_output_path(output_file)
    save_solution_json(output_file, value, sequence,
                       item_sizes, defect_sizes, defect_positions, sheet_size)

    print(f"Solved in {solve_time:.3f}s")
    print(f"Value: {value}/{sheet_size[0]*sheet_size[1]} "
          f"({value/(sheet_size[0]*sheet_size[1])*100:.1f}%)")
    print(f"Output saved to: {output_file}")

    # Plot from the saved JSON — solver memory is no longer referenced
    # and can be collected before matplotlib loads.
    if plot_file:
        plot_file = ensure_output_path(plot_file)
        run_plot(output_file, plot_file)


def run_benchmark(output_file, profile_file=None, plot_file=None):
    """Run the paper benchmark case."""
    print("Running paper benchmark (27x27 sheet)...")
    item_sizes = [[5, 10, 12, 15], [5, 10, 12, 15]]
    defect_sizes = [[2], [2]]
    defect_positions = [[9], [9]]
    sheet_size = (27, 27)
    run_solver(item_sizes, defect_sizes, defect_positions, sheet_size,
               output_file, profile_file, plot_file)


def run_from_json(input_file, output_file, profile_file=None, plot_file=None):
    """Run solver from JSON input file."""
    print(f"Loading problem from {input_file}...")
    problem = load_problem_json(input_file)
    run_solver(
        problem["item_sizes"],
        problem["defect_sizes"],
        problem["defect_positions"],
        problem["sheet_size"],
        output_file,
        profile_file,
        plot_file
    )

def run_plot(solution_file, output_file):
    """Generate visualization from an existing solution file.

    The solution JSON contains the embedded problem definition (item sizes,
    defects, sheet size) so only one file is needed.
    """
    from guillotine.visualize import CuttingVisualizer

    print(f"Loading solution from {solution_file}...")
    data = load_solution_json(solution_file)

    W0, H0 = data["sheet_size"]
    value = data["cut_area"]
    total_area = W0 * H0

    viz = CuttingVisualizer(
        data["item_sizes"],
        data["defect_sizes"],
        data["defect_positions"],
        data["sheet_size"]
    )

    output_file = ensure_output_path(output_file)
    viz.plot(data["sequence"], output_file)

    print(f"Value: {value}/{total_area} ({value/total_area*100:.1f}%)")
    print(f"Plot saved to: {output_file}")


def load_problem_for_dry_run(args):
    """Extract problem data from CLI args (JSON or inline), returns a dict or None on error."""
    if args.command == "benchmark":
        return {
            "item_sizes": [[5, 10, 12, 15], [5, 10, 12, 15]],
            "defect_sizes": [[2], [2]],
            "defect_positions": [[9], [9]],
            "sheet_size": (27, 27),
        }

    # JSON input
    if args.input:
        return load_problem_json(args.input)

    # Inline input
    if args.items and args.sheet:
        try:
            w, h = map(int, args.sheet.lower().split("x"))
        except Exception:
            print("Error: sheet must be formatted as WxH (e.g., 27x27)")
            return None

        try:
            item_sizes = parse_items_cli(args.items)
        except ValueError as e:
            print(e)
            return None

        if args.defects:
            try:
                defect_sizes, defect_positions = parse_defects_cli(args.defects)
            except ValueError as e:
                print(e)
                return None
        else:
            defect_sizes = [[], []]
            defect_positions = [[], []]

        try:
            validate_problem(item_sizes, defect_sizes, defect_positions, (w, h))
        except ValueError as e:
            print(f"Error: {e}")
            return None

        return {
            "item_sizes": item_sizes,
            "defect_sizes": defect_sizes,
            "defect_positions": defect_positions,
            "sheet_size": (w, h),
        }

    print("Error: provide --sheet and --items, or a JSON input file")
    return None


# -------------------------
# Main entry point
# -------------------------
def main(argv=None):
    args = parse_args(argv)

    # ---- dry-run for benchmark and solve ----
    if args.command in ("benchmark", "solve") and args.dry_run:
        problem = load_problem_for_dry_run(args)
        if problem is None:
            return 1
        print_dry_run(
            problem["item_sizes"],
            problem["defect_sizes"],
            problem["defect_positions"],
            problem["sheet_size"],
        )
        return 0

    # ---- benchmark ----
    if args.command == "benchmark":
        run_benchmark(args.output, args.profile, args.plot)
        return 0

    # ---- solve ----
    elif args.command == "solve":
        if args.input:
            run_from_json(args.input, args.output, args.profile, args.plot)
            return 0

        if args.items and args.sheet:
            try:
                w, h = map(int, args.sheet.lower().split("x"))
            except Exception:
                print("Error: sheet must be formatted as WxH (e.g., 27x27)")
                return 1

            try:
                item_sizes = parse_items_cli(args.items)
            except ValueError as e:
                print(e)
                return 1

            if args.defects:
                try:
                    defect_sizes, defect_positions = parse_defects_cli(args.defects)
                except ValueError as e:
                    print(e)
                    return 1
            else:
                defect_sizes = [[], []]
                defect_positions = [[], []]

            try:
                validate_problem(item_sizes, defect_sizes, defect_positions, (w, h))
            except ValueError as e:
                print(f"Error: {e}")
                return 1

            run_solver(
                item_sizes,
                defect_sizes,
                defect_positions,
                (w, h),
                args.output,
                args.profile,
                args.plot
            )
            return 0

        else:
            print("Error: provide --sheet and --items, or a JSON input file")
            return 1

    # ---- plot ----
    elif args.command == "plot":
        run_plot(args.solution, args.output)
        return 0

    return 1


if __name__ == "__main__":
    exit(main())
