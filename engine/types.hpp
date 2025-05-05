#ifndef TYPES_HPP
#define TYPES_HPP



#include<chrono>
#include<optional>
#include<iomanip>
#include<iostream>
enum class OrderType{
    LIMIT, 
    MARKET

};

struct Order {
    int orderId;
    std::optional<double> price;
    int quantity;
    bool isBuy;
    OrderType type;
    std::chrono::high_resolution_clock::time_point timestamp;

    Order(int id, std::optional<double> p, int q, bool side, OrderType orderType)
        : orderId(id), price(p), quantity(q), isBuy(side), type(orderType),
        timestamp(std::chrono::high_resolution_clock::now()) {}

};

struct Trade{
    int buyOrderId;
    int sellOrderId;
    double price;
    int quantity;
    std::chrono::high_resolution_clock::time_point timestamp;


    Trade(int buyId, int sellId, double p, int q)
        : buyOrderId(buyId), sellOrderId(sellId), price(p), quantity(q),
          timestamp(std::chrono::high_resolution_clock::now()) {}
};

std::ostream& operator<<(std::ostream& os, const Trade& t) {
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t.timestamp.time_since_epoch()).count();
    os << "Trade | BuyID: " << t.buyOrderId
       << ", SellID: " << t.sellOrderId
       << ", Price: " << std::fixed << std::setprecision(2) << t.price
       << ", Qty: " << t.quantity
       << ", Timestamp: " << ms << "µs";
    return os;
}


#endif