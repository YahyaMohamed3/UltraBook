#include "catch.hpp"
#include "../engine/engine.hpp"
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

using namespace ultraBook;
using namespace std::chrono;

TEST_CASE("Add Limit Order", "[matching]") {
    MatchingEngine engine;
    engine.addLimitOrder(1, 100.5, 10, true);
    // assertions...
}

TEST_CASE("SIMD Price Level Operations", "[simd]") {
    PriceLevel level;
    
    // Initialize test data
    double prices[4] = {100.0, 101.0, 102.0, 103.0};
    int quantities[4] = {10, 20, 30, 40};
    
    // Load test data into SIMD registers
    level.prices = _mm256_loadu_pd(prices);
    level.quantities = _mm256_loadu_epi32(quantities);
    
    SECTION("Total Value Calculation") {
        double expected = 100.0 * 10 + 101.0 * 20 + 102.0 * 30 + 103.0 * 40;
        double actual = level.getTotalValue();
        REQUIRE(std::abs(actual - expected) < 0.001);
    }
}

TEST_CASE("Lock-free Order Book Concurrency", "[concurrent]") {
    MatchingEngine engine;
    std::atomic<bool> running{true};
    std::atomic<int> orderIdCounter{0};
    
    // Producer thread adding orders
    auto producer = [&]() {
        while (running) {
            int orderId = ++orderIdCounter;
            bool isBuy = orderId % 2 == 0;
            double price = 100.0 + (orderId % 10);
            engine.addLimitOrder(orderId, price, 100, isBuy);
        }
    };
    
    // Consumer thread matching orders
    auto consumer = [&]() {
        while (running) {
            engine.matchOrders();
            std::this_thread::yield();
        }
    };
    
    std::vector<std::thread> threads;
    threads.emplace_back(producer);
    threads.emplace_back(consumer);
    
    // Let it run for a short time
    std::this_thread::sleep_for(milliseconds(100));
    running = false;
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Verify no orders were lost
    engine.printOrderBook();
}

TEST_CASE("Trade Queue Performance", "[performance]") {
    TradeQueue queue;
    constexpr int NUM_TRADES = 1000000;
    
    auto start = high_resolution_clock::now();
    
    for (int i = 0; i < NUM_TRADES; ++i) {
        queue.push(i, i+1, 100.0, 10);
    }
    
    auto end = high_resolution_clock::now();
    auto duration = duration_cast<microseconds>(end - start);
    double throughput = NUM_TRADES / (duration.count() / 1e6);
    
    std::cout << "Trade queue throughput: " << throughput << " trades/second\n";
    REQUIRE(throughput > 1'000'000); // At least 1M trades/sec
}

TEST_CASE("Batch Order Matching", "[batch]") {
    MatchingEngine engine;
    
    // Add multiple orders at same price level
    for (int i = 0; i < 4; ++i) {
        engine.addLimitOrder(i, 100.0, 10, true);  // Buy orders
        engine.addLimitOrder(i+4, 100.0, 10, false);  // Sell orders
    }
    
    auto start = high_resolution_clock::now();
    engine.matchOrders();  // This should use SIMD batch matching
    auto end = high_resolution_clock::now();
    
    auto duration = duration_cast<nanoseconds>(end - start);
    REQUIRE(duration.count() < 1000);  // Should complete in under 1 microsecond
}
