//OrderBook class declaration 

#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <map>
#include <deque>
#include <unordered_map>
#include <optional>
#include <mutex>
#include "types.hpp"

namespace ultraBook {


class OrderBook {
public:
    enum class Side { BUY , SELL };

    explicit OrderBook(Side side);


    // Core operations
    void addOrder(Order order);
    bool cancelOrder(int orderId);
    std::optional<Order*> findOrder(int orderId);
    void modifyOrder(int orderId, const OrderModificationRequest& modRequest);
    std::optional<Order> getBestOrder() const;
    bool isEmpty() const;
    void clear();


    // Stop Order management
    void addStopOrder(Order order);
    void checkAndTrigger(double lastPrice);
    void convertStopToMarket(Order* order);
    void convertStopToLimit(Order* order);

    // Iceberg Order management 
    void replenishIcebergOrder(Order* order);

    // GTD Exiration
    void checkExpiredOrders();

    // order Book state
    void printOrderBook() const;


private:
Side side_;

mutable std::mutex bookMutex; // For thread saftey 


// Price levels: key is price , vlaue is deque of orders (FIFO)
std::map<double, std::deque<Order>, std::greater<>> priceLevels; // High-to-low for BUY, low-to-high for SELL
std::map<double, std::deque<Order>> stopOrders; // Stop orders, key is stop price


// fast lookup for orders by ID (maps orderId  -> {price/stopPrice, iterator})
std::unordered_map<int, std::pair<double, std::deque<Order>::iterator>> orderIndex;
std::unordered_map<int, std::pair<double, std::deque<Order>::iterator>> stopIndex;


void cleanPriceLevel(double price);
void cleanStopLevel(double stopPrice);

};




}// OrderBook class declaration
#endif // ORDERBOOK_HPP

