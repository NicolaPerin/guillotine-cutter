"""Pattern generation for guillotine cuts."""

def compute_normal_patterns(item_sizes, max_L):
    """Compute reachable cut positions using DP."""
    sizes = sorted(set(int(s) for s in item_sizes if s > 0))
    reachable = [False] * (max_L + 1)
    reachable[0] = True
    
    for size in sizes:
        for z in range(size, max_L + 1):
            if reachable[z - size]:
                reachable[z] = True
    
    patterns = {}
    for L in range(max_L + 1):
        patterns[L] = [z for z in range(1, L) if reachable[z]]
    
    return patterns

class CutPatternGenerator:
    """Generates candidate cut positions."""
    
    def __init__(self, item_sizes, geometry):
        """Initialize pattern generator."""
        self.geom = geometry
        self.W0, self.H0 = geometry.W0, geometry.H0
