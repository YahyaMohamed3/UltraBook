//Engine class declaration

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <map>
#include <deque>
#include <vector>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>
#include <memory>
#include <atomic>
#include "types.hpp"
#include <chrono>
#include <optional>
#include <oneapi/tbb/concurrent_unordered_map.h>
#include <oneapi/tbb/concurrent_map.h>

namespace ultraBook {

using TradeQueue = ZeroCopyQueue<Trade>;

class MatchingEngine {
public:
    MatchingEngine();
    ~MatchingEngine();

    void addLimitOrder(int orderId, double price, int quantity, bool isBuy);
    void addMarketOrder(int orderId, int quantity, bool isBuy);
    void addIOCOrder(int orderId, double price, int quantity, bool isBuy);
    void addFOKOrder(int orderId, double price, int quantity, bool isBuy);
    void cancelOrder(int orderId);
    void printOrderBook() const;
    void printTradelog();
    void matchOrders();
    void setOrderStatus(Order* order, OrderStatus newStatus);
    void processTradeQueue();

private:
    // Thread-safe price level caching with SIMD optimization
    alignas(64) PriceLevel bestBuyPrices;
    alignas(64) PriceLevel bestSellPrices;

    // Price level caching with SIMD alignment
    alignas(32) struct PriceLevelCache {
        __m256d prices;
        __m256i quantities;
        __m256i orderCounts;
    } buyCache_, sellCache_;

    // Price caching
    alignas(64) std::atomic<double> bestBidPrice{0.0};
    alignas(64) std::atomic<double> bestAskPrice{std::numeric_limits<double>::max()};

    // Performance metrics
    alignas(64) struct Metrics {
        std::atomic<uint64_t> totalOrders{0};
        std::atomic<uint64_t> totalTrades{0};
        std::atomic<uint64_t> avgLatencyNs{0};
    } metrics_;

    alignas(64) std::atomic<uint64_t> lastUpdateTime_{0};

    // Lock-free order storage
    oneapi::tbb::concurrent_unordered_map<int, Order*> orderMap;
    MemoryPool<Order> orderPool;
    tbb::concurrent_vector<Trade> tradeLog;

    // Zero-copy trade queue
    TradeQueue tradeQueue;

    // Lock-free order books using our custom containers
    oneapi::tbb::concurrent_map<double, std::unique_ptr<LockFreeLevel<Order>>, std::greater<>> buyOrders;
    oneapi::tbb::concurrent_map<double, std::unique_ptr<LockFreeLevel<Order>>> sellOrders;

    // SIMD batch matching
    void batchMatchOrders();
    
    // Helper methods
    void updateBestPrices();
    void updateBestBidPrice();
    void updateBestAskPrice();
    void processTradeQueue();
    void updateOrderStatus(Order* order);
    void matchPriceLevels(LockFreeLevel<Order>* buyLevel, LockFreeLevel<Order>* sellLevel, double price);

    // Trade execution helper that handles synchronization
    void executeTrade(Order* buyOrder, Order* sellOrder, double price, int quantity);

    // SIMD helpers
    void updatePriceLevelCache(double price, int quantity, bool isBuy);
    double calculateTotalValue(bool isBuy) const;

    // High-precision timestamp
    inline uint64_t getCurrentTimestamp() const {
        return rdtsc();
    }
    uint64_t getLatencyNs(uint64_t startTime) const;
    static uint64_t getCpuFrequency();

    // Memory management constants
    static constexpr size_t INITIAL_ORDERS_RESERVE = 10000;
    static constexpr size_t INITIAL_TRADES_RESERVE = 10000;
    static constexpr size_t PRICE_LEVELS_RESERVE = 1000;
    
    // SIMD batch processing
    static constexpr size_t SIMD_WIDTH = 4;  // AVX2 = 256-bit = 4 doubles
    alignas(32) double priceBuffer[SIMD_WIDTH];
    alignas(32) int quantityBuffer[SIMD_WIDTH];
};
}

#endif // ENGINE_HPP