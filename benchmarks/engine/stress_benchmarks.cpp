// benchmarks/engine/stress_benchmarks.cpp
#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <optional>
#include <random>
#include <thread>
#include <vector>
#include <algorithm>
#include <numeric>
#include <mutex>
#include <condition_variable> // C++17 barrier replacement
#include <cassert>
#include <string>

#include "engine.hpp"

// --- Windows high-resolution timer nudger ---
#ifdef _WIN32
#  ifndef NOMINMAX
#    define NOMINMAX // prevent <windows.h> from defining min/max macros
#  endif
#  include <windows.h>
static auto init_windows_timer = []() {
    timeBeginPeriod(1);
    return 0;
}();
#endif

// ---------- Config ----------
static constexpr int64_t BIG_BATCH       = 1'000'000;  // 1M orders for scale
static constexpr int64_t MID_BATCH       =   250'000;  // cancellation-heavy
static constexpr int     SAMPLE_EVERY_N  = 997;        // ~0.1% sampling with a prime to decorrelate
static constexpr double  PRICE_MIN       = 99.5;
static constexpr double  PRICE_MAX       = 110.5;
static constexpr int     QTY_MIN         = 1;
static constexpr int     QTY_MAX         = 100;

// Global atomic for unique IDs across all threads/benches
static std::atomic<int> g_id{1};

// --------- Simple C++17 barrier ----------
class Barrier {
public:
    explicit Barrier(unsigned count) : threshold_(count), count_(count), generation_(0) {}

    void arrive_and_wait() {
        std::unique_lock<std::mutex> lk(m_);
        auto gen = generation_;
        if (--count_ == 0) {
            generation_++;
            count_ = threshold_;
            cv_.notify_all();
        } else {
            cv_.wait(lk, [&]{ return gen != generation_; });
        }
    }

    unsigned threshold() const { return threshold_; }

private:
    std::mutex m_;
    std::condition_variable cv_;
    const unsigned threshold_;
    unsigned count_;
    unsigned generation_;
};

// Generator (not timed inside the benchmark loop)
struct OrderGen {
    std::mt19937_64 rng;
    std::uniform_real_distribution<double> px{PRICE_MIN, PRICE_MAX};
    std::uniform_int_distribution<int> qty{QTY_MIN, QTY_MAX};
    std::bernoulli_distribution side{0.5};

    explicit OrderGen(uint64_t seed) : rng(seed) {}

    ultraBook::Order limit() {
        return ultraBook::Order(
            g_id.fetch_add(1, std::memory_order_relaxed),
            std::optional<double>{px(rng)},
            qty(rng),
            side(rng),
            ultraBook::OrderType::LIMIT
        );
    }
};

// Percentile helper
static double percentile_ns(std::vector<double>& v, double p) {
    if (v.empty()) return 0.0;
    p = std::max(0.0, std::min(100.0, p));
    const size_t k = static_cast<size_t>(p * (v.size() - 1) / 100.0);
    std::nth_element(v.begin(), v.begin() + k, v.end());
    return v[k];
}

// Run a timed parallel region using manual iteration time
template <typename Fn>
static void run_parallel_with_manual_time(benchmark::State& state, Fn&& fn) {
    // Skip manual timing for now - just use regular benchmark timing
    for (auto _ : state) {
        fn();
    }
}

