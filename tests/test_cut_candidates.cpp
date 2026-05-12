#include "catch2.hpp"
#include "gdcut/cut_candidates.hpp"
#include "gdcut/patterns.hpp"
#include <iostream>

using namespace gdcut;

TEST_CASE("PureCuts: delegates to PurePatterns raster points", "[PureCuts]") {
    std::vector<int> sizes = {5, 10, 12, 15};
    PurePatterns px(sizes, 27);
    PurePatterns py(sizes, 27);
    PureCuts cuts(px, py);

    // should match raster_points directly
    REQUIRE(cuts.x_cuts(0, 0, 27, 27) == px.raster_points(27));
    REQUIRE(cuts.y_cuts(0, 0, 27, 27) == py.raster_points(27));
}

TEST_CASE("ExtendedCuts: no defects matches PureCuts", "[ExtendedCuts]") {
    std::vector<int> sizes = {5, 10, 12, 15};
    PurePatterns px(sizes, 27);
    PurePatterns py(sizes, 27);
    DefectMap defect_map(27, 27, {});  // no defects

    PureCuts    pure(px, py);
    ExtendedCuts ext(px, py, defect_map);

    // for a pure sheet, extended cuts should equal pure raster points
    for (int w = 1; w <= 27; ++w) {
        REQUIRE(ext.x_cuts(0, 0, w, 27) == pure.x_cuts(0, 0, w, 27));
        REQUIRE(ext.y_cuts(0, 0, 27, w) == pure.y_cuts(0, 0, 27, w));
    }
}

TEST_CASE("ExtendedCuts: defect adds cut positions", "[ExtendedCuts]") {
    std::vector<int> sizes = {5, 10, 12, 15};
    PurePatterns px(sizes, 27);
    PurePatterns py(sizes, 27);
    DefectMap defect_map(27, 27, {{9, 9, 2, 2}});

    PureCuts     pure(px, py);
    ExtendedCuts ext(px, py, defect_map);

    // extended cuts for the full sheet should have >= as many positions
    // as pure cuts — defects can only add positions, never remove them
    REQUIRE(ext.x_cuts(0, 0, 27, 27).size() >=
            pure.x_cuts(0, 0, 27, 27).size());
}

TEST_CASE("ExtendedCuts: all positions within bounds", "[ExtendedCuts]") {
    std::vector<int> sizes = {5, 10, 12, 15};
    PurePatterns px(sizes, 27);
    PurePatterns py(sizes, 27);
    DefectMap defect_map(27, 27, {{9, 9, 2, 2}});
    ExtendedCuts ext(px, py, defect_map);

    for (int w = 1; w <= 27; ++w)
        for (int cut : ext.x_cuts(0, 0, w, 27)) {
            REQUIRE(cut >= 1);
            REQUIRE(cut < w);
        }

    for (int h = 1; h <= 27; ++h)
        for (int cut : ext.y_cuts(0, 0, 27, h)) {
            REQUIRE(cut >= 1);
            REQUIRE(cut < h);
        }
}

TEST_CASE("ExtendedCuts: results are sorted", "[ExtendedCuts]") {
    std::vector<int> sizes = {5, 10, 12, 15};
    PurePatterns px(sizes, 27);
    PurePatterns py(sizes, 27);
    DefectMap defect_map(27, 27, {{9, 9, 2, 2}});
    ExtendedCuts ext(px, py, defect_map);

    auto& xc = ext.x_cuts(0, 0, 27, 27);
    REQUIRE(std::is_sorted(xc.begin(), xc.end()));

    auto& yc = ext.y_cuts(0, 0, 27, 27);
    REQUIRE(std::is_sorted(yc.begin(), yc.end()));
}