// engine.cpp
#include "engine.hpp"
#include "debug.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace ultraBook {

static void update_status(Order& o) {
    ENGINE_LOG("[update_status] OrderID: " << o.orderId
               << ", filledQty: " << o.filledQuantity
               << ", totalQty: " << o.quantity
               << ", newStatus: "
               << (o.filledQuantity == 0 ? "ACTIVE" :
                   o.filledQuantity < o.quantity ? "PARTIALLY_FILLED" : "FILLED"));
    if (o.filledQuantity == 0) {
        o.status = OrderStatus::ACTIVE;
    } else if (o.filledQuantity < o.quantity) {
        o.status = OrderStatus::PARTIALLY_FILLED;
    } else {
        o.status = OrderStatus::FILLED;
    }
}

//-------------------------------------------
// Constructor
//-------------------------------------------
MatchingEngine::MatchingEngine() {
    ENGINE_LOG("[MatchingEngine] Constructor called");
}

//-------------------------------------------
// (helper) Update lastPrice from top-of-book and trigger stops, even if no trade
//-------------------------------------------
void MatchingEngine::refreshLastPriceFromBook() {
    auto bid = buyBook.getBestOrder();
    auto ask = sellBook.getBestOrder();

    if (!bid && !ask) return;

    double ref;
    if (bid && ask) {
        // mid between best bid/ask (simple reference)
        ref = (bid->price.value() + ask->price.value()) * 0.5;
    } else if (ask) {
        ref = ask->price.value();
    } else { // bid only
        ref = bid->price.value();
    }

    if (ref != lastPrice) {
        lastPrice = ref;
        // trigger any stops that fire off this observed price
        buyBook.checkAndTrigger(lastPrice);
        sellBook.checkAndTrigger(lastPrice);
        // convert triggered STOP/STOPLIMITs immediately
        processTriggeredOrders();
    }
}

//-------------------------------------------
// 1) Order submission
//-------------------------------------------
void MatchingEngine::addLimitOrder(int orderId, double price, int quantity, bool isBuy) {
    ENGINE_LOG("[addLimitOrder] OrderID: " << orderId
               << ", price: " << price
               << ", qty: " << quantity
               << ", isBuy: " << isBuy);
    try {
        if (price <= 0 || quantity <= 0) {
            ENGINE_ERROR("[addLimitOrder] Invalid price/quantity for OrderID: " << orderId);
            return;
        }
        Order o(orderId, price, quantity, isBuy, OrderType::LIMIT);
        allOrdersMap[orderId] = o;
        if (isBuy) {
            buyBook.addOrder(allOrdersMap[orderId]);
        } else {
            sellBook.addOrder(allOrdersMap[orderId]);
        }
    } catch (const std::exception& e) {
        ENGINE_ERROR("[addLimitOrder] Exception for OrderID: " << orderId << ", error: " << e.what());
    } catch (...) {
        ENGINE_ERROR("[addLimitOrder] Unknown exception for OrderID: " << orderId);
    }
}

void MatchingEngine::addGTCOrder(int orderId, double price, int quantity, bool isBuy) {
    if (price <= 0 || quantity <= 0) return;
    Order o(orderId, price, quantity, isBuy, OrderType::GTC);
    allOrdersMap[orderId] = o;
    (isBuy ? buyBook : sellBook).addOrder(allOrdersMap[orderId]);
}

void MatchingEngine::addGTDOrder(int orderId, double price, int quantity, bool isBuy,
                                 std::chrono::system_clock::time_point expiry) {
    if (price <= 0 || quantity <= 0) return;
    if (expiry <= std::chrono::system_clock::now()) {
        Order o(orderId, price, quantity, isBuy, OrderType::GTD, expiry);
        o.status = OrderStatus::EXPIRED;
        allOrdersMap[orderId] = o;
        return;
    }
    Order o(orderId, price, quantity, isBuy, OrderType::GTD, expiry);
    allOrdersMap[orderId] = o;
    (isBuy ? buyBook : sellBook).addOrder(allOrdersMap[orderId]);
}

void MatchingEngine::addIcebergOrder(int orderId, double price, int totalQty,
                                     int visibleQty, int replenishQty, bool isBuy) {
    if (price <= 0 || totalQty <= 0 || visibleQty <= 0 || replenishQty <= 0) return;
    Order o(orderId, price, totalQty, isBuy, OrderType::ICE,
            std::nullopt, std::nullopt, visibleQty, replenishQty);
    allOrdersMap[orderId] = o;
    (isBuy ? buyBook : sellBook).addOrder(allOrdersMap[orderId]);
}

void MatchingEngine::addStopOrder(int orderId, double stopPrice, int quantity, bool isBuy) {
    if (stopPrice <= 0 || quantity <= 0) return;
    Order o(orderId, std::nullopt, quantity, isBuy, OrderType::STOP,
            std::nullopt, stopPrice);
    o.status = OrderStatus::INACTIVE;
    allOrdersMap[orderId] = o;
    (isBuy ? buyBook : sellBook).addStopOrder(allOrdersMap[orderId]);
}

