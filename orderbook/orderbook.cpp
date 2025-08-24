#include "orderbook.hpp"
#include "debug.hpp"  // expects ENGINE_LOG / ENGINE_ERROR macros; no-op if you stub them
#include <algorithm>
#include <cassert>
#include <iostream>   // for printOrderBook()

namespace ultraBook {

// ---------------- ctor ----------------
OrderBook::OrderBook(Side side)
: side_(side)
{
    // Construct the appropriate PMR map in arena_
    if (is_buy_()) {
        buy_tree_ = new (std::pmr::get_default_resource()->allocate(sizeof(BuyTree), alignof(BuyTree)))
            BuyTree{ &arena_ };
    } else {
        sell_tree_ = new (std::pmr::get_default_resource()->allocate(sizeof(SellTree), alignof(SellTree)))
            SellTree{ &arena_ };
    }
    std::atomic_store_explicit(&bestLevel_, std::shared_ptr<Level>{}, std::memory_order_relaxed);
    ENGINE_LOG("[OrderBook] ctor side=" << (is_buy_() ? "BUY" : "SELL"));
}

// ------------- internal helpers -------------
std::shared_ptr<OrderBook::Level>
OrderBook::ensure_level_locked_for_write_(double px) {
    // Caller must hold unique lock on treeMutex_
    if (is_buy_()) {
        auto it = buy_tree_->find(px);
        if (it != buy_tree_->end()) return it->second;
        std::pmr::polymorphic_allocator<Level> alloc(&node_arena_);
        auto lvl = std::allocate_shared<Level>(alloc, px, &node_arena_);
        buy_tree_->emplace(px, lvl);
        return lvl;
    } else {
        auto it = sell_tree_->find(px);
        if (it != sell_tree_->end()) return it->second;
        std::pmr::polymorphic_allocator<Level> alloc(&node_arena_);
        auto lvl = std::allocate_shared<Level>(alloc, px, &node_arena_);
        sell_tree_->emplace(px, lvl);
        return lvl;
    }
}

std::shared_ptr<OrderBook::StopLevel>
OrderBook::ensure_stop_locked_for_write_(double sp) {
    // Caller must hold unique lock on stopTreeMutex_
    auto it = stop_tree_.find(sp);
    if (it != stop_tree_.end()) return it->second;
    std::pmr::polymorphic_allocator<StopLevel> alloc(&node_arena_);
    auto lvl = std::allocate_shared<StopLevel>(alloc, sp, &node_arena_);
    stop_tree_.emplace(sp, lvl);
    return lvl;
}

void OrderBook::maybe_update_best_add_(const std::shared_ptr<Level>& lvl) {
    // treeMutex_ (unique) held by caller
    auto cur = std::atomic_load_explicit(&bestLevel_, std::memory_order_acquire);
    if (!cur) { std::atomic_store_explicit(&bestLevel_, lvl, std::memory_order_release); return; }

    if (is_buy_()) {
        if (lvl->price > cur->price) std::atomic_store_explicit(&bestLevel_, lvl, std::memory_order_release);
    } else {
        if (lvl->price < cur->price) std::atomic_store_explicit(&bestLevel_, lvl, std::memory_order_release);
    }
}

void OrderBook::recompute_best_() {
    // treeMutex_ (unique) held by caller
    std::shared_ptr<Level> next = nullptr;
    if (is_buy_()) {
        if (!buy_tree_->empty()) next = buy_tree_->begin()->second;
    } else {
        if (!sell_tree_->empty()) next = sell_tree_->begin()->second;
    }
    std::atomic_store_explicit(&bestLevel_, next, std::memory_order_release);
}

std::optional<OrderBook::Location>
OrderBook::locate_(int orderId) const {
    // Fast path via id_map_ -> level -> validate index
    std::shared_lock im(id_mtx_);
    auto it = id_map_.find(orderId);
    if (it == id_map_.end()) return std::nullopt;
    auto lvl = it->second.lvl.lock();
    auto hint = it->second.idx;
    im.unlock(); // release map lock early

    if (!lvl) return std::nullopt;
    std::scoped_lock lk(lvl->mtx);

    if (hint < lvl->queue.size() && lvl->queue[hint].orderId == orderId)
        return Location{ lvl, hint };

    for (std::size_t i = 0; i < lvl->queue.size(); ++i) {
        if (lvl->queue[i].orderId == orderId)
            return Location{ lvl, i };
    }
    return std::nullopt;
}

// ---------------- Core API ----------------
void OrderBook::addOrder(Order order) {
    // Priced orders only here; STOP is handled by addStopOrder
    if (!order.price.has_value()) { ENGINE_ERROR("[addOrder] missing price"); return; }
    const double px = *order.price;

    // Ensure price level exists (short exclusive only on creation)
    {
        std::shared_lock r(treeMutex_);
        if (is_buy_()) {
            if (buy_tree_->find(px) == buy_tree_->end()) {
                r.unlock();
                std::unique_lock w(treeMutex_);
                ensure_level_locked_for_write_(px);
                // best may improve if this is first level or superior
                recompute_best_(); // cheap, guarantees cache correctness
            }
        } else {
            if (sell_tree_->find(px) == sell_tree_->end()) {
                r.unlock();
                std::unique_lock w(treeMutex_);
                ensure_level_locked_for_write_(px);
                recompute_best_();
            }
        }
    }

    // Push into level FIFO (no tree lock needed)
    std::shared_ptr<Level> lvl;
    {
        std::shared_lock r(treeMutex_);
        if (is_buy_()) {
            lvl = buy_tree_->find(px)->second;
        } else {
            lvl = sell_tree_->find(px)->second;
        }
    }
    {
        std::scoped_lock qlk(lvl->mtx);
        lvl->queue.push_back(order);
        std::unique_lock im(id_mtx_);
        id_map_[order.orderId] = IdIndex{ lvl, lvl->queue.size() - 1 };
    }

    // Update best if this price is superior (fast path, writer only)
    // (Not strictly necessary after recompute_best_, but keeps cache tight under heavy adds)
    {
        std::unique_lock w(treeMutex_, std::defer_lock);
        if (w.try_lock()) maybe_update_best_add_(lvl);
    }

    ENGINE_LOG("[addOrder] id=" << order.orderId << " px=" << px);
}

bool OrderBook::cancelOrder(int orderId) {
    auto loc = locate_(orderId);
    if (!loc) return false;
    auto lvl = loc->lvl;

    // Remove from queue
    {
        std::scoped_lock qlk(lvl->mtx);
        if (loc->idx >= lvl->queue.size() || lvl->queue[loc->idx].orderId != orderId) {
            auto it = std::find_if(lvl->queue.begin(), lvl->queue.end(),
                                   [&](const Order& o){ return o.orderId == orderId; });
            if (it == lvl->queue.end()) return false;
            loc->idx = static_cast<std::size_t>(std::distance(lvl->queue.begin(), it));
        }
        lvl->queue.erase(lvl->queue.begin() + static_cast<std::ptrdiff_t>(loc->idx));
    }
    {
        std::unique_lock im(id_mtx_);
        id_map_.erase(orderId);
    }

    // If level now empty, erase it and adjust best cache
    if (lvl->queue.empty()) {
        std::unique_lock w(treeMutex_);
        if (is_buy_()) {
            auto it = buy_tree_->find(lvl->price);
            if (it != buy_tree_->end() && it->second.get() == lvl.get()) {
                buy_tree_->erase(it);
            }
        } else {
            auto it = sell_tree_->find(lvl->price);
            if (it != sell_tree_->end() && it->second.get() == lvl.get()) {
                sell_tree_->erase(it);
            }
        }
        recompute_best_();
    }

    ENGINE_LOG("[cancelOrder] id=" << orderId << " removed");
    return true;
}

std::optional<Order*> OrderBook::findOrder(int orderId) {
    auto loc = locate_(orderId);
    if (!loc) return std::nullopt;
    auto lvl = loc->lvl;
    std::scoped_lock lk(lvl->mtx);

    if (loc->idx < lvl->queue.size() && lvl->queue[loc->idx].orderId == orderId)
        return std::optional<Order*>{ &lvl->queue[loc->idx] };

    for (auto &o : lvl->queue) {
        if (o.orderId == orderId) return std::optional<Order*>{ &o };
    }
    return std::nullopt;
}

void OrderBook::modifyOrder(int orderId, const OrderModificationRequest& modRequest) {
    // Reject nonsensical request quickly
    if (!modRequest.hasModifications()) return;

    auto loc = locate_(orderId);
    if (!loc) { ENGINE_ERROR("[modifyOrder] not found"); return; }
    auto old_lvl = loc->lvl;

    Order copy;
    bool remove_from_old = false;
    bool price_changed   = false;
    bool qty_increased   = false;

    {
        std::scoped_lock lk(old_lvl->mtx);
        if (loc->idx >= old_lvl->queue.size() || old_lvl->queue[loc->idx].orderId != orderId) {
            auto it = std::find_if(old_lvl->queue.begin(), old_lvl->queue.end(),
                                   [&](const Order& o){ return o.orderId == orderId; });
            if (it == old_lvl->queue.end()) { ENGINE_ERROR("[modifyOrder] missing in level"); return; }
            loc->idx = static_cast<std::size_t>(std::distance(old_lvl->queue.begin(), it));
        }
        copy = old_lvl->queue[loc->idx];

        // Apply fields
        if (modRequest.newPrice) {
            if (!copy.price || *copy.price != *modRequest.newPrice) price_changed = true;
            copy.price = modRequest.newPrice;
        }
        if (modRequest.newQuantity) {
            qty_increased = (*modRequest.newQuantity > copy.quantity);
            copy.quantity = *modRequest.newQuantity;
        }
        if (modRequest.newStopPrice) {
            copy.stopPrice = modRequest.newStopPrice;
            price_changed = true; // moving between trees
        }
        if (modRequest.newVisibleQuantity)   copy.visibleQuantity   = modRequest.newVisibleQuantity;
        if (modRequest.newReplenishQuantity) copy.replenishQuantity = modRequest.newReplenishQuantity;
        if (modRequest.newExpiry)            copy.expiry            = modRequest.newExpiry;
        if (modRequest.newStatus)            copy.status            = *modRequest.newStatus;

        // Decide if we must requeue (price-time priority rule)
        const bool needsRequeue = price_changed || qty_increased ||
                                  modRequest.newPrice.has_value() || modRequest.newStopPrice.has_value();

        if (needsRequeue) {
            old_lvl->queue.erase(old_lvl->queue.begin() + static_cast<std::ptrdiff_t>(loc->idx));
            remove_from_old = true;
        } else {
            old_lvl->queue[loc->idx] = copy; // in-place update
        }
    }

    if (!remove_from_old) return;

    {
        std::unique_lock im(id_mtx_);
        id_map_.erase(orderId);
    }

    // If old level empty, remove it from tree & refresh best
    if (old_lvl->queue.empty()) {
        std::unique_lock w(treeMutex_);
        if (is_buy_()) {
            auto it = buy_tree_->find(old_lvl->price);
            if (it != buy_tree_->end() && it->second.get() == old_lvl.get()) buy_tree_->erase(it);
        } else {
            auto it = sell_tree_->find(old_lvl->price);
            if (it != sell_tree_->end() && it->second.get() == old_lvl.get()) sell_tree_->erase(it);
        }
        recompute_best_();
    }

    // Reinsert either into price tree or stop tree
    if (copy.price) {
        addOrder(copy); // preserves FIFO at new level by pushing back
        return;
    }
    if (copy.stopPrice) {
        // add to stop tree
        const double sp = *copy.stopPrice;
        {
            std::shared_lock r(stopTreeMutex_);
            if (stop_tree_.find(sp) == stop_tree_.end()) {
                r.unlock();
                std::unique_lock w(stopTreeMutex_);
                ensure_stop_locked_for_write_(sp);
            }
        }
        std::shared_ptr<StopLevel> sl;
        {
            std::shared_lock r(stopTreeMutex_);
            sl = stop_tree_.find(sp)->second;
        }
        {
            std::scoped_lock lk(sl->mtx);
            sl->queue.push_back(copy);
        }
        {
            std::unique_lock s(stopIndexMutex_);
            stop_index_[orderId] = sp;
        }
        return;
    }

    ENGINE_ERROR("[modifyOrder] neither price nor stopPrice present after mods");
}

std::optional<Order> OrderBook::getBestOrder() const {
    // O(1): use cached best level, avoid tree walk on hot reads
    auto lvl = std::atomic_load_explicit(&bestLevel_, std::memory_order_acquire);
    if (!lvl) return std::nullopt;

    std::scoped_lock lk(lvl->mtx);
    if (lvl->queue.empty()) return std::nullopt;
    return lvl->queue.front();
}

bool OrderBook::isEmpty() const {
    std::shared_lock rl(treeMutex_);
    if (is_buy_()) return buy_tree_->empty();
    return sell_tree_->empty();
}

void OrderBook::clear() {
    {
        std::unique_lock w(treeMutex_);
        if (is_buy_()) buy_tree_->clear();
        else sell_tree_->clear();
        recompute_best_();
    }
    {
        std::unique_lock w(stopTreeMutex_);
        stop_tree_.clear();
    }
    {
        std::unique_lock im(id_mtx_);
        id_map_.clear();
    }
    {
        std::unique_lock s(stopIndexMutex_);
        stop_index_.clear();
    }
    triggeredOrders_.clear();
}

// ---------------- Stop orders ----------------
void OrderBook::addStopOrder(Order order) {
    if (!order.stopPrice.has_value()) { ENGINE_ERROR("[addStopOrder] missing stop price"); return; }
    const double sp = *order.stopPrice;

    {
        std::shared_lock r(stopTreeMutex_);
        if (stop_tree_.find(sp) == stop_tree_.end()) {
            r.unlock();
            std::unique_lock w(stopTreeMutex_);
            ensure_stop_locked_for_write_(sp);
        }
    }
    std::shared_ptr<StopLevel> sl;
    {
        std::shared_lock r(stopTreeMutex_);
        sl = stop_tree_.find(sp)->second;
    }
    {
        std::scoped_lock lk(sl->mtx);
        sl->queue.push_back(order);
    }
    {
        std::unique_lock s(stopIndexMutex_);
        stop_index_[order.orderId] = sp;
    }
    ENGINE_LOG("[addStopOrder] id=" << order.orderId << " stop=" << sp);
}

void OrderBook::checkAndTrigger(double lastPrice) {
    std::vector<double> toErase;

    // BUY triggers: lastPrice >= stop  -> all stops with stop <= lastPrice
    // SELL triggers: lastPrice <= stop -> all stops with stop >= lastPrice
    if (is_buy_()) {
        std::shared_lock r(stopTreeMutex_);
        for (auto it = stop_tree_.begin(); it != stop_tree_.end() && it->first <= lastPrice; ++it) {
            auto &sl = *it->second;
            std::scoped_lock lk(sl.mtx);
            while (!sl.queue.empty()) {
                Order o = sl.queue.front(); sl.queue.pop_front();
                { std::unique_lock s(stopIndexMutex_); stop_index_.erase(o.orderId); }
                triggeredOrders_.push_back(o);
            }
            if (sl.queue.empty()) toErase.push_back(it->first);
        }
        r.unlock();
    } else {
        std::shared_lock r(stopTreeMutex_);
        auto it = stop_tree_.lower_bound(lastPrice);
        for (; it != stop_tree_.end(); ++it) {
            auto &sl = *it->second;
            std::scoped_lock lk(sl.mtx);
            while (!sl.queue.empty()) {
                Order o = sl.queue.front(); sl.queue.pop_front();
                { std::unique_lock s(stopIndexMutex_); stop_index_.erase(o.orderId); }
                triggeredOrders_.push_back(o);
            }
            if (sl.queue.empty()) toErase.push_back(it->first);
        }
        r.unlock();
    }

    for (double sp : toErase) cleanStopLevel(sp);
}

std::vector<Order> OrderBook::getAndClearTriggeredOrders() {
    std::vector<Order> out;
    out.swap(triggeredOrders_);
    return out;
}

// ---------------- Iceberg ----------------
void OrderBook::replenishIcebergOrder(Order* order) {
    if (!order) return;
    if (order->visibleQuantity && order->replenishQuantity) {
        const int rem = order->getRemainingQuantity();
        if (rem > 0) order->visibleQuantity = std::min(rem, *order->replenishQuantity);
    }
}

// ---------------- GTD / expiry ----------------
void OrderBook::checkExpiredOrders() {
    const auto now = std::chrono::system_clock::now();
    std::vector<double> emptyPrices;

    std::shared_lock r(treeMutex_);
    if (is_buy_()) {
        for (auto it = buy_tree_->begin(); it != buy_tree_->end(); ++it) {
            auto lvl = it->second;
            std::scoped_lock lk(lvl->mtx);
            auto &q = lvl->queue;
            q.erase(std::remove_if(q.begin(), q.end(), [&](const Order& o){
                if (o.type == OrderType::GTD && o.expiry && now > *o.expiry) {
                    std::unique_lock im(id_mtx_); id_map_.erase(o.orderId);
                    return true;
                }
                return false;
            }), q.end());
            if (q.empty()) emptyPrices.push_back(lvl->price);
        }
    } else {
        for (auto it = sell_tree_->begin(); it != sell_tree_->end(); ++it) {
            auto lvl = it->second;
            std::scoped_lock lk(lvl->mtx);
            auto &q = lvl->queue;
            q.erase(std::remove_if(q.begin(), q.end(), [&](const Order& o){
                if (o.type == OrderType::GTD && o.expiry && now > *o.expiry) {
                    std::unique_lock im(id_mtx_); id_map_.erase(o.orderId);
                    return true;
                }
                return false;
            }), q.end());
            if (q.empty()) emptyPrices.push_back(lvl->price);
        }
    }
    r.unlock();

    if (!emptyPrices.empty()) {
        std::unique_lock w(treeMutex_);
        for (double px : emptyPrices) {
            if (is_buy_()) {
                auto it = buy_tree_->find(px);
                if (it != buy_tree_->end() && it->second->queue.empty()) buy_tree_->erase(it);
            } else {
                auto it = sell_tree_->find(px);
                if (it != sell_tree_->end() && it->second->queue.empty()) sell_tree_->erase(it);
            }
        }
        recompute_best_();
    }
}

void OrderBook::removeExpiredStopOrders() {
    const auto now = std::chrono::system_clock::now();
    std::vector<double> toErase;

    std::shared_lock r(stopTreeMutex_);
    for (auto it = stop_tree_.begin(); it != stop_tree_.end(); ++it) {
        auto sl = it->second;
        std::scoped_lock lk(sl->mtx);
        auto &q = sl->queue;
        q.erase(std::remove_if(q.begin(), q.end(), [&](const Order& o){
            return (o.expiry && now > *o.expiry);
        }), q.end());
        if (q.empty()) toErase.push_back(sl->stop);
    }
    r.unlock();

    for (double sp : toErase) cleanStopLevel(sp);
}

// ---------------- Utilities ----------------
void OrderBook::printOrderBook() const {
    std::cout << "[OrderBook] Side: " << (is_buy_() ? "BUY" : "SELL") << "\n";
    std::shared_lock r(treeMutex_);
    if (is_buy_()) {
        for (const auto& [price, lvlPtr] : *buy_tree_) {
            std::lock_guard lk(lvlPtr->mtx);
            std::cout << "Price: " << price << " -> ";
            for (const auto& o : lvlPtr->queue) {
                std::cout << "(ID:" << o.orderId
                          << ", Rem:" << o.getRemainingQuantity()
                          << ", St:" << static_cast<int>(o.status) << ") ";
            }
            std::cout << "\n";
        }
    } else {
        for (const auto& [price, lvlPtr] : *sell_tree_) {
            std::lock_guard lk(lvlPtr->mtx);
            std::cout << "Price: " << price << " -> ";
            for (const auto& o : lvlPtr->queue) {
                std::cout << "(ID:" << o.orderId
                          << ", Rem:" << o.getRemainingQuantity()
                          << ", St:" << static_cast<int>(o.status) << ") ";
            }
            std::cout << "\n";
        }
    }
}

std::vector<Order> OrderBook::getAllOrders() const {
    std::vector<Order> result;
    std::shared_lock r(treeMutex_);
    if (is_buy_()) {
        for (const auto& [_, lvl] : *buy_tree_) {
            std::lock_guard lk(lvl->mtx);
            result.insert(result.end(), lvl->queue.begin(), lvl->queue.end());
        }
    } else {
        for (const auto& [_, lvl] : *sell_tree_) {
            std::lock_guard lk(lvl->mtx);
            result.insert(result.end(), lvl->queue.begin(), lvl->queue.end());
        }
    }
    r.unlock();

    std::shared_lock s(stopTreeMutex_);
    for (const auto& [_, sl] : stop_tree_) {
        std::lock_guard lk(sl->mtx);
        result.insert(result.end(), sl->queue.begin(), sl->queue.end());
    }
    return result;
}

// ---------------- cleanups ----------------
void OrderBook::cleanPriceLevel(double price) {
    std::unique_lock w(treeMutex_);
    if (is_buy_()) {
        auto it = buy_tree_->find(price);
        if (it != buy_tree_->end() && it->second->queue.empty()) buy_tree_->erase(it);
    } else {
        auto it = sell_tree_->find(price);
        if (it != sell_tree_->end() && it->second->queue.empty()) sell_tree_->erase(it);
    }
    recompute_best_();
}

void OrderBook::cleanStopLevel(double stopPrice) {
    std::unique_lock w(stopTreeMutex_);
    auto it = stop_tree_.find(stopPrice);
    if (it != stop_tree_.end() && it->second->queue.empty()) stop_tree_.erase(it);
}

} // namespace ultraBook
