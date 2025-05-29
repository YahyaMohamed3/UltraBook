#include <benchmark/benchmark.h>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include <atomic>
#include <iostream>
#include "engine.hpp"

// Constants for benchmarking
constexpr int ORDER_COUNT_SMALL = 10000;    // Small but significant benchmark size
constexpr int ORDER_COUNT_MEDIUM = 10000;   // Medium benchmark size for more reliable results
constexpr double PRICE_MIN = 109.9;
constexpr double PRICE_MAX = 110.0;
constexpr int QTY_MIN = 1;
constexpr int QTY_MAX = 100;

// Global atomic counter to ensure unique order IDs across all benchmarks
static std::atomic<int> global_order_id_counter{1};

// Helper class to generate test orders
class OrderGenerator {
private:
    std::mt19937 rng;
    std::uniform_real_distribution<double> price_dist;
    std::uniform_int_distribution<int> qty_dist;
    std::bernoulli_distribution side_dist;
    int benchmark_start_id; // Each benchmark instance will have its own starting ID

public:
    OrderGenerator() 
        : rng(std::random_device{}()),
          price_dist(PRICE_MIN, PRICE_MAX),
          qty_dist(QTY_MIN, QTY_MAX),
          side_dist(0.5), // 50% buy, 50% sell
          benchmark_start_id(global_order_id_counter.fetch_add(10000)) // Reserve a range of IDs
    {}
    
    // Generate a random limit order with a guaranteed unique ID
    ultraBook::Order generateLimitOrder() {
        double price = price_dist(rng);
        int qty = qty_dist(rng);
        bool is_buy = side_dist(rng);
        int order_id = global_order_id_counter.fetch_add(1); // Atomically get next ID
        return ultraBook::Order(
            order_id,
            std::optional<double>{price},
            qty,
            is_buy,
            ultraBook::OrderType::LIMIT
        );
    }
    
    // Generate a batch of orders with a specified buy/sell ratio
    std::vector<ultraBook::Order> generateOrders(int count, double buy_ratio = 0.5) {
        std::vector<ultraBook::Order> orders;
        orders.reserve(count);
        
        std::bernoulli_distribution custom_side_dist(buy_ratio);
        
        for (int i = 0; i < count; i++) {
            double price = price_dist(rng);
            int qty = qty_dist(rng);
            bool is_buy = custom_side_dist(rng);
            int order_id = global_order_id_counter.fetch_add(1); // Atomically get next ID
            orders.emplace_back(
                order_id,
                std::optional<double>{price},
                qty,
                is_buy,
                ultraBook::OrderType::LIMIT
            );
        }
        return orders;
    }
    
    // This is kept for backward compatibility with existing code
    // but now it doesn't reset to 1, it just ensures the next set of IDs
    // will be sequential starting from a new base
    void resetOrderIds() {
        // Instead of resetting to 1, we reserve a new block of IDs
        benchmark_start_id = global_order_id_counter.fetch_add(1000);
    }
    
    std::mt19937& getRng() {
        return rng;
    }
};

// Benchmark 1: Order Processing Throughput
static void BM_OrderProcessingThroughput(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    // No need to reset order IDs anymore as we use the global counter
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        ultraBook::MatchingEngine engine;
        
        // Create a more realistic order flow with different order profiles
        std::vector<ultraBook::Order> orders;
        orders.reserve(order_count);
        
        auto& rng = generator.getRng();
        std::uniform_real_distribution<> price_dist(PRICE_MIN, PRICE_MAX);
        std::uniform_int_distribution<> qty_dist(QTY_MIN, QTY_MAX);
        
        // Simulate different trading patterns to make the benchmark more realistic
        double current_price = 100.0; // Simulated current market price
        std::uniform_int_distribution<> pattern_dist(0, 9); // 0-9 for different patterns
        std::uniform_int_distribution<> cluster_index(0, 2); // For price clusters
        std::uniform_int_distribution<> tech_index(0, 4);   // For technical levels
        std::bernoulli_distribution buy_sell_dist(0.5);      // 50% buy, 50% sell
        
        for (int i = 0; i < order_count; i++) {
            double price;
            int quantity;
            bool is_buy;
            
            int pattern = pattern_dist(rng);
            
            if (pattern < 6) {
                // 1. Normal trading activity (60%)
                price = price_dist(rng);
                quantity = qty_dist(rng);
                is_buy = (price <= current_price); // Buy if below current price, sell if above
            }
            else if (pattern < 8) {
                // 2. Price discovery burst (20%)
                // Cluster orders around certain price points
                std::uniform_real_distribution<> cluster_dist(-0.5, 0.5);
                double base_price = 95.0 + cluster_index(rng) * 5.0; // Cluster around 95, 100, or 105
                price = base_price + cluster_dist(rng);
                quantity = qty_dist(rng);
                is_buy = buy_sell_dist(rng);
            }
            else if (pattern < 9) {
                // 3. Large institutional orders (10%)
                price = price_dist(rng);
                // Larger quantity for institutional orders
                std::uniform_int_distribution<> large_qty_dist(50, 200);
                quantity = large_qty_dist(rng);
                is_buy = buy_sell_dist(rng);
            }
            else {
                // 4. Technical levels trading (10%)
                // Orders at key price points (psychological levels)
                double tech_prices[] = {90.0, 95.0, 100.0, 105.0, 110.0};
                price = tech_prices[tech_index(rng)];
                quantity = qty_dist(rng);
                is_buy = (price <= current_price);
            }
            
            int orderId = global_order_id_counter.fetch_add(1);
            orders.emplace_back(
                orderId,
                std::optional<double>{price},
                quantity,
                is_buy,
                ultraBook::OrderType::LIMIT
            );
        }
        
        state.ResumeTiming();
        
        for (const auto& order : orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                order.isBuy
            );
        }
    }
    
    state.SetItemsProcessed(state.iterations() * order_count);
    state.SetLabel(std::to_string(order_count) + " orders");
}

