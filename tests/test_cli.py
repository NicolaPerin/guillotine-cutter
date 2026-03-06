"""Tests for CLI module."""

def test_cli_main_exists():
    """Test that CLI main function exists."""
    from guillotine.__main__ import main
    assert callable(main)


def test_cli_parse_args_json():
    """Test parsing JSON input."""
    from guillotine.__main__ import parse_args

    args = parse_args(['solve', 'problem.json'])
    assert args.command == 'solve'
    assert args.input == 'problem.json'


def test_cli_parse_inline_args():
    """Test parsing inline sheet/items/defects."""
    from guillotine.__main__ import parse_args

    args = parse_args([
        'solve',
        '--sheet', '27x27',
        '--items', '5x5', '10x10',
        '--defects', '9,9,2x2'
    ])

    assert args.command == 'solve'
    assert args.sheet == '27x27'
    assert args.items == ['5x5', '10x10']
    assert args.defects == ['9,9,2x2']


def test_cli_parse_output_and_benchmark():
    """Test parsing output and benchmark command."""
    from guillotine.__main__ import parse_args

    args = parse_args(['benchmark', '--output', 'solution.json'])
    assert args.command == 'benchmark'
    assert args.output == 'solution.json'


def test_cli_run_benchmark(tmp_path):
    """Test running the benchmark."""
    from guillotine.__main__ import main

    output_file = tmp_path / "benchmark_solution.json"

    result = main([
        'benchmark',
        '--output', str(output_file)
    ])

    assert result == 0
    assert output_file.exists()

    import json
    with open(output_file) as f:
        data = json.load(f)

    assert data["solution"]["cut_area"] == 644
    assert data["solution"]["total_area"] == 729


def test_cli_run_from_json(tmp_path):
    """Test running solver from JSON input file."""
    from guillotine.__main__ import main
    from guillotine.io import save_problem_json

    input_file = tmp_path / "problem.json"
    output_file = tmp_path / "solution.json"

    save_problem_json(
        str(input_file),
        [[5, 10], [5, 10]],
        [[2], [2]],
        [[9], [9]],
        (20, 20)
    )

    result = main([
        'solve',
        str(input_file),
        '--output', str(output_file)
    ])

    assert result == 0
    assert output_file.exists()

    import json
    with open(output_file) as f:
        data = json.load(f)

    assert data["solution"]["total_area"] == 400
    assert data["solution"]["cut_area"] > 0


def test_cli_run_inline_mode(tmp_path):
    """Test running solver with inline sheet/items/defects."""
    from guillotine.__main__ import main

    output_file = tmp_path / "solution.json"

    result = main([
        'solve',
        '--sheet', '20x20',
        '--items', '5x5', '10x10',
        '--defects', '9,9,2x2',
        '--output', str(output_file)
    ])

    assert result == 0
    assert output_file.exists()

    import json
    with open(output_file) as f:
        data = json.load(f)

    assert data["solution"]["total_area"] == 400
    assert data["solution"]["cut_area"] > 0


def test_cli_error_no_input():
    """Test error when solve has no input."""
    from guillotine.__main__ import main

    result = main(['solve'])
    assert result == 1


def test_cli_with_plot(tmp_path):
    """Test CLI with plot generation."""
    from guillotine.__main__ import main

    output_file = tmp_path / "solution.json"
    plot_file = tmp_path / "test_plot.png"

    result = main([
        'benchmark',
        '--output', str(output_file),
        '--plot', str(plot_file)
    ])

    assert result == 0
    assert output_file.exists()
    assert plot_file.exists()
    assert plot_file.stat().st_size > 0


def test_cli_with_profiling(tmp_path):
    """Test CLI with profiling enabled."""
    from guillotine.__main__ import main

    output_file = tmp_path / "solution.json"
    profile_file = tmp_path / "profile.txt"

    result = main([
        'benchmark',
        '--output', str(output_file),
        '--profile', str(profile_file)
    ])

    assert result == 0
    assert output_file.exists()
    assert profile_file.exists()
    assert profile_file.stat().st_size > 0

    with open(profile_file) as f:
        content = f.read()
        assert 'function calls' in content
        assert 'cumulative' in content