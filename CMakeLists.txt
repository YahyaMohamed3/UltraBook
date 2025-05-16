cmake_minimum_required(VERSION 3.14)
project(FastChain VERSION 0.1.0)

# Set C++ standard
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set build type to Release by default for better performance metrics
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

# Add advanced compiler optimization flags for benchmarks
# These optimizations are applied only to Release builds and won't affect Debug builds (used for tests)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  # GCC/Clang optimizations
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -fno-omit-frame-pointer")
  
  # Optional: Enable Link-Time Optimization for even better performance
  # Comment out if you experience any issues with benchmarks
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  # MSVC optimizations (Visual Studio)
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} /O2 /Ob2 /Oi /Ot /Oy /GL")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "${CMAKE_EXE_LINKER_FLAGS_RELEASE} /LTCG")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "${CMAKE_SHARED_LINKER_FLAGS_RELEASE} /LTCG")
endif()

# Detailed explanation of optimization flags:
#
# GCC/Clang flags:
# -O3                - Maximum optimization level (already in your original CMakeLists.txt)
# -march=native      - Optimizes for the specific CPU architecture you're running on
#                      This generates code that utilizes all available CPU instructions
#                      (AVX, SSE, etc.) but may not run on different CPUs
# -fno-omit-frame-pointer - Keep frame pointers for better profiling without significant performance loss
# -flto              - Link-Time Optimization: allows optimizations across compilation units
#                      Can significantly improve performance but increases compilation time
#
# MSVC flags (Visual Studio):
# /O2                - Maximum optimization for speed
# /Ob2               - Inline function expansion
# /Oi                - Enable intrinsic functions
# /Ot                - Favor speed over size
# /Oy                - Frame-pointer omission
# /GL                - Whole program optimization
# /LTCG              - Link-Time Code Generation (MSVC's equivalent to LTO)
#
# Benchmark-specific flags (GCC/Clang):
# -ffast-math        - Aggressive floating-point optimizations that may slightly
#                      change results but improve performance
# -funroll-loops     - Unroll loops for better performance
#
# Benchmark-specific flags (MSVC):
# /fp:fast           - Fast floating-point model
# /Qpar              - Enable parallel code generation
# /Qfast_transcendentals - Fast transcendentals
#
# Note: These optimization flags will only affect Release builds.
# Debug builds will still use debug flags for better debugging experience.
# Unit tests typically run in Debug mode so they won't be affected

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

# Download and configure Google Benchmark
FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.7.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark's tests" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable benchmark's gtest tests" FORCE)

FetchContent_MakeAvailable(googletest benchmark)

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

# Create benchmark executable
# Allow customization of which benchmark file to use
if(NOT DEFINED BENCHMARK_FILE)
    set(BENCHMARK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/engine/benchmark_matching_engine.cpp")
endif()

add_executable(matching_engine_benchmarks
    ${BENCHMARK_FILE}
)

# Create basic benchmarks executable
add_executable(basic_benchmarks
    benchmarks/engine/basic_benchmarks.cpp
)

# Link against Google Benchmark and your engine
target_link_libraries(matching_engine_benchmarks
    PRIVATE
    engine
    benchmark::benchmark
)

target_link_libraries(basic_benchmarks
    PRIVATE
    engine
    benchmark::benchmark
)

# Additional compiler flags specifically for the benchmark executables
# These won't affect the unit tests or other parts of the code
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(matching_engine_benchmarks PRIVATE 
    -ffast-math                # Aggressive floating-point optimizations
    -funroll-loops             # Unroll loops for better performance
  )
  target_compile_options(basic_benchmarks PRIVATE 
    -ffast-math                # Aggressive floating-point optimizations
    -funroll-loops             # Unroll loops for better performance
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(matching_engine_benchmarks PRIVATE 
    /fp:fast                   # Fast floating-point model
    /Qpar                      # Enable parallel code generation
    /Qfast_transcendentals     # Fast transcendentals
  )
  target_compile_options(basic_benchmarks PRIVATE 
    /fp:fast                   # Fast floating-point model
    /Qpar                      # Enable parallel code generation
    /Qfast_transcendentals     # Fast transcendentals
  )
endif()

# Add custom target for running benchmarks
add_custom_target(run_benchmarks
    COMMAND $<TARGET_FILE:matching_engine_benchmarks>
    DEPENDS matching_engine_benchmarks
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running performance benchmarks..."
)

# Add custom target for running basic benchmarks
add_custom_target(run_basic_benchmarks
    COMMAND $<TARGET_FILE:basic_benchmarks>
    DEPENDS basic_benchmarks
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running basic performance benchmarks..."
)

message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")