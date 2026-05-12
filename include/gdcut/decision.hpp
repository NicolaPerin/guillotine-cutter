#pragma once

#include <cstdint>

namespace gdcut {
    /** @brief Enum class representing different decisions in the guillotine cutting algorithm. */
    enum class Decision : int8_t {
        Empty  = 0,
        Fill   = 1,
        CutX   = 2,
        CutY   = 3,
        Defect = 4,
        Pure   = 5,
    };
}
