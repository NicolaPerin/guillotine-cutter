#include "gdcut/recursive_solver.hpp"
#include <stdexcept>
#include <algorithm>

namespace gdcut {

RecursiveSolver::RecursiveSolver(const Problem&      problem,
                                 const PureTable&    pure_table,
                                 const DefectMap&    defect_map,
                                 const PurePatterns& patterns_x,
                                 const PurePatterns& patterns_y)
    : problem_(problem)
    , pure_table_(pure_table)
    , defect_map_(defect_map)
    , cuts_(patterns_x, patterns_y, defect_map)
    , solved_(false)
{
    min_w_ = *std::min_element(problem.item_widths().begin(),  problem.item_widths().end());
    min_h_ = *std::min_element(problem.item_heights().begin(), problem.item_heights().end());
}

uint64_t RecursiveSolver::cont_key(int x, int y, int w, int h) {
    return  (uint64_t)(uint16_t)x
         | ((uint64_t)(uint16_t)y << 16)
         | ((uint64_t)(uint16_t)w << 32)
         | ((uint64_t)(uint16_t)h << 48);
}

int32_t RecursiveSolver::solve() {
    solved_ = true;
    return solve_cont(0, 0, problem_.sheet_width(), problem_.sheet_height());
}

int32_t RecursiveSolver::solve_cont(int x, int y, int w, int h) {
    if (w < min_w_ || h < min_h_) return 0; // Catches both <= 0 and smaller than minimum item
    if (defect_map_.is_defect_free_rectangle(x, y, w, h)) return pure_table_.value(w, h);

    const uint64_t key = cont_key(x, y, w, h);
    auto it = cont_memo_.find(key);
    if (it != cont_memo_.end()) return it->second.value;

    int32_t  best = 0;
    Decision best_dec = Decision::Defect;
    int32_t  best_param = 0;

    const std::vector<int> x_cands = cuts_.x_cuts(x, y, w, h);
    for (int z : x_cands) {
        int32_t v = solve_cont(x,     y, z,     h)
                  + solve_cont(x + z, y, w - z, h);
        if (v > best) { best = v; best_dec = Decision::CutX; best_param = z; }
    }

    const std::vector<int> y_cands = cuts_.y_cuts(x, y, w, h);
    for (int z : y_cands) {
        int32_t v = solve_cont(x, y,     w, z)
                  + solve_cont(x, y + z, w, h - z);
        if (v > best) { best = v; best_dec = Decision::CutY; best_param = z; }
    }

    cont_memo_[key] = {best, best_dec, best_param};
    return best;
}

CutSequence RecursiveSolver::reconstruct() const {
    if (!solved_)
        throw std::runtime_error("RecursiveSolver::reconstruct() called before solve()");
    return reconstruct_cont(0, 0, problem_.sheet_width(), problem_.sheet_height());
}

CutSequence RecursiveSolver::reconstruct_cont(int x, int y, int w, int h) const {
    if (w <= 0 || h <= 0) return CutNode::make_empty();

    if (defect_map_.is_defect_free_rectangle(x, y, w, h))
        return reconstruct_pure(w, h);

    auto it = cont_memo_.find(cont_key(x, y, w, h));
    if (it == cont_memo_.end()) return CutNode::make_defect();

    const ContEntry& e = it->second;
    switch (e.decision) {
        case Decision::Defect:
        case Decision::Empty:
            return CutNode::make_defect();
        case Decision::CutX:
            return CutNode::make_cut_x(e.param,
                reconstruct_cont(x,          y, e.param,     h),
                reconstruct_cont(x + e.param, y, w - e.param, h));
        case Decision::CutY:
            return CutNode::make_cut_y(e.param,
                reconstruct_cont(x, y,           w, e.param),
                reconstruct_cont(x, y + e.param, w, h - e.param));
        default:
            return CutNode::make_defect();
    }
}

CutSequence RecursiveSolver::reconstruct_pure(int w, int h) const {
    const Decision dec = pure_table_.decision(w, h);
    switch (dec) {
        case Decision::Empty:
            return CutNode::make_empty();
        case Decision::Fill:
            return CutNode::make_fill(pure_table_.best_item(w, h));
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
            return CutNode::make_empty();
    }
}

} // namespace gdcut