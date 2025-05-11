# Engine Core

## Overview
The matching engine core implements a high-performance order matching system using:
- Lock-free data structures
- SIMD optimizations
- Custom memory pools
- Zero-copy messaging

## Key Components
- `engine.hpp/cpp`: Main matching engine implementation
- `types.hpp`: Core data structures and memory pools
- `orderbook.hpp`: Price level and order book implementations

## Performance Characteristics
- Order processing: <300ns latency
- Memory efficiency: Zero-copy operations
- SIMD utilization: 4-wide price level processing

## last steps are benchmakring then working on the top optimizations 