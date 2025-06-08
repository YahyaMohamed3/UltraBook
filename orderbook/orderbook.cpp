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
    Order orderCopy = *orderIt;

    std::optional<double> oldStopPrice = orderCopy.stopPrice;
    bool priceChanged = false;
    bool quantityIncreased = false;
    bool needsRepositioning = false;

    // Apply modifications
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
    if (modRequest.newStatus) orderCopy.status = *modRequest.newStatus;

    needsRepositioning = priceChanged || quantityIncreased;

    // Remove from old price level
    if (orderCopy.price.has_value()) {
        priceLevels[oldPrice].erase(orderIt);
        cleanPriceLevel(oldPrice);
        orderIndex.erase(orderId);
    } else if (oldStopPrice.has_value()) {
        auto itStop = stopOrders.find(oldStopPrice.value());
        if (itStop != stopOrders.end()) {
            auto& queue = itStop->second;
            queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Order& o) {
                return o.orderId == orderId;
            }), queue.end());
            cleanStopLevel(oldStopPrice.value());
            stopIndex.erase(orderId);
        }
    }

    if (needsRepositioning) {
        orderCopy.timestamp = std::chrono::high_resolution_clock::now();
    }

    // Reinsert to updated level
    if (orderCopy.price.has_value()) {
        double newPrice = orderCopy.price.value();
        priceLevels[newPrice].push_back(orderCopy);
        auto newIt = --priceLevels[newPrice].end();
        orderIndex[orderId] = {newPrice, newIt};

        // Preserve status and filledQuantity
        newIt->status = orderCopy.status;
        newIt->filledQuantity = orderCopy.filledQuantity;

        ENGINE_LOG("[modifyOrder] Re-inserted order " << orderId
                   << ", status: " << newIt->status
                   << ", filled: " << newIt->filledQuantity);
    } else if (orderCopy.stopPrice.has_value()) {
        double newStop = orderCopy.stopPrice.value();
        stopOrders[newStop].push_back(orderCopy);
        auto newIt = --stopOrders[newStop].end();
        stopIndex[orderId] = {newStop, newIt};

        newIt->status = orderCopy.status;
        ENGINE_LOG("[modifyOrder] Re-inserted stop order " << orderId
                   << " at stop: " << newStop << ", status: " << newIt->status);
    } else {
        ENGINE_ERROR("[modifyOrder] OrderID: " << orderId << " has no valid price or stop price.");
    }
}

std::optional<Order> OrderBook::getBestOrder() const {
    std::lock_guard<std::mutex> lock(bookMutex);
    if (priceLevels.empty()) return std::nullopt;
    
    auto it = side_ == Side::BUY ? priceLevels.begin() : --priceLevels.end();
    if (it->second.empty()) return std::nullopt;
    
    return it->second.front();

}

bool OrderBook::isEmpty() const{
    std::lock_guard<std::mutex> lock(bookMutex);
    return priceLevels.empty();
}

void OrderBook::clear(){
    std::lock_guard<std::mutex> lock(bookMutex);
    priceLevels.clear();
    stopOrders.clear();
    orderIndex.clear();
    stopIndex.clear();
    ENGINE_LOG("[clear] Order book cleared." );
}

void OrderBook::addStopOrder(Order order) {
    std::lock_guard<std::mutex> lock(bookMutex);
    double stopPrice = order.stopPrice.value();
    stopOrders[stopPrice].push_back(order);
    auto it = --stopOrders[stopPrice].end();
    stopIndex[order.orderId] = {stopPrice, it};
    ENGINE_LOG("[addStopOrder] Stop order added: ID=" << order.orderId << " StopPrice=" << stopPrice);
}

