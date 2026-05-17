#pragma once
#include "gdcut/patterns.hpp"
#include "gdcut/defect.hpp"
#include <vector>

namespace gdcut {

/**
 * @brief Abstract interface for cut candidate generation.
 *
 * Decouples the recursive solver from the specific set of cut positions
 * it evaluates. The solver calls x_cuts/y_cuts at each sub-sheet and
 * iterates over whatever positions are returned — it never knows whether
 * it is using normal patterns, raster points, or a future candidate set.
 *
 * Implement this interface to plug a new cut strategy into the solver
 * without modifying any solver code.
 */
class CutCandidates {
public:
    /** @brief Returns the x-coordinates of possible vertical cuts within the given rectangle. */
    virtual const std::vector<int>& x_cuts(int x, int y, int w, int h) const = 0;

    /** @brief Returns the y-coordinates of possible horizontal cuts within the given rectangle. */
    virtual const std::vector<int>& y_cuts(int x, int y, int w, int h) const = 0;

    virtual ~CutCandidates() = default;
};

/**
 * @brief Cut candidates for defect-free sub-sheets.
 *
 * Returns precomputed raster points from PurePatterns. Position parameters
 * (x, y) are ignored — for pure sheets optimal cuts depend only on (w, h).
 */
class PureCuts : public CutCandidates {
public:
    PureCuts(const PurePatterns& patterns_x, const PurePatterns& patterns_y);

    const std::vector<int>& x_cuts(int x, int y, int w, int h) const override;
    const std::vector<int>& y_cuts(int x, int y, int w, int h) const override;

private:
    const PurePatterns& patterns_x_;
    const PurePatterns& patterns_y_;
};

/**
 * @brief Cut candidates for defect-affected sub-sheets.
 *
 * Computes extended raster points (DPR) per Zhang et al. on each call.
 * Three scratch vectors (offsets, positions, output) are reused across
 * calls via clear() — capacity is retained after the first few calls so
 * no heap allocation occurs at steady state.
 *
 * The returned reference is valid only until the next call to x_cuts or
 * y_cuts — callers must not hold it across calls.
 */
class ExtendedCuts : public CutCandidates {
public:
    ExtendedCuts(const PurePatterns& patterns_x,
                 const PurePatterns& patterns_y,
                 const DefectMap&    defect_map);

    const std::vector<int>& x_cuts(int x, int y, int w, int h) const override;
    const std::vector<int>& y_cuts(int x, int y, int w, int h) const override;

private:
    const PurePatterns& patterns_x_;
    const PurePatterns& patterns_y_;
    const DefectMap&    defect_map_;

    // output buffers — returned by reference, overwritten on each call
    mutable std::vector<int> x_cuts_cache_;
    mutable std::vector<int> y_cuts_cache_;

    // scratch buffers — reused across calls to avoid heap allocation
    mutable std::vector<int> scratch_offsets_;
    mutable std::vector<int> scratch_positions_;

    void compute_cuts(int primary_origin, int secondary_origin,
                  int primary_span,   int secondary_span,
                  const PurePatterns& patterns,
                  bool use_x,
                  std::vector<int>& out) const;
};

} // namespace gdcut