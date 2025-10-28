#include "orderbook.hpp"
#include "debug.hpp"
#include <algorithm>
#include <cassert>
#include <iostream>   

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
    std::shared_lock im(id_mtx_);
    auto map_it = id_map_.find(orderId);
    if (map_it == id_map_.end()) return std::nullopt;

    auto lvl = map_it->second.lvl.lock();
    if (!lvl) return std::nullopt; // Stale entry, level was destroyed

    return Location{ lvl, map_it->second.it };
}

// ---------------- Core API ----------------
void OrderBook::addOrder(Order order) {
    if (!order.price.has_value()) { ENGINE_ERROR("[addOrder] missing price"); return; }
    const double px = *order.price;

    std::shared_ptr<Level> lvl;

    {
        std::shared_lock r(treeMutex_);
        bool exists = is_buy_() ? (buy_tree_->find(px) != buy_tree_->end())
                                : (sell_tree_->find(px) != sell_tree_->end());
        if (!exists) {
            r.unlock();
            std::unique_lock w(treeMutex_);
            lvl = ensure_level_locked_for_write_(px);
            recompute_best_(); // This level *must* be the new best or is the first
        } else {
            lvl = is_buy_() ? buy_tree_->find(px)->second : sell_tree_->find(px)->second;
        }
    }

    {
        std::unique_lock im(id_mtx_);
        std::scoped_lock qlk(lvl->mtx);

        lvl->queue.push_back(order);
        auto order_it = --lvl->queue.end(); // Get iterator to the new element
        id_map_[order.orderId] = IdIndex{ lvl, order_it };
    }

    {
        std::unique_lock w(treeMutex_, std::defer_lock);
        if (w.try_lock()) maybe_update_best_add_(lvl);
    }

    ENGINE_LOG("[addOrder] id=" << order.orderId << " px=" << px);
}

bool OrderBook::cancelOrder(int orderId) {
    std::shared_ptr<Level> lvl;
    OrderQueue::iterator order_it;

    {
        std::unique_lock im(id_mtx_);
        auto map_it = id_map_.find(orderId);
        if (map_it == id_map_.end()) return false; // Not a priced order, or already cancelled

        lvl = map_it->second.lvl.lock();
        order_it = map_it->second.it;

        if (!lvl) {
            id_map_.erase(map_it); // Clean up stale entry
            return false;
        }

        std::scoped_lock qlk(lvl->mtx);
        lvl->queue.erase(order_it); // O(1) erase using iterator
        id_map_.erase(map_it);      // O(1) erase from map
    }

    if (lvl->queue.empty()) { // Safe to check empty() without lock
        std::unique_lock w(treeMutex_);
        if (lvl->queue.empty()) { // Re-check after acquiring tree lock
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
            recompute_best_(); // Best price may have changed
        }
    }

    ENGINE_LOG("[cancelOrder] id=" << orderId << " removed");
    return true;
}

std::optional<Order*> OrderBook::findOrder(int orderId) {
    std::shared_lock im(id_mtx_);
    auto map_it = id_map_.find(orderId);
    if (map_it == id_map_.end()) return std::nullopt;

    auto lvl = map_it->second.lvl.lock();
    if (!lvl) return std::nullopt;

    auto order_it = map_it->second.it;

    std::scoped_lock lk(lvl->mtx);
    im.unlock();

    // Check if iterator still points to the same orderId within the locked level
    // This guards against rare race conditions where the order might have been
    // modified *just* before the level lock was acquired.
    if (order_it != lvl->queue.end() && order_it->orderId == orderId) {
         return std::optional<Order*>{ &(*order_it) };
    }
    // If the check fails, the map entry was stale, return nullopt
    return std::nullopt;
}


/**
 * @brief Modifies an order IN-PLACE.
 * @note This function is now only for in-place updates (e.g., quantity reduction,
 * status change). It will *not* move an order between levels. Price changes
 * must be handled by the MatchingEngine via cancelOrder + addOrder.
 */
