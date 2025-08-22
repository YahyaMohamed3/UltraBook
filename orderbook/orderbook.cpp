#include "../orderbook/orderbook.hpp"
#include <iostream>
#include <algorithm>
#include "debug.hpp"
#include "types.hpp"

namespace ultraBook {

// ---- ctor ----
OrderBook::OrderBook(Side side)
  : side_{side}
{
    comp_ = (side == Side::BUY)
        ? std::function<bool(double,double)>([](double a, double b){ return a > b; })
        : std::function<bool(double,double)>([](double a, double b){ return a < b; });

    priceLevels = PriceMap(comp_);
    ENGINE_LOG("[OrderBook] Constructor, side: " << (side == Side::BUY ? "BUY" : "SELL"));
}

// ---- add limit/gtc/gtd/ice... (priced orders only) ----
void OrderBook::addOrder(Order order) {
    if (!order.price) { ENGINE_ERROR("[addOrder] missing price"); return; }
    const double px = *order.price;

    // find or create level
    {
        std::shared_lock r(treeMutex);
        auto it = priceLevels.find(px);
        if (it == priceLevels.end()) {
            r.unlock();
            std::unique_lock w(treeMutex);
            priceLevels.try_emplace(px); // in-place construct Level (no move/copy of mutex)
        }
    }
    // push into level
    {
        std::shared_lock r(treeMutex);
        auto it = priceLevels.find(px);
        auto &lvl = it->second;
        std::lock_guard lk(lvl.mtx);
        lvl.queue.push_back(order);
    }
    // index
    {
        std::unique_lock idx(orderIndexMutex);
        orderPriceIndex[order.orderId] = px;
    }
    ENGINE_LOG("[addOrder] Added order " << order.orderId << " @ " << px);
}

// ---- cancel by orderId ----
bool OrderBook::cancelOrder(int orderId) {
    double px;
    {   // read index
        std::shared_lock idx(orderIndexMutex);
        auto it = orderPriceIndex.find(orderId);
        if (it == orderPriceIndex.end()) return false;
        px = it->second;
    }
    // erase within level
    bool removed = false;
    {
        std::shared_lock r(treeMutex);
        auto it = priceLevels.find(px);
        if (it == priceLevels.end()) return false;
        auto &lvl = it->second;
        std::lock_guard lk(lvl.mtx);
        auto &q = lvl.queue;
        auto oit = std::find_if(q.begin(), q.end(), [&](const Order& o){ return o.orderId == orderId; });
        if (oit != q.end()) {
            q.erase(oit);
            removed = true;
        }
    }
    if (!removed) return false;

    {   // drop index
        std::unique_lock idx(orderIndexMutex);
        orderPriceIndex.erase(orderId);
    }
    // cleanup empty level
    cleanPriceLevel(px);
    ENGINE_LOG("[cancelOrder] " << orderId << " removed");
    return true;
}

// ---- locate pointer (careful: valid only while caller holds no assumptions) ----
std::optional<Order*> OrderBook::findOrder(int orderId) {
    double px;
    {   std::shared_lock idx(orderIndexMutex);
        auto it = orderPriceIndex.find(orderId);
        if (it == orderPriceIndex.end()) return std::nullopt;
        px = it->second;
    }
    std::shared_lock r(treeMutex);
    auto it = priceLevels.find(px);
    if (it == priceLevels.end()) return std::nullopt;
    auto &lvl = it->second;
    std::lock_guard lk(lvl.mtx);
    for (auto &o : lvl.queue) {
        if (o.orderId == orderId) return std::optional<Order*>{ &o };
    }
    return std::nullopt;
}


// ---- modify (price-time rules: price change or qty increase → requeue) ----
void OrderBook::modifyOrder(int orderId, const OrderModificationRequest& modRequest) {
    double oldPx;
    {
        std::shared_lock idx(orderIndexMutex);
        auto it = orderPriceIndex.find(orderId);
        if (it == orderPriceIndex.end()) { ENGINE_ERROR("[modifyOrder] not found"); return; }
        oldPx = it->second;
    }

    Order orderCopy;
    bool found = false;
    {   std::shared_lock r(treeMutex);
        auto pit = priceLevels.find(oldPx);
        if (pit == priceLevels.end()) { ENGINE_ERROR("[modifyOrder] bad price level"); return; }
        auto &lvl = pit->second;
        std::lock_guard lk(lvl.mtx);
        auto &q = lvl.queue;
        auto oit = std::find_if(q.begin(), q.end(), [&](const Order& o){ return o.orderId == orderId; });
        if (oit == q.end()) { ENGINE_ERROR("[modifyOrder] missing in level"); return; }
        orderCopy = *oit;
        found = true;
        // apply fields
        bool priceChanged = false, qtyIncreased = false;

        if (modRequest.newPrice) {
            if (!orderCopy.price || *orderCopy.price != *modRequest.newPrice) { priceChanged = true; }
            orderCopy.price = modRequest.newPrice;
        }
        if (modRequest.newQuantity) {
            if (*modRequest.newQuantity > orderCopy.quantity) qtyIncreased = true;
            orderCopy.quantity = *modRequest.newQuantity;
        }
        if (modRequest.newStopPrice) { orderCopy.stopPrice = modRequest.newStopPrice; priceChanged = true; }
        if (modRequest.newVisibleQuantity)  orderCopy.visibleQuantity   = modRequest.newVisibleQuantity;
        if (modRequest.newReplenishQuantity) orderCopy.replenishQuantity = modRequest.newReplenishQuantity;
        if (modRequest.newExpiry)            orderCopy.expiry            = modRequest.newExpiry;
        if (modRequest.newStatus)            orderCopy.status            = *modRequest.newStatus;

        const bool needsRequeue = priceChanged || qtyIncreased;
        if (needsRequeue || (modRequest.newPrice || modRequest.newStopPrice)) {
            q.erase(oit); // remove from old level
        } else {
            *oit = orderCopy; // in-place update
            return;
        }
    }

    if (!found) return;

    // reinsert by price or stopPrice
    if (orderCopy.price) {
        const double newPx = *orderCopy.price;
        {   std::shared_lock r(treeMutex);
            auto it = priceLevels.find(newPx);
            if (it == priceLevels.end()) {
                r.unlock();
                std::unique_lock w(treeMutex);
                priceLevels.try_emplace(newPx); // fix
            }
        }
        {
            std::shared_lock r(treeMutex);
            auto it = priceLevels.find(newPx);
            auto &lvl = it->second;
            std::lock_guard lk(lvl.mtx);
            lvl.queue.push_back(orderCopy);
        }
        {   std::unique_lock idx(orderIndexMutex);
            orderPriceIndex[orderId] = newPx;
        }
        cleanPriceLevel(oldPx);
        return;
    }

    if (orderCopy.stopPrice) {
        const double sp = *orderCopy.stopPrice;
        {   std::shared_lock r(stopTreeMutex);
            auto it = stopOrders.find(sp);
            if (it == stopOrders.end()) {
                r.unlock();
                std::unique_lock w(stopTreeMutex);
                stopOrders.try_emplace(sp); // fix
            }
        }
        {
            std::shared_lock r(stopTreeMutex);
            auto it = stopOrders.find(sp);
            auto &lvl = it->second;
            std::lock_guard lk(lvl.mtx);
            lvl.queue.push_back(orderCopy);
        }
        {   std::unique_lock s(stopIndexMutex);
            stopPriceIndex[orderId] = sp;
        }
        cleanPriceLevel(oldPx);
        return;
    }

    ENGINE_ERROR("[modifyOrder] no price/stopPrice to place");
}

// ---- best order (copy) ----
std::optional<Order> OrderBook::getBestOrder() const {
    std::shared_lock r(treeMutex);
    if (priceLevels.empty()) return std::nullopt;
    auto it = priceLevels.begin();
    const double px = it->first;
    auto &lvl = it->second;
    std::lock_guard lk(lvl.mtx);
    if (lvl.queue.empty()) return std::nullopt;
    return lvl.queue.front();
}

bool OrderBook::isEmpty() const {
    std::shared_lock r(treeMutex);
    return priceLevels.empty();
}

void OrderBook::clear() {
    {
        std::unique_lock w(treeMutex);
        priceLevels.clear();
    }
    {
        std::unique_lock w(stopTreeMutex);
        stopOrders.clear();
    }
    {
        std::unique_lock idx(orderIndexMutex);
        orderPriceIndex.clear();
    }
    {
        std::unique_lock s(stopIndexMutex);
        stopPriceIndex.clear();
    }
    triggeredOrders_.clear();
}

// ---- stop orders ----
void OrderBook::addStopOrder(Order order) {
    if (!order.stopPrice) { ENGINE_ERROR("[addStopOrder] missing stop price"); return; }
    const double sp = *order.stopPrice;

    {   std::shared_lock r(stopTreeMutex);
        auto it = stopOrders.find(sp);
        if (it == stopOrders.end()) {
            r.unlock();
            std::unique_lock w(stopTreeMutex);
            stopOrders.try_emplace(sp); // fix
        }
    }
    {
        std::shared_lock r(stopTreeMutex);
        auto it = stopOrders.find(sp);
        auto &lvl = it->second;
        std::lock_guard lk(lvl.mtx);
        lvl.queue.push_back(order);
    }
    {
        std::unique_lock s(stopIndexMutex);
        stopPriceIndex[order.orderId] = sp;
    }
}

void OrderBook::checkAndTrigger(double lastPrice) {
    std::vector<double> toErase;

    {   std::shared_lock r(stopTreeMutex);
        for (auto it = stopOrders.begin(); it != stopOrders.end(); ++it) {
            const double sp = it->first;
            const bool trigger = (side_ == Side::BUY) ? (lastPrice >= sp) : (lastPrice <= sp);
            if (!trigger) continue;

            auto &lvl = it->second;
            std::lock_guard lk(lvl.mtx);
            while (!lvl.queue.empty()) {
                Order o = lvl.queue.front();
                lvl.queue.pop_front();
                { std::unique_lock s(stopIndexMutex); stopPriceIndex.erase(o.orderId); }
                triggeredOrders_.push_back(o);
            }
            toErase.push_back(sp);
        }
    }
    // erase empty stop levels
    for (double sp : toErase) cleanStopLevel(sp);
}

std::vector<Order> OrderBook::getAndClearTriggeredOrders() {
    std::vector<Order> out;
    out.swap(triggeredOrders_);
    return out;
}

// ---- iceberg ----
void OrderBook::replenishIcebergOrder(Order* order) {
    if (!order) return;
    if (order->visibleQuantity && order->replenishQuantity) {
        const int rem = order->getRemainingQuantity();
        if (rem > 0) order->visibleQuantity = std::min(rem, *order->replenishQuantity);
    }
}

// ---- GTD / expiry ----
void OrderBook::checkExpiredOrders() {
    const auto now = std::chrono::system_clock::now();
    std::vector<double> emptyPrices;

    std::shared_lock r(treeMutex);
    for (auto it = priceLevels.begin(); it != priceLevels.end(); ++it) {
        const double px = it->first;
        auto &lvl = it->second;
        std::lock_guard lk(lvl.mtx);
        auto &q = lvl.queue;

        q.erase(std::remove_if(q.begin(), q.end(), [&](const Order& o){
            if (o.type == OrderType::GTD && o.expiry && now > *o.expiry) {
                std::unique_lock idx(orderIndexMutex);
                orderPriceIndex.erase(o.orderId);
                return true;
            }
            return false;
        }), q.end());

        if (q.empty()) emptyPrices.push_back(px);
    }
    r.unlock();
    for (double px : emptyPrices) cleanPriceLevel(px);
}

void OrderBook::removeExpiredStopOrders() {
    const auto now = std::chrono::system_clock::now();
    std::vector<double> emptyStops;

    std::shared_lock r(stopTreeMutex);
    for (auto it = stopOrders.begin(); it != stopOrders.end(); ++it) {
        const double sp = it->first;
        auto &lvl = it->second;
        std::lock_guard lk(lvl.mtx);
        auto &q = lvl.queue;

        q.erase(std::remove_if(q.begin(), q.end(), [&](const Order& o){
            return (o.expiry && now > *o.expiry);
        }), q.end());

        if (q.empty()) emptyStops.push_back(sp);
    }
    r.unlock();
    for (double sp : emptyStops) cleanStopLevel(sp);
}

// ---- utilities ----
void OrderBook::printOrderBook() const {
    std::cout << "[OrderBook] Side: " << (side_ == Side::BUY ? "BUY" : "SELL") << "\n";
    std::shared_lock r(treeMutex);
    for (const auto& [price, lvl] : priceLevels) {
        std::lock_guard lk(lvl.mtx);
        std::cout << "Price: " << price << " -> ";
        for (const auto& o : lvl.queue) {
            std::cout << "(ID:" << o.orderId << ", Rem:" << o.getRemainingQuantity()
                      << ", St:" << static_cast<int>(o.status) << ") ";
        }
        std::cout << "\n";
    }
}

std::vector<Order> OrderBook::getAllOrders() const {
    std::vector<Order> result;
    std::shared_lock r(treeMutex);
    for (const auto& [_, lvl] : priceLevels) {
        std::lock_guard lk(lvl.mtx);
        result.insert(result.end(), lvl.queue.begin(), lvl.queue.end());
    }
    std::shared_lock s(stopTreeMutex);
    for (const auto& [_, lvl] : stopOrders) {
        std::lock_guard lk(lvl.mtx);
        result.insert(result.end(), lvl.queue.begin(), lvl.queue.end());
    }
    return result;
}

// ---- cleanups (must be called without holding level lock) ----
void OrderBook::cleanPriceLevel(double price) {
    std::unique_lock w(treeMutex);
    auto it = priceLevels.find(price);
    if (it != priceLevels.end() && it->second.queue.empty()) priceLevels.erase(it);
}

void OrderBook::cleanStopLevel(double stopPrice) {
    std::unique_lock w(stopTreeMutex);
    auto it = stopOrders.find(stopPrice);
    if (it != stopOrders.end() && it->second.queue.empty()) stopOrders.erase(it);
}

} // namespace ultraBook
