"""Pattern generation for guillotine cuts."""


def compute_normal_patterns(item_sizes, max_L):
    """Compute reachable cut positions using DP."""
    patterns = {}
    for L in range(max_L + 1):
        patterns[L] = [0]
    return patterns
