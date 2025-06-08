// OrderBook class declaration

#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <map>
#include <deque>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <functional>
#include "types.hpp"

namespace ultraBook {

class OrderBook {
public:
    enum class Side { BUY, SELL };

    explicit OrderBook(Side side);

    // Core order operations
    void addOrder(Order order); // Handles Limit, GTC, GTD, Iceberg
    bool cancelOrder(int orderId);
    std::optional<Order*> findOrder(int orderId);
    void modifyOrder(int orderId, const OrderModificationRequest& modRequest);
    std::optional<Order> getBestOrder() const;
    bool isEmpty() const;
    void clear();

    // Stop order operations
    void addStopOrder(Order order);
    void checkAndTrigger(double lastPrice);
    void convertStopToMarket(Order* order);
    void convertStopToLimit(Order* order);

    // Iceberg order visibility
    void replenishIcebergOrder(Order* order);

    // GTD expiration
    void checkExpiredOrders();

    // Optional: remove expired stop orders if using expiry on them
    void removeExpiredStopOrders();

    // Diagnostics
    void printOrderBook() const;
    std::vector<Order> getAllOrders() const;

private:
    Side side_;
    mutable std::mutex bookMutex;

    // Dynamic comparator for BUY (high-to-low) or SELL (low-to-high)
    std::map<double, std::deque<Order>, std::function<bool(double, double)>> priceLevels;

    // Stop order book (always low-to-high for consistency)
    std::map<double, std::deque<Order>> stopOrders;

    // Fast lookup: orderId -> {price/stopPrice, iterator}
    std::unordered_map<int, std::pair<double, std::deque<Order>::iterator>> orderIndex;
    std::unordered_map<int, std::pair<double, std::deque<Order>::iterator>> stopIndex;

    // Cleanup helpers
    void cleanPriceLevel(double price);
    void cleanStopLevel(double stopPrice);
};

} // namespace ultraBook

#endif // ORDERBOOK_HPP
