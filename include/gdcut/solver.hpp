#pragma once

#include "gdcut/problem.hpp"
#include "gdcut/patterns.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/cut_node.hpp"
#include <cstdint>

namespace gdcut {

/**
 * @brief Orchestrates the guillotine cutting solver.
 *
 * Currently handles pure sheets only (no defects). DefectSlab support
 * will be added once Phase 3 is implemented.
 *
 * Usage:
 *   Solver solver(problem);
 *   int32_t value = solver.solve();
 *   CutSequence seq = solver.reconstruct();
 */
class Solver {
public:
    /**
     * @brief Constructs the solver from a problem instance.
     *
     * Builds PurePatterns and PureTable eagerly — the problem is ready
     * to solve immediately after construction.
     *
     * @param problem The problem instance to solve
     */
    explicit Solver(const Problem& problem);

    /**
     * @brief Computes and returns the optimal cut value.
     *
     * For pure sheets, delegates to PureTable. For defect-affected sheets,
     * will delegate to DefectSlab once implemented.
     *
     * @return Optimal total profit achievable by guillotine cuts
     */
    int32_t solve();

    /**
     * @brief Reconstructs the optimal cut sequence.
     *
     * Only valid after solve() has been called.
     *
     * @return Root of the cut sequence tree
     * @throws std::runtime_error if called before solve()
     */
    CutSequence reconstruct() const;

private:
    const Problem&  problem_;
    PurePatterns    patterns_x_;
    PurePatterns    patterns_y_;
    PureTable       pure_table_;
    bool            solved_;

    /** @brief Recursively reconstructs the cut sequence for a pure rectangle. */
    CutSequence reconstruct_pure(int w, int h) const;
};

} // namespace gdcut