void OrderBook::modifyOrder(int orderId, const OrderModificationRequest& modRequest) {
    if (!modRequest.hasModifications()) return;

    // --- PERFORMANCE FIX ---
    // The "re-queue" logic (slow mode) has been completely removed.
    // This function now *only* performs in-place updates.

    std::shared_lock im(id_mtx_);
    auto map_it = id_map_.find(orderId);
    if (map_it == id_map_.end()) { ENGINE_ERROR("[modifyOrder] not found"); return; }

    auto old_lvl = map_it->second.lvl.lock();
    auto old_it = map_it->second.it;
    if (!old_lvl) { return; } // Stale, no-op

    std::scoped_lock lk(old_lvl->mtx);
    im.unlock(); // Release map lock once level lock is held
    
    // Validate iterator before dereferencing within the locked level
    if (old_it == old_lvl->queue.end() || old_it->orderId != orderId) {
        ENGINE_ERROR("[modifyOrder] Stale iterator detected for orderId " << orderId);
        return; // Stale iterator, modification cannot proceed
    }

    // Apply fields in-place
    if (modRequest.newQuantity)         (*old_it).quantity = *modRequest.newQuantity;
    if (modRequest.newVisibleQuantity)  (*old_it).visibleQuantity = *modRequest.newVisibleQuantity;
    if (modRequest.newReplenishQuantity) (*old_it).replenishQuantity = *modRequest.newReplenishQuantity;
    if (modRequest.newExpiry)           (*old_it).expiry = *modRequest.newExpiry;
    if (modRequest.newStatus)           (*old_it).status = *modRequest.newStatus;

    // Price or StopPrice changes are *explicitly ignored* by this function.
    // The MatchingEngine must use cancel+add.
}

std::optional<Order> OrderBook::getBestOrder() const {
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

    std::shared_ptr<StopLevel> sl;

    {
        std::shared_lock r(stopTreeMutex_);
        if (stop_tree_.find(sp) == stop_tree_.end()) {
            r.unlock();
            std::unique_lock w(stopTreeMutex_);
            sl = ensure_stop_locked_for_write_(sp);
        } else {
            sl = stop_tree_.find(sp)->second;
        }
    }

    {
        std::unique_lock s(stopIndexMutex_);
        std::scoped_lock lk(sl->mtx);
        sl->queue.push_back(order);
        auto order_it = --sl->queue.end();
        stop_index_[order.orderId] = StopIdIndex{ sl, order_it };
    }
    ENGINE_LOG("[addStopOrder] id=" << order.orderId << " stop=" << sp);
}

bool OrderBook::cancelStopOrder(int orderId) {
    std::shared_ptr<StopLevel> sl;
    OrderQueue::iterator order_it;

    {
        std::unique_lock s(stopIndexMutex_);
        auto map_it = stop_index_.find(orderId);
        if (map_it == stop_index_.end()) return false; // Not a stop order

        sl = map_it->second.lvl.lock();
        order_it = map_it->second.it;

        if (!sl) {
            stop_index_.erase(map_it); // Clean stale
            return false;
        }

        std::scoped_lock lk(sl->mtx);
        // Validate iterator before erasing
        if (order_it == sl->queue.end() || order_it->orderId != orderId) {
             ENGINE_ERROR("[cancelStopOrder] Stale iterator detected for orderId " << orderId);
             stop_index_.erase(map_it); // Clean up map entry even if erase fails
             return false; // Stale iterator
        }
        sl->queue.erase(order_it);
        stop_index_.erase(map_it);
    }

    if (sl->queue.empty()) {
        cleanStopLevel(sl->stop); // This helper acquires its own lock
    }
    return true;
}

