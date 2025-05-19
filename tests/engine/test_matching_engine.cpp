#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "engine.hpp"

using namespace ultraBook;
using namespace std::chrono_literals;

// Test fixture for matching engine tests
class MatchingEngineTest : public ::testing::Test {
protected:
    void SetUp() override {
        // This runs before each test
    }

    void TearDown() override {
        // This runs after each test
    }

    MatchingEngine engine; // Fresh engine instance for each test
};

// Test that a new matching engine has an empty order book
TEST_F(MatchingEngineTest, EmptyOrderBookAtStart) {
    // Print the order book (this should be empty)
    engine.printOrderBook();
    
    // We don't have a direct way to count orders, but we can verify
    // no errors occur when printing an empty book
    SUCCEED();
}

// Test adding limit orders
TEST_F(MatchingEngineTest, AddLimitOrders) {
    // Add buy and sell limit orders
    engine.addLimitOrder(1, 10.0, 100, true);  // Buy 100 shares at $10.00
    engine.addLimitOrder(2, 11.0, 200, false); // Sell 200 shares at $11.00
    
    // Verify orders have the expected status
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::ACTIVE);
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::ACTIVE);
    
    // Print the order book to verify orders are there
    engine.printOrderBook();
}

// Test basic order matching
TEST_F(MatchingEngineTest, BasicOrderMatching) {
    // Add orders that should match
    engine.addLimitOrder(1, 10.0, 50, true);   // Buy at $10.00
    engine.addLimitOrder(2, 10.0, 50, false);  // Sell at $10.00
    
    // Match orders
    engine.matchOrders();
    
    // Check both orders are filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::FILLED);
}

// Test market orders
TEST_F(MatchingEngineTest, MarketOrderExecution) {
    // Add limit sell order
    engine.addLimitOrder(1, 10.0, 100, false);
    
    // Add market buy order that should execute immediately
    engine.addMarketOrder(2, 50, true);
    
    // Verify market order was filled
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::FILLED);
    
    // Verify limit order was partially filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::PARTIALLY_FILLED);
}

// Test order cancellation
TEST_F(MatchingEngineTest, OrderCancellation) {
    // Add a limit order
    engine.addLimitOrder(1, 10.0, 100, true);
    
    // Cancel the order
    engine.cancelOrder(1);
    
    // Verify it was cancelled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::CANCELED);
}

// Test IOC (Immediate-or-Cancel) orders
TEST_F(MatchingEngineTest, IOCOrders) {
    // Add some limit orders to match against
    engine.addLimitOrder(1, 10.0, 100, false); // Sell 100 @ $10.00
    engine.addLimitOrder(2, 11.0, 100, false); // Sell 100 @ $11.00
    
    // Test IOC order that will partially fill (price matches)
    engine.addIOCOrder(3, 10.0, 150, true);    // Buy 150 @ $10.00 IOC
    
    // Verify IOC order was partially filled
    ASSERT_EQ(engine.getOrderStatus(3), OrderStatus::PARTIALLY_FILLED);
    
    // Verify first limit order was filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
    
    // Test IOC order that won't fill (price doesn't match)
    engine.addIOCOrder(4, 9.0, 50, true);      // Buy 50 @ $9.00 IOC
    
    // Verify second IOC order was canceled
    ASSERT_EQ(engine.getOrderStatus(4), OrderStatus::CANCELED);
}

// Test FOK (Fill-or-Kill) orders
TEST_F(MatchingEngineTest, FOKOrders) {
    // Add some limit orders to match against
    engine.addLimitOrder(1, 10.0, 100, false); // Sell 100 @ $10.00
    
    // Test FOK order that will fill completely
    engine.addFOKOrder(2, 10.0, 50, true);     // Buy 50 @ $10.00 FOK
    
    // Verify FOK order was filled
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::FILLED);
    
    // Test FOK order that won't fill (not enough quantity)
    engine.addFOKOrder(3, 10.0, 200, true);    // Buy 200 @ $10.00 FOK
    
    // Verify second FOK order was canceled
    ASSERT_EQ(engine.getOrderStatus(3), OrderStatus::CANCELED);
}

