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
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  # MSVC optimizations (Visual Studio)
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Oi /Ot /Oy /GL")  # Cleaned out ignored GCC flags
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/LTCG")
  add_compile_options(/EHsc)  # Enable C++ exception handling to fix warning C4530
endif()

# Add engine library
add_library(engine
    engine/engine.cpp
)

# Add include directories
target_include_directories(engine PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/engine
)

# Link winmm on Windows to fix timeBeginPeriod linker error
if(WIN32)
  target_link_libraries(engine PRIVATE winmm)
endif()

# Testing setup
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
    benchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG v1.7.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark's tests" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable benchmark's gtest tests" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)

# NUCLEAR OPTION: Force Google Benchmark to build in Release mode
# Override ALL configuration flags to prevent Debug mode
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
    set(CMAKE_CXX_FLAGS_DEBUG_BACKUP ${CMAKE_CXX_FLAGS_DEBUG})
    set(CMAKE_C_FLAGS_DEBUG_BACKUP ${CMAKE_C_FLAGS_DEBUG})
    set(CMAKE_CXX_FLAGS_DEBUG "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "Debug forced to Release" FORCE)
    set(CMAKE_C_FLAGS_DEBUG "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "Debug forced to Release" FORCE)
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "" FORCE)
endif()

# Force single Release configuration
set(CMAKE_BUILD_TYPE_BACKUP ${CMAKE_BUILD_TYPE})
set(CMAKE_CONFIGURATION_TYPES_BACKUP ${CMAKE_CONFIGURATION_TYPES})
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Forced Release for benchmark" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Only Release" FORCE)

# Global NDEBUG for all targets during benchmark fetch
add_compile_definitions(NDEBUG)

FetchContent_MakeAvailable(googletest benchmark)

# Restore original settings after benchmark is built
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE_BACKUP} CACHE STRING "" FORCE)
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_CONFIGURATION_TYPES_BACKUP} CACHE STRING "" FORCE)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC" AND DEFINED CMAKE_CXX_FLAGS_DEBUG_BACKUP)
    set(CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG_BACKUP} CACHE STRING "" FORCE)
    set(CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG_BACKUP} CACHE STRING "" FORCE)
endif()

message(STATUS "Google Benchmark configured with FORCED RELEASE mode")

enable_testing()

add_executable(matching_engine_tests 
    tests/engine/test_matching_engine.cpp
)
target_link_libraries(matching_engine_tests
    PRIVATE 
    engine
    gtest
    gtest_main
)
include(GoogleTest)
gtest_discover_tests(matching_engine_tests)

# Benchmark files
if(NOT DEFINED BENCHMARK_FILE)
    if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/engine/benchmark_matching_engine.cpp")
        set(BENCHMARK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/engine/benchmark_matching_engine.cpp")
    else()
        message(STATUS "Default benchmark file not found, using basic_benchmarks.cpp instead")
        set(BENCHMARK_FILE "${CMAKE_CURRENT_SOURCE_DIR}/benchmarks/engine/basic_benchmarks.cpp")
    endif()
endif()

add_executable(matching_engine_benchmarks
    ${BENCHMARK_FILE}
)
add_executable(basic_benchmarks
    benchmarks/engine/basic_benchmarks.cpp
)

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

# Also link winmm to benchmark executables (in case timeBeginPeriod is used there too)
if(WIN32)
  target_link_libraries(matching_engine_benchmarks PRIVATE winmm)
  target_link_libraries(basic_benchmarks PRIVATE winmm)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(matching_engine_benchmarks PRIVATE 
    -ffast-math
    -funroll-loops
  )
  target_compile_options(basic_benchmarks PRIVATE 
    -ffast-math
    -funroll-loops
  )
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(matching_engine_benchmarks PRIVATE 
    /fp:fast
    /Qpar
    /Qfast_transcendentals
  )
  target_compile_options(basic_benchmarks PRIVATE 
    /fp:fast
    /Qpar
    /Qfast_transcendentals
  )
endif()

target_compile_definitions(matching_engine_benchmarks PRIVATE BENCHMARK_MODE)
target_compile_definitions(basic_benchmarks PRIVATE BENCHMARK_MODE)
target_compile_definitions(matching_engine_tests PRIVATE DEBUG_MODE)

add_custom_target(run_benchmarks
    COMMAND $<TARGET_FILE:matching_engine_benchmarks>
    DEPENDS matching_engine_benchmarks
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running performance benchmarks..."
)
add_custom_target(run_basic_benchmarks
    COMMAND $<TARGET_FILE:basic_benchmarks>
    DEPENDS basic_benchmarks
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Running basic performance benchmarks..."
)

message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")