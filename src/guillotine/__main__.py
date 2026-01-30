"""CLI entry point for guillotine cutting solver."""

import argparse
import time
import cProfile
import pstats
from guillotine.core.geometry import SheetGeometry
from guillotine.core.patterns import CutPatternGenerator
from guillotine.core.dp_solver import GuillotineDP
from guillotine.io import save_solution_json, load_problem_json


def parse_args(argv=None):
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Guillotine cutting optimizer with defect handling"
    )
    
    # Input method 1: JSON file
    parser.add_argument(
        'input',
        nargs='?',
        help='Input JSON file'
    )
    
    # Input method 2: CSV files
    parser.add_argument(
        '--items',
        help='Items CSV file (width,height)'
    )
    parser.add_argument(
        '--defects',
        help='Defects CSV file (x,y,width,height)'
    )
    parser.add_argument(
        '--sheet',
        help='Sheet size (e.g., 27x27)'
    )
    
    # Output options
    parser.add_argument(
        '--output', '-o',
        help='Output JSON file (default: solution.json)',
        default='solution.json'
    )
    
    # Visualization
    parser.add_argument(
        '--plot',
        nargs='?',
        const='plot.png',
        metavar='IMAGE_FILE',
        help='Generate visualization (default: plot.png)'
    )
    
    # Built-in test
    parser.add_argument(
        '--benchmark',
        action='store_true',
        help='Run paper benchmark case (27x27 sheet)'
    )
    
    # Profiling
    parser.add_argument(
        '--profile',
        help='Enable profiling and save to file',
        metavar='PROFILE_FILE'
    )

    return parser.parse_args(argv)


def run_solver(item_sizes, defect_sizes, defect_positions, sheet_size, 
               output_file, profile_file=None, plot_file=None):
    """Run the solver and save results."""
    
    def solve():
        """Inner function for profiling."""
        # Setup
        geom = SheetGeometry(sheet_size, defect_sizes, defect_positions)
        patterns = CutPatternGenerator(item_sizes, geom)
        dp = GuillotineDP(item_sizes, geom, patterns)
        
        # Solve
        start = time.time()
        value, sequence = dp.solve()
        solve_time = time.time() - start
        
        return value, sequence, solve_time
    
    # Run with or without profiling
    if profile_file:
        print(f"Profiling enabled, output: {profile_file}")
        profiler = cProfile.Profile()
        profiler.enable()
        value, sequence, solve_time = solve()
        profiler.disable()
        
        # Save profile to file
        with open(profile_file, 'w') as f:
            stats = pstats.Stats(profiler, stream=f)
            stats.sort_stats('cumulative')
            stats.print_stats(50)  # Top 50 functions
        
        print(f"Profile saved to: {profile_file}")
    else:
        value, sequence, solve_time = solve()
    
    # Calculate defect area
    defect_area = sum(defect_sizes[0][i] * defect_sizes[1][i] 
                     for i in range(len(defect_sizes[0])))
    
    # Save solution
    save_solution_json(output_file, value, sequence, sheet_size, defect_area)
    
    # Generate visualization if requested
    if plot_file:
        from guillotine.visualize import CuttingVisualizer
        print(f"Generating visualization: {plot_file}")
        viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
        viz.plot(sequence, plot_file)
        print(f"Plot saved to: {plot_file}")
    
    # Print summary
    print(f"Solved in {solve_time:.3f}s")
    print(f"Value: {value}/{sheet_size[0]*sheet_size[1]} "
          f"({value/(sheet_size[0]*sheet_size[1])*100:.1f}%)")
    print(f"Output saved to: {output_file}")


def run_benchmark(output_file, profile_file=None, plot_file=None):
    """Run the paper benchmark case."""
    print("Running paper benchmark (27x27 sheet)...")
    
    # Paper benchmark parameters
    item_sizes = [[5, 10, 12, 15], [5, 10, 12, 15]]
    defect_sizes = [[2], [2]]
    defect_positions = [[9], [9]]
    sheet_size = (27, 27)
    
    run_solver(item_sizes, defect_sizes, defect_positions, 
              sheet_size, output_file, profile_file, plot_file)


def run_from_json(input_file, output_file, profile_file=None, plot_file=None):
    """Run solver from JSON input file."""
    print(f"Loading problem from {input_file}...")
    
    # Load problem
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


def main():
    """Main CLI function."""
    args = parse_args()
    
    if args.benchmark:
        run_benchmark(args.output, args.profile, args.plot)
    elif args.input:
        run_from_json(args.input, args.output, args.profile, args.plot)
    else:
        print("Error: Please specify --benchmark or provide input file")
        return 1
    
    return 0


if __name__ == "__main__":
    exit(main())