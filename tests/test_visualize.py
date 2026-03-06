"""Tests for visualization module."""

def test_visualizer_init():
    """Test CuttingVisualizer initialization."""
    from guillotine.visualize import CuttingVisualizer
    
    item_sizes = [[3, 4], [3, 4]]
    defect_sizes = [[2], [2]]
    defect_positions = [[5], [5]]
    sheet_size = (20, 20)
    
    viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
    
    assert viz.sheet_size == sheet_size
    assert viz.item_sizes == item_sizes

def test_save_plot(tmp_path):
    """Test saving plot to file."""
    from guillotine.visualize import CuttingVisualizer
    
    item_sizes = [[5], [5]]
    defect_sizes = [[], []]
    defect_positions = [[], []]
    sheet_size = (10, 10)
    
    viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
    
    # Simple sequence: fill with one item
    sequence = "g_0"
    output_file = tmp_path / "test_plot.png"
    
    viz.plot(sequence, str(output_file))
    
    # File should exist
    assert output_file.exists()
    # Should be a valid image file (check size > 0)
    assert output_file.stat().st_size > 0

def test_plot_with_defects(tmp_path):
    """Test plotting with defects."""
    from guillotine.visualize import CuttingVisualizer
    
    item_sizes = [[5], [5]]
    defect_sizes = [[2, 3], [2, 3]]
    defect_positions = [[5, 10], [5, 10]]
    sheet_size = (20, 20)
    
    viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
    
    sequence = "g_0"
    output_file = tmp_path / "test_defects.png"
    
    viz.plot(sequence, str(output_file))
    
    assert output_file.exists()
    assert output_file.stat().st_size > 0

def test_plot_with_cuts(tmp_path):
    """Test plotting with actual cuts."""
    from guillotine.visualize import CuttingVisualizer
    
    item_sizes = [[5], [5]]
    defect_sizes = [[], []]
    defect_positions = [[], []]
    sheet_size = (10, 10)
    
    viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
    
    # Sequence with a vertical cut
    sequence = ("X", 5, "g_0", "g_0")
    output_file = tmp_path / "test_cuts.png"
    
    viz.plot(sequence, str(output_file))
    
    assert output_file.exists()
    assert output_file.stat().st_size > 0

def test_plot_with_horizontal_cut(tmp_path):
    """Test plotting with horizontal cut."""
    from guillotine.visualize import CuttingVisualizer
    
    item_sizes = [[5], [5]]
    defect_sizes = [[], []]
    defect_positions = [[], []]
    sheet_size = (10, 10)
    
    viz = CuttingVisualizer(item_sizes, defect_sizes, defect_positions, sheet_size)
    
    # Sequence with a horizontal cut
    sequence = ("Y", 5, "g_0", "g_0")
    output_file = tmp_path / "test_horizontal.png"
    
    viz.plot(sequence, str(output_file))
    
    assert output_file.exists()
    assert output_file.stat().st_size > 0
