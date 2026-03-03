"""CLI entry point for guillotine cutting solver."""

import os
import argparse
import time
import cProfile
import pstats
from guillotine.core.geometry import SheetGeometry
from guillotine.core.patterns import CutPatternGenerator
from guillotine.core.dp_solver import GuillotineDP
from guillotine.io import save_solution_json, load_problem_json


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
            "  guillotine solve problem.json --plot\n"
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

    return parser.parse_args(argv)


# -------------------------
# Solver functions
# -------------------------
def run_solver(item_sizes, defect_sizes, defect_positions, sheet_size, 
               output_file, profile_file=None, plot_file=None):
    """Run the solver and save results."""
    
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
    
    defect_area = sum(defect_sizes[0][i] * defect_sizes[1][i] 
                     for i in range(len(defect_sizes[0])))
    
    output_file = ensure_output_path(output_file)
    save_solution_json(output_file, value, sequence, sheet_size, defect_area)
    
    if plot_file:
        from guillotine.visualize import CuttingVisualizer
        print(f"Generating visualization: {plot_file}")
        viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
        plot_file = ensure_output_path(plot_file)
        viz.plot(sequence, plot_file)
        print(f"Plot saved to: {plot_file}")
    
    print(f"Solved in {solve_time:.3f}s")
    print(f"Value: {value}/{sheet_size[0]*sheet_size[1]} "
          f"({value/(sheet_size[0]*sheet_size[1])*100:.1f}%)")
    print(f"Output saved to: {output_file}")


def run_benchmark(output_file, profile_file=None, plot_file=None):
    """Run the paper benchmark case."""
    print("Running paper benchmark (27x27 sheet)...")
    item_sizes = [[5, 10, 12, 15], [5, 10, 12, 15]]
    defect_sizes = [[2], [2]]
    defect_positions = [[9], [9]]
    sheet_size = (27, 27)
    run_solver(item_sizes, defect_sizes, defect_positions, sheet_size, output_file, profile_file, plot_file)


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


# -------------------------
# Main entry point
# -------------------------
def main(argv=None):
    args = parse_args(argv)

    if args.command == "benchmark":
        run_benchmark(args.output, args.profile, args.plot)
        return 0

    elif args.command == "solve":
        # JSON input mode
        if args.input:
            run_from_json(args.input, args.output, args.profile, args.plot)
            return 0

        # Inline CLI mode
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

    return 1


if __name__ == "__main__":
    exit(main())