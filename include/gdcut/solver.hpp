#pragma once

#include "gdcut/problem.hpp"
#include "gdcut/patterns.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/cut_node.hpp"
#include "gdcut/defect_table.hpp"
#include "gdcut/recursive_solver.hpp"
#include <cstdint>
#include <optional>

namespace gdcut {

/**
 * @brief Guillotine cutting solver with automatic backend dispatch.
 *
 * Pure sheets use PureTable directly. Defect-affected sheets dispatch
 * based on raster density = (|RP_x(W)| × |RP_y(H)|) / (W × H):
 *   - density <  SPARSE_THRESHOLD  →  RecursiveSolver (top-down, DPR)
 *   - density >= SPARSE_THRESHOLD  →  DefectTable     (bottom-up)
 * DefectTable overflow also falls back to RecursiveSolver.
 */
class Solver {
public:
    /** @brief Default raster density threshold for auto dispatch. */
    static constexpr double SPARSE_THRESHOLD = 0.5;

    /** @brief Which backend was selected by the last solve() call. */
    enum class Backend { Pure, Iterative, Recursive };

    /** @brief Backend selection strategy passed to solve(). */
    enum class Mode    { Auto, Iterative, Recursive };

    /** @brief Constructs the solver. PureTable and DefectMap are built
     *         eagerly; DefectTable and RecursiveSolver are lazy. */
    explicit Solver(const Problem& problem);

    /** @brief Computes and returns the optimal cut value.
     *  @param mode             Backend selection (default: Auto).
     *  @param sparse_threshold Density threshold used by Auto (default: SPARSE_THRESHOLD).
     *  @throws std::runtime_error if Mode::Iterative is forced but overflows. */
    int32_t solve(Mode mode = Mode::Auto,
                  double sparse_threshold = SPARSE_THRESHOLD);

    /** @brief Reconstructs the optimal cut sequence. Must be called after solve().
     *  @throws std::runtime_error if called before solve(). */
    CutSequence reconstruct() const;

    /** @brief Returns the backend selected by the last solve() call. */
    Backend backend() const { return backend_; }

private:
    const Problem&             problem_;
    PurePatterns               patterns_x_;
    PurePatterns               patterns_y_;
    PureTable                  pure_table_;
    DefectMap                  defect_map_;
    std::optional<DefectTable>      defect_table_;
    std::optional<RecursiveSolver>  recursive_solver_;
    Backend                    backend_;
    bool                       solved_;

    CutSequence reconstruct_pure(int w, int h) const;
    CutSequence reconstruct_defect(int x, int y, int w, int h) const;

    /** @brief density = (|RP_x(W)| × |RP_y(H)|) / (W × H) */
    double raster_density() const;
};

} // namespace gdcut