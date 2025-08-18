#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <map>
#include <deque>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <functional>
#include <vector>
#include <chrono>
#include "types.hpp"

namespace ultraBook {

class OrderBook {
public:
    enum class Side { BUY, SELL };

    explicit OrderBook(Side side);

    // --- Core order operations ---
    void addOrder(Order order);
    bool cancelOrder(int orderId);
    std::optional<Order*> findOrder(int orderId) const;
    void modifyOrder(int orderId, const OrderModificationRequest& modRequest);
    std::optional<Order> getBestOrder() const;
    bool isEmpty() const;
    void clear();

    // --- Stop order operations ---
    void addStopOrder(Order order);
    void checkAndTrigger(double lastPrice);
    void convertStopToMarket(Order* order);
    void convertStopToLimit(Order* order);

    // --- Iceberg order visibility ---
    void replenishIcebergOrder(Order* order);

    // --- GTD expiration ---
    void checkExpiredOrders();
    void removeExpiredStopOrders();

    // --- Utility ---
    void printOrderBook() const;
    std::vector<Order> getAllOrders() const;

    // Retrieve and clear triggered stop/stop-limit orders (for engine reprocessing) 
    std::vector<Order> getAndClearTriggeredOrders();

private:
    // --- Internal data structures ---
    using PriceLevel = std::deque<Order>;
    using PriceMap = std::map<double, PriceLevel, std::function<bool(double, double)>>;

    mutable std::mutex bookMutex;
    PriceMap priceLevels;
    Side side_;

    // Maps orderId to (price, iterator in deque)
    std::unordered_map<int, std::pair<double, PriceLevel::iterator>> orderIndex;

    // --- Stop orders ---
    std::map<double, std::deque<Order>> stopOrders;
    std::unordered_map<int, std::pair<double, std::deque<Order>::iterator>> stopIndex;

    // --- For triggered stop/stop-limit orders ---
    std::vector<Order> triggeredOrders_;

    std::function<bool(double, double)> comp_;

    // --- Internal clean-up helpers ---
    void cleanPriceLevel(double price);
    void cleanStopLevel(double stopPrice);
};

} // namespace ultraBook

#endif // ORDERBOOK_HPP
