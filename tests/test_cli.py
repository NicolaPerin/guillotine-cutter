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

def test_cli_run_from_json(tmp_path):
    """Test running solver from JSON input file."""
    from guillotine.__main__ import main
    from guillotine.io import save_problem_json
    import sys
    
    # Create input JSON
    input_file = tmp_path / "problem.json"
    output_file = tmp_path / "solution.json"
    
    save_problem_json(
        str(input_file),
        [[5, 10], [5, 10]],
        [[2], [2]],
        [[9], [9]],
        (20, 20)
    )
    
    # Run CLI
    sys.argv = ['guillotine', str(input_file), '--output', str(output_file)]
    main()
    
    # Should create output
    assert output_file.exists()
    
    # Check solution
    with open(output_file) as f:
        import json
        data = json.load(f)
    
    assert data["solution"]["total_area"] == 400
    assert data["solution"]["cut_area"] > 0

def test_cli_error_no_input(capsys):
    """Test error message when no input provided."""
    from guillotine.__main__ import main
    import sys
    
    # Run with no arguments
    sys.argv = ['guillotine']
    
    result = main()
    
    # Should return error code
    assert result == 1
    
    # Should print error message
    captured = capsys.readouterr()
    assert "Error" in captured.out

def test_cli_with_plot(tmp_path):
    """Test CLI with plot generation."""
    from guillotine.__main__ import main
    import sys
    
    output_file = tmp_path / "solution.json"
    plot_file = tmp_path / "test_plot.png"
    
    sys.argv = ['guillotine', '--benchmark', 
                '--output', str(output_file),
                '--plot', str(plot_file)]
    
    result = main()
    
    assert result == 0
    assert output_file.exists()
    assert plot_file.exists()
    assert plot_file.stat().st_size > 0