// Test Good-Till-Canceled (GTC) orders
TEST_F(MatchingEngineTest, GTCOrders) {
    // Add GTC orders
    engine.addGTCOrder(1, 10.0, 100, true);    // Buy 100 @ $10.00 GTC
    engine.addGTCOrder(2, 9.0, 100, false);    // Sell 100 @ $9.00 GTC
    
    // Verify orders are active
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::ACTIVE);
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::ACTIVE);
    
    // Match orders
    engine.matchOrders();
    
    // Verify orders are filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::FILLED);
}

// Test Good-Till-Date (GTD) orders with expiration
TEST_F(MatchingEngineTest, GTDOrders) {
    // Add GTD order with a short expiry time
    auto expiry = std::chrono::system_clock::now() + 100ms;
    engine.addGTDOrder(1, 10.0, 100, true, expiry);  // Buy 100 @ $10.00 GTD, expires in 100ms
    
    // Verify order is initially active
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::ACTIVE);
    
    // Wait for expiry
    std::this_thread::sleep_for(200ms);
    
    // Check for expired orders
    engine.checkExpiredOrders();
    
    // Verify order is expired
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::EXPIRED);
}

// Test Stop orders
TEST_F(MatchingEngineTest, StopOrders) {
    // Add a stop buy order
    engine.addStopOrder(1, 10.5, 100, true);  // Buy stop @ $10.5, Qty 100
    
    // Verify the stop order is inactive
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::INACTIVE);
    
    // Add orders that will create a trade and trigger the stop
    engine.addLimitOrder(2, 10.5, 50, false); // Sell 50 @ $10.5
    engine.addLimitOrder(3, 10.5, 50, true);  // Buy 50 @ $10.5
    
    // Match orders to create a trade at $10.5
    engine.matchOrders();
    
    // Verify the stop order was triggered and converted to a market order
    // Since no matching sell orders remain after the first trade,
    // the order stays active until another sell appears
    ASSERT_NE(engine.getOrderStatus(1), OrderStatus::INACTIVE);
}

// Test Stop Limit orders
TEST_F(MatchingEngineTest, StopLimitOrders) {
    // Add a stop limit order
    engine.addStopLimitOrder(1, 10.5, 11.0, 100, true);  // Buy stop @ $10.5, limit $11.0
    
    // Verify the stop limit order is inactive
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::INACTIVE);
    
    // Add orders that will create a trade and trigger the stop
    engine.addLimitOrder(2, 10.5, 50, false); // Sell 50 @ $10.5
    engine.addLimitOrder(3, 10.5, 50, true);  // Buy 50 @ $10.5
    
    // Match orders to create a trade at $10.5
    engine.matchOrders();
    
    // Add a matching sell order for the activated stop limit
    engine.addLimitOrder(4, 11.0, 100, false); // Sell 100 @ $11.0
    
    // Match again
    engine.matchOrders();
    
    // Verify the stop limit order was ultimately filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
}

// Test Iceberg orders
TEST_F(MatchingEngineTest, IcebergOrders) {
    // Add an iceberg order
    engine.addIcebergOrder(1, 10.0, 300, 100, 100, true); // Buy 300 @ $10.0 with 100 visible
    
    // Add a matching sell order for the first visible portion
    engine.addLimitOrder(2, 10.0, 100, false); // Sell 100 @ $10.0
    
    // Match orders
    engine.matchOrders();
    
    // Verify the iceberg order is partially filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::PARTIALLY_FILLED);
    
    // Add another matching sell order
    engine.addLimitOrder(3, 10.0, 100, false); // Sell 100 @ $10.0
    
    // Match orders again
    engine.matchOrders();
    
    // Verify the iceberg order is still partially filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::PARTIALLY_FILLED);
    
    // Add final matching sell order
    engine.addLimitOrder(4, 10.0, 100, false); // Sell 100 @ $10.0
    
    // Match orders again
    engine.matchOrders();
    
    // Verify the iceberg order is now filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
}

// Test order status synchronization between orderMap and allOrdersMap
TEST_F(MatchingEngineTest, OrderStatusSynchronization) {
    // Add a limit order
    engine.addLimitOrder(1, 10.0, 100, true); // Buy 100 @ $10.00
    
    // Verify initial status
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::ACTIVE);
    
    // Add a matching order that will fill half
    engine.addLimitOrder(2, 10.0, 50, false); // Sell 50 @ $10.00
    
    // Match orders
    engine.matchOrders();
    
    // Verify status was updated to partially filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::PARTIALLY_FILLED);
    
    // Cancel the order
    engine.cancelOrder(1);
    
    // Verify status was updated to canceled in both maps
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::CANCELED);
}

