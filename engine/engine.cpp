#include "engine.hpp"
#include "debug.hpp"
#include <iostream>
#include <algorithm>
#include <chrono>

namespace ultraBook {

static inline void update_status(Order& o) {
    if (o.filledQuantity == 0)       o.status = OrderStatus::ACTIVE;
    else if (o.filledQuantity < o.quantity) o.status = OrderStatus::PARTIALLY_FILLED;
    else                             o.status = OrderStatus::FILLED;
}

//-------------------------------------------
// Constructor
//-------------------------------------------
MatchingEngine::MatchingEngine() {
    ENGINE_LOG("[MatchingEngine] ctor");
}

//-------------------------------------------
// (helper) Update lastPrice from top-of-book and trigger stops, even if no trade
//-------------------------------------------
void MatchingEngine::refreshLastPriceFromBook() {
    auto bid = buyBook.getBestOrder();
    auto ask = sellBook.getBestOrder();
    if (!bid && !ask) return;

    double ref;
    if (bid && ask)      ref = (bid->price.value() + ask->price.value()) * 0.5;
    else if (ask)        ref = ask->price.value();
    else                 ref = bid->price.value();

    if (ref != lastPrice) {
        lastPrice = ref;
        buyBook.checkAndTrigger(lastPrice);
        sellBook.checkAndTrigger(lastPrice);
        processTriggeredOrders();
    }
}

//-------------------------------------------
// Internal: sweep opposite book up to 'remaining' with optional limit
//-------------------------------------------
int MatchingEngine::sweepOpposite(int takerId, int remaining, bool isBuy,
                                  std::optional<double> limitPrice)
{
    if (remaining <= 0) return 0;

    Order& taker = allOrdersMap[takerId];
    int filledTotal = 0;
    bool anyPriceChange = false;

    while (remaining > 0) {
        // Hit O(1) best-of-book cache
        auto oppOpt = isBuy ? sellBook.getBestOrder() : buyBook.getBestOrder();
        if (!oppOpt) break;

        const Order& oppSnap = *oppOpt; // snapshot
        if (!oppSnap.price.has_value()) break; // should not happen for resting priced orders
        const double px = *oppSnap.price;

        // Limit gate if provided
        if (limitPrice) {
            if (isBuy && px > *limitPrice) break;
            if (!isBuy && px < *limitPrice) break;
        }

        // Compute trade size from snapshot remaining (authoritative enough for a single step)
        int oppRemain = oppSnap.getRemainingQuantity();
        if (oppRemain <= 0) break;

        int tradeQty = std::min(remaining, oppRemain);
        const int takerIdBuy  = isBuy ? taker.orderId : oppSnap.orderId;
        const int takerIdSell = isBuy ? oppSnap.orderId : taker.orderId;

        // Book the trade (append-only)
        Trade t(takerIdBuy, takerIdSell, px, tradeQty);
        tradeLog.push_back(t);
        tradesByOrderId[taker.orderId].push_back(t);
        tradesByOrderId[oppSnap.orderId].push_back(t);

        // Update lastPrice and trigger stops (batch conversions later)
        lastPrice = px;
        buyBook.checkAndTrigger(lastPrice);
        sellBook.checkAndTrigger(lastPrice);
        anyPriceChange = true;

        // Accounting on local copies
        taker.filledQuantity                 += tradeQty;
        allOrdersMap[oppSnap.orderId].filledQuantity += tradeQty;
        update_status(taker);
        update_status(allOrdersMap[oppSnap.orderId]);

        remaining   -= tradeQty;
        filledTotal += tradeQty;

        // Reflect into the opposite book: cancel if fully done, else shrink quantity
        if (tradeQty == oppRemain) {
            (isBuy ? sellBook : buyBook).cancelOrder(oppSnap.orderId);
        } else {
            OrderModificationRequest r;
            r.newQuantity = oppRemain - tradeQty;
            (isBuy ? sellBook : buyBook).modifyOrder(oppSnap.orderId, r);
        }
    }

    // Convert any stops triggered by the latest tape update once
    if (anyPriceChange) processTriggeredOrders();

    return filledTotal;
}

//-------------------------------------------
// 1) Order submission
//-------------------------------------------
void MatchingEngine::addLimitOrder(int orderId, double price, int quantity, bool isBuy) {
    if (price <= 0 || quantity <= 0) {
        ENGINE_ERROR("[addLimitOrder] invalid params id=" << orderId);
        return;
    }
    Order o(orderId, price, quantity, isBuy, OrderType::LIMIT);
    allOrdersMap[orderId] = o;
    (isBuy ? buyBook : sellBook).addOrder(allOrdersMap[orderId]);
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

// ---------- Fast paths (do not rest the taker) ----------
void MatchingEngine::addMarketOrder(int orderId, int quantity, bool isBuy) {
    if (quantity <= 0) return;
    allOrdersMap[orderId] = Order(orderId, std::nullopt, quantity, isBuy, OrderType::MARKET);
    int filled = sweepOpposite(orderId, quantity, isBuy, std::nullopt);

    Order& o = allOrdersMap[orderId];
    if (filled == 0)            o.status = OrderStatus::CANCELED;
    else if (filled < quantity) o.status = OrderStatus::PARTIALLY_FILLED;
    else                        o.status = OrderStatus::FILLED;
}

void MatchingEngine::addIOCOrder(int orderId, double price, int quantity, bool isBuy) {
    if (price <= 0 || quantity <= 0) return;
    allOrdersMap[orderId] = Order(orderId, price, quantity, isBuy, OrderType::IOC);
    int filled = sweepOpposite(orderId, quantity, isBuy, price);

    Order& o = allOrdersMap[orderId];
    if (filled == 0)            o.status = OrderStatus::CANCELED;
    else if (filled < quantity) o.status = OrderStatus::PARTIALLY_FILLED;
    else                        o.status = OrderStatus::FILLED;
}

void MatchingEngine::addFOKOrder(int orderId, double price, int quantity, bool isBuy) {
    if (price <= 0 || quantity <= 0) return;

    // Availability pre-check (kept for correctness vs. races)
    int available = 0;
    auto depth = (isBuy ? sellBook.getAllOrders() : buyBook.getAllOrders());
    for (auto& o : depth) {
        if ((isBuy && o.price.value() <= price) ||
            (!isBuy && o.price.value() >= price)) {
            available += o.getRemainingQuantity();
            if (available >= quantity) break;
        } else {
            // As soon as we cross the limit boundary, stop scanning
            if (isBuy && o.price.value() > price) break;
            if (!isBuy && o.price.value() < price) break;
        }
    }
    if (available < quantity) {
        allOrdersMap[orderId] = Order(orderId, price, quantity, isBuy, OrderType::FOK);
        allOrdersMap[orderId].status = OrderStatus::CANCELED;
        return;
    }

    // Execute directly (no resting), should fully fill under stable book
    allOrdersMap[orderId] = Order(orderId, price, quantity, isBuy, OrderType::FOK);
    int filled = sweepOpposite(orderId, quantity, isBuy, price);

    Order& o = allOrdersMap[orderId];
    if (filled < quantity) {
        // In a race, counterparties may have vanished; mark canceled (partial fills remain on tape)
        o.status = OrderStatus::CANCELED;
    } else {
        o.status = OrderStatus::FILLED;
    }
}

//-------------------------------------------
// 2) Core matching sweep (continuous cross)
//-------------------------------------------
void MatchingEngine::matchOrders() {
    bool didWork = true;
    while (didWork) {
        didWork = false;

        // Observe book & convert any stops based on the latest quote
        refreshLastPriceFromBook();

        bool anyPriceChange = false;

        while (true) {
            auto bOpt = buyBook.getBestOrder();
            auto sOpt = sellBook.getBestOrder();
            if (!bOpt || !sOpt) break;

            const Order& b = *bOpt;
            const Order& s = *sOpt;
            if (b.price.value() < s.price.value()) break;

            int bRemain = b.getRemainingQuantity();
            int sRemain = s.getRemainingQuantity();
            int qty     = std::min(bRemain, sRemain);

            const double execPrice = s.price.value(); // passive (sell) price
            Trade t(b.orderId, s.orderId, execPrice, qty);
            tradeLog.push_back(t);
            tradesByOrderId[b.orderId].push_back(t);
            tradesByOrderId[s.orderId].push_back(t);

            lastPrice = execPrice;
            buyBook.checkAndTrigger(lastPrice);
            sellBook.checkAndTrigger(lastPrice);
            anyPriceChange = true;

            // Accounting (orders live in allOrdersMap)
            allOrdersMap[b.orderId].filledQuantity += qty;
            allOrdersMap[s.orderId].filledQuantity += qty;
            update_status(allOrdersMap[b.orderId]);
            update_status(allOrdersMap[s.orderId]);

            // Reflect book quantities (cancel if zero, else shrink)
            if (qty == bRemain) {
                buyBook.cancelOrder(b.orderId);
            } else {
                OrderModificationRequest rb; rb.newQuantity = bRemain - qty;
                buyBook.modifyOrder(b.orderId, rb);
            }
            if (qty == sRemain) {
                sellBook.cancelOrder(s.orderId);
            } else {
                OrderModificationRequest rs; rs.newQuantity = sRemain - qty;
                sellBook.modifyOrder(s.orderId, rs);
            }

            didWork = true;
        }

        if (anyPriceChange) processTriggeredOrders();
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
    if (req.newPrice)    o.price   = *req.newPrice;
    if (req.newQuantity) o.quantity= *req.newQuantity;
    if (req.newExpiry)   o.expiry  = *req.newExpiry;
    if (req.newStatus)   o.status  = *req.newStatus;

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
