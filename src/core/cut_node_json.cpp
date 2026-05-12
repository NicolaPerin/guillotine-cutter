#include "gdcut/cut_node_json.hpp"

namespace gdcut {

nlohmann::json cut_sequence_to_json(const CutSequence& seq) {
    if (!seq)
        return {{"type", "empty"}};

    switch (seq->decision) {
        case Decision::Empty:
            return {{"type", "empty"}};
        case Decision::Fill:
            return {{"type", "fill"}, {"item", seq->parameter}};
        case Decision::CutX:
            return {{"type", "cut_x"},
                    {"position", seq->parameter},
                    {"left",  cut_sequence_to_json(seq->left)},
                    {"right", cut_sequence_to_json(seq->right)}};
        case Decision::CutY:
            return {{"type", "cut_y"},
                    {"position", seq->parameter},
                    {"left",  cut_sequence_to_json(seq->left)},
                    {"right", cut_sequence_to_json(seq->right)}};
        case Decision::Defect:
            return {{"type", "defect"}};
        default:
            return {{"type", "empty"}};
    }
}

} // namespace gdcut
