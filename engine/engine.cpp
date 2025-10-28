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
        if (oppRemain <= 0) {
            // This can happen if the book's bestLevel_ cache is slightly stale.
            // Just cancel the dead order and continue the loop.
            (isBuy ? sellBook : buyBook).cancelOrder(oppSnap.orderId);
            continue;
        }

        // *** FIX 1 ***: Wrap std::min in parens to avoid Windows macro collision
        int tradeQty = (std::min)(remaining, oppRemain);
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
        // *** NOTE ***: These calls are now O(1) due to the orderbook.cpp refactor.
        if (tradeQty == oppRemain) {
            (isBuy ? sellBook : buyBook).cancelOrder(oppSnap.orderId);
        } else {
            // Create a modification request to reduce quantity (in-place, O(1))
            OrderModificationRequest r;
            r.newQuantity = allOrdersMap[oppSnap.orderId].quantity; // Use master quantity
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
    else if (filled < quantity) o.status = OrderStatus::PARTIALLY_FILLED; // Market orders can be partial
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

    // --- PERFORMANCE FIX ---
    // The previous O(N) "stop-the-world" pre-check using getAllOrders() was
    // removed. It was a major performance bottleneck and subject to race
    // conditions. The correct FOK logic is to sweep and check the *result*.
    
    // 1. Create the order in the master list
    allOrdersMap[orderId] = Order(orderId, price, quantity, isBuy, OrderType::FOK);
    
    // 2. Execute the sweep
    int filled = sweepOpposite(orderId, quantity, isBuy, price);

    Order& o = allOrdersMap[orderId];

    // 3. Check the result: FOK means Fill-OR-Kill.
    //    If not *fully* filled, it's CANCELED.
    if (filled < quantity) {
        o.status = OrderStatus::CANCELED;
        // NOTE: Any partial fills from the sweep (trades in tradeLog) are
        // still valid, but the *order itself* is marked Canceled
        // as it failed the "FOK" constraint.
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
            if (!b.price.has_value() || !s.price.has_value() || b.price.value() < s.price.value()) {
                break;
            }

            int bRemain = b.getRemainingQuantity();
            int sRemain = s.getRemainingQuantity();
            
            // *** FIX 2 ***: Typo was 'std.min', now 'std::min'
            // Wrapped in parens to avoid Windows macro collision
            int qty     = (std::min)(bRemain, sRemain);
            
            if (qty <= 0) {
                // Should not happen, but as a safeguard, clean the book
                buyBook.cancelOrder(b.orderId);
                sellBook.cancelOrder(s.orderId);
                didWork = true;
                continue;
            }

            // Passive side (Sell) sets the price
            const double execPrice = s.price.value(); 
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
            // *** NOTE ***: These calls are now O(1)
            if (qty == bRemain) {
                buyBook.cancelOrder(b.orderId);
            } else {
                OrderModificationRequest rb; 
                rb.newQuantity = allOrdersMap[b.orderId].quantity;
                buyBook.modifyOrder(b.orderId, rb);
            }
            if (qty == sRemain) {
                sellBook.cancelOrder(s.orderId);
            } else {
                OrderModificationRequest rs; 
                rs.newQuantity = allOrdersMap[s.orderId].quantity;
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
            // Re-use existing fast path
            addMarketOrder(o.orderId, it->second.getRemainingQuantity(), o.isBuy);
        } else if (o.type == OrderType::STOPLIMIT) {
            it->second.type = OrderType::LIMIT;
            it->second.status = OrderStatus::ACTIVE;
            it->second.stopPrice = std::nullopt;
            if (o.price.has_value())
                // Re-use existing path
                addLimitOrder(o.orderId, o.price.value(), it->second.getRemainingQuantity(), o.isBuy);
        }
    }

    // SELL side triggers
    for (Order o : sellBook.getAndClearTriggeredOrders()) {
        auto it = allOrdersMap.find(o.orderId);
        if (it == allOrdersMap.end()) continue;

        if (o.type == OrderType::STOP) {
            it->second.type = OrderType::MARKET;
            it->second.status = OrderStatus::TRIGGERED;
            addMarketOrder(o.orderId, it->second.getRemainingQuantity(), o.isBuy);
        } else if (o.type == OrderType::STOPLIMIT) {
            it->second.type = OrderType::LIMIT;
            it->second.status = OrderStatus::ACTIVE;
            it->second.stopPrice = std::nullopt;
            if (o.price.has_value())
                addLimitOrder(o.orderId, o.price.value(), it->second.getRemainingQuantity(), o.isBuy);
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

/**
 * @brief Cancels an order, whether it is a priced order or a stop order.
 */
void MatchingEngine::cancelOrder(int orderId) {
    // 1. Update master list first
    auto it = allOrdersMap.find(orderId);
    if (it != allOrdersMap.end()) {
        it->second.status = OrderStatus::CANCELED;
    }

    // 2. Try to cancel from priced books.
    //    If it's not found (returns false), try to cancel from stop books.
    //    This is robust and O(1) due to the new stop_index_ in OrderBook.
    if (!buyBook.cancelOrder(orderId)) {
        buyBook.cancelStopOrder(orderId);
    }
    if (!sellBook.cancelOrder(orderId)) {
        sellBook.cancelStopOrder(orderId);
    }
}

/**
 * @brief Modifies an order using a "cancel and resubmit" pattern.
 * This is the safest way to handle complex state changes, such as
 * changing a priced order to a stop order, or vice-versa.
 */
void MatchingEngine::modifyOrder(int orderId, const OrderModificationRequest& req) {
    auto it = allOrdersMap.find(orderId);
    if (it == allOrdersMap.end()) {
        ENGINE_ERROR("[modifyOrder] orderId " << orderId << " not found.");
        return;
    }
    Order& o = it->second;

    // --- 1. Cancel the existing order from its book ---
    // We use the new robust cancel functions from the OrderBook
    bool wasStopOrder = o.stopPrice.has_value();
    bool wasBuy = o.isBuy;
    
    if (wasStopOrder) {
        if (wasBuy) buyBook.cancelStopOrder(orderId);
        else sellBook.cancelStopOrder(orderId);
    } else {
        if (wasBuy) buyBook.cancelOrder(orderId);
        else sellBook.cancelOrder(orderId);
    }

    // --- 2. Apply modifications to the master copy in allOrdersMap ---
    if (req.newPrice)    o.price    = req.newPrice;
    if (req.newQuantity) o.quantity = *req.newQuantity;
    if (req.newExpiry)   o.expiry   = req.newExpiry;
    if (req.newStatus)   o.status   = *req.newStatus;
    if (req.newStopPrice) o.stopPrice = req.newStopPrice;
    if (req.newVisibleQuantity)   o.visibleQuantity   = req.newVisibleQuantity;
    if (req.newReplenishQuantity) o.replenishQuantity = req.newReplenishQuantity;

    // Handle logic where one price type clears the other
    if (req.newPrice.has_value() && !req.newStopPrice.has_value()) {
        o.stopPrice = std::nullopt; // It's now a priced order
    }
    if (req.newStopPrice.has_value() && !req.newPrice.has_value()) {
         if (o.type == OrderType::STOPLIMIT) o.price = std::nullopt; // Now a STOP order
    }
    
    // --- 3. Re-submit the modified order ---
    // Check if it's still active
    if (o.getRemainingQuantity() <= 0) {
         o.status = OrderStatus::FILLED;
         return; // No need to re-add a filled order
    }
    if (o.status == OrderStatus::CANCELED || o.status == OrderStatus::EXPIRED) {
         return; // No need to re-add
    }

    // Re-add to the correct book based on its *new* state
    if (o.stopPrice.has_value()) {
        o.status = OrderStatus::INACTIVE;
        (wasBuy ? buyBook : sellBook).addStopOrder(o);
    } else if (o.price.has_value()) {
        o.status = OrderStatus::ACTIVE;
        (wasBuy ? buyBook : sellBook).addOrder(o); // addOrder handles GTC/GTD/etc.
    } else {
        ENGINE_ERROR("[modifyOrder] Order " << orderId << " has no price/stopPrice after modify.");
        o.status = OrderStatus::CANCELED;
    }
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