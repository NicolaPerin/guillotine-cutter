cmake_minimum_required(VERSION 3.20)
project(gdcut CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Default to Release build
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native)
elseif(CMAKE_BUILD_TYPE STREQUAL "RelWithDebInfo")
    add_compile_options(-O3 -march=native -g -fno-omit-frame-pointer)
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-O0 -g)
endif()

include_directories(include)

include_directories(vendor)

find_package(OpenMP REQUIRED)

find_package(Threads REQUIRED)

add_library(gdcut_core
    src/core/cut_candidates.cpp
    src/core/cut_node.cpp
    src/core/cut_node_json.cpp
    src/core/defect.cpp
    src/core/defect_table.cpp
    src/core/patterns.cpp
    src/core/problem.cpp
    src/core/pure_table.cpp
    src/core/recursive_solver.cpp
    src/core/solution_writer.cpp
    src/core/solver.cpp
    src/server/server.cpp      # ← add
)

target_link_libraries(gdcut_core PUBLIC OpenMP::OpenMP_CXX Threads::Threads)

add_executable(gdcut src/main.cpp)
target_link_libraries(gdcut PRIVATE gdcut_core)

add_subdirectory(tests)
