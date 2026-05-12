#include "catch2.hpp"
#include "gdcut/defect_table.hpp"
#include "gdcut/defect.hpp"
#include "gdcut/problem.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/patterns.hpp"
using namespace gdcut;

// =============================================================================
// Helper
// =============================================================================

struct Fixture {
    Problem      problem;
    DefectMap    defect_map;
    PurePatterns px;
    PurePatterns py;
    PureTable    pure_table;
    DefectTable  defect_table;

    Fixture(int W, int H,
            const std::vector<int>& iw,
            const std::vector<int>& ih,
            const std::vector<Defect>& defects)
        : problem(W, H, iw, ih, profits(iw, ih), defects)
        , defect_map(W, H, defects)
        , px(iw, W)
        , py(ih, H)
        , pure_table(W, H, iw, ih, profits(iw, ih), px, py)
        , defect_table(problem, pure_table, defect_map)
    {}

    static std::vector<int> profits(const std::vector<int>& iw,
                                     const std::vector<int>& ih) {
        std::vector<int> p;
        for (int i = 0; i < (int)iw.size(); ++i)
            p.push_back(iw[i] * ih[i]);
        return p;
    }
};

// =============================================================================
// Pure sheet — no defects
// =============================================================================

TEST_CASE("DefectTable: no defects — no affected positions", "[DefectTable]") {
    Fixture f(10, 10, {3}, {3}, {});
    for (int w = 1; w <= 10; ++w)
        for (int h = 1; h <= 10; ++h)
            REQUIRE_FALSE(f.defect_table.has_affected_positions(w, h));
}

TEST_CASE("DefectTable: no defects — value equals pure value everywhere", "[DefectTable]") {
    Fixture f(10, 10, {3}, {3}, {});
    for (int w = 1; w <= 10; ++w)
        for (int h = 1; h <= 10; ++h)
            for (int sx = 0; sx <= 10 - w; ++sx)
                for (int sy = 0; sy <= 10 - h; ++sy)
                    REQUIRE(f.defect_table.value(w, h, sx, sy) ==
                            f.pure_table.value(w, h));
}

TEST_CASE("DefectTable: no defects — overflow flag false", "[DefectTable]") {
    Fixture f(10, 10, {3}, {3}, {});
    REQUIRE_FALSE(f.defect_table.overflow());
}

// =============================================================================
// Single defect — geometry
// =============================================================================

TEST_CASE("DefectTable: single defect — affected positions identified", "[DefectTable]") {
    // defect at (3,3,2,2) on 10x10 sheet, single 2x2 item
    // a 2x2 rectangle placed at (sx,sy) overlaps the defect if
    // sx in [2,4] and sy in [2,4]
    Fixture f(10, 10, {2}, {2}, {{3, 3, 2, 2}});
    REQUIRE(f.defect_table.has_affected_positions(2, 2));
}

TEST_CASE("DefectTable: single defect — pure positions unaffected", "[DefectTable]") {
    // a 2x2 item placed at (0,0) does not overlap defect at (3,3,2,2)
    Fixture f(10, 10, {2}, {2}, {{3, 3, 2, 2}});
    REQUIRE(f.defect_table.value(2, 2, 0, 0) == f.pure_table.value(2, 2));
    REQUIRE(f.defect_table.value(2, 2, 8, 8) == f.pure_table.value(2, 2));
}

TEST_CASE("DefectTable: single defect — impure value <= pure value", "[DefectTable]") {
    // for any position overlapping a defect, value must be <= pure value
    Fixture f(27, 27, {5, 10, 12, 15}, {5, 10, 12, 15}, {{9, 9, 2, 2}});
    for (int w = 1; w <= 27; ++w) {
        for (int h = 1; h <= 27; ++h) {
            if (!f.defect_table.has_affected_positions(w, h)) continue;
            int32_t pure_val = f.pure_table.value(w, h);
            for (int sx = 0; sx <= 27 - w; ++sx)
                for (int sy = 0; sy <= 27 - h; ++sy)
                    REQUIRE(f.defect_table.value(w, h, sx, sy) <= pure_val);
        }
    }
}

// =============================================================================
// Value correctness
// =============================================================================

TEST_CASE("DefectTable: pure position inside affected region equals pure value", "[DefectTable]") {
    // defect at (5,5,2,2), item 3x3
    // position (0,0) is inside the affected region for w=3,h=3
    // but placing 3x3 at (0,0) does not overlap the defect
    Fixture f(10, 10, {3}, {3}, {{5, 5, 2, 2}});
    int32_t v = f.defect_table.value(3, 3, 0, 0);
    REQUIRE(v == f.pure_table.value(3, 3));
}

TEST_CASE("DefectTable: fully blocked rectangle has value zero", "[DefectTable]") {
    // 2x2 item, defect covers entire 2x2 region at (1,1)
    // placing 2x2 at (1,1) is fully blocked — value should be 0
    Fixture f(5, 5, {2}, {2}, {{1, 1, 2, 2}});
    REQUIRE(f.defect_table.value(2, 2, 1, 1) == 0);
}

// =============================================================================
// Paper Example 1 — defect sheet
// =============================================================================

TEST_CASE("DefectTable: paper Example 1 full sheet value is 644", "[DefectTable]") {
    // W0=H0=27, items 5x5, 10x10, 12x12, 15x15
    // single defect at (9,9,2,2)
    // optimal value from Table 8 DPR: 644 * 100 = not directly but
    // from paper the value for this instance is known
    Fixture f(27, 27,
              {5, 10, 12, 15},
              {5, 10, 12, 15},
              {{9, 9, 2, 2}});

    int32_t val = f.defect_table.value(27, 27, 0, 0);
    REQUIRE(val == 644);
}

TEST_CASE("DefectTable: paper Example 1 value <= pure sheet value", "[DefectTable]") {
    Fixture f_defect(27, 27,
                     {5, 10, 12, 15},
                     {5, 10, 12, 15},
                     {{9, 9, 2, 2}});

    Fixture f_pure(27, 27,
                   {5, 10, 12, 15},
                   {5, 10, 12, 15},
                   {});

    REQUIRE(f_defect.defect_table.value(27, 27, 0, 0) <=
            f_pure.pure_table.value(27, 27));
}

TEST_CASE("DefectTable: overflow flag false for normal instances", "[DefectTable]") {
    Fixture f(27, 27,
              {5, 10, 12, 15},
              {5, 10, 12, 15},
              {{9, 9, 2, 2}});
    REQUIRE_FALSE(f.defect_table.overflow());
}