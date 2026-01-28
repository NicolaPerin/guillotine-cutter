"""CLI entry point for guillotine cutting solver."""

import argparse


def parse_args(argv=None):
    """Parse command line arguments."""
    parser = argparse.ArgumentParser(
        description="Guillotine cutting optimizer with defect handling"
    )
    
    # Input methods
    parser.add_argument(
        'input',
        nargs='?',
        help='Input JSON file'
    )
    
    return parser.parse_args(argv)

def main():
    """Main CLI function."""
    args = parse_args()
    print(f"Args: {args}")

if __name__ == "__main__":
    main()
