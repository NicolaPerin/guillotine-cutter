#include "gdcut/cut_node.hpp"

namespace gdcut {

    CutSequence CutNode::make_empty() {
        return std::make_unique<CutNode>(Decision::Empty, 0, nullptr, nullptr);
    }

    CutSequence CutNode::make_fill(int32_t item_index) {
        return std::make_unique<CutNode>(Decision::Fill, item_index, nullptr, nullptr);
    }

    CutSequence CutNode::make_cut_x(int32_t position, CutSequence left, CutSequence right) {
        return std::make_unique<CutNode>(Decision::CutX, position, std::move(left), std::move(right));
    }

    CutSequence CutNode::make_cut_y(int32_t position, CutSequence left, CutSequence right) {
        return std::make_unique<CutNode>(Decision::CutY, position, std::move(left), std::move(right));
    }
}