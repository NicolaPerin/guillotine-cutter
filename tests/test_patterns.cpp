#include "catch2.hpp"
#include "gdcut/patterns.hpp"

using namespace gdcut;

TEST_CASE("PurePatterns correctly identifies reachable dimensions", "[PurePatterns]") {
    std::vector<int> item_sizes = {}; // No item sizes, only dimension 0 should be reachable
    int max_dim = 10;
    PurePatterns patterns(item_sizes, max_dim);
    REQUIRE(patterns.is_reachable(0)); // dimension 0 is always reachable
    for (int dim = 1; dim <= max_dim; ++dim)
        REQUIRE_FALSE(patterns.is_reachable(dim)); // no other dimensions should be reachable
}

TEST_CASE("PurePatterns correctly computes maximum reachable dimensions", "[PurePatterns]") {
    std::vector<int> item_sizes = {5, 10, 12, 15}; // Example 1 from the paper
    int max_dim = 27;
    PurePatterns patterns(item_sizes, max_dim);
    // normal cut points for dimension 27
    REQUIRE(patterns.normal_cuts(27) == std::vector<int>{5,10,12,15,17,20,22,24,25});
        // reachable
    for (int dim : {0,5,10,12,15,17,20,22,24,25,27})
        REQUIRE(patterns.is_reachable(dim));
    // not reachable
    for (int dim : {1,2,3,4,6,7,8,9,11,13,14,16,18,19,21,23,26})
        REQUIRE_FALSE(patterns.is_reachable(dim));

    REQUIRE(patterns.max_reachable(27) == 27); // 27 is reachable
    REQUIRE(patterns.max_reachable(26) == 25); // 26 not reachable, falls back to 25
    REQUIRE(patterns.max_reachable(7)  == 5);  // 7 not reachable, falls back to 5
}