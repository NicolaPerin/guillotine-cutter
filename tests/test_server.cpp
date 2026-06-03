// Integration tests for the local serve subcommand.
//
// run_server() binds and blocks in listen() with no stop hook, so each test
// launches it on its own port in a detached thread and exercises it over HTTP
// with an httplib::Client. Detached threads are torn down when the test
// process exits; that is acceptable here because the server holds no state
// that outlives the process.

#include "gdcut/server.hpp"
#include "httplib.h"
#include "json.hpp"
#include "catch2.hpp"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace {

std::atomic<int> g_next_port{18080};

// Create an isolated temp directory holding an index.html with the given
// content. The same directory doubles as the server's work_dir, so each test
// reads and writes problem.json / solution.json in its own sandbox.
fs::path make_workspace(const std::string& index_content) {
    static std::atomic<unsigned> counter{0};
    const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
    fs::path dir = fs::temp_directory_path() /
                   ("gdcut_test_" + std::to_string(stamp) + "_" +
                    std::to_string(counter++));
    fs::create_directories(dir);
    std::ofstream(dir / "index.html") << index_content;
    return dir;
}

// Start run_server on a fresh port in a detached thread and block until it
// answers a request, or fail the test if it never comes up.
int start_server(const fs::path& workspace) {
    const int port = g_next_port++;
    std::thread([port, workspace]() {
        gdcut::run_server(port, workspace.string(), workspace.string(), true);
    }).detach();

    httplib::Client probe("127.0.0.1", port);
    probe.set_connection_timeout(0, 100 * 1000);  // 100 ms
    for (int i = 0; i < 100; ++i) {
        if (auto res = probe.Get("/")) return port;  // got a response: server is up
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    FAIL("server did not come up on port " << port);
    return port;  // unreachable; keeps the compiler quiet
}

httplib::Client client_for(int port) {
    httplib::Client cli("127.0.0.1", port);
    cli.set_connection_timeout(2, 0);
    cli.set_read_timeout(5, 0);
    cli.set_write_timeout(5, 0);
    return cli;
}

std::string read_file(const fs::path& p) {
    std::ifstream f(p);
    return std::string(std::istreambuf_iterator<char>(f),
                       std::istreambuf_iterator<char>());
}

// A small valid instance: one 3x4 item on a 10x10 sheet, no defects, so it
// takes the pure backend and yields a positive cut area.
const char* VALID_PROBLEM = R"({
  "problem": {
    "sheet_size": [10, 10],
    "items": [ { "width": 3, "height": 4, "profit": 12 } ]
  }
})";

} // namespace

TEST_CASE("GET / serves index.html", "[server]") {
    auto ws   = make_workspace("<!doctype html><title>GDCUT_TEST_MARKER</title>");
    int  port = start_server(ws);
    auto cli  = client_for(port);

    auto res = cli.Get("/");
    REQUIRE(res);
    REQUIRE(res->status == 200);
    REQUIRE(res->body.find("GDCUT_TEST_MARKER") != std::string::npos);
}

TEST_CASE("POST /save validates the body before writing", "[server]") {
    SECTION("valid JSON under the size cap is persisted verbatim") {
        auto ws   = make_workspace("index");
        int  port = start_server(ws);
        auto cli  = client_for(port);

        auto res = cli.Post("/save", VALID_PROBLEM, "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 200);
        REQUIRE(read_file(ws / "problem.json") == VALID_PROBLEM);
    }

    SECTION("malformed JSON is rejected with 400 and nothing is written") {
        auto ws   = make_workspace("index");
        int  port = start_server(ws);
        auto cli  = client_for(port);

        auto res = cli.Post("/save", "{ not valid json ", "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 400);
        REQUIRE_FALSE(fs::exists(ws / "problem.json"));
    }

    SECTION("oversized body is rejected with 413 and nothing is written") {
        auto ws   = make_workspace("index");
        int  port = start_server(ws);
        auto cli  = client_for(port);

        // 2 MB: over the 1 MB /save cap, under the 4 MB global payload cap,
        // so it reaches the handler's own size check rather than being
        // refused by httplib first.
        std::string big(2u << 20, 'x');
        auto res = cli.Post("/save", big, "application/json");
        REQUIRE(res);
        REQUIRE(res->status == 413);
        REQUIRE_FALSE(fs::exists(ws / "problem.json"));
    }
}

TEST_CASE("save then solve produces a solution that /solution returns", "[server]") {
    auto ws   = make_workspace("index");
    int  port = start_server(ws);
    auto cli  = client_for(port);

    auto saved = cli.Post("/save", VALID_PROBLEM, "application/json");
    REQUIRE(saved);
    REQUIRE(saved->status == 200);

    auto solved = cli.Post("/solve", R"({"solver":"auto"})", "application/json");
    REQUIRE(solved);
    REQUIRE(solved->status == 200);

    auto sol = cli.Get("/solution");
    REQUIRE(sol);
    REQUIRE(sol->status == 200);

    auto j = json::parse(sol->body);
    REQUIRE(j.contains("solution"));
    const auto& s = j["solution"];
    REQUIRE(s.contains("cut_area"));
    REQUIRE(s.contains("backend"));
    REQUIRE(s.contains("elapsed_s"));        // chrono timing made it into the output
    REQUIRE(s["backend"].get<std::string>() == "pure");
    REQUIRE(s["cut_area"].get<int>() > 0);   // the 3x4 item fits in the 10x10 sheet
}

TEST_CASE("GET /solution returns 404 before any solve", "[server]") {
    auto ws   = make_workspace("index");
    int  port = start_server(ws);
    auto cli  = client_for(port);

    auto res = cli.Get("/solution");
    REQUIRE(res);
    REQUIRE(res->status == 404);
}