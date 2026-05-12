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
            /** @brief Returns the x-coordinates of possible vertical cuts within the given rectangle */
            virtual const std::vector<int>& x_cuts(int x, int y, int w, int h) const = 0;

            /** @brief Returns the y-coordinates of possible horizontal cuts within the given rectangle */
            virtual const std::vector<int>& y_cuts(int x, int y, int w, int h) const = 0;

            virtual ~CutCandidates() = default;
    };

    /**
     * @brief Cut candidates for defect-free sub-sheets.
     *
     * Returns precomputed raster points from PurePatterns. The position
     * parameters (x, y) are ignored — for pure sheets the optimal cuts
     * depend only on the rectangle dimensions (w, h), not its position
     * on the sheet.
     */
    class PureCuts : public CutCandidates {
        public:
            /** @brief Constructs a PureCuts instance with the given patterns */
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
     * Computes extended raster points on the fly for each sub-sheet,
     * incorporating defect boundary positions as additional cut origins.
     * Results are cached in x_cuts_cache_ and y_cuts_cache_ to satisfy
     * the const reference return type — the cache is overwritten on each
     * call and the reference is only valid until the next call.
     */
    class ExtendedCuts : public CutCandidates {
        public:
            /** @brief Constructs an ExtendedCuts instance with the given patterns and defect map */
            ExtendedCuts(const PurePatterns& patterns_x, const PurePatterns& patterns_y, const DefectMap& defect_map);

            const std::vector<int>& x_cuts(int x, int y, int w, int h) const override;
            const std::vector<int>& y_cuts(int x, int y, int w, int h) const override;

        private:
            const PurePatterns& patterns_x_;
            const PurePatterns& patterns_y_;
            const DefectMap& defect_map_;

            // mutable means it can be modified even in const methods
            mutable std::vector<int> x_cuts_cache_;
            mutable std::vector<int> y_cuts_cache_;
    };
}