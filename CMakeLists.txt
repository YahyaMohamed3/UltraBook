cmake_minimum_required(VERSION 3.14)
project(FastChain VERSION 0.1.0)

# Global options
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON) # good for static libs linked into shared libs

# Default to Release for better perf metrics
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

# Optimizations for Release builds
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O3 -march=native -fno-omit-frame-pointer")
  set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -flto")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(CMAKE_CXX_FLAGS_RELEASE "/O2 /Ob2 /Oi /Ot /Oy /GL")
  set(CMAKE_EXE_LINKER_FLAGS_RELEASE "/LTCG")
  set(CMAKE_SHARED_LINKER_FLAGS_RELEASE "/LTCG")
  add_compile_options(/EHsc)
endif()

# ---------------- Core engine ----------------
add_library(engine
  engine/engine.cpp
  orderbook/orderbook.cpp
)

target_include_directories(engine PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/engine
  ${CMAKE_CURRENT_SOURCE_DIR}/orderbook   # ensure orderbook headers visible
)

if(WIN32)
  target_link_libraries(engine PRIVATE winmm) # timeBeginPeriod on Windows
endif()

# ---------------- Agents & Data libs ----------------
add_library(agents
  agents/market_maker_bot.cpp
  agents/momentum_bot.cpp
  agents/arbitrage_bot.cpp
)
target_include_directories(agents PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/engine
  ${CMAKE_CURRENT_SOURCE_DIR}/orderbook
  ${CMAKE_CURRENT_SOURCE_DIR}/data
)
target_link_libraries(agents PRIVATE engine)

add_library(market_data
  data/csv_feed.cpp
  data/replay.cpp
)
target_include_directories(market_data PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/engine
  ${CMAKE_CURRENT_SOURCE_DIR}/orderbook
)
target_link_libraries(market_data PRIVATE engine)

# ---------------- Testing & Benchmark deps ----------------
include(FetchContent)

# googletest
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

