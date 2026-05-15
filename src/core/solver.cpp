#include "gdcut/solver.hpp"
#include <stdexcept>
#include <algorithm>

namespace gdcut {

Solver::Solver(const Problem& problem)
    : problem_(problem)
    , patterns_x_(problem.item_widths(),  problem.sheet_width())
    , patterns_y_(problem.item_heights(), problem.sheet_height())
    , pure_table_(problem.sheet_width(),  problem.sheet_height(),
                  problem.item_widths(),  problem.item_heights(),
                  problem.item_profits(), patterns_x_, patterns_y_)
    , defect_map_(problem.sheet_width(),  problem.sheet_height(),
                  problem.defects())
    , backend_(Backend::Pure)
    , solved_(false)
{}

double Solver::raster_density() const {
    const int W = problem_.sheet_width();
    const int H = problem_.sheet_height();
    const int rx = (int)patterns_x_.raster_points(W).size();
    const int ry = (int)patterns_y_.raster_points(H).size();
    return (double)(rx * ry) / (double)(W * H);
}

int32_t Solver::solve(Mode mode, double sparse_threshold) {
    solved_ = true;
    const int W = problem_.sheet_width();
    const int H = problem_.sheet_height();

    if (!problem_.has_defects()) {
        backend_ = Backend::Pure;
        return pure_table_.value(W, H);
    }

    const double density = raster_density();
    const bool use_recursive = mode == Mode::Recursive || (mode == Mode::Auto && density < sparse_threshold);

    if (!use_recursive) {
        defect_table_.emplace(problem_, pure_table_, defect_map_);
        if (!defect_table_->overflow()) {
            backend_ = Backend::Iterative;
            return defect_table_->value(W, H, 0, 0);
        }
        if (mode == Mode::Iterative)
            throw std::runtime_error(
                "DefectTable overflow: problem exceeds iterative solver capacity. "
                "Use --solver recursive or --solver auto.");
        defect_table_.reset();
    }

    backend_ = Backend::Recursive;
    recursive_solver_.emplace(problem_, pure_table_, defect_map_,
                               patterns_x_, patterns_y_);
    return recursive_solver_->solve();
}

CutSequence Solver::reconstruct() const {
    if (!solved_)
        throw std::runtime_error("reconstruct() called before solve()");

    switch (backend_) {
        case Backend::Pure:
            return reconstruct_pure(problem_.sheet_width(), problem_.sheet_height());
        case Backend::Iterative:
            return reconstruct_defect(0, 0, problem_.sheet_width(), problem_.sheet_height());
        case Backend::Recursive:
            return recursive_solver_->reconstruct();
    }
    throw std::runtime_error("unknown backend");
}

CutSequence Solver::reconstruct_pure(int w, int h) const {
    switch (pure_table_.decision(w, h)) {
        case Decision::Empty:  return CutNode::make_empty();
        case Decision::Fill:   return CutNode::make_fill(pure_table_.best_item(w, h));
        case Decision::CutX: {
            int32_t z = pure_table_.cut_position(w, h);
            return CutNode::make_cut_x(z,
                reconstruct_pure(z,     h),
                reconstruct_pure(w - z, h));
        }
        case Decision::CutY: {
            int32_t z = pure_table_.cut_position(w, h);
            return CutNode::make_cut_y(z,
                reconstruct_pure(w, z),
                reconstruct_pure(w, h - z));
        }
        default:
            throw std::runtime_error("invalid decision in pure reconstruction");
    }
}

CutSequence Solver::reconstruct_defect(int x, int y, int w, int h) const {
    if (w <= 0 || h <= 0) return CutNode::make_empty();

    const int32_t target_val = defect_table_->value(w, h, x, y);

    if (defect_map_.is_defect_free_rectangle(x, y, w, h))
        return reconstruct_pure(w, h);

    if (target_val == 0) return CutNode::make_defect();

    for (int z = 1; z < w; ++z)
        if (defect_table_->value(z,     h, x,     y) +
            defect_table_->value(w - z, h, x + z, y) == target_val)
            return CutNode::make_cut_x(z,
                reconstruct_defect(x,     y, z,     h),
                reconstruct_defect(x + z, y, w - z, h));

    for (int z = 1; z < h; ++z)
        if (defect_table_->value(w, z,     x, y) +
            defect_table_->value(w, h - z, x, y + z) == target_val)
            return CutNode::make_cut_y(z,
                reconstruct_defect(x, y,     w, z),
                reconstruct_defect(x, y + z, w, h - z));

    return CutNode::make_defect();
}

} // namespace gdcut