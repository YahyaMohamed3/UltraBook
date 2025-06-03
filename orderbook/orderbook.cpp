// orderbook.cpp 

#include "orderbook.hpp"
#include<iostream>
#include <algorithm>
#include "debug.hpp"

namespace ultraBook {


OrderBook::OrderBook(Side side) : side_(side){}

void OrderBook::addOrder(Order order){
    std::lock_guard<std::mutex> lock(bookMutex);
    double price = order.price.value();
    priceLevels[price].push_back(order);
    auto it = --priceLevels[price].end();
    orderIndex[order.orderId] = {price , it};
}

bool OrderBook::cancelOrder(int orderId){
    std::lock_guard<std::mutex> lock(bookMutex);
    auto it = orderIndex.find(orderId);
    if(it == orderIndex.end()) return false;
    auto [price, iter] = it -> second;
    priceLevels[price].erase(iter);
    cleanPriceLevel(price);
    orderIndex.erase(it);
    return true;
}

std::optional<Order *> OrderBook::findOrder(int orderId){
    std::lock_guard<std::mutex> lock(bookMutex);
    auto it = orderIndex.find(orderId);
    if(it == orderIndex.end()) return std::nullopt;
    return &(*it->second.second);
}

void OrderBook::modifyOrder(int orderId, const OrderModificationRequest& modRequest) {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto it = orderIndex.find(orderId);
    if (it == orderIndex.end()) {
        ENGINE_ERROR("[modifyOrder] OrderID: " << orderId << " not found.");
        return;
    }

    auto [oldPrice, orderIt] = it->second;
    Order orderCopy = *orderIt;  // Copy for safe editing
    bool priceChanged = false;
    bool quantityIncreased = false;
    bool needsRepositioning = false;

    if (modRequest.newPrice && modRequest.newPrice != oldPrice) {
        orderCopy.price = modRequest.newPrice;
        priceChanged = true;
        ENGINE_LOG("[modifyOrder] OrderID: " << orderId << " price changed to " << *modRequest.newPrice);
    }
    if (modRequest.newQuantity) {
        if (modRequest.newQuantity > orderCopy.quantity) quantityIncreased = true;
        orderCopy.quantity = *modRequest.newQuantity;
        ENGINE_LOG("[modifyOrder] OrderID: " << orderId << " quantity changed to " << *modRequest.newQuantity);
    }
    if (modRequest.newStopPrice) {
        orderCopy.stopPrice = modRequest.newStopPrice;
        priceChanged = true;  // For stop price change, consider reposition
    }
    if (modRequest.newVisibleQuantity) orderCopy.visibleQuantity = modRequest.newVisibleQuantity;
    if (modRequest.newReplenishQuantity) orderCopy.replenishQuantity = modRequest.newReplenishQuantity;
    if (modRequest.newExpiry) orderCopy.expiry = modRequest.newExpiry;
    if (modRequest.newStatus) orderCopy.status = *modRequest.newStatus;

    needsRepositioning = priceChanged || quantityIncreased;

    if (needsRepositioning) {
        priceLevels[oldPrice].erase(orderIt);
        cleanPriceLevel(oldPrice);

        double newPrice = orderCopy.price.value_or(oldPrice);  // fallback
        priceLevels[newPrice].push_back(orderCopy);
        auto newIt = --priceLevels[newPrice].end();
        orderIndex[orderId] = {newPrice, newIt};

        // Update timestamp to reflect reposition
        orderCopy.timestamp = std::chrono::high_resolution_clock::now();

        ENGINE_LOG("[modifyOrder] OrderID: " << orderId << " repositioned at price " << newPrice);
    } else {
        *orderIt = orderCopy;  // Modify in place if no reposition needed
    }
}














}