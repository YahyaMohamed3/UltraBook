#ifndef TYPES_HPP
#define TYPES_HPP



#include<chrono>

struct Order {
    int orderId;
    double price;
    int quantity;
    bool isBuy;
    std::chrono::high_resolution_clock::time_point timestamp;

    Order(int id, double p, int q, bool side)
        : orderId(id) , price(p), quantity(q), isBuy(side),
        timestamp(std::chrono::high_resolution_clock::now()){}

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


#endif