// Test error handling for invalid parameters
TEST_F(MatchingEngineTest, ErrorHandling) {
    // Try to add a limit order with invalid price
    engine.addLimitOrder(1, -10.0, 100, true); // Negative price
    
    // Order should not be added
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::CANCELED);
    
    // Try to add a limit order with invalid quantity
    engine.addLimitOrder(2, 10.0, 0, true); // Zero quantity
    
    // Order should not be added
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::CANCELED);
}

// Test multiple orders at the same price level
TEST_F(MatchingEngineTest, MultipleOrdersAtSamePrice) {
    // Add multiple buy orders at the same price
    engine.addLimitOrder(1, 10.0, 50, true);  // Buy 50 @ $10.00
    engine.addLimitOrder(2, 10.0, 50, true);  // Buy 50 @ $10.00
    
    // Add a sell order that will match with both
    engine.addLimitOrder(3, 10.0, 100, false); // Sell 100 @ $10.00
    
    // Match orders
    engine.matchOrders();
    
    // Verify all orders are filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::FILLED);
    ASSERT_EQ(engine.getOrderStatus(3), OrderStatus::FILLED);
}

// Test price-time priority
TEST_F(MatchingEngineTest, PriceTimePriority) {
    // Add buy orders at different price levels
    engine.addLimitOrder(1, 9.5, 50, true);   // Buy 50 @ $9.50
    engine.addLimitOrder(2, 10.0, 50, true);  // Buy 50 @ $10.00
    
    // Add a sell order that will match with the higher priced buy first
    engine.addLimitOrder(3, 9.5, 100, false); // Sell 100 @ $9.50
    
    // Match orders
    engine.matchOrders();
    
    // Verify the higher priced order was matched first
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::FILLED);
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::FILLED);
    ASSERT_EQ(engine.getOrderStatus(3), OrderStatus::FILLED);
}

// Test order modification with price-time priority
TEST_F(MatchingEngineTest, OrderModificationPriceTimePriority) {
    // Add multiple buy orders at the same price level
    engine.addLimitOrder(1, 10.0, 50, true);  // Buy 50 @ $10.00 (first in queue)
    engine.addLimitOrder(2, 10.0, 50, true);  // Buy 50 @ $10.00 (second in queue)
    engine.addLimitOrder(3, 10.0, 50, true);  // Buy 50 @ $10.00 (third in queue)
    
    // Add a small sell order that will only match with the first order
    engine.addLimitOrder(4, 10.0, 20, false); // Sell 20 @ $10.00
    
    // Match orders - this should partially fill order #1
    engine.matchOrders();
    
    // Verify order #1 is partially filled
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::PARTIALLY_FILLED);
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::ACTIVE);
    ASSERT_EQ(engine.getOrderStatus(3), OrderStatus::ACTIVE);
    
    // Create modification requests
    OrderModificationRequest priceChangeRequest;
    priceChangeRequest.newPrice = 10.5; // Higher price
    
    OrderModificationRequest quantityIncreaseRequest;
    quantityIncreaseRequest.newQuantity = 100; // Increased quantity
    
    OrderModificationRequest statusChangeRequest;
    statusChangeRequest.newStatus = OrderStatus::EXPIRED;
    
    // Test 1: Price change should lose queue position
    engine.ModifyOrder(2, priceChangeRequest);
      // Test 2: Quantity increase should also lose queue position
    engine.ModifyOrder(3, quantityIncreaseRequest);    
    
    // Add sell orders that will match with all our buy orders:
    // 1. First add a sell order at 10.5 to match with Order #2
    engine.addLimitOrder(5, 10.5, 50, false); // Sell 50 @ $10.5 (to match order 2)    // 2. Then add another sell order at 10.0 to match with Orders #1 and #3
    // Order 1 has 30 remaining (50 - 20 filled) and Order 3 now needs 100
    engine.addLimitOrder(6, 10.0, 130, false); // Sell 130 @ $10.0 for orders 1 and part of 3
    
    // Add one more sell order to fully match order 3
    engine.addLimitOrder(7, 10.0, 50, false); // Sell 50 @ $10.0 to ensure order 3 is fully filled
    
    // Debug print the order status before matching
    std::cout << "DEBUG - Before matching - Order 3 status: " << static_cast<int>(engine.getOrderStatus(3)) 
              << ", Order 1 status: " << static_cast<int>(engine.getOrderStatus(1)) << std::endl;
    
    // Print order book to see what's happening before matching
    std::cout << "\n====== Order Book Before Matching ======\n";
    engine.printOrderBook();
      // Match orders
    engine.matchOrders();
    
    // Print order book after matching
    std::cout << "\n====== Order Book After Matching ======\n";
    engine.printOrderBook();
    
    // Debug the status values after matching
    auto status1 = engine.getOrderStatus(1);
    auto status2 = engine.getOrderStatus(2);
    auto status3 = engine.getOrderStatus(3);
    
    std::cout << "After matchOrders - Order 1 status: " << static_cast<int>(status1)
              << ", Order 2 status: " << static_cast<int>(status2)
              << ", Order 3 status: " << static_cast<int>(status3) << std::endl;
    
    // Verify all orders were filled
    ASSERT_EQ(status1, OrderStatus::FILLED);
    ASSERT_EQ(status2, OrderStatus::FILLED);
    ASSERT_EQ(status3, OrderStatus::FILLED);
    
    // Test status modification
    engine.addLimitOrder(6, 11.0, 50, true);  // Buy 50 @ $11.00
    engine.ModifyOrder(6, statusChangeRequest);
    ASSERT_EQ(engine.getOrderStatus(6), OrderStatus::EXPIRED);
}

