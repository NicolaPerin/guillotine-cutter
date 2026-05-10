#include "gdcut/defect.hpp"
#include <vector>

namespace gdcut {

    DefectMap::DefectMap(int sheet_width, int sheet_height, const std::vector<Defect>& defects) 
        : sheet_width_(sheet_width), sheet_height_(sheet_height) {
        
        /* Step 1: Build a flat grid marking each cell as defective (1) or clean (0).
        // Layout is column-major: index = cell_x * sheet_height_ + cell_y,
        // so consecutive cell_y values are contiguous in memory (cache-friendly inner loop).*/
        std::vector<int32_t> defective_cells(sheet_width_ * sheet_height_, 0);

        for (const auto& defect : defects) // const auto& to avoid unnecessary copying of Defect objects, this is a read-only loop
            for (int cell_x = defect.x; (cell_x < defect.x_end()) && (cell_x < sheet_width_); ++cell_x)
                for (int cell_y = defect.y; (cell_y < defect.y_end()) && (cell_y < sheet_height_); ++cell_y)
                    defective_cells[cell_x * sheet_height_ + cell_y] = 1;

        // Step 2: Build a 2D prefix-sum table of defective-cell counts.
        //
        // cumul_defective_cells_table_[x][y] stores the number of defective cells
        // inside the rectangle [0, x) x [0, y), measured from the sheet origin.
        //
        // The table dimensions are (sheet_width_ + 1) x (sheet_height_ + 1).
        // The extra row and column at index 0 are initialized to zero, which
        // eliminates boundary checks when applying the inclusion-exclusion formula.
        // As a result, every rectangle query uses the same fixed four-lookup formula.
        //
        // The table is computed once during construction so that later rectangle
        // queries cost in O(1) time (four lookups and three additions) instead of O(w * h)

        cumul_defective_cells_table_.resize((sheet_width_ + 1) * (sheet_height_ + 1)); // the +1 is for the extra row and column of zeros at the beginning
        for (int x = 1; x <= sheet_width_; ++x)
            for (int y = 1; y <= sheet_height_; ++y)
                cumul_defective_cells_table_[x * (sheet_height_ + 1) + y] = defective_cells[(x - 1) * sheet_height_ + (y - 1)] 
                    + cumul_defective_cells_table_[(x - 1) * (sheet_height_ + 1) + y] 
                    + cumul_defective_cells_table_[x * (sheet_height_ + 1) + (y - 1)] 
                    - cumul_defective_cells_table_[(x - 1) * (sheet_height_ + 1) + (y - 1)];
    }

    int DefectMap::sheet_width() const { return sheet_width_; }

    int DefectMap::sheet_height() const { return sheet_height_; }

    int DefectMap::cumul_defective_cells_table_stride() const { return sheet_height_ + 1; }

    const int32_t* DefectMap::cumul_defective_cells_table() const { return cumul_defective_cells_table_.data(); }

    // Count defective cells in the rectangle [x_start, x_end) x [y_start, y_end)
    // using the inclusion-exclusion formula on the cumulative table.
    //
    // The rectangle of interest always sits at the bottom-right of the four lookups (origin is top-left). Visually:
    //
    //   (0,0)
    //        +--------------------+
    //        |top_left   top_right|
    //        |                    |
    //        |bottom_left   [????]| <- region we want
    //        +--------------------+ (x_end, y_end)
    //
    //   result = bottom_right - top_right - bottom_left + top_left
    //
    // top_right and bottom_left overlap in the top_left region, so we add it
    // back once to correct the double subtraction.
    int DefectMap::count_defective_cells_in_rectangle(int x_start, int y_start, int x_end, int y_end) const {
        int stride       = cumul_defective_cells_table_stride();
        int top_left     = cumul_defective_cells_table_[x_start * stride + y_start];
        int top_right    = cumul_defective_cells_table_[x_end * stride + y_start];
        int bottom_left  = cumul_defective_cells_table_[x_start * stride + y_end];
        int bottom_right = cumul_defective_cells_table_[x_end * stride + y_end];

        return bottom_right - top_right - bottom_left + top_left;
    }

    bool DefectMap::is_defect_free_rectangle(int x, int y, int w, int h) const {
        return count_defective_cells_in_rectangle(x, y, x + w, y + h) == 0;
    }
}
