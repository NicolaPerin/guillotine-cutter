#pragma once

#include "gdcut/decision.hpp"
#include <cstdint>
#include <memory>

namespace gdcut {

    struct CutNode;  // forward declaration
    using CutSequence = std::unique_ptr<CutNode>;  // now CutSequence is defined

    /** @brief A node in the cut tree. */
    struct CutNode {
        Decision decision; // the cut decision that led to this node
        int32_t parameter; // cut position for cut nodes, or item index for leaf nodes

        std::unique_ptr<CutNode> left;
        std::unique_ptr<CutNode> right;

        CutNode(Decision decision, int32_t parameter, CutSequence left, CutSequence right)
            : decision(decision), parameter(parameter), left(std::move(left)), right(std::move(right)) {}

        /** @brief Creates an empty cut sequence. */
        static CutSequence make_empty();

        /** @brief Creates a fill node in the cut sequence. */
        static CutSequence make_fill(int32_t item_index);

        /** @brief Creates a horizontal cut node in the cut sequence. */
        static CutSequence make_cut_x(int32_t position, CutSequence left, CutSequence right);

        /** @brief Creates a vertical cut node in the cut sequence. */
        static CutSequence make_cut_y(int32_t position, CutSequence left, CutSequence right);

        /** @brief Creates a defect node in the cut sequence. */
        static CutSequence make_defect();
    };
}