void OrderBook::checkAndTrigger(double lastPrice) {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto it = stopOrders.begin();
    while (it != stopOrders.end()) {
        bool trigger = (side_ == Side::BUY) ? (lastPrice >= it->first) : (lastPrice <= it->first);
        if (trigger) {
            auto& queue = it->second;
            while (!queue.empty()) {
                Order stopOrder = queue.front();
                queue.pop_front();
                stopIndex.erase(stopOrder.orderId);
                stopOrder.status = OrderStatus::TRIGGERED;
                if (stopOrder.type == OrderType::STOP) {
                    convertStopToMarket(&stopOrder);
                } else if (stopOrder.type == OrderType::STOPLIMIT) {
                    convertStopToLimit(&stopOrder);
                }
            }
            it = stopOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void OrderBook::convertStopToMarket(Order* order) {
    order->type = OrderType::MARKET;
    order->price = std::nullopt;
    ENGINE_LOG("[convertStopToMarket] OrderID: " << order->orderId << " converted to MARKET.");
    // MatchingEngine should re-process this externally.
}

void OrderBook::convertStopToLimit(Order* order) {
    ENGINE_LOG("[convertStopToLimit] OrderID: " << order->orderId << " converted to LIMIT.");
    order->type = OrderType::LIMIT;
    order->stopPrice = std::nullopt;
    addOrder(*order); // Reinsert as regular limit order
}

void OrderBook::replenishIcebergOrder(Order* order) {
    if (order->visibleQuantity && order->replenishQuantity) {
        int remainingQty = order->getRemainingQuantity();
        int replenishQty = order->replenishQuantity.value();
        if (remainingQty > 0) {
            int newVisibleQty = std::min(remainingQty, replenishQty);
            order->visibleQuantity = newVisibleQty;
            ENGINE_LOG("[replenishIcebergOrder] OrderID: " << order->orderId << " visible reset to " << newVisibleQty);
        }
    }
}

void OrderBook::checkExpiredOrders() {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto now = std::chrono::system_clock::now();
    for (auto it = priceLevels.begin(); it != priceLevels.end();) {
        auto& queue = it->second;
        queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Order& o) {
            return o.type == OrderType::GTD && o.expiry && now > o.expiry.value();
        }), queue.end());
        if (queue.empty()) {
            it = priceLevels.erase(it);
        } else {
            ++it;
        }
    }
}

void OrderBook::removeExpiredStopOrders() {
    std::lock_guard<std::mutex> lock(bookMutex);
    auto now = std::chrono::system_clock::now();
    for (auto it = stopOrders.begin(); it != stopOrders.end();) {
        auto& queue = it->second;
        queue.erase(std::remove_if(queue.begin(), queue.end(), [&](const Order& o) {
            return o.expiry && now > o.expiry.value();
        }), queue.end());
        if (queue.empty()) {
            it = stopOrders.erase(it);
        } else {
            ++it;
        }
    }
}

void OrderBook::printOrderBook() const {
    std::lock_guard<std::mutex> lock(bookMutex);
    std::cout << "[OrderBook] Side: " << (side_ == Side::BUY ? "BUY" : "SELL") << "\n";
    for (const auto& [price, orders] : priceLevels) {
        std::cout << "Price: " << price << " -> ";
        for (const auto& o : orders) {
            std::cout << "(ID: " << o.orderId << ", Qty: " << o.getRemainingQuantity() << ", Status: " << o.status << ") ";
        }
        std::cout << "\n";
    }
}

std::vector<Order> OrderBook::getAllOrders() const {
    std::lock_guard<std::mutex> lock(bookMutex);
    std::vector<Order> result;
    for (const auto& [_, orders] : priceLevels) {
        result.insert(result.end(), orders.begin(), orders.end());
    }
    for (const auto& [_, stops] : stopOrders) {
        result.insert(result.end(), stops.begin(), stops.end());
    }
    return result;
}

void OrderBook::cleanPriceLevel(double price) {
    if (priceLevels[price].empty()) {
        priceLevels.erase(price);
    }
}

void OrderBook::cleanStopLevel(double stopPrice) {
    if (stopOrders[stopPrice].empty()) {
        stopOrders.erase(stopPrice);
    }
}

} // namespace ultraBook
// End of orderbook.cpp