// Benchmark 2: Matching Latency
static void BM_MatchingLatency(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    // No need to reset order IDs anymore, as each generator has unique IDs
    const int book_depth = state.range(0); // Number of orders in book
    const int match_count = state.range(1); // Number of orders to match
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        
        // Create a realistic market structure with price levels
        // 1. Define price levels for a market with a spread
        std::vector<double> buy_price_levels;
        std::vector<double> sell_price_levels;
        
        // Create buy price levels (90.0 to 99.5, with 0.5 increments)
        for (double price = 99.5; price >= 90.0; price -= 0.5) {
            buy_price_levels.push_back(price);
        }
        
        // Create sell price levels (100.5 to 110.0, with 0.5 increments)
        for (double price = 100.5; price <= 110.0; price += 0.5) {
            sell_price_levels.push_back(price);
        }
        
        // 2. Distribute orders with liquidity concentrated near the spread (more realistic)
        std::uniform_int_distribution<> qty_dist(1, 100);
        auto& rng = generator.getRng();
        
        // Calculate how many orders to place at each price level with an exponential distribution
        // More orders near the spread, fewer orders away from it
        int total_buy_orders = book_depth / 2;
        int total_sell_orders = book_depth - total_buy_orders;
        
        int remaining_buy_orders = total_buy_orders;
        int remaining_sell_orders = total_sell_orders;
        
        // Place buy orders with concentration near the spread
        for (size_t i = 0; i < buy_price_levels.size() && remaining_buy_orders > 0; i++) {
            // Exponential decline in liquidity with distance from spread
            double price_level_factor = std::exp(-static_cast<double>(i) * 0.3);
            int orders_at_level = static_cast<int>(std::max(1.0, price_level_factor * total_buy_orders * 0.2));
            orders_at_level = std::min(orders_at_level, remaining_buy_orders);
            
            // Place orders at this price level
            double price = buy_price_levels[i];
            for (int j = 0; j < orders_at_level; j++) {
                int quantity = qty_dist(rng);
                int order_id = global_order_id_counter.fetch_add(1);
                engine.addLimitOrder(order_id, price, quantity, true);
            }
            
            remaining_buy_orders -= orders_at_level;
        }
        
        // Place sell orders with concentration near the spread
        for (size_t i = 0; i < sell_price_levels.size() && remaining_sell_orders > 0; i++) {
            // Exponential decline in liquidity with distance from spread
            double price_level_factor = std::exp(-static_cast<double>(i) * 0.3);
            int orders_at_level = static_cast<int>(std::max(1.0, price_level_factor * total_sell_orders * 0.2));
            orders_at_level = std::min(orders_at_level, remaining_sell_orders);
            
            // Place orders at this price level
            double price = sell_price_levels[i];
            for (int j = 0; j < orders_at_level; j++) {
                int quantity = qty_dist(rng);
                int order_id = global_order_id_counter.fetch_add(1);
                engine.addLimitOrder(order_id, price, quantity, false);
            }
            
            remaining_sell_orders -= orders_at_level;
        }
        
        // Create orders that will match - with realistic price improvements and quantities
        std::uniform_real_distribution<> price_improvement_dist(0.0, 5.0);
        std::vector<ultraBook::Order> matching_orders;
        for (int i = 0; i < match_count; i++) {
            int order_id = global_order_id_counter.fetch_add(1);
            if (i % 2 == 0) {
                // Buy orders with prices that cross the spread (market aggressors)
                matching_orders.emplace_back(
                    order_id,
                    std::optional<double>{100.5 + price_improvement_dist(rng)},
                    qty_dist(rng), // Varying quantities
                    true,
                    ultraBook::OrderType::LIMIT
                );
            } else {
                // Sell orders with prices that cross the spread (market aggressors)
                matching_orders.emplace_back(
                    order_id,
                    std::optional<double>{99.5 - price_improvement_dist(rng)},
                    qty_dist(rng), // Varying quantities
                    false,
                    ultraBook::OrderType::LIMIT
                );
            }
        }
        
        state.ResumeTiming();
        
        // Add the matching orders and measure the time it takes
        for (const auto& order : matching_orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                order.isBuy
            );
        }
    }
    
    state.SetItemsProcessed(state.iterations() * match_count);
}

