#include "catch2.hpp"
#include "gdcut/solution_writer.hpp"
#include "gdcut/problem.hpp"
#include "gdcut/solver.hpp"
#include <fstream>
#include "json.hpp"

using namespace gdcut;
using json = nlohmann::json;

static Problem small_pure_problem() {
    return Problem(10, 10, {3, 5}, {4, 6}, {12, 30}, {});
}

TEST_CASE("write_solution produces valid JSON file", "[solution_writer]") {
    Problem p = small_pure_problem();
    Solver  s(p);
    int32_t value = s.solve();
    auto    seq   = s.reconstruct();

    const std::string path = "/tmp/test_solution_writer.json";
    REQUIRE_NOTHROW(write_solution(path, p, value, seq));

    std::ifstream f(path);
    REQUIRE(f.is_open());
    json j = json::parse(f);

    SECTION("problem block") {
        REQUIRE(j["problem"]["sheet_size"] == json::array({10, 10}));
        REQUIRE(j["problem"]["items"].size() == 2);
        REQUIRE(j["problem"]["defects"].size() == 0);
    }

    SECTION("solution block fields present") {
        REQUIRE(j["solution"].contains("cut_area"));
        REQUIRE(j["solution"].contains("total_area"));
        REQUIRE(j["solution"].contains("defect_area"));
        REQUIRE(j["solution"].contains("usable_area"));
        REQUIRE(j["solution"].contains("utilization"));
        REQUIRE(j["solution"].contains("defect_loss"));
        REQUIRE(j["solution"].contains("efficiency"));
        REQUIRE(j["solution"].contains("cut_sequence"));
    }

    SECTION("solution values consistent") {
        REQUIRE(j["solution"]["cut_area"]   == value);
        REQUIRE(j["solution"]["total_area"] == 100);
        REQUIRE(j["solution"]["defect_area"] == 0);
        REQUIRE(j["solution"]["usable_area"] == 100);
    }
}

TEST_CASE("write_solution throws on bad output path", "[solution_writer]") {
    Problem p = small_pure_problem();
    Solver  s(p);
    int32_t value = s.solve();
    auto    seq   = s.reconstruct();

    REQUIRE_THROWS_AS(
        write_solution("/nonexistent/path/out.json", p, value, seq),
        std::runtime_error
    );
}