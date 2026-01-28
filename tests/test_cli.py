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