// Benchmark 3: Order Book Updates
static void BM_OrderBookUpdates(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    // No need to reset order IDs anymore as we use the global counter
    const int update_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        
        // Pre-populate the book
        auto initial_orders = generator.generateOrders(100);
        for (const auto& order : initial_orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                order.isBuy
            );
        }
        
        // Create a mix of new orders and cancellations
        std::vector<std::pair<bool, ultraBook::Order>> updates; // true=add, false=cancel
        updates.reserve(update_count);
        
        std::vector<int> order_ids;
        for (int i = 0; i < update_count; i++) {
            if (i < 100 || i % 3 != 0) {
                // Add a new order
                auto order = generator.generateLimitOrder();
                updates.emplace_back(true, order);
                order_ids.push_back(order.orderId);
            } else {
                // Cancel an existing order
                int cancel_id = -1;
                if (!order_ids.empty()) {
                    std::uniform_int_distribution<size_t> index_dist(0, order_ids.size() - 1);
                    size_t index = index_dist(generator.getRng());
                    cancel_id = order_ids[index];
                    order_ids.erase(order_ids.begin() + index);
                }
                
                if (cancel_id != -1) {
                    ultraBook::Order dummy;
                    dummy.orderId = cancel_id;
                    updates.emplace_back(false, dummy);
                } else {
                    // Fall back to adding a new order if no orders to cancel
                    auto order = generator.generateLimitOrder();
                    updates.emplace_back(true, order);
                    order_ids.push_back(order.orderId);
                }
            }
        }
        
        state.ResumeTiming();
        
        // Process all the updates
        for (const auto& update : updates) {
            if (update.first) {
                // Add order
                const auto& order = update.second;
                engine.addLimitOrder(
                    order.orderId,
                    order.price.value(),
                    order.quantity,
                    order.isBuy
                );
            } else {
                // Cancel order
                engine.cancelOrder(update.second.orderId);
            }
        }
    }
    
    state.SetItemsProcessed(state.iterations() * update_count);
}

// Benchmark 4: Single Order Lookup Performance
static void BM_OrderLookup(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    // No need to reset order IDs anymore as we use the global counter
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        
        // Add orders to the book
        auto orders = generator.generateOrders(order_count);
        std::vector<int> order_ids;
        order_ids.reserve(order_count);
        
        for (const auto& order : orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                order.isBuy
            );
            order_ids.push_back(order.orderId);
        }
        
        // Shuffle the order IDs to randomize lookups
        std::shuffle(order_ids.begin(), order_ids.end(), generator.getRng());
        
        state.ResumeTiming();
        
        // Perform order lookups - limit to exactly order_count lookups
        for (int i = 0; i < order_count; i++) {
            int id = order_ids[i % order_ids.size()];
            benchmark::DoNotOptimize(engine.getOrderStatus(id));
        }
    }
    
    state.SetItemsProcessed(state.iterations() * order_count);
}

// Benchmark 5: Order Cancellation Performance
static void BM_OrderCancellation(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    // No need to reset order IDs anymore as we use the global counter
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        
        // Add orders to the book
        auto orders = generator.generateOrders(order_count);
        std::vector<int> order_ids;
        order_ids.reserve(order_count);
        
        for (const auto& order : orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                order.isBuy
            );
            order_ids.push_back(order.orderId);
        }
        
        state.ResumeTiming();
        
        // Cancel all orders
        for (int id : order_ids) {
            engine.cancelOrder(id);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * order_count);
}

