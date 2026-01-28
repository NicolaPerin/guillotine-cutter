"""CLI entry point for guillotine cutting solver."""

import argparse
import time
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
    
    # Built-in test
    parser.add_argument(
        '--benchmark',
        action='store_true',
        help='Run paper benchmark case (27x27 sheet)'
    )
    
    return parser.parse_args(argv)

def run_solver(item_sizes, defect_sizes, defect_positions, sheet_size, output_file):
    """Run the solver and save results."""
    # Solve
    geom = SheetGeometry(sheet_size, defect_sizes, defect_positions)
    patterns = CutPatternGenerator(item_sizes, geom)
    dp = GuillotineDP(item_sizes, geom, patterns)
    
    start = time.time()
    value, sequence = dp.solve()
    solve_time = time.time() - start
    
    # Calculate defect area
    defect_area = sum(defect_sizes[0][i] * defect_sizes[1][i] for i in range(len(defect_sizes[0])))
    
    # Save solution
    save_solution_json(output_file, value, sequence, sheet_size, defect_area)
    
    # Print summary
    print(f"Solved in {solve_time:.3f}s")
    print(f"Value: {value}/{sheet_size[0]*sheet_size[1]} ({value/(sheet_size[0]*sheet_size[1])*100:.1f}%)")
    print(f"Output saved to: {output_file}")

def run_benchmark(output_file):
    """Run the paper benchmark case."""
    print("Running paper benchmark (27x27 sheet)...")
    
    # Paper benchmark parameters
    item_sizes = [[5, 10, 12, 15], [5, 10, 12, 15]]
    defect_sizes = [[2], [2]]
    defect_positions = [[9], [9]]
    sheet_size = (27, 27)
    
    run_solver(item_sizes, defect_sizes, defect_positions, sheet_size, output_file)

def run_from_json(input_file, output_file):
    """Run solver from JSON input file."""
    print(f"Loading problem from {input_file}...")
    
    # Load problem
    problem = load_problem_json(input_file)
    
    run_solver(
        problem["item_sizes"],
        problem["defect_sizes"],
        problem["defect_positions"],
        problem["sheet_size"],
        output_file
    )

def main():
    """Main CLI function."""
    args = parse_args()
    
    if args.benchmark:
        run_benchmark(args.output)
    elif args.input:
        run_from_json(args.input, args.output)
    else:
        print("Error: Please specify --benchmark or provide input file")
        return 1
    
    return 0

if __name__ == "__main__":
    exit(main())