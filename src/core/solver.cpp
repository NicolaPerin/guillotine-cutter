#include "gdcut/solver.hpp"
#include <stdexcept>

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
        , solved_(false) {    
            if (problem.has_defects())
                defect_table_.emplace(problem, pure_table_, defect_map_);
        }

    int32_t Solver::solve() {
        solved_ = true;
        if (!problem_.has_defects())
            return pure_table_.value(problem_.sheet_width(),
                                    problem_.sheet_height());
        if (defect_table_->overflow())
            throw std::runtime_error(
                "DefectTable overflow: delta exceeded uint16_t range. "
                "This problem likely belongs to the sparse-raster regime "
                "and should be solved by the recursive solver.");
        return defect_table_->value(problem_.sheet_width(),
                                    problem_.sheet_height(),
                                    0, 0);
    }

    CutSequence Solver::reconstruct() const {
        if (!solved_) {
            throw std::runtime_error("Cannot reconstruct cut sequence before solving the problem.");
        }
        if (!problem_.has_defects()) {
            return reconstruct_pure(problem_.sheet_width(), problem_.sheet_height());
        }
        return reconstruct_defect(0, 0, problem_.sheet_width(), problem_.sheet_height());
    }

    CutSequence Solver::reconstruct_pure(int w, int h) const {
        Decision decision = pure_table_.decision(w, h);
        switch (decision) {
            case Decision::Empty:
                return CutNode::make_empty();
            case Decision::Fill: {
                int32_t item_index = pure_table_.best_item(w, h);
                return CutNode::make_fill(item_index);
            }
            case Decision::CutX: {
                int32_t cut_pos = pure_table_.cut_position(w, h);
                auto left = reconstruct_pure(cut_pos, h);
                auto right = reconstruct_pure(w - cut_pos, h);
                return CutNode::make_cut_x(cut_pos, std::move(left), std::move(right));
            }
            case Decision::CutY: {
                int32_t cut_pos = pure_table_.cut_position(w, h);
                auto top = reconstruct_pure(w, cut_pos);
                auto bottom = reconstruct_pure(w, h - cut_pos);
                return CutNode::make_cut_y(cut_pos, std::move(top), std::move(bottom));
            }
            default:
                throw std::runtime_error("Invalid decision in pure table reconstruction.");
        }
    }

    CutSequence Solver::reconstruct_defect(int x, int y, int w, int h) const {

        if (w <= 0 || h <= 0) return CutNode::make_empty();

        const int32_t target_val = defect_table_->value(w, h, x, y);

        // pure sub-rectangle — delegate to pure reconstruction which uses
        // the decision table directly rather than searching all cuts
        if (defect_map_.is_defect_free_rectangle(x, y, w, h)) return reconstruct_pure(w, h);

        // rectangle is entirely blocked by defects — no items can be placed
        if (target_val == 0) return CutNode::make_defect();

        // find the vertical cut that achieves target_val by checking which
        // split produces left + right == target_val
        for (int z = 1; z < w; ++z) {
            if (defect_table_->value(z,     h, x,     y) +
                defect_table_->value(w - z, h, x + z, y) == target_val)
                return CutNode::make_cut_x(z,
                    reconstruct_defect(x,     y, z,     h),
                    reconstruct_defect(x + z, y, w - z, h));
        }

        // find the horizontal cut that achieves target_val
        for (int z = 1; z < h; ++z) {
            if (defect_table_->value(w, z,     x, y) +
                defect_table_->value(w, h - z, x, y + z) == target_val)
                return CutNode::make_cut_y(z,
                    reconstruct_defect(x, y,     w, z),
                    reconstruct_defect(x, y + z, w, h - z));
        }

        // should never reach here if fill_deltas() is correct —
        // every non-zero value must be achievable by some cut
        return CutNode::make_defect();
    }
}