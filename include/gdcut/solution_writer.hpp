#pragma once
#include "gdcut/problem.hpp"
#include "gdcut/cut_node.hpp"
#include <cstdint>
#include <string>

namespace gdcut {

void write_solution(const std::string& output_path,
                    const Problem& problem,
                    int32_t value,
                    const CutSequence& sequence);

} // namespace gdcut