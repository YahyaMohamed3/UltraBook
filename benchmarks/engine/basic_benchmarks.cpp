#include <benchmark/benchmark.h>
#include <vector>
#include <random>
#include <algorithm>
#include <chrono>
#include "engine.hpp"

// Constants for benchmarking
constexpr int ORDER_COUNT_SMALL = 1000;
constexpr int ORDER_COUNT_MEDIUM = 10000;
constexpr double PRICE_MIN = 90.0;
constexpr double PRICE_MAX = 110.0;
constexpr int QTY_MIN = 1;
constexpr int QTY_MAX = 100;

// Helper class to generate test orders
class OrderGenerator {
private:
    std::mt19937 rng;
    std::uniform_real_distribution<double> price_dist;
    std::uniform_int_distribution<int> qty_dist;
    std::bernoulli_distribution side_dist;
    int next_order_id = 1;

public:
    OrderGenerator() 
        : rng(std::random_device{}()),
          price_dist(PRICE_MIN, PRICE_MAX),
          qty_dist(QTY_MIN, QTY_MAX),
          side_dist(0.5) // 50% buy, 50% sell
    {}
    
    // Generate a random limit order
    ultraBook::Order generateLimitOrder() {
        double price = price_dist(rng);
        int qty = qty_dist(rng);
        bool is_buy = side_dist(rng);
        return ultraBook::Order(
            next_order_id++,
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
            orders.emplace_back(
                next_order_id++,
                std::optional<double>{price},
                qty,
                is_buy,
                ultraBook::OrderType::LIMIT
            );
        }
        return orders;
    }
    
    void resetOrderIds() {
        next_order_id = 1;
    }
    
    std::mt19937& getRng() {
        return rng;
    }
};

// Benchmark 1: Order Processing Throughput
static void BM_OrderProcessingThroughput(benchmark::State& state) {
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        ultraBook::MatchingEngine engine;
        OrderGenerator generator;
        
        // Create a more realistic order flow with different order profiles
        std::vector<ultraBook::Order> orders;
        orders.reserve(order_count);
        
        auto& rng = generator.getRng();
        std::uniform_real_distribution<> price_dist(PRICE_MIN, PRICE_MAX);
        std::uniform_int_distribution<> qty_dist(QTY_MIN, QTY_MAX);
        
        // Simulate different trading patterns to make the benchmark more realistic:
        // 1. Normal trading activity (60% of orders)
        // 2. Price discovery burst (20% of orders tightly clustered around certain price points)
        // 3. Large institutional orders (10% of orders with larger sizes)
        // 4. Technical levels trading (10% of orders at key price points)
        
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
            
            orders.emplace_back(
                generator.generateLimitOrder().orderId,
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
    const int book_depth = state.range(0); // Number of orders in book
    const int match_count = state.range(1); // Number of orders to match
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        OrderGenerator generator;
        
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
                engine.addLimitOrder(generator.generateLimitOrder().orderId, price, quantity, true);
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
                engine.addLimitOrder(generator.generateLimitOrder().orderId, price, quantity, false);
            }
            
            remaining_sell_orders -= orders_at_level;
        }
        
        // Create orders that will match - with realistic price improvements and quantities
        std::uniform_real_distribution<> price_improvement_dist(0.0, 5.0);
        std::vector<ultraBook::Order> matching_orders;
        for (int i = 0; i < match_count; i++) {
            if (i % 2 == 0) {
                // Buy orders with prices that cross the spread (market aggressors)
                matching_orders.emplace_back(
                    generator.generateLimitOrder().orderId,
                    std::optional<double>{100.5 + price_improvement_dist(rng)},
                    qty_dist(rng), // Varying quantities
                    true,
                    ultraBook::OrderType::LIMIT
                );
            } else {
                // Sell orders with prices that cross the spread (market aggressors)
                matching_orders.emplace_back(
                    generator.generateLimitOrder().orderId,
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
    const int update_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        OrderGenerator generator;
        
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
                    size_t index = generator.getRng() % order_ids.size();
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
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        OrderGenerator generator;
        
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
        
        // Perform order lookups
        for (int i = 0; i < std::min(1000, order_count); i++) {
            int id = order_ids[i % order_ids.size()];
            benchmark::DoNotOptimize(engine.getOrderStatus(id));
        }
    }
    
    state.SetItemsProcessed(state.iterations() * std::min(1000, order_count));
}

// Benchmark 5: Order Cancellation Performance
static void BM_OrderCancellation(benchmark::State& state) {
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        OrderGenerator generator;
        
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
    const int order_count = state.range(0);
    
    for (auto _ : state) {
        state.PauseTiming();
        
        ultraBook::MatchingEngine engine;
        OrderGenerator generator;
        
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
        
        std::uniform_real_distribution<> size_dist(0.0, 1.0);
        
        for (int i = 0; i < 10; i++) {
            // Generate order ID
            int order_id = generator.generateLimitOrder().orderId;
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
    
    state.SetItemsProcessed(state.iterations() * 10); // 10 market orders
}

// Register the benchmarks
BENCHMARK(BM_OrderProcessingThroughput)
    ->Arg(1000)     // 1K orders
    ->Arg(10000)    // 10K orders
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_MatchingLatency)
    ->Args({100, 10})      // 100 orders in book, 10 matches
    ->Args({1000, 100})    // 1K orders in book, 100 matches
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_OrderBookUpdates)
    ->Arg(1000)     // 1K updates
    ->Arg(10000)    // 10K updates
    ->Unit(benchmark::kMillisecond);

BENCHMARK(BM_OrderLookup)
    ->Arg(100)      // 100 orders
    ->Arg(10000)    // 10K orders
    ->Unit(benchmark::kNanosecond);

BENCHMARK(BM_OrderCancellation)
    ->Arg(100)      // 100 orders
    ->Arg(1000)     // 1K orders
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(BM_MarketOrderExecution)
    ->Arg(1000)     // 1K limit orders in book
    ->Arg(10000)    // 10K limit orders in book
    ->Unit(benchmark::kMicrosecond);

BENCHMARK_MAIN();
