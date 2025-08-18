#include "../orderbook/orderbook.hpp"
#include <iostream>
#include <algorithm>
#include "debug.hpp"
#include "types.hpp"

namespace ultraBook {

OrderBook::OrderBook(Side side)
  : priceLevels(side == Side::BUY ? PriceComp(true) : PriceComp(false))
  , side_{side}
{
    ENGINE_LOG("[OrderBook] Constructor, side: " << (side == Side::BUY ? "BUY" : "SELL"));
}

void OrderBook::addOrder(Order order) {
    ENGINE_LOG("[addOrder] OrderID: " << order.orderId << ", type: " << static_cast<int>(order.type)
               << ", price: " << (order.price.has_value() ? order.price.value() : -1)
               << ", qty: " << order.quantity << ", isBuy: " << order.isBuy);
    std::lock_guard<std::mutex> lock(bookMutex);
    if (!order.price.has_value()) {
        ENGINE_ERROR("[addOrder] OrderID: " << order.orderId << " has no price");
        return;
    }
    double price = order.price.value();
    priceLevels[price].push_back(order);
    auto it = --priceLevels[price].end();
    orderIndex[order.orderId] = {price, it};
    ENGINE_LOG("[addOrder] Added OrderID: " << order.orderId << " to price level: " << price);
}

bool OrderBook::cancelOrder(int orderId) {
    ENGINE_LOG("[cancelOrder] OrderID: " << orderId);
    std::lock_guard<std::mutex> lock(bookMutex);
    auto it = orderIndex.find(orderId);
    if (it == orderIndex.end()) {
        ENGINE_LOG("[cancelOrder] OrderID: " << orderId << " not found in orderIndex");
        return false;
    }
    auto [price, iter] = it->second;
    priceLevels[price].erase(iter);
    cleanPriceLevel(price);
    orderIndex.erase(it);
    ENGINE_LOG("[cancelOrder] Removed OrderID: " << orderId << " from price level: " << price);
    return true;
}

std::optional<Order *> OrderBook::findOrder(int orderId) const {
    ENGINE_LOG("[findOrder] OrderID: " << orderId);
    std::lock_guard<std::mutex> lock(bookMutex);
    auto it = orderIndex.find(orderId);
    if (it == orderIndex.end()) {
        ENGINE_LOG("[findOrder] OrderID: " << orderId << " not found in orderIndex");
        return std::nullopt;
    }
    ENGINE_LOG("[findOrder] Found OrderID: " << orderId << " at price: " << it->second.first);
    return &(*it->second.second);
}

void OrderBook::modifyOrder(int orderId, const OrderModificationRequest& modRequest) {
    ENGINE_LOG("[modifyOrder] OrderID: " << orderId << ", newPrice: "
               << (modRequest.newPrice ? *modRequest.newPrice : -1) << ", newQty: "
               << (modRequest.newQuantity ? *modRequest.newQuantity : -1));
    std::lock_guard<std::mutex> lock(bookMutex);

    auto it = orderIndex.find(orderId);
    if (it == orderIndex.end()) {
        ENGINE_ERROR("[modifyOrder] OrderID: " << orderId << " not found in orderIndex");
        return;
    }

    auto [oldPrice, orderIt] = it->second;
    auto priceLevelIt = priceLevels.find(oldPrice);
    if (priceLevelIt == priceLevels.end() || orderIt == priceLevelIt->second.end()) {
        ENGINE_ERROR("[modifyOrder] Invalid price level or iterator for OrderID: " << orderId);
        orderIndex.erase(orderId);
        return;
    }

    Order orderCopy = *orderIt;
    ENGINE_LOG("[modifyOrder] Current state OrderID: " << orderId << ", price: "
               << (orderCopy.price ? *orderCopy.price : -1) << ", qty: " << orderCopy.quantity);

    std::optional<double> oldStopPrice = orderCopy.stopPrice;
    bool priceChanged = false;
    bool quantityIncreased = false;
    bool needsRepositioning = false;

    if (modRequest.newPrice && orderCopy.price != modRequest.newPrice) {
        orderCopy.price = modRequest.newPrice;
        priceChanged = true;
        ENGINE_LOG("[modifyOrder] OrderID: " << orderId << " price modified to " << *modRequest.newPrice);
    }
    if (modRequest.newQuantity) {
        if (*modRequest.newQuantity > orderCopy.quantity) quantityIncreased = true;
        orderCopy.quantity = *modRequest.newQuantity;
        ENGINE_LOG("[modifyOrder] OrderID: " << orderId << " quantity modified to " << *modRequest.newQuantity);
    }
    if (modRequest.newStopPrice) {
        orderCopy.stopPrice = modRequest.newStopPrice;
        priceChanged = true;
        ENGINE_LOG("[modifyOrder] OrderID: " << orderId << " stop price modified to " << *modRequest.newStopPrice);
    }
    if (modRequest.newVisibleQuantity) orderCopy.visibleQuantity = modRequest.newVisibleQuantity;
    if (modRequest.newReplenishQuantity) orderCopy.replenishQuantity = modRequest.newReplenishQuantity;
    if (modRequest.newExpiry) orderCopy.expiry = modRequest.newExpiry;

    needsRepositioning = priceChanged || quantityIncreased;

    if (orderCopy.price.has_value()) {
        priceLevels[oldPrice].erase(orderIt);
        cleanPriceLevel(oldPrice);
        orderIndex.erase(orderId);
        ENGINE_LOG("[modifyOrder] Removed OrderID: " << orderId << " from price level: " << oldPrice);
    } else if (oldStopPrice.has_value()) {
        auto itStop = stopOrders.find(oldStopPrice.value());
        if (itStop != stopOrders.end()) {
            auto& queue = itStop->second;
            queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Order& o) {
                return o.orderId == orderId;
            }), queue.end());
            cleanStopLevel(oldStopPrice.value());
            stopIndex.erase(orderId);
            ENGINE_LOG("[modifyOrder] Removed OrderID: " << orderId << " from stop level: " << oldStopPrice.value());
        }
    }

    if (needsRepositioning) {
        orderCopy.timestamp = std::chrono::high_resolution_clock::now();
        ENGINE_LOG("[modifyOrder] Updated timestamp for OrderID: " << orderId);
    }

    if (orderCopy.price.has_value()) {
        double newPrice = orderCopy.price.value();
        priceLevels[newPrice].push_back(orderCopy);
        auto newIt = --priceLevels[newPrice].end();
        orderIndex[orderId] = {newPrice, newIt};
        newIt->filledQuantity = orderCopy.filledQuantity;
        ENGINE_LOG("[modifyOrder] Re-inserted OrderID: " << orderId << " to price level: " << newPrice
                   << ", filled: " << newIt->filledQuantity);
    } else if (orderCopy.stopPrice.has_value()) {
        double newStop = orderCopy.stopPrice.value();
        stopOrders[newStop].push_back(orderCopy);
        auto newIt = --stopOrders[newStop].end();
        stopIndex[orderId] = {newStop, newIt};
        ENGINE_LOG("[modifyOrder] Re-inserted stop OrderID: " << orderId << " to stop level: " << newStop);
    } else {
        ENGINE_ERROR("[modifyOrder] OrderID: " << orderId << " has no valid price or stop price");
    }
}

