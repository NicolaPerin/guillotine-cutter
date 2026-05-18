#include "gdcut/server.hpp"
#include "gdcut/solver.hpp"
#include "gdcut/problem.hpp"
#include "gdcut/solution_writer.hpp"
#include "httplib.h"
#include "json.hpp"
#include <fstream>
#include <iostream>
#include <sstream>

using json = nlohmann::json;
using namespace gdcut;

namespace gdcut {

void run_server(int port,
                const std::string& frontend_dir,
                const std::string& work_dir) {
    httplib::Server server;

    // serve index.html at root
    server.Get("/", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(frontend_dir + "/index.html");
        if (!f) {
            res.status = 404;
            res.set_content("index.html not found", "text/plain");
            return;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        res.set_content(ss.str(), "text/html; charset=utf-8");
    });

    // save problem JSON to disk
    server.Post("/save", [&](const httplib::Request& req, httplib::Response& res) {
        const std::string path = work_dir + "/problem.json";
        std::ofstream f(path);
        if (!f) {
            res.status = 500;
            res.set_content("Cannot write problem.json", "text/plain");
            return;
        }
        f << req.body;
        res.set_content("saved", "text/plain");
    });

    // solve the current problem.json
    server.Post("/solve", [&](const httplib::Request&, httplib::Response& res) {
        const std::string prob_path = work_dir + "/problem.json";
        const std::string sol_path  = work_dir + "/solution.json";
        try {
            Problem     problem  = Problem::from_json(prob_path);
            Solver      solver(problem);
            int32_t     value    = solver.solve();
            CutSequence sequence = solver.reconstruct();
            write_solution(sol_path, problem, value, sequence);
            res.set_content("ok", "text/plain");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(e.what(), "text/plain");
        }
    });

    // return solution.json
    server.Get("/solution", [&](const httplib::Request&, httplib::Response& res) {
        std::ifstream f(work_dir + "/solution.json");
        if (!f) {
            res.status = 404;
            res.set_content("No solution yet", "text/plain");
            return;
        }
        std::ostringstream ss;
        ss << f.rdbuf();
        res.set_content(ss.str(), "application/json");
    });

    std::cout << "gdcut UI running at http://localhost:" << port << "\n"
              << "Press Ctrl+C to stop.\n";
    if (!server.listen("0.0.0.0", port)) {
        std::cerr << "Error: failed to bind to port " << port
                << " - is it already in use?\n";
        return;
    }
}

} // namespace gdcut