// ------------------------------------------------------------
// 1) High-Contention Throughput: many threads add LIMIT orders
// ------------------------------------------------------------
static void BM_Throughput_Contention(benchmark::State& state) {
    const int64_t total_orders = state.range(0);
    const int     T            = state.threads();
    const int64_t per_thread   = total_orders / T;

    for (auto _ : state) {
        ultraBook::MatchingEngine engine; // one shared per-iteration

        // Pre-generate orders per thread (outside timed section)
        std::vector<std::vector<ultraBook::Order>> thread_orders(static_cast<size_t>(T));
        for (int tid = 0; tid < T; ++tid) {
            OrderGen gen(0xC0FFEEULL + static_cast<uint64_t>(tid));
            auto& v = thread_orders[static_cast<size_t>(tid)];
            v.reserve(static_cast<size_t>(per_thread));
            for (int64_t i = 0; i < per_thread; ++i) v.emplace_back(gen.limit());
        }

        // Shared latency samples
        std::mutex lat_mx;
        std::vector<double> lat_samples_ns;
        lat_samples_ns.reserve(static_cast<size_t>(total_orders / SAMPLE_EVERY_N));

        run_parallel_with_manual_time(state, [&]{
            const int tid = state.thread_index();
            auto& v = thread_orders[static_cast<size_t>(tid)];

            for (size_t i = 0; i < v.size(); ++i) {
                if ((i % SAMPLE_EVERY_N) == 0) {
                    auto t0 = std::chrono::high_resolution_clock::now();
                    engine.addLimitOrder(v[i].orderId, *v[i].price, v[i].quantity, v[i].isBuy);
                    auto t1 = std::chrono::high_resolution_clock::now();
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                    std::lock_guard<std::mutex> lk(lat_mx);
                    lat_samples_ns.push_back(static_cast<double>(ns));
                } else {
                    engine.addLimitOrder(v[i].orderId, *v[i].price, v[i].quantity, v[i].isBuy);
                }
            }
        });

        if (state.thread_index() == 0) {
            auto samples = lat_samples_ns;
            state.counters["p50_ns"] = percentile_ns(samples, 50.0);
            state.counters["p90_ns"] = percentile_ns(samples, 90.0);
            state.counters["p99_ns"] = percentile_ns(samples, 99.0);
            state.SetItemsProcessed(total_orders);
            state.counters["ops_per_sec"] = benchmark::Counter(
                static_cast<double>(total_orders),
                benchmark::Counter::kIsRate | benchmark::Counter::kIsIterationInvariantRate
            );
        }
    }
}
BENCHMARK(BM_Throughput_Contention)
    ->Arg(BIG_BATCH)
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(12)
    ->ReportAggregatesOnly(true);

// ----------------------------------------------------------------------
// 2) Cancel-Heavy: build book once, then many threads cancel aggressively
// ----------------------------------------------------------------------
static void BM_Cancel_Heavy_Contention(benchmark::State& state) {
    const int64_t total_orders = state.range(0);
    const int     T            = state.threads();
    const int64_t per_thread   = total_orders / T;

    for (auto _ : state) {
        ultraBook::MatchingEngine engine;

        // Seed book (not timed)
        {
            OrderGen gen(0xDEADBEEF);
            std::vector<ultraBook::Order> seed;
            seed.reserve(static_cast<size_t>(total_orders));
            for (int64_t i = 0; i < total_orders; ++i) seed.emplace_back(gen.limit());
            for (auto& o : seed) {
                engine.addLimitOrder(o.orderId, *o.price, o.quantity, o.isBuy);
            }
        }

        // Prepare per-thread cancel lists (not timed)
        std::vector<std::vector<int>> cancel_ids(static_cast<size_t>(T));
        {
            // Collect recent IDs roughly evenly
            std::vector<int> all_ids;
            all_ids.reserve(static_cast<size_t>(total_orders));
            const int last = g_id.load(std::memory_order_relaxed);
            for (int i = last - static_cast<int>(total_orders); i < last; ++i) {
                all_ids.push_back(i);
            }
            std::mt19937 rng(123);
            std::shuffle(all_ids.begin(), all_ids.end(), rng);

            size_t idx = 0;
            for (int tid = 0; tid < T; ++tid) {
                auto& v = cancel_ids[static_cast<size_t>(tid)];
                v.reserve(static_cast<size_t>(per_thread));
                for (int64_t k = 0; k < per_thread && idx < all_ids.size(); ++k, ++idx) {
                    v.push_back(all_ids[idx]);
                }
            }
        }

        // Latency samples
        std::mutex lat_mx;
        std::vector<double> lat_samples_ns;
        lat_samples_ns.reserve(static_cast<size_t>(total_orders / SAMPLE_EVERY_N));

        run_parallel_with_manual_time(state, [&]{
            const int tid = state.thread_index();
            auto& ids = cancel_ids[static_cast<size_t>(tid)];

            for (size_t i = 0; i < ids.size(); ++i) {
                if ((i % SAMPLE_EVERY_N) == 0) {
                    auto t0 = std::chrono::high_resolution_clock::now();
                    engine.cancelOrder(ids[i]);
                    auto t1 = std::chrono::high_resolution_clock::now();
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                    std::lock_guard<std::mutex> lk(lat_mx);
                    lat_samples_ns.push_back(static_cast<double>(ns));
                } else {
                    engine.cancelOrder(ids[i]);
                }
            }
        });

        if (state.thread_index() == 0) {
            auto samples = lat_samples_ns;
            state.counters["p50_ns"] = percentile_ns(samples, 50.0);
            state.counters["p90_ns"] = percentile_ns(samples, 90.0);
            state.counters["p99_ns"] = percentile_ns(samples, 99.0);
            state.SetItemsProcessed(total_orders);
            state.counters["ops_per_sec"] = benchmark::Counter(
                static_cast<double>(total_orders),
                benchmark::Counter::kIsRate | benchmark::Counter::kIsIterationInvariantRate
            );
        }
    }
}
BENCHMARK(BM_Cancel_Heavy_Contention)
    ->Arg(MID_BATCH)
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(12)
    ->ReportAggregatesOnly(true);