std::optional<Order> OrderBook::getBestOrder() const {
    ENGINE_LOG("[getBestOrder] Side: " << (side_ == Side::BUY ? "BUY" : "SELL"));
    std::lock_guard<std::mutex> lock(bookMutex);

    if (priceLevels.empty()) {
        ENGINE_LOG("[getBestOrder] priceLevels is empty");
        return std::nullopt;
    }

    // Because the map comparator is:
    //  - descending for BUY (best/highest at begin())
    //  - ascending  for SELL (best/lowest  at begin())
    // the best price is always at begin() for both sides.
    auto it = priceLevels.begin();

    // (Rare) guard if top level got emptied but not cleaned yet.
    if (it->second.empty()) {
        ENGINE_LOG("[getBestOrder] Price level " << it->first << " is empty");
        return std::nullopt;
    }

    ENGINE_LOG("[getBestOrder] Returning OrderID: " << it->second.front().orderId
               << ", price: " << it->first);
    return it->second.front();
}

bool OrderBook::isEmpty() const {
    ENGINE_LOG("[isEmpty] Checking if order book is empty");
    std::lock_guard<std::mutex> lock(bookMutex);
    bool empty = priceLevels.empty();
    ENGINE_LOG("[isEmpty] Result: " << (empty ? "true" : "false"));
    return empty;
}

