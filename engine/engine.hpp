//Engine class declaration

#ifndef ENGINE_HPP
#define ENGINE_HPP

#include <map>
#include <deque>
#include <vector>
#include <unordered_map>
#include "types.hpp"
#include<chrono>
#include<chrono>

namespace ultraBook{
class MatchingEngine{
public:
    MatchingEngine();
    void addLimitOrder(int orderId, double price, int quantity, bool isBuy);
    void addMarketOrder(int orderId, int quantity, bool isBuy, bool isConverted = false, Order* existingOrder = nullptr);
    void addGTCOrder(int orderId, double price, int quantity, bool isBuy);
    void addGTDOrder(int orderId, double price, int quantity, bool isBuy, std::chrono::system_clock::time_point expiry);
    void addStopOrder(int orderId, double stopPrice, int quantity, bool isBuy);
    void addStopLimitOrder(int orderId, double stopPrice, double limitPrice, int quantity, bool isBuy);
    void addIOCOrder(int orderId, double price, int quantity, bool isBuy);
    void addFOKOrder(int orderId, double price, int quantity, bool isBuy);
    void addIcebergOrder(int orderId, double price, int quantity, int visibleQuantity, int replenishQuantity, bool isBuy);
    void cancelOrder(int orderId);
    void printOrderBook() const;
    void printTradelog();
    void matchOrders();
    void setOrderStatus(Order* order, OrderStatus newStatus);
    OrderStatus getOrderStatus(int orderId) const;
    void convertStopToMarket(Order* order);
    void checkandTrigger(double lastprice);
    void replenishIcebergOrder(Order* order);
    void convertStopToLimit(Order* order);
    void checkExpiredOrders(); // New method to check expired GTD orders
    void ModifyOrder(int OrderId , double newPrice , int newQuantity);

private:
    // Internal data structures for buy/sell order books
    double lastPrice = 0.0;
    std::map<double, std::deque<Order>, std::greater<>> buyOrders; // High-to-low
    std::map<double, std::deque<Order>> sellOrders; // Low-to-high default
    std::map<double, std::deque<Order>> buyStopOrders; // Buy Orders
    std::map<double, std::deque<Order>> sellStopOrders; // Sell Orders

    // Order ID to order pointer (for fast lookup/cancel)
    std::unordered_map<int, Order*> orderMap;
    std::vector<Trade> tradeLog;
    std::unordered_map<int , std::vector<Trade>> tradesByOrderId;
    std::unordered_map<int, Order> allOrdersMap; 
};
}
#endif // ENGINE_HPP
// engine.hpp