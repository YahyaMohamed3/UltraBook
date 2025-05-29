#ifndef DEBUG_HPP
#define DEBUG_HPP

#include <iostream>
#include <iomanip>

// Define BENCHMARK_MODE to disable all output during benchmarking
// This significantly improves benchmark performance by eliminating I/O overhead
#ifdef BENCHMARK_MODE
    // In benchmark mode, all logging macros are no-ops (compile to nothing)
    #define ENGINE_LOG(x) do {} while(0)
    #define ENGINE_DEBUG(x) do {} while(0)
    #define ENGINE_ERROR(x) do {} while(0)
    #define ENGINE_PRINT(x) do {} while(0)
#else
    // In normal mode, logging macros work as expected
    #define ENGINE_LOG(x) std::cout << x << std::endl
    #define ENGINE_DEBUG(x) std::cout << "[DEBUG] " << x << std::endl
    #define ENGINE_ERROR(x) std::cerr << "[ERROR] " << x << std::endl
    #define ENGINE_PRINT(x) std::cout << x << std::endl
#endif

#endif // DEBUG_HPP
