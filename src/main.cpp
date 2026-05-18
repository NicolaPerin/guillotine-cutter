#include "gdcut/problem.hpp"
#include "gdcut/solver.hpp"
#include "gdcut/solution_writer.hpp"
#include "CLI11.hpp"
#include "gdcut/server.hpp"
#include <omp.h>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <string>

using namespace gdcut;

int main(int argc, char* argv[]) {
    CLI::App app{"gdcut - guillotine cutting solver"};
    app.require_subcommand(1);

    auto* solve_cmd = app.add_subcommand("solve", "Solve a cutting problem from JSON");

    std::string input_path;
    std::string output_path    = "solution.json";
    std::string solver_mode    = "auto";
    int         threads        = 0;
    double      sparse_threshold = Solver::SPARSE_THRESHOLD;

    solve_cmd->add_option("input",  input_path,  "Input problem JSON file")->required();
    solve_cmd->add_option("output", output_path, "Output solution JSON file");
    solve_cmd->add_option("-t,--threads", threads,
        "Number of OpenMP threads (default: OMP_NUM_THREADS or all cores)");
    solve_cmd->add_option("-s,--solver", solver_mode,
        "Solver backend: auto (default), iterative, recursive")
        ->check(CLI::IsMember({"auto", "iterative", "recursive"}));
    solve_cmd->add_option("--sparse-threshold", sparse_threshold,
        "Raster density threshold for auto dispatch (default: "
        + std::to_string(Solver::SPARSE_THRESHOLD) + ")");

    auto* serve_cmd = app.add_subcommand("serve", "Start the local web interface");
    int port = 8080;
    std::string frontend_dir = "src/frontend";
    serve_cmd->add_option("-p,--port", port, "Port to listen on (default: 8080)");
    serve_cmd->add_option("--frontend", frontend_dir, "Path to frontend directory (default: src/frontend)");

    CLI11_PARSE(app, argc, argv);

    if (serve_cmd->parsed()) {
        gdcut::run_server(port, frontend_dir, ".");
        return 0;
    }

    try {
        if (threads > 0) {
            omp_set_dynamic(0);
            omp_set_num_threads(threads);
        }

        Solver::Mode mode = Solver::Mode::Auto;
        if      (solver_mode == "iterative") mode = Solver::Mode::Iterative;
        else if (solver_mode == "recursive") mode = Solver::Mode::Recursive;

        auto t0 = std::chrono::high_resolution_clock::now();

        Problem     problem  = Problem::from_json(input_path);
        Solver      solver(problem);
        int32_t     value    = solver.solve(mode, sparse_threshold);
        CutSequence sequence = solver.reconstruct();

        write_solution(output_path, problem, value, sequence);

        auto   t1      = std::chrono::high_resolution_clock::now();
        double elapsed = std::chrono::duration<double>(t1 - t0).count();
        const int total_area = problem.sheet_width() * problem.sheet_height();

        auto backend_str = [&]() -> std::string {
            switch (solver.backend()) {
                case Solver::Backend::Pure:      return "pure";
                case Solver::Backend::Iterative: return "iterative";
                case Solver::Backend::Recursive: return "recursive";
                default:                         return "unknown";
            }
        }();

        const bool is_recursive = solver.backend() == Solver::Backend::Recursive;

        std::cout << "Solved in  " << std::fixed << std::setprecision(3)
                  << elapsed << "s\n"
                  << "Backend:   " << backend_str
                  << (solver_mode == "auto" ? " (auto)" : "") << "\n"
                  << "Threads:   "
                  << (is_recursive ? "1 (recursive is single-threaded)"
                                   : std::to_string(omp_get_max_threads())) << "\n"
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