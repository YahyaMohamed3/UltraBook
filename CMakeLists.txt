cmake_minimum_required(VERSION 3.14)
project(fastchain)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Engine library
add_library(engine
    engine/engine.cpp
    engine/types.hpp
    engine/engine.hpp
)

target_include_directories(engine PUBLIC engine)

# Engine executable
add_executable(engine_main engine/main.cpp)
target_link_libraries(engine_main PRIVATE engine)

# Add tests directory
add_subdirectory(tests)

# Add benchmarks directory
add_subdirectory(benchmarks)