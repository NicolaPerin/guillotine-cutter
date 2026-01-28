"""Tests for CLI module."""

import pytest  # noqa: F401


def test_cli_main_exists():
    """Test that CLI main function exists."""
    from guillotine.__main__ import main
    
    # Should be callable
    assert callable(main)

def test_cli_parse_args():
    """Test that CLI can parse arguments."""
    from guillotine.__main__ import parse_args
    
    # Test JSON input
    args = parse_args(['problem.json'])
    assert args.input == 'problem.json'

def test_cli_parse_csv_args():
    """Test parsing CSV file arguments."""
    from guillotine.__main__ import parse_args
    
    args = parse_args([
        '--items', 'items.csv',
        '--defects', 'defects.csv',
        '--sheet', '27x27'
    ])
    
    assert args.items == 'items.csv'
    assert args.defects == 'defects.csv'
    assert args.sheet == '27x27'


def test_cli_parse_output_and_benchmark():
    """Test parsing output and benchmark arguments."""
    from guillotine.__main__ import parse_args
    
    # Test output argument
    args = parse_args(['problem.json', '--output', 'solution.json'])
    assert args.output == 'solution.json'
    
    # Test benchmark flag
    args = parse_args(['--benchmark'])
    assert args.benchmark is True

def test_cli_run_benchmark(tmp_path, capsys):
    """Test running the benchmark."""
    from guillotine.__main__ import main, parse_args
    import sys
    
    # Set output path
    output_file = tmp_path / "benchmark_solution.json"
    
    # Mock sys.argv
    sys.argv = ['guillotine', '--benchmark', '--output', str(output_file)]
    
    # Run main
    main()
    
    # Should create output file
    assert output_file.exists()
    
    # Check output contains expected value
    with open(output_file) as f:
        import json
        data = json.load(f)
    
    assert data["solution"]["cut_area"] == 644
    assert data["solution"]["total_area"] == 729