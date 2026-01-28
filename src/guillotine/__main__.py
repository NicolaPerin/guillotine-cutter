"""CLI entry point for guillotine cutting solver."""

import argparse


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
    
    return parser.parse_args(argv)

def main():
    """Main CLI function."""
    args = parse_args()
    print(f"Args: {args}")

if __name__ == "__main__":
    main()
