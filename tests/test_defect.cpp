#include "catch2.hpp"
#include "gdcut/defect.hpp"

using namespace gdcut;

TEST_CASE("DefectMap with no defects reports all rectangles as defect-free", "[DefectMap]") {
    int sheet_width = 5;
    int sheet_height = 5;
    std::vector<Defect> defects; // empty list of defects
    DefectMap defect_map(sheet_width, sheet_height, defects);
    // All rectangles should be defect-free
    for (int x = 0; x < sheet_width; ++x)
        for (int y = 0; y < sheet_height; ++y)
            for (int w = 1; w <= sheet_width - x; ++w)
                for (int h = 1; h <= sheet_height - y; ++h)
                    REQUIRE(defect_map.is_defect_free_rectangle(x, y, w, h));
}

TEST_CASE("DefectMap with a single defect correctly identifies defective rectangles", "[DefectMap]") {
    int sheet_width = 5;
    int sheet_height = 5;
    std::vector<Defect> defects = { {1, 1, 2, 2} }; // A single defect covering cells (1,1), (1,2), (2,1), (2,2)
    DefectMap defect_map(sheet_width, sheet_height, defects);
    // Rectangles that include any of the defective cells should not be defect-free
    for (int x = 0; x < sheet_width; ++x)
        for (int y = 0; y < sheet_height; ++y)
            for (int w = 1; w <= sheet_width - x; ++w)
                for (int h = 1; h <= sheet_height - y; ++h) {
                    bool should_be_defective = (x < 3 && x + w > 1) && (y < 3 && y + h > 1); // overlaps the defect
                    if (should_be_defective)
                        REQUIRE_FALSE(defect_map.is_defect_free_rectangle(x, y, w, h));
                    else
                        REQUIRE(defect_map.is_defect_free_rectangle(x, y, w, h));
                }
}

TEST_CASE("count_defective_cells_in_rectangle returns correct counts", "[DefectMap]") {
    int sheet_width = 5;
    int sheet_height = 5;
    std::vector<Defect> defects = { {1, 1, 2, 2} }; // A single defect covering cells (1,1), (1,2), (2,1), (2,2)
    DefectMap defect_map(sheet_width, sheet_height, defects);
    // Test specific rectangles for correct defective cell counts
    REQUIRE(defect_map.count_defective_cells_in_rectangle(0, 0, 5, 5) == 4); // entire sheet
    REQUIRE(defect_map.count_defective_cells_in_rectangle(1, 1, 3, 3) == 4); // rectangle covering the defect
    REQUIRE(defect_map.count_defective_cells_in_rectangle(0, 0, 1, 1) == 0); // top-left corner
    REQUIRE(defect_map.count_defective_cells_in_rectangle(3, 3, 5, 5) == 0); // bottom-right corner
}

TEST_CASE("DefectMap correctly handles defects on and beyond sheet boundaries", "[DefectMap]") {
    int sheet_width = 5;
    int sheet_height = 5;
    std::vector<Defect> defects = { {4, 4, 2, 2} }; // Defect partially outside the sheet
    DefectMap defect_map(sheet_width, sheet_height, defects);
    // The defect should only affect the cell (4,4)
    REQUIRE(defect_map.count_defective_cells_in_rectangle(0, 0, 5, 5) == 1); // entire sheet
    REQUIRE_FALSE(defect_map.is_defect_free_rectangle(4, 4, 1, 1)); // cell (4,4) is defective
    REQUIRE(defect_map.is_defect_free_rectangle(3, 3, 1, 1)); // cell (3,3) is clean
}

TEST_CASE("Defect extending beyond the sheet is safely clipped", "[DefectMap]") {
    int sheet_width = 5;
    int sheet_height = 5;
    std::vector<Defect> defects = { {3, 3, 3, 3} }; // Defect extends beyond the sheet boundaries
    DefectMap defect_map(sheet_width, sheet_height, defects);
    // The defect should only affect cells (3,3), (3,4), (4,3), (4,4)
    REQUIRE(defect_map.count_defective_cells_in_rectangle(0, 0, 5, 5) == 4); // entire sheet
    REQUIRE_FALSE(defect_map.is_defect_free_rectangle(3, 3, 2, 2)); // covers the defective area
    REQUIRE(defect_map.is_defect_free_rectangle(0, 0, 3, 3)); // top-left corner is clean
}