#pragma once
#include <string>

namespace gdcut {

/**
 * @brief Starts the local web interface.
 *
 * Serves the frontend from frontend_dir and exposes:
 *   GET  /           — index.html
 *   POST /save       — validate and persist problem JSON to work_dir/problem.json
 *   POST /solve      — solve the problem JSON in work_dir/problem.json
 *   GET  /solution   — return work_dir/solution.json
 *
 * Blocks until the server is stopped (Ctrl+C).
 *
 * @param port         Port to listen on
 * @param frontend_dir Path to the directory containing index.html
 * @param work_dir     Directory where problem.json and solution.json are read/written
 * @param quiet        If true, suppresses the startup message (useful for testing)
 */
void run_server(int port,
                const std::string& frontend_dir,
                const std::string& work_dir,
                bool quiet = false);

} // namespace gdcut