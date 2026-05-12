#include "catch2.hpp"
#include "gdcut/problem.hpp"
#include <fstream>

using namespace gdcut;

// Helper: write a temporary JSON file and return its path

static std::string write_temp_json(const std::string& content) {
    std::string path = "/tmp/gdcut_test_problem.json";
    std::ofstream f(path);
    f << content;
    return path;
}

// Construction from raw data

TEST_CASE("Problem: basic construction", "[Problem]") {
    Problem p(10, 20,
              {3, 5}, {4, 6}, {12, 30},
              {});
    REQUIRE(p.sheet_width()  == 10);
    REQUIRE(p.sheet_height() == 20);
    REQUIRE(p.n_items()      == 2);
    REQUIRE_FALSE(p.has_defects());
}

TEST_CASE("Problem: construction with defects", "[Problem]") {
    std::vector<Defect> defects = {{2, 3, 1, 1}, {5, 5, 2, 2}};
    Problem p(10, 10, {3}, {3}, {9}, defects);
    REQUIRE(p.has_defects());
    REQUIRE(p.defects().size() == 2);
    REQUIRE(p.defects()[0].x == 2);
    REQUIRE(p.defects()[0].y == 3);
    REQUIRE(p.defects()[1].x == 5);
    REQUIRE(p.defects()[1].y == 5);
}

TEST_CASE("Problem: item data accessible", "[Problem]") {
    Problem p(10, 10,
              {3, 5, 7},
              {4, 6, 8},
              {12, 30, 56},
              {});
    REQUIRE(p.item_widths()  == std::vector<int>{3, 5, 7});
    REQUIRE(p.item_heights() == std::vector<int>{4, 6, 8});
    REQUIRE(p.item_profits() == std::vector<int>{12, 30, 56});
}

TEST_CASE("Problem: zero items", "[Problem]") {
    Problem p(10, 10, {}, {}, {}, {});
    REQUIRE(p.n_items()     == 0);
    REQUIRE_FALSE(p.has_defects());
}

TEST_CASE("Problem: zero defects", "[Problem]") {
    Problem p(10, 10, {3}, {3}, {9}, {});
    REQUIRE_FALSE(p.has_defects());
    REQUIRE(p.defects().empty());
}

// pure sheet

TEST_CASE("Problem: from_json pure sheet unweighted", "[Problem]") {
    auto path = write_temp_json(R"({
        "problem": {
            "sheet_size": [27, 27],
            "items": [
                {"width": 5,  "height": 5},
                {"width": 10, "height": 10},
                {"width": 12, "height": 12},
                {"width": 15, "height": 15}
            ]
        }
    })");

    Problem p = Problem::from_json(path);
    REQUIRE(p.sheet_width()  == 27);
    REQUIRE(p.sheet_height() == 27);
    REQUIRE(p.n_items()      == 4);
    REQUIRE_FALSE(p.has_defects());

    // unweighted: profit = area
    REQUIRE(p.item_profits()[0] == 25);   // 5*5
    REQUIRE(p.item_profits()[1] == 100);  // 10*10
    REQUIRE(p.item_profits()[2] == 144);  // 12*12
    REQUIRE(p.item_profits()[3] == 225);  // 15*15
}

TEST_CASE("Problem: from_json weighted instance", "[Problem]") {
    auto path = write_temp_json(R"({
        "problem": {
            "sheet_size": [10, 10],
            "items": [
                {"width": 3, "height": 3, "profit": 99},
                {"width": 5, "height": 5, "profit": 1}
            ]
        }
    })");

    Problem p = Problem::from_json(path);
    REQUIRE(p.item_profits()[0] == 99);
    REQUIRE(p.item_profits()[1] == 1);
}

TEST_CASE("Problem: from_json mixed weighted and unweighted items", "[Problem]") {
    // items without "profit" field default to area
    auto path = write_temp_json(R"({
        "problem": {
            "sheet_size": [10, 10],
            "items": [
                {"width": 3, "height": 3, "profit": 99},
                {"width": 5, "height": 5}
            ]
        }
    })");

    Problem p = Problem::from_json(path);
    REQUIRE(p.item_profits()[0] == 99);   // explicit profit
    REQUIRE(p.item_profits()[1] == 25);   // defaults to 5*5
}

// sheet with defects

TEST_CASE("Problem: from_json with defects", "[Problem]") {
    auto path = write_temp_json(R"({
        "problem": {
            "sheet_size": [27, 27],
            "items": [
                {"width": 5, "height": 5}
            ],
            "defects": [
                {"x": 9, "y": 9, "width": 2, "height": 2}
            ]
        }
    })");

    Problem p = Problem::from_json(path);
    REQUIRE(p.has_defects());
    REQUIRE(p.defects().size() == 1);
    REQUIRE(p.defects()[0].x == 9);
    REQUIRE(p.defects()[0].y == 9);
    REQUIRE(p.defects()[0].w == 2);
    REQUIRE(p.defects()[0].h == 2);
}

TEST_CASE("Problem: from_json multiple defects", "[Problem]") {
    auto path = write_temp_json(R"({
        "problem": {
            "sheet_size": [100, 100],
            "items": [
                {"width": 10, "height": 10}
            ],
            "defects": [
                {"x": 10, "y": 10, "width": 5, "height": 5},
                {"x": 50, "y": 50, "width": 3, "height": 3},
                {"x": 80, "y": 80, "width": 4, "height": 4}
            ]
        }
    })");

    Problem p = Problem::from_json(path);
    REQUIRE(p.defects().size() == 3);
    REQUIRE(p.defects()[2].x == 80);
}

// error handling

TEST_CASE("Problem: from_json nonexistent file throws", "[Problem]") {
    REQUIRE_THROWS_AS(
        Problem::from_json("/tmp/does_not_exist_gdcut.json"),
        std::runtime_error
    );
}