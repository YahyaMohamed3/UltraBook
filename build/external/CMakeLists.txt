cmake_minimum_required(VERSION 3.15)
project(ultrabook_engine)

# Enable C++20
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Add SIMD optimization flags
if(MSVC)
    add_compile_options(/arch:AVX2 /O2)
    add_compile_definitions(_CRT_SECURE_NO_WARNINGS)
    include_directories("${CMAKE_SOURCE_DIR}/../engine")
    if(EXISTS "C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/LLVM/include")
        include_directories("C:/Program Files (x86)/Microsoft Visual Studio/2019/Community/VC/Tools/LLVM/include")
    endif()
else()
    add_compile_options(-mavx2 -O3)
endif()

# Find TBB package for concurrent containers
find_package(TBB REQUIRED)

# Add engine library
add_library(engine
    ${CMAKE_SOURCE_DIR}/engine/engine.cpp
    ${CMAKE_SOURCE_DIR}/engine/types.hpp
    ${CMAKE_SOURCE_DIR}/engine/engine.hpp
)

target_link_libraries(engine PRIVATE TBB::tbb)

# Include directories
target_include_directories(engine PUBLIC 
    ${CMAKE_SOURCE_DIR}/engine
    ${CMAKE_SOURCE_DIR}/build/external
)

# Fetch and configure Catch2 for tests
include(FetchContent)
FetchContent_Declare(
    catch
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.3
)
FetchContent_MakeAvailable(catch)

# Add test executable
add_executable(engine_tests
    ${CMAKE_SOURCE_DIR}/tests/engine/test_main.cpp
    ${CMAKE_SOURCE_DIR}/tests/engine/test_matching_engine.cpp
)

target_link_libraries(engine_tests PRIVATE 
    engine
    Catch2::Catch2WithMain
    TBB::tbb
)
