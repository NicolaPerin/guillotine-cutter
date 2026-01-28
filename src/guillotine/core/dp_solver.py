"""DP solver for guillotine cutting."""


class GuillotineDP:
    """Dynamic programming solver for guillotine cutting problem."""
    
    def __init__(self, item_sizes, geometry, patterns):
        """Initialize DP solver."""
        self.geom = geometry
        self.patterns = patterns
        self.W0, self.H0 = geometry.W0, geometry.H0
        self.item_sizes = item_sizes
        
        # Memoization cache
        self.memo = {}

    def solve(self):
        """Solve the cutting problem."""
        return self._F(0, 0, self.W0, self.H0)
    
    def _F(self, x, y, w, h):
        """Compute optimal value for rectangle (x, y, w, h)."""
        # Base cases
        if w <= 0 or h <= 0:
            return 0, "empty"
        
        # Check memo
        key = (x, y, w, h)
        if key in self.memo:
            return self.memo[key]
        
        # Check if pure (no defects)
        if self.geom.is_pure(x, y, w, h):
            result = self._solve_pure(x, y, w, h)
        else:
            result = self._solve_defected(x, y, w, h)
        
        self.memo[key] = result
        return result

    def _solve_pure(self, x, y, w, h):
        """Solve for pure rectangle (no defects).
        
        This is the core DP recursion. For a rectangle (x,y) with size (w,h),
        we try three strategies and pick the best:
        
        Strategy 1: FILL WITH SINGLE ITEM
            If an item exactly matches (w,h), fill the entire rectangle.
            Value = w * h (entire area used)
            
            Example: Rectangle 3x3, have item 3x3 → fill it!
            
        Strategy 2: VERTICAL CUT (X direction)
            Make a vertical cut at position z, creating two sub-rectangles:
            - Left piece:  (x, y, z, h)
            - Right piece: (x+z, y, w-z, h)
            
            Recursively solve both pieces, sum their values.
            
            Example: Rectangle 6x3
                     Cut at z=3:
                     ┌───┬───┐
                     │ L │ R │  L=3x3, R=3x3
                     └───┴───┘
                     
        Strategy 3: HORIZONTAL CUT (Y direction)
            Make a horizontal cut at position z, creating two sub-rectangles:
            - Bottom piece: (x, y, w, z)
            - Top piece:    (x, y+z, w, h-z)
            
            Recursively solve both pieces, sum their values.
            
            Example: Rectangle 3x6
                     Cut at z=3:
                     ┌───┐
                     │ T │  T=3x3 (top)
                     ├───┤
                     │ B │  B=3x3 (bottom)
                     └───┘
        
        The DP nature:
            F(w,h) = max of:
                     - fill with item if exact match
                     - max over all z: F(z,h) + F(w-z,h)  [X cuts]
                     - max over all z: F(w,z) + F(w,h-z)  [Y cuts]
        
        Memoization ensures we don't recompute the same subproblem twice.
        """

        best_val = 0
        best_seq = "empty"
        
        # Strategy 1: Try filling with single item
        for i, (iw, ih) in enumerate(zip(self.item_sizes[0], self.item_sizes[1])):
            if iw == w and ih == h:
                # Item fits exactly!
                if w * h > best_val:
                    best_val = w * h
                    best_seq = f"g_{i}"
        
        # Strategy 2: Try all vertical cuts (X direction)
        for z in self.patterns.cuts_pure_x(w):
            # Cut creates: left piece (width z) and right piece (width w-z)
            left_val, left_seq = self._F(x, y, z, h)
            right_val, right_seq = self._F(x + z, y, w - z, h)
            total = left_val + right_val
            
            if total > best_val:
                best_val = total
                best_seq = ("X", z, left_seq, right_seq)
        
        # Strategy 3: Try all horizontal cuts (Y direction)
        for z in self.patterns.cuts_pure_y(h):
            # Cut creates: bottom piece (height z) and top piece (height h-z)
            bot_val, bot_seq = self._F(x, y, w, z)
            top_val, top_seq = self._F(x, y + z, w, h - z)
            total = bot_val + top_val
            
            if total > best_val:
                best_val = total
                best_seq = ("Y", z, bot_seq, top_seq)
        
        return best_val, best_seq
    
    def _solve_defected(self, x, y, w, h):
        """Solve for defected rectangle (contains defects).
        
        Similar to _solve_pure, but:
        1. Cannot fill with single item (defect makes it unusable)
        2. Cut positions include defect boundaries (to isolate defects)
        3. Goal: Cut around defects to recover as much clear area as possible
        
        Example: Rectangle with defect at (10,10):
                 ┌─────────┬───┬─────┐
                 │  CLEAR  │DEF│CLEAR│  Cut at defect edges
                 └─────────┴───┴─────┘
                             ↑
                        Defect isolated
        """
        best_val = 0
        best_seq = "defect"  # Default: entire area is defected (unusable)
        
        # Get candidate cuts (includes normal patterns + defect boundaries)
        x_cuts, y_cuts = self.patterns.cuts_defected(x, y, w, h)
        
        # Try vertical cuts - same logic as pure case
        for z in x_cuts:
            left_val, left_seq = self._F(x, y, z, h)
            right_val, right_seq = self._F(x + z, y, w - z, h)
            total = left_val + right_val
            
            if total > best_val:
                best_val = total
                best_seq = ("X", z, left_seq, right_seq)
        
        # Try horizontal cuts - same logic as pure case
        for z in y_cuts:
            bot_val, bot_seq = self._F(x, y, w, z)
            top_val, top_seq = self._F(x, y + z, w, h - z)
            total = bot_val + top_val
            
            if total > best_val:
                best_val = total
                best_seq = ("Y", z, bot_seq, top_seq)
        
        return best_val, best_seq