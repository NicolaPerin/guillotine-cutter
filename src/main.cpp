#include "gdcut/problem.hpp"
#include "gdcut/solver.hpp"
#include "gdcut/cut_node_json.hpp"
#include "CLI11.hpp"
#include "json.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;
using namespace gdcut;

void write_solution(const std::string& output_path,const Problem& problem, int32_t value, const CutSequence& sequence) {
    json out_file;
    out_file["problem"]["sheet_size"] = {problem.sheet_width(), problem.sheet_height()};
    out_file["problem"]["items"] = json::array();

    for (int i = 0; i < problem.n_items(); ++i) {
        out_file["problem"]["items"].push_back({
            {"width", problem.item_widths()[i]},
            {"height", problem.item_heights()[i]},
            {"profit", problem.item_profits()[i]}
        });
    }

    // defects
    out_file["problem"]["defects"] = json::array();
    for (const auto& d : problem.defects())
        out_file["problem"]["defects"].push_back({
            {"x", d.x}, {"y", d.y},
            {"width", d.w}, {"height", d.h}
        });

    // solution statistics
    int total_area  = problem.sheet_width() * problem.sheet_height();
    int defect_area = 0;
    for (const auto& d : problem.defects())
        defect_area += d.w * d.h;
    int usable_area = total_area - defect_area;

    // helper lambda for "value/total (pct%)" strings
    auto pct_str = [](int a, int b) {
        std::ostringstream ss;
        ss << a << "/" << b << " ("
           << std::fixed << std::setprecision(1)
           << (b > 0 ? 100.0 * a / b : 0.0) << "%)";
        return ss.str();
    };

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

int main(int argc, char* argv[]) {

    CLI::App app{"gdcut — guillotine cutting solver"};
    app.require_subcommand(1);

    auto* solve = app.add_subcommand("solve", "Solve a cutting problem from JSON");
    std::string input_path;
    std::string output_path = "solution.json";  // default output
    solve->add_option("input", input_path, "Input problem JSON file")->required();
    solve->add_option("output", output_path, "Output solution JSON file");

    CLI11_PARSE(app, argc, argv);
    
    try {
        auto t0 = std::chrono::high_resolution_clock::now();

        Problem problem = Problem::from_json(input_path);
        Solver solver(problem);
        int32_t value = solver.solve();
        CutSequence sequence = solver.reconstruct();
        write_solution(output_path, problem, value, sequence);

        auto t1 = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        
        std::cout << "Solved in " << std::fixed << std::setprecision(3)
                << elapsed << "s\n"
                << "Value:  " << value << " / "
                << problem.sheet_width() * problem.sheet_height()
                << " (" << std::fixed << std::setprecision(1)
                << 100.0 * value / (problem.sheet_width() * problem.sheet_height())
                << "%)\n"
                << "Output: " << output_path << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}