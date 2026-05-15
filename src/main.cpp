#include "gdcut/problem.hpp"
#include "gdcut/solver.hpp"
#include "gdcut/cut_node_json.hpp"
#include "gdcut/solution_writer.hpp"
#include "CLI11.hpp"
#include "json.hpp"
#include <omp.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <sstream>

using json = nlohmann::json;
using namespace gdcut;


int main(int argc, char* argv[]) {
    CLI::App app{"gdcut — guillotine cutting solver"};
    app.require_subcommand(1);

    // ── solve subcommand ──────────────────────────────────────────────────
    auto* solve_cmd = app.add_subcommand("solve", "Solve a cutting problem from JSON");

    std::string input_path;
    std::string output_path = "solution.json";
    int threads = 0;

    solve_cmd->add_option("input",  input_path,  "Input problem JSON file")->required();
    solve_cmd->add_option("output", output_path, "Output solution JSON file");
    solve_cmd->add_option("-t,--threads", threads,
        "Number of OpenMP threads (default: OMP_NUM_THREADS or all cores)");

    CLI11_PARSE(app, argc, argv);

    try {
        if (threads > 0) {
            omp_set_dynamic(0);
            omp_set_num_threads(threads);
        }

        auto t0 = std::chrono::high_resolution_clock::now();

        Problem      problem  = Problem::from_json(input_path);
        Solver       solver(problem);
        int32_t      value    = solver.solve();
        CutSequence  sequence = solver.reconstruct();

        write_solution(output_path, problem, value, sequence);

        auto t1      = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        const int total_area = problem.sheet_width() * problem.sheet_height();

        std::cout << "Solved in  " << std::fixed << std::setprecision(3)
                  << elapsed << "s\n"
                  << "Threads:   " << omp_get_max_threads() << "\n"
                  << "Value:     " << value << " / " << total_area
                  << " (" << std::fixed << std::setprecision(1)
                  << 100.0 * value / total_area << "%)\n"
                  << "Output:    " << output_path << "\n";

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}