void OrderBook::checkAndTrigger(double lastPrice) {
    std::vector<double> toErase;

    if (is_buy_()) {
        std::shared_lock r(stopTreeMutex_);
        for (auto it = stop_tree_.begin(); it != stop_tree_.end() && it->first <= lastPrice; ++it) {
            auto &sl = *it->second;
            std::scoped_lock lk(sl.mtx);
            while (!sl.queue.empty()) {
                Order o = sl.queue.front(); sl.queue.pop_front(); // O(1)
                {
                    std::unique_lock s(stopIndexMutex_);
                    stop_index_.erase(o.orderId); // O(1)
                }
                triggeredOrders_.push_back(o);
            }
            if (sl.queue.empty()) toErase.push_back(it->first);
        }
    } else { // SELL side
        std::shared_lock r(stopTreeMutex_);
        auto it = stop_tree_.lower_bound(lastPrice); // Find first level >= lastPrice
        for (; it != stop_tree_.end(); ++it) {
            auto &sl = *it->second;
            std::scoped_lock lk(sl.mtx);
            while (!sl.queue.empty()) {
                Order o = sl.queue.front(); sl.queue.pop_front(); // O(1)
                {
                    std::unique_lock s(stopIndexMutex_);
                    stop_index_.erase(o.orderId); // O(1)
                }
                triggeredOrders_.push_back(o);
            }
            if (sl.queue.empty()) toErase.push_back(it->first);
        }
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
        if (rem > 0) {
            // Use parens for std::min just in case windows.h is included somewhere
            order->visibleQuantity = (std::min)(rem, *order->replenishQuantity);
        }
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
            for (auto order_it = q.begin(); order_it != q.end(); /*no increment*/) {
                if (order_it->type == OrderType::GTD && order_it->expiry && now > *order_it->expiry) {
                    {
                        std::unique_lock im(id_mtx_);
                        id_map_.erase(order_it->orderId);
                    }
                    order_it = q.erase(order_it); // O(1)
                } else {
                    ++order_it;
                }
            }
            if (q.empty()) emptyPrices.push_back(lvl->price);
        }
    } else { // SELL side
        for (auto it = sell_tree_->begin(); it != sell_tree_->end(); ++it) {
            auto lvl = it->second;
            std::scoped_lock lk(lvl->mtx);
            auto &q = lvl->queue;
            for (auto order_it = q.begin(); order_it != q.end(); /*no increment*/) {
                if (order_it->type == OrderType::GTD && order_it->expiry && now > *order_it->expiry) {
                    {
                        std::unique_lock im(id_mtx_);
                        id_map_.erase(order_it->orderId);
                    }
                    order_it = q.erase(order_it); // O(1)
                } else {
                    ++order_it;
                }
            }
            if (q.empty()) emptyPrices.push_back(lvl->price);
        }
    }
    r.unlock();

    if (!emptyPrices.empty()) {
        std::unique_lock w(treeMutex_);
        for (double px_level : emptyPrices) { // Renamed variable to avoid confusion
            if (is_buy_()) {
                auto it = buy_tree_->find(px_level);
                if (it != buy_tree_->end() && it->second->queue.empty()) buy_tree_->erase(it);
            } else {
                auto it = sell_tree_->find(px_level);
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
        for (auto order_it = q.begin(); order_it != q.end(); /*no increment*/) {
            if (order_it->expiry && now > *order_it->expiry) {
                {
                    std::unique_lock s(stopIndexMutex_);
                    stop_index_.erase(order_it->orderId);
                }
                order_it = q.erase(order_it); // O(1)
            } else {
                ++order_it;
            }
        }
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
        // *** FIX 1 & 2 ***: Use 'price' variable, correct find/erase logic
        auto it = buy_tree_->find(price);
        if (it != buy_tree_->end() && it->second->queue.empty()) {
            buy_tree_->erase(it);
        }
    } else {
        // *** FIX 1 & 2 ***: Use 'price' variable, correct find/erase logic
        auto it = sell_tree_->find(price);
        if (it != sell_tree_->end() && it->second->queue.empty()) {
            sell_tree_->erase(it);
        }
    }
    recompute_best_(); // Recompute best only if a level might have been removed
}

void OrderBook::cleanStopLevel(double stopPrice) {
    std::unique_lock w(stopTreeMutex_);
    // *** FIX 3 ***: Use '.' instead of '->'
    auto it = stop_tree_.find(stopPrice);
    if (it != stop_tree_.end() && it->second->queue.empty()) {
        stop_tree_.erase(it);
    }
}

} // namespace ultraBook