void OrderBook::clear() {
    ENGINE_LOG("[clear] Clearing order book");
    std::lock_guard<std::mutex> lock(bookMutex);
    priceLevels.clear();
    stopOrders.clear();
    orderIndex.clear();
    stopIndex.clear();
    triggeredOrders_.clear();
    ENGINE_LOG("[clear] Order book cleared");
}

void OrderBook::addStopOrder(Order order) {
    ENGINE_LOG("[addStopOrder] OrderID: " << order.orderId << ", type: " << static_cast<int>(order.type)
               << ", stopPrice: " << (order.stopPrice.has_value() ? order.stopPrice.value() : -1)
               << ", qty: " << order.quantity << ", isBuy: " << order.isBuy);
    std::lock_guard<std::mutex> lock(bookMutex);
    if (!order.stopPrice.has_value()) {
        ENGINE_ERROR("[addStopOrder] OrderID: " << order.orderId << " has no stop price");
        return;
    }
    double stopPrice = order.stopPrice.value();
    stopOrders[stopPrice].push_back(order);
    auto it = --stopOrders[stopPrice].end();
    stopIndex[order.orderId] = {stopPrice, it};
    ENGINE_LOG("[addStopOrder] Added OrderID: " << order.orderId << " to stop level: " << stopPrice);
}

void OrderBook::checkAndTrigger(double lastPrice) {
    ENGINE_LOG("[checkAndTrigger] lastPrice: " << lastPrice << ", side: "
               << (side_ == Side::BUY ? "BUY" : "SELL"));
    std::lock_guard<std::mutex> lock(bookMutex);
    for (auto it = stopOrders.begin(); it != stopOrders.end();) {
        bool trigger = side_ == Side::BUY ? lastPrice >= it->first : lastPrice <= it->first;
        ENGINE_LOG("[checkAndTrigger] Checking stop price: " << it->first
                   << ", trigger: " << (trigger ? "true" : "false"));
        if (!trigger) { ++it; continue; }

        auto &q = it->second;
        while (!q.empty()) {
            Order o = q.front(); q.pop_front();
            ENGINE_LOG("[checkAndTrigger] Processing OrderID: " << o.orderId
                       << ", type: " << static_cast<int>(o.type));
            stopIndex.erase(o.orderId);
            // Do NOT set status/type here!
            triggeredOrders_.push_back(o);
            ENGINE_LOG("[checkAndTrigger] Added OrderID: " << o.orderId << " to triggeredOrders");
        }
        it = stopOrders.erase(it);
    }
}

std::vector<Order> OrderBook::getAndClearTriggeredOrders() {
    ENGINE_LOG("[getAndClearTriggeredOrders] Retrieving triggered orders");
    std::lock_guard<std::mutex> lock(bookMutex);
    std::vector<Order> out(triggeredOrders_.begin(), triggeredOrders_.end());
    ENGINE_LOG("[getAndClearTriggeredOrders] Retrieved " << out.size() << " triggered orders");
    for (const auto& o : out) {
        ENGINE_LOG("[getAndClearTriggeredOrders] OrderID: " << o.orderId
                   << ", type: " << static_cast<int>(o.type) << ", price: "
                   << (o.price.has_value() ? o.price.value() : -1));
    }
    triggeredOrders_.clear();
    ENGINE_LOG("[getAndClearTriggeredOrders] Cleared triggeredOrders");
    return out;
}

