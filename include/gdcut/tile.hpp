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
    * @brief Represents a tile to be cut from the sheet.
    * 
    * Stores the tile's position and dimensions on the sheet, as well as
    * the offset in the flat uint16 delta array where this tile's data will be stored.
    */
    struct Tile {
        // 16-bit integers are sufficient for sheet dimensions up to 32767, which is more than enough for typical use cases
        int16_t sheet_x_start, sheet_y_start, width, height;
        int64_t data_offset;
    };

    /** 
     * @brief Metadata for tiles of a specific size (w, h).
     * 
     * Stores the count of tiles of this size and the offset in the flat Tile array
     * where these tiles begin. This allows efficient grouping and lookup of tiles by size.
     */
    struct TileIndex {
        int tile_count;
        int tiles_offset; // index into the flat Tile array where this (w,h)'s tiles begin
    };
}