#include "catch2.hpp"
#include "gdcut/recursive_solver.hpp"
#include "gdcut/problem.hpp"
#include "gdcut/pure_table.hpp"
#include "gdcut/defect.hpp"
#include "gdcut/defect_table.hpp"
#include "gdcut/patterns.hpp"

using namespace gdcut;

// Helper to compute profits based on area
static std::vector<int> make_profits(const std::vector<int>& iw, const std::vector<int>& ih) {
    std::vector<int> p;
    p.reserve(iw.size());
    for (size_t i = 0; i < iw.size(); ++i) {
        p.push_back(iw[i] * ih[i]);
    }
    return p;
}

// Pure sheet — recursive should match pure table

TEST_CASE("pure sheet matches PureTable value", "[recursive_solver]") {
    std::vector<int> iw = {3, 5};
    std::vector<int> ih = {4, 6};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {};

    Problem prob(20, 20, iw, ih, p, defects);
    PurePatterns px(iw, 20);
    PurePatterns py(ih, 20);
    PureTable pt(20, 20, iw, ih, p, px, py);
    DefectMap dmap(20, 20, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    int32_t expected = pt.value(20, 20);
    int32_t got = solver.solve();
    REQUIRE(got == expected);
}

// Single defect

TEST_CASE("single defect — value does not exceed pure table", "[recursive_solver]") {
    std::vector<int> iw = {3, 5};
    std::vector<int> ih = {4, 6};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {{8, 8, 2, 2}};

    Problem prob(20, 20, iw, ih, p, defects);
    PurePatterns px(iw, 20);
    PurePatterns py(ih, 20);
    PureTable pt(20, 20, iw, ih, p, px, py);
    DefectMap dmap(20, 20, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    int32_t pure_val = pt.value(20, 20);
    int32_t defect_val = solver.solve();
    REQUIRE(defect_val <= pure_val);
    REQUIRE(defect_val > 0);
}

TEST_CASE("single defect — reconstruction returns non-null sequence", "[recursive_solver]") {
    std::vector<int> iw = {3, 5};
    std::vector<int> ih = {4, 6};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {{8, 8, 2, 2}};

    Problem prob(20, 20, iw, ih, p, defects);
    PurePatterns px(iw, 20);
    PurePatterns py(ih, 20);
    PureTable pt(20, 20, iw, ih, p, px, py);
    DefectMap dmap(20, 20, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    solver.solve();
    auto seq = solver.reconstruct();
    REQUIRE(seq != nullptr);
}

// Paper example — Zhang et al. instance should yield known value

TEST_CASE("paper instance value is 644", "[recursive_solver]") {
    std::vector<int> iw = {5, 10, 12, 15};
    std::vector<int> ih = {5, 10, 12, 15};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {{9, 9, 2, 2}};

    Problem prob(27, 27, iw, ih, p, defects);
    PurePatterns px(iw, 27);
    PurePatterns py(ih, 27);
    PureTable pt(27, 27, iw, ih, p, px, py);
    DefectMap dmap(27, 27, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    int32_t val = solver.solve();
    REQUIRE(val == 644);
}

// Matches iterative solver on small defect problems

TEST_CASE("recursive matches iterative on 20x20 with defect", "[recursive_solver]") {
    std::vector<int> iw = {3, 5};
    std::vector<int> ih = {4, 6};
    std::vector<int> p = {12, 30};
    std::vector<Defect> defects = {{5, 5, 2, 2}};

    Problem prob(20, 20, iw, ih, p, defects);
    DefectMap dmap(20, 20, defects);

    PurePatterns px(iw, 20);
    PurePatterns py(ih, 20);
    PureTable pt(20, 20, iw, ih, p, px, py);

    DefectTable iterative(prob, pt, dmap);
    RecursiveSolver recursive(prob, pt, dmap, px, py);

    int32_t iterative_val = iterative.value(20, 20, 0, 0);
    int32_t recursive_val = recursive.solve();

    REQUIRE(recursive_val == iterative_val);
}

// Reconstruction is consistent with solve value

TEST_CASE("reconstruct before solve throws", "[recursive_solver]") {
    std::vector<int> iw = {3};
    std::vector<int> ih = {4};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {{2, 2, 1, 1}};

    Problem prob(10, 10, iw, ih, p, defects);
    PurePatterns px(iw, 10);
    PurePatterns py(ih, 10);
    PureTable pt(10, 10, iw, ih, p, px, py);
    DefectMap dmap(10, 10, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    REQUIRE_THROWS_AS(solver.reconstruct(), std::runtime_error);
}

TEST_CASE("reconstruction does not crash on defect problem", "[recursive_solver]") {
    std::vector<int> iw = {10, 14};
    std::vector<int> ih = {9, 4};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {{9, 9, 2, 2}};

    Problem prob(27, 27, iw, ih, p, defects);
    PurePatterns px(iw, 27);
    PurePatterns py(ih, 27);
    PureTable pt(27, 27, iw, ih, p, px, py);
    DefectMap dmap(27, 27, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    solver.solve();
    REQUIRE_NOTHROW(solver.reconstruct());
}

// memo_size grows only for contaminated states

TEST_CASE("memo is empty for pure sheet", "[recursive_solver]") {
    std::vector<int> iw = {3, 5};
    std::vector<int> ih = {4, 6};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {};

    Problem prob(20, 20, iw, ih, p, defects);
    PurePatterns px(iw, 20);
    PurePatterns py(ih, 20);
    PureTable pt(20, 20, iw, ih, p, px, py);
    DefectMap dmap(20, 20, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    solver.solve();
    REQUIRE(solver.memo_size() == 0);
}

TEST_CASE("memo is non-empty for defect sheet", "[recursive_solver]") {
    std::vector<int> iw = {3, 5};
    std::vector<int> ih = {4, 6};
    std::vector<int> p = make_profits(iw, ih);
    std::vector<Defect> defects = {{8, 8, 2, 2}};

    Problem prob(20, 20, iw, ih, p, defects);
    PurePatterns px(iw, 20);
    PurePatterns py(ih, 20);
    PureTable pt(20, 20, iw, ih, p, px, py);
    DefectMap dmap(20, 20, defects);
    RecursiveSolver solver(prob, pt, dmap, px, py);

    solver.solve();
    REQUIRE(solver.memo_size() > 0);
}