# google/benchmark
FetchContent_Declare(
  benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG v1.7.1
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "Disable benchmark's tests" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "Disable benchmark's gtest tests" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Disable benchmark install" FORCE)

# Force google/benchmark to build in Release (MSVC special case)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  set(CMAKE_CXX_FLAGS_DEBUG_BACKUP ${CMAKE_CXX_FLAGS_DEBUG})
  set(CMAKE_C_FLAGS_DEBUG_BACKUP ${CMAKE_C_FLAGS_DEBUG})
  set(CMAKE_CXX_FLAGS_DEBUG "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "Debug forced to Release" FORCE)
  set(CMAKE_C_FLAGS_DEBUG "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "Debug forced to Release" FORCE)
  set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "" FORCE)
  set(CMAKE_C_FLAGS_RELWITHDEBINFO "/O2 /Ob2 /DNDEBUG /MT" CACHE STRING "" FORCE)
endif()

# Force single Release configuration during fetch of benchmark
set(CMAKE_BUILD_TYPE_BACKUP ${CMAKE_BUILD_TYPE})
set(CMAKE_CONFIGURATION_TYPES_BACKUP ${CMAKE_CONFIGURATION_TYPES})
set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Forced Release for benchmark" FORCE)
set(CMAKE_CONFIGURATION_TYPES "Release" CACHE STRING "Only Release" FORCE)
add_compile_definitions(NDEBUG)

FetchContent_MakeAvailable(googletest benchmark)

# Restore original settings
set(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE_BACKUP} CACHE STRING "" FORCE)
set(CMAKE_CONFIGURATION_TYPES ${CMAKE_CONFIGURATION_TYPES_BACKUP} CACHE STRING "" FORCE)
if(CMAKE_CXX_COMPILER_ID MATCHES "MSVC" AND DEFINED CMAKE_CXX_FLAGS_DEBUG_BACKUP)
  set(CMAKE_CXX_FLAGS_DEBUG ${CMAKE_CXX_FLAGS_DEBUG_BACKUP} CACHE STRING "" FORCE)
  set(CMAKE_C_FLAGS_DEBUG ${CMAKE_C_FLAGS_DEBUG_BACKUP} CACHE STRING "" FORCE)
endif()

message(STATUS "Google Benchmark configured with FORCED RELEASE mode")

# ---------------- Unit Tests ----------------
enable_testing()
include(GoogleTest)

# Engine tests
add_executable(matching_engine_tests
  tests/engine/test_matching_engine.cpp
)
target_link_libraries(matching_engine_tests
  PRIVATE engine gtest gtest_main
)
gtest_discover_tests(matching_engine_tests)

# OrderBook tests
add_executable(orderbook_tests
  tests/orderbook/test_orderbook.cpp
)
target_link_libraries(orderbook_tests
  PRIVATE engine gtest_main
)
gtest_discover_tests(orderbook_tests)

# OrderBook concurrency tests
add_executable(orderbook_concurrency_tests
  tests/orderbook/test_orderbook_concurrency.cpp
)
target_link_libraries(orderbook_concurrency_tests
  PRIVATE engine gtest_main
)
gtest_discover_tests(orderbook_concurrency_tests)

# ---- NEW: Data tests ----
add_executable(test_csv_feed
  tests/data/test_csv_feed.cpp
)
target_link_libraries(test_csv_feed
  PRIVATE market_data gtest gtest_main
)
# ensure relative CSV path (data/test_ticks.csv) resolves
gtest_discover_tests(test_csv_feed
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

add_executable(test_replay
  tests/data/test_replay.cpp
)
target_link_libraries(test_replay
  PRIVATE market_data agents engine gtest gtest_main
)
gtest_discover_tests(test_replay
  WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
)

# ---- NEW: Agent tests ----
add_executable(test_market_maker
  tests/agents/test_market_maker_bot.cpp
)
target_link_libraries(test_market_maker
  PRIVATE agents engine gtest gtest_main
)
gtest_discover_tests(test_market_maker)

add_executable(test_momentum
  tests/agents/test_momentum_bot.cpp
)
target_link_libraries(test_momentum
  PRIVATE agents engine gtest gtest_main
)
gtest_discover_tests(test_momentum)

add_executable(test_arbitrage
  tests/agents/test_arbitrage_bot.cpp
)
target_link_libraries(test_arbitrage
  PRIVATE agents engine gtest gtest_main
)
gtest_discover_tests(test_arbitrage)

# If you added test-only hooks (#ifdef ULTRABOOK_TEST), enable them:
target_compile_definitions(test_csv_feed     PRIVATE ULTRABOOK_TEST)
target_compile_definitions(test_replay       PRIVATE ULTRABOOK_TEST)
target_compile_definitions(test_market_maker PRIVATE ULTRABOOK_TEST)
target_compile_definitions(test_momentum     PRIVATE ULTRABOOK_TEST)
target_compile_definitions(test_arbitrage    PRIVATE ULTRABOOK_TEST)

# Keep existing DEBUG_MODE on engine tests
target_compile_definitions(matching_engine_tests PRIVATE DEBUG_MODE)

# ---------------- Benchmarks ----------------
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
add_executable(stress_benchmarks
  benchmarks/engine/stress_benchmarks.cpp
)

target_link_libraries(matching_engine_benchmarks PRIVATE engine benchmark::benchmark)
target_link_libraries(basic_benchmarks          PRIVATE engine benchmark::benchmark)
target_link_libraries(stress_benchmarks         PRIVATE engine benchmark::benchmark)
target_compile_features(stress_benchmarks PRIVATE cxx_std_20)

if(WIN32)
  target_link_libraries(matching_engine_benchmarks PRIVATE winmm)
  target_link_libraries(basic_benchmarks          PRIVATE winmm)
  target_link_libraries(stress_benchmarks         PRIVATE winmm)
endif()

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(matching_engine_benchmarks PRIVATE -ffast-math -funroll-loops)
  target_compile_options(basic_benchmarks          PRIVATE -ffast-math -funroll-loops)
  target_compile_options(stress_benchmarks         PRIVATE -ffast-math -funroll-loops)
elseif(CMAKE_CXX_COMPILER_ID MATCHES "MSVC")
  target_compile_options(matching_engine_benchmarks PRIVATE /fp:fast /Qpar /Qfast_transcendentals)
  target_compile_options(basic_benchmarks          PRIVATE /fp:fast /Qpar /Qfast_transcendentals)
  target_compile_options(stress_benchmarks         PRIVATE /fp:fast /Qpar /Qfast_transcendentals)
endif()

# Compile-time markers for benches
target_compile_definitions(matching_engine_benchmarks PRIVATE BENCHMARK_MODE)
target_compile_definitions(basic_benchmarks          PRIVATE BENCHMARK_MODE)
target_compile_definitions(stress_benchmarks         PRIVATE BENCHMARK_MODE)

# Convenience targets
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
add_custom_target(run_stress_benchmarks
  COMMAND $<TARGET_FILE:stress_benchmarks> --benchmark_counters_tabular=true
  DEPENDS stress_benchmarks
  WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  COMMENT "Running stress benchmarks..."
)

message(STATUS "Release flags: ${CMAKE_CXX_FLAGS_RELEASE}")
