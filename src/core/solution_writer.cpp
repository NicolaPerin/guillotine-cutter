#include "gdcut/solution_writer.hpp"
#include "gdcut/cut_node_json.hpp"
#include "json.hpp"
#include <fstream>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;

namespace gdcut {

static std::string pct_str(int a, int b) {
    std::ostringstream ss;
    ss << a << "/" << b << " ("
       << std::fixed << std::setprecision(1)
       << (b > 0 ? 100.0 * a / b : 0.0) << "%)";
    return ss.str();
}

void write_solution(const std::string& output_path,
                    const Problem& problem,
                    int32_t value,
                    const CutSequence& sequence) {
    json out_file;
    out_file["problem"]["sheet_size"] = {problem.sheet_width(), problem.sheet_height()};

    out_file["problem"]["items"] = json::array();
    for (int i = 0; i < problem.n_items(); ++i)
        out_file["problem"]["items"].push_back({
            {"width",  problem.item_widths()[i]},
            {"height", problem.item_heights()[i]},
            {"profit", problem.item_profits()[i]}
        });

    out_file["problem"]["defects"] = json::array();
    for (const auto& d : problem.defects())
        out_file["problem"]["defects"].push_back({
            {"x", d.x}, {"y", d.y}, {"width", d.w}, {"height", d.h}
        });

    const int total_area  = problem.sheet_width() * problem.sheet_height();
    const int defect_area = [&] {
        int a = 0;
        for (const auto& d : problem.defects()) a += d.w * d.h;
        return a;
    }();
    const int usable_area = total_area - defect_area;

    out_file["solution"] = {
        {"cut_area",     value},
        {"total_area",   total_area},
        {"defect_area",  defect_area},
        {"usable_area",  usable_area},
        {"utilization",  pct_str(value, total_area)},
        {"defect_loss",  pct_str(defect_area, total_area)},
        {"efficiency",   pct_str(value, usable_area)},
        {"cut_sequence", cut_sequence_to_json(sequence)}
    };

    std::ofstream f(output_path);
    if (!f) throw std::runtime_error("Cannot write: " + output_path);
    f << out_file.dump(2) << "\n";
}

} // namespace gdcut