// Build with C++17.

#include <gtest/gtest.h>

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../orderbook/orderbook.hpp"
#include "types.hpp"

using namespace ultraBook;

// Simple reusable start barrier for C++17
class Cpp17Barrier {
public:
    explicit Cpp17Barrier(int count) : total_(count), count_(count), gen_(0) {}
    void arrive_and_wait() {
        std::unique_lock<std::mutex> lk(m_);
        int g = gen_;
        if (--count_ == 0) {
            gen_++;
            count_ = total_;
            cv_.notify_all();
        } else {
            cv_.wait(lk, [&]{ return gen_ != g; });
        }
    }
private:
    const int total_;
    int count_;
    int gen_;
    std::mutex m_;
    std::condition_variable cv_;
};

class OrderBookConcurrencyTest : public ::testing::Test {
protected:
    OrderBook buyBook{OrderBook::Side::BUY};
    OrderBook sellBook{OrderBook::Side::SELL};

    static std::pair<bool, std::optional<double>>
    findInSnapshot(const std::vector<Order>& snap, int id) {
        for (const auto& o : snap) if (o.orderId == id) return {true, o.price};
        return {false, std::optional<double>{}};
    }
};

// 1) Parallel adds to disjoint price levels (minimal contention)
TEST_F(OrderBookConcurrencyTest, ParallelAdds_DifferentLevels) {
    constexpr int T = 8;
    constexpr int N = 1000;
    Cpp17Barrier start(T);

    std::vector<std::thread> th;
    th.reserve(T);

    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t] {
            start.arrive_and_wait();
            const double base = 1000.0 + t; // distinct level per thread
            for (int i = 0; i < N; ++i) {
                const int id = t * 1'000'000 + i;
                buyBook.addOrder({id, base, 1, true, OrderType::LIMIT});
            }
        });
    }
    for (auto& x : th) x.join();

    const auto all = buyBook.getAllOrders();
    ASSERT_EQ(all.size(), static_cast<size_t>(T * N));

    std::unordered_map<double, int> counts;
    counts.reserve(T);
    for (const auto& o : all) {
        ASSERT_TRUE(o.price.has_value());
        counts[*o.price] += 1;
    }
    ASSERT_EQ(counts.size(), static_cast<size_t>(T));
    for (const auto& kv : counts) EXPECT_EQ(kv.second, N);
}

// 2) Parallel adds to the SAME price level (tests per-level locking)
TEST_F(OrderBookConcurrencyTest, ParallelAdds_SameLevel) {
    constexpr int T = 8;
    constexpr int N = 2000;
    constexpr double P = 123.45;
    Cpp17Barrier start(T);

    std::vector<std::thread> th;
    th.reserve(T);

    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t] {
            start.arrive_and_wait();
            for (int i = 0; i < N; ++i) {
                const int id = 10'000'000 + t * 1'000'000 + i;
                sellBook.addOrder({id, P, 1, false, OrderType::LIMIT});
            }
        });
    }
    for (auto& x : th) x.join();

    const auto all = sellBook.getAllOrders();
    ASSERT_EQ(all.size(), static_cast<size_t>(T * N));

    std::unordered_set<int> ids;
    ids.reserve(all.size());
    for (const auto& o : all) {
        ASSERT_TRUE(o.price.has_value());
        EXPECT_DOUBLE_EQ(*o.price, P);
        ids.insert(o.orderId);
    }
    EXPECT_EQ(ids.size(), all.size());
}

