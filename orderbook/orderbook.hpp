#ifndef ORDERBOOK_HPP
#define ORDERBOOK_HPP

#include <atomic>
#include <memory>
#include <memory_resource>
#include <map>
#include <list> // <--- Changed from <deque>
#include <unordered_map>
#include <optional>
#include <mutex>
#include <shared_mutex>
#include <functional>
#include <vector>
#include <chrono>
#include "types.hpp"

namespace ultraBook {

class OrderBook {
public:
    enum class Side { BUY, SELL };

    explicit OrderBook(Side side);

    // ---------- Core API (thread-safe) ----------
    void addOrder(Order order);
    bool cancelOrder(int orderId);
    std::optional<Order*> findOrder(int orderId);   // returns non-owning pointer; not thread-safe to mutate concurrently
    void modifyOrder(int orderId, const OrderModificationRequest& modRequest);
    std::optional<Order> getBestOrder() const;      // snapshot copy (O(1) via best-level cache)
    bool isEmpty() const;
    void clear();

    // ---------- Stop orders ----------
    void addStopOrder(Order order);
    void checkAndTrigger(double lastPrice);           // collect triggered to a buffer
    std::vector<Order> getAndClearTriggeredOrders();  // drain triggered buffer
    bool cancelStopOrder(int orderId); // <-- Added helper for O(1) stop cancel

    // ---------- Iceberg ----------
    void replenishIcebergOrder(Order* order);

    // ---------- GTD / housekeeping ----------
    void checkExpiredOrders();
    void removeExpiredStopOrders();

    // ---------- Utilities ----------
    void printOrderBook() const;
    std::vector<Order> getAllOrders() const;

private:
    // -------- Internal structures (PMR-aware) --------
    
    // Using std::pmr::list for O(1) erase
    using OrderQueue = std::pmr::list<Order>;
    
    struct Level {
        explicit Level(double px, std::pmr::memory_resource* mr)
        : price(px), queue(OrderQueue(mr)) {} // <-- Use list

        double price;
        OrderQueue queue;             // <-- Use list
        mutable std::mutex mtx;       // protects only this level's queue
    };

    struct StopLevel {
        explicit StopLevel(double sp, std::pmr::memory_resource* mr)
        : stop(sp), queue(OrderQueue(mr)) {} // <-- Use list

        double stop;
        OrderQueue queue;             // <-- Use list
        mutable std::mutex mtx;
    };

    struct BuyCmp   { bool operator()(double a, double b) const noexcept { return a > b; } };
    struct SellCmp  { bool operator()(double a, double b) const noexcept { return a < b; } };

    using BuyTree   = std::pmr::map<double, std::shared_ptr<Level>,   BuyCmp>;
    using SellTree  = std::pmr::map<double, std::shared_ptr<Level>,   SellCmp>;
    using StopTree  = std::pmr::map<double, std::shared_ptr<StopLevel>, std::less<double>>;

    // -------- Helpers (tree write lock must be held where noted) --------
    std::shared_ptr<Level> ensure_level_locked_for_write_(double px); // tree write lock must be held
    std::shared_ptr<StopLevel> ensure_stop_locked_for_write_(double sp); // stop tree write lock must be held

    void maybe_update_best_add_(const std::shared_ptr<Level>& lvl);   // tree write lock held
    void recompute_best_();                                           // tree write lock held

    // Locate via id index (fast path), returns level + iterator
    struct Location { std::shared_ptr<Level> lvl; OrderQueue::iterator it; }; // <-- Use iterator
    std::optional<Location> locate_(int orderId) const;

    // Clean empty nodes (call without holding level lock)
    void cleanPriceLevel(double price);
    void cleanStopLevel(double stopPrice);

private:
    // -------- Config / side --------
    Side side_;

    // -------- PMR arenas --------
    mutable std::pmr::monotonic_buffer_resource arena_{ 2 * 1024 * 1024 };
    mutable std::pmr::synchronized_pool_resource node_arena_{ std::pmr::new_delete_resource() };

    // -------- Price trees --------
    BuyTree* buy_tree_  = nullptr;
    SellTree* sell_tree_ = nullptr;
    mutable std::shared_mutex treeMutex_; 
    mutable std::shared_ptr<Level> bestLevel_;

    // -------- Stop trees --------
    StopTree stop_tree_{ &arena_ };
    mutable std::shared_mutex stopTreeMutex_;

    // -------- Global id → location index (for priced orders) --------
    // Stores iterator for O(1) lookup/erase
    struct IdIndex { std::weak_ptr<Level> lvl; OrderQueue::iterator it; }; // <-- Use iterator
    mutable std::pmr::unordered_map<int, IdIndex> id_map_{ &arena_ };
    mutable std::shared_mutex id_mtx_; // protects id_map_

    // For stop orders: orderId -> location (for O(1) cancel/modify)
    struct StopIdIndex { std::weak_ptr<StopLevel> lvl; OrderQueue::iterator it; }; // <-- New struct
    std::pmr::unordered_map<int, StopIdIndex> stop_index_{ &arena_ }; // <-- Changed type
    mutable std::shared_mutex stopIndexMutex_;

    // Triggered buffer for handoff to engine
    std::vector<Order> triggeredOrders_; 

    // Utility
    bool is_buy_() const noexcept { return side_ == Side::BUY; }
};

} // namespace ultraBook

#endif // ORDERBOOK_HPP