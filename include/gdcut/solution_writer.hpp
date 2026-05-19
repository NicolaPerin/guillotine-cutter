#pragma once

#include "gdcut/problem.hpp"
#include "gdcut/cut_node.hpp"
#include "gdcut/solver.hpp"
#include <cstdint>
#include <string>

namespace gdcut {

void write_solution(const std::string& output_path,
                    const Problem&     problem,
                    int32_t            value,
                    const CutSequence& sequence,
                    Solver::Backend    backend   = Solver::Backend::Pure,
                    double             elapsed_s = 0.0);

} // namespace gdcut