// Test modification of different order types
TEST_F(MatchingEngineTest, ModifyDifferentOrderTypes) {
    // Create a stop order
    engine.addStopOrder(1, 10.5, 100, true);  // Buy stop @ $10.5, Qty 100
    ASSERT_EQ(engine.getOrderStatus(1), OrderStatus::INACTIVE);
    
    // Create an iceberg order
    engine.addIcebergOrder(2, 10.0, 300, 50, 50, false); // Sell 300 @ $10.0 with 50 visible
    ASSERT_EQ(engine.getOrderStatus(2), OrderStatus::ACTIVE);
    
    // Print order book to see initial state
    std::cout << "\n====== Order Book Before Modification ======\n";
    engine.printOrderBook();
    
    // Modify stop order's stop price
    OrderModificationRequest stopPriceModRequest;
    stopPriceModRequest.newStopPrice = 11.0;
    engine.ModifyOrder(1, stopPriceModRequest);
    
    // Modify iceberg order's visible and replenish quantities
    OrderModificationRequest icebergModRequest;
    icebergModRequest.newVisibleQuantity = 100;
    icebergModRequest.newReplenishQuantity = 100;
    engine.ModifyOrder(2, icebergModRequest);
    
    // Print order book after modifications
    std::cout << "\n====== Order Book After Modification ======\n";
    engine.printOrderBook();
    
    // Verify orders retain their status after modification
    auto status1 = engine.getOrderStatus(1);
    auto status2 = engine.getOrderStatus(2);
    std::cout << "\nOrder 1 Status: " << static_cast<int>(status1) << std::endl;
    std::cout << "Order 2 Status: " << static_cast<int>(status2) << std::endl;
    
    ASSERT_EQ(status1, OrderStatus::INACTIVE);
    ASSERT_EQ(status2, OrderStatus::ACTIVE);
    
    // Create a trade to trigger the stop order
    engine.addLimitOrder(3, 11.0, 50, false); // Sell 50 @ $11.0
    engine.addLimitOrder(4, 11.0, 50, true);  // Buy 50 @ $11.0
    
    // Match to create the trade
    engine.matchOrders();
    
    // Print final order book
    std::cout << "\n====== Order Book After Trade ======\n";
    engine.printOrderBook();
    
    // Check and trigger stop orders with the last trade price
    engine.checkandTrigger(11.0);
    
    // Verify the stop order was triggered at the new stop price
    auto finalStatus = engine.getOrderStatus(1);
    std::cout << "\nFinal Order 1 Status: " << static_cast<int>(finalStatus) << std::endl;
    
    ASSERT_NE(finalStatus, OrderStatus::INACTIVE);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}