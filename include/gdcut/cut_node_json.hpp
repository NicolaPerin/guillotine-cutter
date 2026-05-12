#pragma once
#include "gdcut/cut_node.hpp"
#include "json.hpp"

namespace gdcut {

/**
 * @brief Serializes a CutSequence to a nlohmann::json object.
 *
 * The cut sequence is serialized as a recursive JSON structure:
 *   {"type": "empty"}
 *   {"type": "fill", "item": 0}
 *   {"type": "cut_x", "position": 5, "left": {...}, "right": {...}}
 *   {"type": "cut_y", "position": 3, "left": {...}, "right": {...}}
 */
nlohmann::json cut_sequence_to_json(const CutSequence& seq);

} // namespace gdcut