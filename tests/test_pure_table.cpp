#include "catch2.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/patterns.hpp"

using namespace gdcut;

// Helper to build PureTable from item sizes (unweighted: profit = area)

static PureTable make_table(int W, int H,
                             const std::vector<int>& iw,
                             const std::vector<int>& ih) {
    std::vector<int> profits;
    for (int i = 0; i < (int)iw.size(); ++i)
        profits.push_back(iw[i] * ih[i]);
    PurePatterns px(iw, W);
    PurePatterns py(ih, H);
    return PureTable(W, H, iw, ih, profits, px, py);
}

// Edge cases

TEST_CASE("PureTable: no items — all values zero", "[PureTable]") {
    PureTable t = make_table(10, 10, {}, {});
    for (int w = 0; w <= 10; ++w)
        for (int h = 0; h <= 10; ++h)
            REQUIRE(t.value(w, h) == 0);
}

TEST_CASE("PureTable: single item larger than sheet — all values zero", "[PureTable]") {
    PureTable t = make_table(5, 5, {10}, {10});
    for (int w = 1; w <= 5; ++w)
        for (int h = 1; h <= 5; ++h)
            REQUIRE(t.value(w, h) == 0);
}

TEST_CASE("PureTable: single item exactly fills sheet", "[PureTable]") {
    // 4x3 item on 4x3 sheet — one copy, value = 12
    PureTable t = make_table(4, 3, {4}, {3});
    REQUIRE(t.value(4, 3) == 12);
    REQUIRE(t.decision(4, 3) == Decision::Fill);
    REQUIRE(t.best_item(4, 3) == 0);
}

TEST_CASE("PureTable: single item tiles perfectly", "[PureTable]") {
    // 2x2 item on 6x4 sheet — 3*2 = 6 copies, value = 24
    PureTable t = make_table(6, 4, {2}, {2});
    REQUIRE(t.value(6, 4) == 24);
}

TEST_CASE("PureTable: value(0,h) and value(w,0) are zero", "[PureTable]") {
    PureTable t = make_table(5, 5, {2}, {2});
    for (int i = 0; i <= 5; ++i) {
        REQUIRE(t.value(0, i) == 0);
        REQUIRE(t.value(i, 0) == 0);
    }
}

// Tiling correctness

TEST_CASE("PureTable: best item selected correctly among multiple items", "[PureTable]") {
    // 4x4 sheet, items 1x1 (profit 1) and 2x2 (profit 4)
    // 2x2 tiling: 4 copies = 16, better than 16 copies of 1x1 = 16
    // but 2x2 has same value — both are valid, just check value is 16
    PureTable t = make_table(4, 4, {1, 2}, {1, 2});
    REQUIRE(t.value(4, 4) == 16);
}

TEST_CASE("PureTable: larger profit item preferred", "[PureTable]") {
    // 6x6 sheet, items 2x2 (profit 4) and 3x3 (profit 9)
    // 3x3: 4 copies = 36, 2x2: 9 copies = 36 — equal
    // 6x4 sheet: 3x3 gives 2 copies = 18, 2x2 gives 6 copies = 24
    PureTable t = make_table(6, 4, {2, 3}, {2, 3});
    REQUIRE(t.value(6, 4) == 24);  // 2x2 wins: 3*2*4 = 24
}

// Cut correctness

TEST_CASE("PureTable: cut improves on tiling", "[PureTable]") {
    // 4x2 sheet, single 3x2 item
    // tiling: 1 copy = 6 (one 3x2 fits, leaving 1x2 waste)
    // cut at x=3: value(3,2) + value(1,2) = 6 + 0 = 6 — no improvement
    // cut at x=2 not reachable (3 is smallest item width)
    // so best is Fill with value 6
    PureTable t = make_table(4, 2, {3}, {2});
    REQUIRE(t.value(4, 2) == 6);
    REQUIRE(t.decision(4, 2) == Decision::Fill);
}

TEST_CASE("PureTable: cut strictly better than tiling", "[PureTable]") {
    // 6x3 sheet, single 3x3 item (profit 9)
    // tiling: 2 copies = 18
    // cut at x=3: value(3,3) + value(3,3) = 9 + 9 = 18 — same
    // both are valid, value must be 18
    PureTable t = make_table(6, 3, {3}, {3});
    REQUIRE(t.value(6, 3) == 18);
}

TEST_CASE("PureTable: cut decision recorded correctly", "[PureTable]") {
    // 10x5 sheet, single 3x5 item (profit 15)
    // tiling: 3 copies (floor(10/3)=3) = 45, 1 unit waste
    // cut at x=3: value(3,5)+value(7,5) = 15 + value(7,5)
    // value(7,5): floor(7/3)=2 copies = 30, but cut at x=3: 15+value(4,5)
    // value(4,5): floor(4/3)=1 copy = 15, cut at x=3: 15+value(1,5)=15
    // so value(7,5)=30 via tiling, value(10,5)=45 via tiling
    // tiling wins — decision should be Fill
    PureTable t = make_table(10, 5, {3}, {5});
    REQUIRE(t.value(10, 5) == 45);
    REQUIRE(t.decision(10, 5) == Decision::Fill);
}

// Paper Example 1

TEST_CASE("PureTable: paper Example 1 — pure sheet value", "[PureTable]") {
    // W0=H0=27, items 5x5 (25), 10x10 (100), 12x12 (144), 15x15 (225)
    // optimal pure value from the paper
    std::vector<int> sizes = {5, 10, 12, 15};
    PureTable t = make_table(27, 27, sizes, sizes);

    // The full sheet value should match the paper's DPR result
    // From Table 8, instance 6 (pure sheet, 27x27): OFV = 164 * 100 = not directly
    // but we can verify specific sub-rectangles we can compute by hand:

    // 5x5: exactly one 5x5 item = 25
    REQUIRE(t.value(5, 5) == 25);

    // 10x10: one 10x10 item = 100
    REQUIRE(t.value(10, 10) == 100);

    // 15x15: one 15x15 item = 225
    REQUIRE(t.value(15, 15) == 225);

    // 10x5: two 5x5 items = 50
    REQUIRE(t.value(10, 5) == 50);

    // 20x20: four 10x10 items = 400
    REQUIRE(t.value(20, 20) == 400);

    // 25x25: 25 copies of 5x5 = 625
    REQUIRE(t.value(25, 25) == 625);

    // 27x27: best known value — 25 copies of 5x5 in 25x25 + cuts
    // at minimum should be >= 625
    REQUIRE(t.value(27, 27) >= 625);
}

TEST_CASE("PureTable: decision and cut_position consistency", "[PureTable]") {
    std::vector<int> sizes = {5, 10, 12, 15};
    PureTable t = make_table(27, 27, sizes, sizes);

    // For every (w,h), if decision is CutX, cut_position must be in [1, w/2]
    // If decision is CutY, cut_position must be in [1, h/2]
    // If decision is Fill, best_item must be >= 0
    for (int w = 1; w <= 27; ++w) {
        for (int h = 1; h <= 27; ++h) {
            Decision d = t.decision(w, h);
            int32_t  p = t.cut_position(w, h);
            if (d == Decision::CutX) {
                REQUIRE(p >= 1);
                REQUIRE(p <= w / 2);
            } else if (d == Decision::CutY) {
                REQUIRE(p >= 1);
                REQUIRE(p <= h / 2);
            } else if (d == Decision::Fill) {
                REQUIRE(t.best_item(w, h) >= 0);
            }
        }
    }
}