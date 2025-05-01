//Engine class declaration

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <map>
#include <deque>
#include <unordered_map>

namespace ultraBook{
struct Order {
    int orderId;
    double price;
    int quantity;
    bool isBuy;

};

class MatchingEngine {
public:
    MatchingEngine();
    void addLimitOrder(int orderId, double price, int quantity, bool isBuy);
    void cancelOrder(int orderId);
    void printOrderBook() const;
    void matchOrders();

private:
    // Internal data structures for buy/sell order books
    std::map<double, std::deque<Order>, std::greater<>> buyOrders; // High-to-low
    std::map<double, std::deque<Order>> sellOrders; // Low-to-high (default is std::less<>)

    // Order ID to order pointer (for fast lookup/cancel)
    std::unordered_map<int, Order*> orderMap;
};
}

#endif // ENGINE_HPP