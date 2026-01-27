"""Constants used throughout the guillotine cutting solver."""

# Decision type constants for DP solver
DECISION_EMPTY = 0
DECISION_FILL = 1
DECISION_CUT_X = 2
DECISION_CUT_Y = 3
DECISION_DEFECT = 4
DECISION_PURE = 5

# Sentinel value for uncomputed DP states
NOT_COMPUTED = -1
