#pragma once

#include "gdcut/problem.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/cut_node.hpp"
#include "gdcut/cut_candidates.hpp"
#include "gdcut/defect.hpp"
#include <cstdint>
#include <unordered_map>

namespace gdcut {

/**
 * @brief Top-down memoized recursive solver for defect-affected sheets.
 *
 * Uses extended raster points (DPR) as cut candidates — correct and optimal
 * per Zhang et al. Efficient only on sparse instances (few defects relative
 * to sheet size). For dense instances use DefectTable (iterative).
 *
 * Pure sub-sheets are immediately delegated to PureTable without memoisation.
 */
class RecursiveSolver {
public:
    RecursiveSolver(const Problem&     problem,
                    const PureTable&   pure_table,
                    const DefectMap&   defect_map,
                    const PurePatterns& patterns_x,
                    const PurePatterns& patterns_y);

    /** @brief Computes and returns the optimal value. */
    int32_t solve();

    /** @brief Reconstructs the cut sequence. Only valid after solve(). */
    CutSequence reconstruct() const;

    /** @brief Number of contaminated states memoised — useful for diagnostics. */
    size_t memo_size() const { return cont_memo_.size(); }

private:
    const Problem&   problem_;
    const PureTable& pure_table_;
    const DefectMap& defect_map_;
    ExtendedCuts     cuts_;
    bool             solved_;
    int              min_w_;
    int              min_h_;

    struct ContEntry {
        int32_t  value;
        Decision decision;
        int32_t  param;   // cut position for CutX/CutY, 0 otherwise
    };

    std::unordered_map<uint64_t, ContEntry> cont_memo_;

    static uint64_t cont_key(int x, int y, int w, int h);

    int32_t    solve_cont(int x, int y, int w, int h);
    CutSequence reconstruct_cont(int x, int y, int w, int h) const;
    CutSequence reconstruct_pure(int w, int h) const;
};

} // namespace gdcut