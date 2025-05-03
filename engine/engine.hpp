//Engine class declaration

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <map>
#include <deque>
#include <unordered_map>
#include "types.hpp"

namespace ultraBook{
class MatchingEngine {
public:
    MatchingEngine();
    void addLimitOrder(int orderId, double price, int quantity, bool isBuy);
    void addMarketOrder(int orderId, int qunatity, bool isBuy);
    void cancelOrder(int orderId);
    void printOrderBook() const;
    void printTradelog() const;
    void matchOrders();

private:
    // Internal data structures for buy/sell order books
    std::map<double, std::deque<Order>, std::greater<>> buyOrders; // High-to-low
    std::map<double, std::deque<Order>> sellOrders; // Low-to-high default

    // Order ID to order pointer (for fast lookup/cancel)
    std::unordered_map<int, Order*> orderMap;
    std::vector<Trade> tradeLog;
    std::unordered_map<int , std::vector<Trade>> tradesByOrderId;
};
}

#endif // ENGINE_HPP