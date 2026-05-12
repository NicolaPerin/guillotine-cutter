#pragma once

#include <cstdint>
#include <vector>

namespace gdcut {

    /**
     * @brief A single rectangular defect on the sheet.
     *
     * Stored as origin (x, y) and dimensions (w, h).
     * Convenience methods x_end() and y_end() return the exclusive
     * right and bottom edges of the defect rectangle.
     */
    struct Defect {
        int x, y, w, h;

        /** @brief Exclusive right edge of the defect: x + w. */
        int x_end() const { return x + w; }

        /** @brief Exclusive bottom edge of the defect: y + h. */
        int y_end() const { return y + h; }
    };

    /**
     * @brief Precomputed 2D cumulative defective cell count table.
     *
     * Built once from a list of defects at construction time. Allows
     * O(1) rectangle queries during the DP instead of O(w*h) per query.
     *
     * Internally stores a 2D prefix sum of dimensions
     * (sheet_width+1) x (sheet_height+1), where each cell [x][y] holds
     * the count of defective cells in [0,x) x [0,y). The extra row and
     * column of zeros at index 0 eliminate boundary checks from the
     * inclusion-exclusion query formula.
     */
    class DefectMap {
    public:
        /**
         * @brief Constructs the DefectMap and builds the cumulative table.
         *
         * @param sheet_width   width of the sheet in cells
         * @param sheet_height  height of the sheet in cells
         * @param defects       list of defects on the sheet
         */
        DefectMap(int sheet_width, int sheet_height, const std::vector<Defect>& defects);

        /**
         * @brief Counts defective cells in the rectangle [x_start, x_end) x [y_start, y_end).
         *
         * Uses the inclusion-exclusion formula on the cumulative table:
         *   result = bottom_right - top_right - bottom_left + top_left
         *
         * where each term is a single lookup into cumul_defective_cells_table_.
         * top_right and bottom_left overlap in the top_left region, so top_left
         * is added back once to correct the double subtraction.
         *
         * @param x_start  inclusive left edge
         * @param y_start  inclusive top edge
         * @param x_end    exclusive right edge
         * @param y_end    exclusive bottom edge
         * @return number of defective cells in the rectangle
         */
        int count_defective_cells_in_rectangle(int x_start, int y_start,
                                            int x_end,   int y_end) const;

        /**
         * @brief Returns true if no defective cells overlap the rectangle.
         *
         * @param x  left edge of the rectangle
         * @param y  top edge of the rectangle
         * @param w  width of the rectangle
         * @param h  height of the rectangle
         * @return true if the rectangle contains no defective cells
         */
        bool is_defect_free_rectangle(int x, int y, int w, int h) const;

        /** @brief Width of the sheet in cells. */
        int sheet_width() const;

        /** @brief Height of the sheet in cells. */
        int sheet_height() const;

        /**
         * @brief Raw pointer to the cumulative defective cell count table.
         *
         * Exposed for use by the CPU and GPU fill routines in DefectSlab.
         * Layout is column-major with stride = sheet_height_ + 1.
         */
        const int32_t* cumul_defective_cells_table() const;

        /**
         * @brief Stride of the cumulative defective cell count table.
         *
         * Equal to sheet_height_ + 1. Used to index into the flat array
         * as table[x * stride + y].
         */
        int cumul_defective_cells_table_stride() const;

        /** @brief List of defects on the sheet. */
        const std::vector<Defect>& defects() const;

    private:
        int sheet_width_, sheet_height_;

        std::vector<Defect>  defects_;                    // kept for ExtendedCuts queries
        std::vector<int32_t> cumul_defective_cells_table_; // precomputed prefix sum
    };

} // namespace gdcut