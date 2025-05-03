cmake_minimum_required(VERSION 3.14)
project(ultrabook)

set(CMAKE_CXX_STANDARD 17)

# Engine files
add_library(engine
    engine/engine.cpp
)

target_include_directories(engine PUBLIC engine)

# Catch2
add_subdirectory(tests)