// Benchmark 6: Market Order Execution
static void BM_MarketOrderExecution(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    // No need to reset order IDs anymore as we use the global counter
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        
        // Prepare the book with limit orders
        auto buy_orders = generator.generateOrders(order_count/2, 1.0); // All buys
        auto sell_orders = generator.generateOrders(order_count/2, 0.0); // All sells
        
        // Add the limit orders to create liquidity
        for (const auto& order : buy_orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                true
            );
        }
        
        for (const auto& order : sell_orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                false
            );
        }
        
        // Generate market orders with varying sizes (realistic market impact)
        // Smaller orders are more common, large orders are rare
        std::vector<int> market_order_ids;
        std::vector<int> market_order_quantities;
        std::vector<bool> market_order_sides;
        
        // Generate a fixed number of market orders (20)
        const int NUM_MARKET_ORDERS = 20;
        std::uniform_real_distribution<> size_dist(0.0, 1.0);
        
        for (int i = 0; i < NUM_MARKET_ORDERS; i++) {
            // Use unique order IDs from global counter
            int order_id = global_order_id_counter.fetch_add(1);
            market_order_ids.push_back(order_id);
            
            // Generate quantity with power law distribution (more small orders, fewer large orders)
            double rand_val = size_dist(generator.getRng());
            // Cubic function to bias towards smaller sizes
            int qty = 5 + static_cast<int>(150 * rand_val * rand_val * rand_val);
            market_order_quantities.push_back(qty);
            
            // Alternate buy/sell
            bool is_buy = (i % 2 == 0);
            market_order_sides.push_back(is_buy);
        }
        
        state.ResumeTiming();
        
        // Execute the market orders
        for (size_t i = 0; i < market_order_ids.size(); i++) {
            engine.addMarketOrder(market_order_ids[i], market_order_quantities[i], market_order_sides[i]);
        }
    }
    
    state.SetItemsProcessed(state.iterations() * 20); // 20 market orders
}
// Benchmark 7: Order Modification Performance
static void BM_OrderModification(benchmark::State& state) {
    // Create a generator for this benchmark
    OrderGenerator generator;
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();

        ultraBook::MatchingEngine engine;
        auto orders = generator.generateOrders(order_count);
        std::vector<int> order_ids;
        order_ids.reserve(order_count);

        for (const auto& order : orders) {
            engine.addLimitOrder(
                order.orderId,
                order.price.value(),
                order.quantity,
                order.isBuy
            );
            order_ids.push_back(order.orderId);
        }

        state.ResumeTiming();

        std::uniform_real_distribution<double> price_dist(90.0, 110.0);
        std::uniform_int_distribution<int> qty_dist(1, 100);
        auto& rng = generator.getRng();

        for (int id : order_ids) {
            ultraBook::OrderModificationRequest mod_request;
            mod_request.newPrice = std::optional<double>(price_dist(rng));
            mod_request.newQuantity = qty_dist(rng);
            engine.ModifyOrder(id, mod_request);  // ✅ Pass id separately
        }
    }

    state.SetItemsProcessed(state.iterations() * order_count);
}

// Add a simple warmup benchmark to verify benchmark system is working
static void BM_Warmup(benchmark::State& state) {
    for (auto _ : state) {
        // Just a simple operation to make sure the benchmark framework is working
        std::vector<int> v(100, 0);
        benchmark::DoNotOptimize(v.data());
    }
}

// Register the warmup benchmark first
BENCHMARK(BM_Warmup)
    ->Unit(benchmark::kNanosecond)
    ->Iterations(1);  // Just run once for warm-up

// Register the benchmarks with more realistic sizes
// but use fixed iterations to limit runtime
BENCHMARK(BM_OrderProcessingThroughput)
    ->Arg(ORDER_COUNT_SMALL)      // Small but meaningful benchmark
    ->Unit(benchmark::kMicrosecond)  // Use μs for order processing
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

BENCHMARK(BM_MatchingLatency)
    ->Args({100, 20})             // 100 orders in book, 20 matches
    ->Unit(benchmark::kNanosecond)   // Use ns for latency measurements
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

BENCHMARK(BM_OrderBookUpdates)
    ->Arg(ORDER_COUNT_SMALL)      // Reasonable number of updates
    ->Unit(benchmark::kMicrosecond)  // Use μs for updates
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

BENCHMARK(BM_OrderLookup)
    ->Arg(ORDER_COUNT_SMALL)      // Reasonable number of lookups
    ->Unit(benchmark::kNanosecond)   // Use ns for lookups
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

BENCHMARK(BM_OrderCancellation)
    ->Arg(ORDER_COUNT_SMALL)      // Reasonable number of cancellations
    ->Unit(benchmark::kNanosecond)   // Use ns for cancellations
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

BENCHMARK(BM_MarketOrderExecution)
    ->Arg(100)                    // 100 limit orders with 20 market orders
    ->Unit(benchmark::kNanosecond)   // Use ns for market order execution
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

BENCHMARK(BM_OrderModification)
    ->Arg(ORDER_COUNT_SMALL)      // Reasonable number of modifications
    ->Unit(benchmark::kMicrosecond)   // Use μs for modifications
    ->Iterations(3)                  // Fixed number of iterations
    ->ReportAggregatesOnly();

// Use BENCHMARK_MAIN() which is simpler
BENCHMARK_MAIN();
