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
        , solved_(false) {}

    int32_t Solver::solve() {
        if (problem_.has_defects())
            throw std::runtime_error("Defect sheets not yet supported — DefectSlab not implemented.");
        solved_ = true;
        return pure_table_.value(problem_.sheet_width(), problem_.sheet_height());
    }

    CutSequence Solver::reconstruct() const {
        if (!solved_) {
            throw std::runtime_error("Cannot reconstruct cut sequence before solving the problem.");
        }
        return reconstruct_pure(problem_.sheet_width(), problem_.sheet_height());
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
}