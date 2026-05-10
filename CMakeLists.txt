cmake_minimum_required(VERSION 3.20)
project(gdcut CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

include_directories(include)

include_directories(vendor)

add_library(gdcut_core
    src/core/defect.cpp
)

add_subdirectory(tests)