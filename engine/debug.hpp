#ifndef DEBUG_HPP
#define DEBUG_HPP

#include <iostream>
#include <iomanip>

// Logging is OFF by default. It is enabled only when DEBUG_MODE is defined.
// This avoids any I/O in benchmarks and Release builds, even if the engine
// is compiled as a separate library without BENCHMARK_MODE visible.

#ifdef DEBUG_MODE
  #define ENGINE_LOG(x)   do { std::cout << x << std::endl; } while(0)
  #define ENGINE_DEBUG(x) do { std::cout << "[DEBUG] " << x << std::endl; } while(0)
  #define ENGINE_ERROR(x) do { std::cerr << "[ERROR] " << x << std::endl; } while(0)
  #define ENGINE_PRINT(x) do { std::cout << x << std::endl; } while(0)
#else
  #define ENGINE_LOG(x)   do {} while(0)
  #define ENGINE_DEBUG(x) do {} while(0)
  #define ENGINE_ERROR(x) do {} while(0)
  #define ENGINE_PRINT(x) do {} while(0)
#endif

#endif // DEBUG_HPP
