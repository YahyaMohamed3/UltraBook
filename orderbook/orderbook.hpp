#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <map>
#include <deque>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <vector>
#include <chrono>
#include "types.hpp"

namespace ultraBook {

class OrderBook {
public:
    enum class Side { BUY, SELL };

    explicit OrderBook(Side side);

    // Core API (thread-safe)
    void addOrder(Order order);
    bool cancelOrder(int orderId);
    std::optional<Order*> findOrder(int orderId);   // non-const: returns modifiable pointer
    void modifyOrder(int orderId, const OrderModificationRequest& modRequest);
    std::optional<Order> getBestOrder() const;      // snapshot copy
    bool isEmpty() const;
    void clear();

    // Stop orders
    void addStopOrder(Order order);
    void checkAndTrigger(double lastPrice);

    // Iceberg
    void replenishIcebergOrder(Order* order);

    // GTD / housekeeping
    void checkExpiredOrders();
    void removeExpiredStopOrders();

    // Utilities
    void printOrderBook() const;
    std::vector<Order> getAllOrders() const;
    std::vector<Order> getAndClearTriggeredOrders();

private:
    struct Level {
        std::deque<Order> queue;
        mutable std::mutex mtx;  // protects only this price level
    };

    using PriceMap = std::map<double, Level, std::function<bool(double, double)>>;

    // Price tree: shared for lookup, unique for insert/erase
    mutable std::shared_mutex treeMutex;
    PriceMap                  priceLevels;
    Side                      side_;
    std::function<bool(double, double)> comp_;  // comparator (BUY: desc, SELL: asc)

    // Order index: orderId -> price. Find within level under level lock.
    std::unordered_map<int, double> orderPriceIndex;
    mutable std::shared_mutex       orderIndexMutex;

    // Stop orders follow the same structure
    struct StopLevel {
        std::deque<Order> queue;
        mutable std::mutex mtx;
    };

    mutable std::shared_mutex       stopTreeMutex;
    std::map<double, StopLevel>     stopOrders;     // keyed by stop price (ascending)
    std::unordered_map<int, double> stopPriceIndex; // orderId -> stop price
    mutable std::shared_mutex       stopIndexMutex;

    std::vector<Order> triggeredOrders_;            // hand-off buffer

    // Helpers (call without holding level locks)
    void cleanPriceLevel(double price);
    void cleanStopLevel(double stopPrice);
};

} // namespace ultraBook

#endif // ORDERBOOK_HPP
