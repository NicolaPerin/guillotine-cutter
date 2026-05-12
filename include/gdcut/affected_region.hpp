#pragma once

#include <cstdint>
#include <vector>

namespace gdcut {
    /** 
     * @brief Represents a region where tiles overlap 
     */
    struct OverlapRegion {
        int x_start, x_end, y_start, y_end;
    };

    /** 
    * @brief Represents a region affected by tile placement.
    * 
    * Stores the region's position and dimensions on the sheet, as well as
    * the offset in the flat uint16 delta array where this region's data will be stored.
    */
    struct AffectedRegion {
        // 16-bit integers are sufficient for sheet dimensions up to 32767, which is more than enough for typical use cases
        int16_t sheet_x_start, sheet_y_start, width, height;
        int64_t delta_offset;
    };

    /** 
     * @brief Metadata for affected regions of a specific size (w, h).
     * 
     * Stores the count of regions of this size and the offset in the flat AffectedRegion array
     * where these regions begin. This allows efficient grouping and lookup of regions by size.
     */
    struct AffectedRegionIndex {
        int region_count;
        int regions_offset; // index into the flat AffectedRegion array where this (w,h)'s regions begin
    };
}