void MatchingEngine::addStopLimitOrder(int orderId, double stopPrice,
                                       double limitPrice, int quantity, bool isBuy) {
    if (stopPrice <= 0 || limitPrice <= 0 || quantity <= 0) return;
    Order o(orderId, limitPrice, quantity, isBuy, OrderType::STOPLIMIT,
            std::nullopt, stopPrice);
    o.status = OrderStatus::INACTIVE;
    allOrdersMap[orderId] = o;
    (isBuy ? buyBook : sellBook).addStopOrder(allOrdersMap[orderId]);
}

void MatchingEngine::addMarketOrder(int orderId, int quantity, bool isBuy) {
    if (quantity <= 0) return;

    // create tracking record (market orders are ephemeral but we keep for status)
    Order o(orderId, std::nullopt, quantity, isBuy, OrderType::MARKET);
    allOrdersMap[orderId] = o;

    int remaining = quantity;
    while (remaining > 0) {
        // FIX: always take the OPPOSITE side best order as optional
        std::optional<Order> opp =
            isBuy ? sellBook.getBestOrder()
                  : buyBook.getBestOrder();

        if (!opp) break;

        Order resting = *opp;
        int restingRemain = resting.getRemainingQuantity();
        if (restingRemain <= 0) break;

        int tradeQty  = std::min(remaining, restingRemain);
        double execPx = resting.price.value();

        // book the trade
        Trade t(isBuy ? orderId : resting.orderId,
                isBuy ? resting.orderId : orderId,
                execPx, tradeQty);
        tradeLog.push_back(t);
        tradesByOrderId[orderId].push_back(t);
        tradesByOrderId[resting.orderId].push_back(t);

        // update lastPrice and trigger stops
        lastPrice = execPx;
        buyBook.checkAndTrigger(lastPrice);
        sellBook.checkAndTrigger(lastPrice);
        processTriggeredOrders();

        // fill accounting
        allOrdersMap[orderId].filledQuantity       += tradeQty;
        allOrdersMap[resting.orderId].filledQuantity += tradeQty;
        update_status(allOrdersMap[orderId]);
        update_status(allOrdersMap[resting.orderId]);

        remaining -= tradeQty;

        // remove or resize the resting order on the opposite book
        if (tradeQty == restingRemain) {
            (isBuy ? sellBook : buyBook).cancelOrder(resting.orderId);
        } else {
            OrderModificationRequest r;
            r.newQuantity = restingRemain - tradeQty;
            (isBuy ? sellBook : buyBook).modifyOrder(resting.orderId, r);
        }
    }

    // finalize market order status
    if (remaining == quantity) {
        allOrdersMap[orderId].status = OrderStatus::CANCELED;
    } else if (remaining > 0) {
        allOrdersMap[orderId].status = OrderStatus::PARTIALLY_FILLED;
    } else {
        allOrdersMap[orderId].status = OrderStatus::FILLED;
    }
}

void MatchingEngine::addIOCOrder(int orderId, double price, int quantity, bool isBuy) {
    addLimitOrder(orderId, price, quantity, isBuy);
    addMarketOrder(orderId, quantity, isBuy);
    if (allOrdersMap[orderId].filledQuantity < quantity) {
        if (allOrdersMap[orderId].filledQuantity > 0)
            allOrdersMap[orderId].status = OrderStatus::PARTIALLY_FILLED;
        else
            allOrdersMap[orderId].status = OrderStatus::CANCELED;
        buyBook.cancelOrder(orderId);
        sellBook.cancelOrder(orderId);
    }
}

void MatchingEngine::addFOKOrder(int orderId, double price, int quantity, bool isBuy) {
    int available = 0;
    auto depth = (isBuy ? sellBook.getAllOrders() : buyBook.getAllOrders());
    for (auto& o : depth) {
        if ((isBuy && o.price.value() <= price) ||
            (!isBuy && o.price.value() >= price)) {
            available += o.getRemainingQuantity();
            if (available >= quantity) break;
        }
    }
    if (available < quantity) {
        Order o(orderId, price, quantity, isBuy, OrderType::FOK);
        o.status = OrderStatus::CANCELED;
        allOrdersMap[orderId] = o;
        return;
    }
    addLimitOrder(orderId, price, quantity, isBuy);
    addMarketOrder(orderId, quantity, isBuy);
    if (allOrdersMap[orderId].filledQuantity < quantity) {
        allOrdersMap[orderId].status = OrderStatus::CANCELED;
        buyBook.cancelOrder(orderId);
        sellBook.cancelOrder(orderId);
    } else {
        allOrdersMap[orderId].status = OrderStatus::FILLED;
    }
}