// 3) Readers hammer find/getBest while a writer modifies a hot order’s price (no deadlocks)
TEST_F(OrderBookConcurrencyTest, ReadersVsWriter_ModifyHotOrder) {
    for (int i = 0; i < 1000; ++i)
        buyBook.addOrder({i + 1, 100.0 + (i % 10), 1, true, OrderType::LIMIT});

    const int hotId = 777;
    {
        auto snap = buyBook.getAllOrders();
        auto [found, _] = findInSnapshot(snap, hotId);
        if (!found) buyBook.addOrder({hotId, 110.0, 1, true, OrderType::LIMIT});
    }

    std::atomic<bool> run{true};
    std::thread writer([&]{
        OrderModificationRequest r1, r2;
        r1.newPrice = 150.0;
        r2.newPrice = 110.0;
        for (int k = 0; k < 50'000; ++k)
            buyBook.modifyOrder(hotId, (k & 1) ? r1 : r2);
        run.store(false, std::memory_order_release);
    });

    constexpr int R = 6;
    std::vector<std::thread> readers;
    readers.reserve(R);
    for (int r = 0; r < R; ++r) {
        readers.emplace_back([&]{
            while (run.load(std::memory_order_acquire)) {
                (void)buyBook.findOrder(hotId);
                (void)buyBook.getBestOrder(); // best cache path
            }
        });
    }

    writer.join();
    for (auto& t : readers) t.join();

    const auto snap = buyBook.getAllOrders();
    auto [found, px] = findInSnapshot(snap, hotId);
    ASSERT_TRUE(found);
    ASSERT_TRUE(px.has_value());
    EXPECT_TRUE(*px == 150.0 || *px == 110.0);
}

// 4) Concurrent cancel after bulk insert (ensures no deadlocks, consistent remainder)
TEST_F(OrderBookConcurrencyTest, ConcurrentCancelAfterBulkInsert) {
    constexpr int TOTAL = 5000;
    for (int i = 0; i < TOTAL; ++i) {
        const double p = 200.0 + (i % 5);
        buyBook.addOrder({100'000 + i, p, 1, true, OrderType::LIMIT});
    }

    constexpr int T = 6;
    std::vector<std::thread> th;
    th.reserve(T);

    for (int t = 0; t < T; ++t) {
        th.emplace_back([&, t]{
            for (int i = t; i < TOTAL; i += T) {
                const int id = 100'000 + i;
                if ((id % 2) == 0) buyBook.cancelOrder(id);
            }
        });
    }
    for (auto& x : th) x.join();

    const auto all = buyBook.getAllOrders();
    for (const auto& o : all) EXPECT_EQ(o.orderId % 2, 1) << "Even id remained: " << o.orderId;
}

/* NEW: cache/id-map stress & invariants */

// 5) Readers see consistent best while head level churns (cache never dangles)
TEST_F(OrderBookConcurrencyTest, ReadersSeeConsistentBest) {
    // Seed two levels; 101.0 will be best on BUY
    buyBook.addOrder({1, 100.0, 1, true, OrderType::LIMIT});
    buyBook.addOrder({2, 101.0, 1, true, OrderType::LIMIT});

    std::atomic<bool> run{true};
    std::thread reader([&]{
        while (run.load(std::memory_order_acquire)) {
            auto b = buyBook.getBestOrder();
            if (b) { ASSERT_GT(b->quantity, 0); }
        }
    });

    // Writer repeatedly toggles who is best & empties head
    for (int k = 0; k < 5000; ++k) {
        buyBook.addOrder({10'000 + k, 102.0, 1, true, OrderType::LIMIT}); // superior
        auto best = buyBook.getBestOrder();
        ASSERT_TRUE(best.has_value());
        buyBook.cancelOrder(best->orderId); // erase head; cache must recompute
    }

    run.store(false, std::memory_order_release);
    reader.join();
}

// 6) Id-map stress: many modify/cancel across disjoint levels
TEST_F(OrderBookConcurrencyTest, IdMapStress_ModifyCancel) {
    constexpr int N = 3000;
    for (int i = 0; i < N; ++i) {
        double px = 50.0 + (i % 50);
        buyBook.addOrder({200'000 + i, px, 2, true, OrderType::LIMIT});
    }

    std::atomic<bool> run{true};
    std::thread mods([&]{
        for (int i = 0; i < N; ++i) {
            OrderModificationRequest r;
            if ((i % 3) == 0) r.newPrice = 60.0 + (i % 50); // move across levels
            if ((i % 5) == 0) r.newQuantity = 1;            // decrease qty (no requeue)
            buyBook.modifyOrder(200'000 + i, r);
        }
        run.store(false, std::memory_order_release);
    });

    std::thread cancels([&]{
        int i = 0;
        while (run.load(std::memory_order_acquire) && i < N) {
            if ((i % 7) == 0) buyBook.cancelOrder(200'000 + i);
            ++i;
        }
    });

    mods.join();
    cancels.join();

    // Ensure no phantom entries remained (light sanity)
    for (int i = 0; i < N; ++i) {
        auto optPtr = buyBook.findOrder(200'000 + i); // std::optional<Order*>
        if (optPtr.has_value()) {
            EXPECT_NE(optPtr.value(), nullptr);
        }
    }
}
