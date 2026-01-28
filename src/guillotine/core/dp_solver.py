"""DP solver for guillotine cutting."""


class GuillotineDP:
    """Dynamic programming solver for guillotine cutting problem."""
    
    def __init__(self, item_sizes, geometry, patterns):
        """Initialize DP solver."""
        self.geom = geometry
        self.patterns = patterns
        self.W0, self.H0 = geometry.W0, geometry.H0
