cmake_minimum_required(VERSION 3.14)
project(FastChain VERSION 0.1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add engine library
add_library(engine
    engine/engine.cpp
)

# Add include directories
target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

# Testing setup
# Download and configure Google Test
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

# Enable testing
enable_testing()

# Create test executable
add_executable(matching_engine_tests 
    tests/engine/test_matching_engine.cpp
)

# Link against Google Test and your engine
target_link_libraries(matching_engine_tests
    PRIVATE 
    engine
    gtest
    gtest_main
)

# Register tests
include(GoogleTest)
gtest_discover_tests(matching_engine_tests)