//-------------------------------------------
// 2) Core matching sweep
//-------------------------------------------
void MatchingEngine::matchOrders() {
    bool didWork = true;
    while (didWork) {
        didWork = false;

        // NEW: even if there’s no cross, observe the book, set lastPrice, and trigger stops
        refreshLastPriceFromBook();

        while (true) {
            auto bOpt = buyBook.getBestOrder();
            auto sOpt = sellBook.getBestOrder();
            if (!bOpt || !sOpt) break;
            const Order& b = *bOpt;
            const Order& s = *sOpt;
            if (b.price.value() < s.price.value()) break;

            int qty = std::min(b.getRemainingQuantity(), s.getRemainingQuantity());
            double execPrice = (b.price.value() + s.price.value()) / 2.0;

            Trade t(b.orderId, s.orderId, execPrice, qty);
            tradeLog.push_back(t);
            tradesByOrderId[b.orderId].push_back(t);
            tradesByOrderId[s.orderId].push_back(t);

            lastPrice = execPrice;
            buyBook.checkAndTrigger(lastPrice);
            sellBook.checkAndTrigger(lastPrice);

            // convert any triggered stops/stop-limits immediately
            processTriggeredOrders();

            allOrdersMap[b.orderId].filledQuantity += qty;
            allOrdersMap[s.orderId].filledQuantity += qty;
            update_status(allOrdersMap[b.orderId]);
            update_status(allOrdersMap[s.orderId]);

            if (allOrdersMap[b.orderId].status == OrderStatus::FILLED) buyBook.cancelOrder(b.orderId);
            if (allOrdersMap[s.orderId].status == OrderStatus::FILLED) sellBook.cancelOrder(s.orderId);

            didWork = true;
        }

        // house-keeping for already-emptied stop/stoplimit records
        for (auto& [oid, ord] : allOrdersMap) {
            if ((ord.type == OrderType::STOP || ord.type == OrderType::STOPLIMIT) &&
                ord.getRemainingQuantity() == 0) {
                ord.status = OrderStatus::FILLED;
            }
        }

        // run conversions one more time in case price moved due to cancels/requeues
        processTriggeredOrders();
    }
}

void MatchingEngine::processTriggeredOrders() {
    // BUY side triggers
    for (Order o : buyBook.getAndClearTriggeredOrders()) {
        auto it = allOrdersMap.find(o.orderId);
        if (it == allOrdersMap.end()) continue;

        if (o.type == OrderType::STOP) {
            it->second.type = OrderType::MARKET;
            it->second.status = OrderStatus::TRIGGERED;
            addMarketOrder(o.orderId, o.quantity, o.isBuy);
        } else if (o.type == OrderType::STOPLIMIT) {
            it->second.type = OrderType::LIMIT;
            it->second.status = OrderStatus::ACTIVE;
            it->second.stopPrice = std::nullopt;
            if (o.price.has_value())
                addLimitOrder(o.orderId, o.price.value(), o.quantity, o.isBuy);
        }
    }

    // SELL side triggers
    for (Order o : sellBook.getAndClearTriggeredOrders()) {
        auto it = allOrdersMap.find(o.orderId);
        if (it == allOrdersMap.end()) continue;

        if (o.type == OrderType::STOP) {
            it->second.type = OrderType::MARKET;
            it->second.status = OrderStatus::TRIGGERED;
            addMarketOrder(o.orderId, o.quantity, o.isBuy);
        } else if (o.type == OrderType::STOPLIMIT) {
            it->second.type = OrderType::LIMIT;
            it->second.status = OrderStatus::ACTIVE;
            it->second.stopPrice = std::nullopt;
            if (o.price.has_value())
                addLimitOrder(o.orderId, o.price.value(), o.quantity, o.isBuy);
        }
    }
}

//-------------------------------------------
// 3) Maintenance
//-------------------------------------------
void MatchingEngine::checkExpiredOrders() {
    buyBook.checkExpiredOrders();
    sellBook.checkExpiredOrders();
}

//-------------------------------------------
// 4) Utility
//-------------------------------------------
void MatchingEngine::cancelOrder(int orderId) {
    allOrdersMap[orderId].status = OrderStatus::CANCELED;
    buyBook.cancelOrder(orderId);
    sellBook.cancelOrder(orderId);
}

void MatchingEngine::modifyOrder(int orderId, const OrderModificationRequest& req) {
    auto &o = allOrdersMap[orderId];
    if (req.newPrice)   o.price  = *req.newPrice;
    if (req.newQuantity)o.quantity = *req.newQuantity;
    if (req.newExpiry)  o.expiry = *req.newExpiry;
    if (req.newStatus)  o.status = *req.newStatus;

    buyBook.modifyOrder(orderId, req);
    sellBook.modifyOrder(orderId, req);

    if (o.filledQuantity == o.quantity)
        o.status = OrderStatus::FILLED;
}

//-------------------------------------------
// 5) Introspection & printing
//-------------------------------------------
OrderStatus MatchingEngine::getOrderStatus(int orderId) const {
    auto it = allOrdersMap.find(orderId);
    return it == allOrdersMap.end() ? OrderStatus::CANCELED : it->second.status;
}

void MatchingEngine::printOrderBook() const {
    std::cout << "--- BUY SIDE ---\n";
    buyBook.printOrderBook();
    std::cout << "--- SELL SIDE ---\n";
    sellBook.printOrderBook();
}

void MatchingEngine::printTradelog() const {
    for (auto &t : tradeLog) ENGINE_LOG(t);
}

} // namespace ultraBook
