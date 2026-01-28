"""Tests for CLI module."""

import pytest  # noqa: F401


def test_cli_main_exists():
    """Test that CLI main function exists."""
    from guillotine.__main__ import main
    
    # Should be callable
    assert callable(main)