void OrderBook::replenishIcebergOrder(Order* order) {
    ENGINE_LOG("[replenishIcebergOrder] OrderID: " << order->orderId);
    if (order->visibleQuantity && order->replenishQuantity) {
        int remainingQty = order->getRemainingQuantity();
        int replenishQty = order->replenishQuantity.value();
        if (remainingQty > 0) {
            int newVisibleQty = std::min(remainingQty, replenishQty);
            order->visibleQuantity = newVisibleQty;
            ENGINE_LOG("[replenishIcebergOrder] OrderID: " << order->orderId
                       << " visible reset to " << newVisibleQty);
        }
    }
}

void OrderBook::checkExpiredOrders() {
    ENGINE_LOG("[checkExpiredOrders] Checking expired orders");
    std::lock_guard<std::mutex> lock(bookMutex);
    auto now = std::chrono::system_clock::now();
    for (auto it = priceLevels.begin(); it != priceLevels.end();) {
        auto &q = it->second;
        for (auto oit = q.begin(); oit != q.end();) {
            if (oit->type == OrderType::GTD && oit->expiry && now > *oit->expiry) {
                ENGINE_LOG("[checkExpiredOrders] Removing expired OrderID: " << oit->orderId);
                orderIndex.erase(oit->orderId);
                oit = q.erase(oit);
            } else {
                ++oit;
            }
        }
        if (q.empty()) it = priceLevels.erase(it);
        else ++it;
    }
    ENGINE_LOG("[checkExpiredOrders] Completed");
}

void OrderBook::removeExpiredStopOrders() {
    ENGINE_LOG("[removeExpiredStopOrders] Checking expired stop orders");
    std::lock_guard<std::mutex> lock(bookMutex);
    auto now = std::chrono::system_clock::now();
    for (auto it = stopOrders.begin(); it != stopOrders.end();) {
        auto& queue = it->second;
        queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Order& o) {
            if (o.expiry && now > o.expiry.value()) {
                ENGINE_LOG("[removeExpiredStopOrders] Removing expired OrderID: " << o.orderId);
                return true;
            }
            return false;
        }), queue.end());
        if (queue.empty()) {
            it = stopOrders.erase(it);
        } else {
            ++it;
        }
    }
    ENGINE_LOG("[removeExpiredStopOrders] Completed");
}

void OrderBook::printOrderBook() const {
    ENGINE_LOG("[printOrderBook] Printing order book, side: " << (side_ == Side::BUY ? "BUY" : "SELL"));
    std::lock_guard<std::mutex> lock(bookMutex);
    std::cout << "[OrderBook] Side: " << (side_ == Side::BUY ? "BUY" : "SELL") << "\n";
    for (const auto& [price, orders] : priceLevels) {
        std::cout << "Price: " << price << " -> ";
        for (const auto& o : orders) {
            std::cout << "(ID: " << o.orderId << ", Qty: " << o.getRemainingQuantity()
                      << ", Status: " << static_cast<int>(o.status) << ") ";
        }
        std::cout << "\n";
    }
    ENGINE_LOG("[printOrderBook] Completed");
}

std::vector<Order> OrderBook::getAllOrders() const {
    ENGINE_LOG("[getAllOrders] Retrieving all orders");
    std::lock_guard<std::mutex> lock(bookMutex);
    std::vector<Order> result;
    for (const auto& [_, orders] : priceLevels) {
        result.insert(result.end(), orders.begin(), orders.end());
    }
    for (const auto& [_, stops] : stopOrders) {
        result.insert(result.end(), stops.begin(), stops.end());
    }
    ENGINE_LOG("[getAllOrders] Retrieved " << result.size() << " orders");
    return result;
}

void OrderBook::cleanPriceLevel(double price) {
    ENGINE_LOG("[cleanPriceLevel] Price: " << price);
    if (priceLevels[price].empty()) {
        priceLevels.erase(price);
        ENGINE_LOG("[cleanPriceLevel] Erased empty price level: " << price);
    }
}

void OrderBook::cleanStopLevel(double stopPrice) {
    ENGINE_LOG("[cleanStopLevel] StopPrice: " << stopPrice);
    if (stopOrders[stopPrice].empty()) {
        stopOrders.erase(stopPrice);
        ENGINE_LOG("[cleanStopLevel] Erased empty stop level: " << stopPrice);
    }
}

} // namespace ultraBook
