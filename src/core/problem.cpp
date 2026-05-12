#include "gdcut/problem.hpp"
#include "json.hpp"
#include <fstream>
#include <stdexcept>

using json = nlohmann::json;

namespace gdcut {

// Vectors are passed by value and moved into members rather than copied
// This is efficient because the caller is done with them after construction
Problem::Problem(int sheet_width, int sheet_height,
                 std::vector<int> item_widths,
                 std::vector<int> item_heights,
                 std::vector<int> item_profits,
                 std::vector<Defect> defects)
    : sheet_width_ (sheet_width)
    , sheet_height_(sheet_height)
    , item_widths_ (std::move(item_widths))
    , item_heights_(std::move(item_heights))
    , item_profits_(std::move(item_profits))
    , defects_     (std::move(defects))
{}

Problem Problem::from_json(const std::string& path) {
    std::ifstream f(path);
    if (!f)
        throw std::runtime_error("Cannot open: " + path);

    json j = json::parse(f);
    auto& prob = j["problem"];

    int sheet_width  = prob["sheet_size"][0];
    int sheet_height = prob["sheet_size"][1];

    std::vector<int> item_widths, item_heights, item_profits;
    for (auto& item : prob["items"]) {
        int w = item["width"];
        int h = item["height"];
        item_widths .push_back(w);
        item_heights.push_back(h);
        // from_json supports both weighted and unweighted instances:
        // - weighted instances set a "profit" field per item;
        // - unweighted instances omit it and profit defaults to the area w * h.
        item_profits.push_back(item.value("profit", w * h));
    }

    // Defects are optional — a pure sheet has no "defects" key in the JSON.
    std::vector<Defect> defects;
    if (prob.contains("defects")) {
        for (auto& d : prob["defects"])
            defects.push_back(Defect{d["x"], d["y"], d["width"], d["height"]});
    }

    return Problem(sheet_width, sheet_height,
                   std::move(item_widths),
                   std::move(item_heights),
                   std::move(item_profits),
                   std::move(defects));
}

int  Problem::sheet_width()  const { return sheet_width_;  }
int  Problem::sheet_height() const { return sheet_height_; }
int  Problem::n_items()      const { return static_cast<int>(item_widths_.size()); }
bool Problem::has_defects()  const { return !defects_.empty(); }

const std::vector<int>&    Problem::item_widths()  const { return item_widths_;  }
const std::vector<int>&    Problem::item_heights() const { return item_heights_; }
const std::vector<int>&    Problem::item_profits() const { return item_profits_; }
const std::vector<Defect>& Problem::defects()      const { return defects_;      }

} // namespace gdcut