// ---------------------------------------------------------------------------
// 3) Mixed Stress: add + cancel + modify + occasional market orders (contention)
// ---------------------------------------------------------------------------
static void BM_Mixed_Stress(benchmark::State& state) {
    const int64_t total_ops  = state.range(0);
    const int     T          = state.threads();
    const int64_t per_thread = total_ops / T;

    // Mix ratios
    const double add_ratio    = 0.55;
    const double cancel_ratio = 0.30;
    const double modify_ratio = 0.10;
    const double market_ratio = 0.05;

    for (auto _ : state) {
        ultraBook::MatchingEngine engine;

        // Warm seed (not timed)
        {
            OrderGen gen(20240815);
            for (int i = 0; i < 50'000; ++i) {
                auto o = gen.limit();
                engine.addLimitOrder(o.orderId, *o.price, o.quantity, o.isBuy);
            }
        }

        // Per-thread scripted ops (not timed)
        struct Op {
            uint8_t kind;              // 0=add, 1=cancel, 2=modify, 3=market
            ultraBook::Order o{};      // used for 'add' and to carry qty/side for market
            int id{0};                 // order id for cancel/modify
        };

        std::vector<std::vector<Op>> thread_ops(static_cast<size_t>(T));
        for (int tid = 0; tid < T; ++tid) {
            OrderGen gen(0xABC000ULL + static_cast<uint64_t>(tid));
            std::discrete_distribution<int> which({
                add_ratio, cancel_ratio, modify_ratio, market_ratio
            });
            auto& ops = thread_ops[static_cast<size_t>(tid)];
            ops.reserve(static_cast<size_t>(per_thread));

            for (int64_t i = 0; i < per_thread; ++i) {
                int k = which(gen.rng);
                Op op{};
                op.kind = static_cast<uint8_t>(k);
                if (k == 0) {
                    op.o = gen.limit();
                } else if (k == 1) {
                    // cancel: choose a recent id to increase hit rate
                    op.id = std::max(1, g_id.load() - static_cast<int>(gen.rng() % 40'000));
                } else if (k == 2) {
                    // modify: choose a recent id
                    op.id = std::max(1, g_id.load() - static_cast<int>(gen.rng() % 40'000));
                } else {
                    // market: store qty/side in op.o
                    op.o.quantity = 1 + static_cast<int>(gen.rng() % 200);
                    op.o.isBuy    = (gen.rng() & 1) != 0;
                    op.id         = g_id.fetch_add(1);
                }
                ops.push_back(op);
            }
        }

        // Latency samples
        std::mutex lat_mx;
        std::vector<double> lat_samples_ns;
        lat_samples_ns.reserve(static_cast<size_t>(total_ops / SAMPLE_EVERY_N));

        run_parallel_with_manual_time(state, [&]{
            const int tid = state.thread_index();
            auto& ops = thread_ops[static_cast<size_t>(tid)];

            std::uniform_real_distribution<double> px(PRICE_MIN, PRICE_MAX);
            std::uniform_int_distribution<int>    qd(QTY_MIN, QTY_MAX);
            std::mt19937_64 rng(0xFACE0000ULL + static_cast<uint64_t>(tid));

            auto do_one = [&](const Op& op){
                switch (op.kind) {
                    case 0: // add
                        // op.o.price is set for limit orders
                        engine.addLimitOrder(op.o.orderId, *op.o.price, op.o.quantity, op.o.isBuy);
                        break;
                    case 1: // cancel
                        engine.cancelOrder(op.id);
                        break;
                    case 2: { // modify
                        ultraBook::OrderModificationRequest m;
                        m.newPrice    = std::optional<double>(px(rng));
                        m.newQuantity = qd(rng);
                        engine.modifyOrder(op.id, m);
                        break;
                    }
                    case 3: // market
                        engine.addMarketOrder(op.id, op.o.quantity, op.o.isBuy);
                        break;
                }
            };

            for (size_t i = 0; i < ops.size(); ++i) {
                if ((i % SAMPLE_EVERY_N) == 0) {
                    auto t0 = std::chrono::high_resolution_clock::now();
                    do_one(ops[i]);
                    auto t1 = std::chrono::high_resolution_clock::now();
                    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count();
                    std::lock_guard<std::mutex> lk(lat_mx);
                    lat_samples_ns.push_back(static_cast<double>(ns));
                } else {
                    do_one(ops[i]);
                }
            }
        });

        if (state.thread_index() == 0) {
            auto samples = lat_samples_ns;
            state.counters["p50_ns"] = percentile_ns(samples, 50.0);
            state.counters["p90_ns"] = percentile_ns(samples, 90.0);
            state.counters["p99_ns"] = percentile_ns(samples, 99.0);
            state.SetItemsProcessed(total_ops);
            state.counters["ops_per_sec"] = benchmark::Counter(
                static_cast<double>(total_ops),
                benchmark::Counter::kIsRate | benchmark::Counter::kIsIterationInvariantRate
            );
        }
    }
}
BENCHMARK(BM_Mixed_Stress)
    ->Arg(1'200'000)               // 1.2M mixed ops total
    ->Unit(benchmark::kMillisecond)
    ->Threads(1)->Threads(2)->Threads(4)->Threads(8)->Threads(12)
    ->ReportAggregatesOnly(true);

// A tiny smoke test to verify the harness works (not stressed)
static void BM_Smoke(benchmark::State& state) {
    for (auto _ : state) {
        ultraBook::MatchingEngine e;
        const int N = 1000;
        for (int i = 0; i < N; ++i) {
            int id = g_id.fetch_add(1);
            e.addLimitOrder(id, 100.0 + (i % 5), 10 + (i % 3), (i & 1) == 0);
        }
        state.SetItemsProcessed(N);
        state.counters["ops_per_sec"] = benchmark::Counter(
            static_cast<double>(N),
            benchmark::Counter::kIsRate | benchmark::Counter::kIsIterationInvariantRate
        );
    }
}
BENCHMARK(BM_Smoke)->Unit(benchmark::kMillisecond);

BENCHMARK_MAIN();