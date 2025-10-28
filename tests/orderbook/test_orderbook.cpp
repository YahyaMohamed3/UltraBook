#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include "../orderbook/orderbook.hpp"
#include "types.hpp"
#include <unordered_set>

using namespace ultraBook;
using namespace std::chrono_literals;

class OrderBookTest : public ::testing::Test {
protected:
    OrderBook buyBook{OrderBook::Side::BUY};
    OrderBook sellBook{OrderBook::Side::SELL};
};

// 1. Empty & best-order
TEST_F(OrderBookTest, InitiallyEmpty) {
    EXPECT_TRUE(buyBook.isEmpty());
    EXPECT_TRUE(sellBook.isEmpty());
    EXPECT_FALSE(buyBook.getBestOrder().has_value());
    EXPECT_FALSE(sellBook.getBestOrder().has_value());
}

TEST_F(OrderBookTest, GetBestOrder_BuySide) {
    buyBook.addOrder({1, 10.0, 100, true, OrderType::LIMIT});
    buyBook.addOrder({2, 20.0, 100, true, OrderType::LIMIT});
    auto best = buyBook.getBestOrder();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->orderId, 2); // BUY: highest price is best
}

TEST_F(OrderBookTest, GetBestOrder_SellSide) {
    sellBook.addOrder({3, 30.0,  50, false, OrderType::LIMIT});
    sellBook.addOrder({4, 15.0,  50, false, OrderType::LIMIT});
    auto best = sellBook.getBestOrder();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->orderId, 4); // SELL: lowest price is best
}

// 2. Add / Find / Cancel / Clear
TEST_F(OrderBookTest, AddAndFindOrder) {
    Order o{5, {42.0},  7, true, OrderType::LIMIT};
    buyBook.addOrder(o);
    ASSERT_FALSE(buyBook.isEmpty());
    auto opt = buyBook.findOrder(5);
    ASSERT_TRUE(opt.has_value());
    EXPECT_EQ((*opt)->orderId, 5);
    EXPECT_DOUBLE_EQ((*opt)->price.value(), 42.0);
}

TEST_F(OrderBookTest, CancelOrder) {
    buyBook.addOrder({6, {50.0}, 10, true, OrderType::LIMIT});
    EXPECT_TRUE(buyBook.findOrder(6).has_value());
    EXPECT_TRUE(buyBook.cancelOrder(6));
    EXPECT_FALSE(buyBook.findOrder(6).has_value());
    EXPECT_FALSE(buyBook.cancelOrder(9999)); // non-existent
}

TEST_F(OrderBookTest, ClearBook) {
    buyBook.addOrder({7, {5.0},  1, true, OrderType::LIMIT});
    buyBook.addOrder({8, {15.0}, 1, true, OrderType::LIMIT});
    EXPECT_FALSE(buyBook.isEmpty());
    buyBook.clear();
    EXPECT_TRUE(buyBook.isEmpty());
    EXPECT_FALSE(buyBook.findOrder(7).has_value());
    EXPECT_FALSE(buyBook.findOrder(8).has_value());
}

// 3. ModifyOrder: NOW ONLY TESTS IN-PLACE UPDATES (e.g., quantity)
//    (Price change/reposition logic moved to engine tests)
TEST_F(OrderBookTest, SameLevel_FIFO_NoRequeueOnDecrease) {
    // BUY side, single level @100: id 1001 then 1002
    buyBook.addOrder({1001, 100.0, 5, true, OrderType::LIMIT});
    buyBook.addOrder({1002, 100.0, 5, true, OrderType::LIMIT});

    auto best = buyBook.getBestOrder();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->orderId, 1001); // first-in at best level

    // Decrease qty of head; should NOT requeue
    OrderModificationRequest m; m.newQuantity = 4;
    buyBook.modifyOrder(1001, m);

    best = buyBook.getBestOrder();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->orderId, 1001); // still head of the FIFO

    // Check quantity updated
    auto ptr = buyBook.findOrder(1001);
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ((*ptr)->quantity, 4);
}

// --- Test ModifyOrder_Reposition REMOVED ---
// --- Test RequeueOnPriceChange_GoesToBackAtNewLevel REMOVED ---
// Reason: This logic is now correctly handled by the MatchingEngine's
// cancel+resubmit pattern, not by OrderBook::modifyOrder.

// 4. GTD expiration
TEST_F(OrderBookTest, GTDExpiration_Removes) {
    auto expiry = std::chrono::system_clock::now() + 50ms;
    Order o{11, {100.0}, 1, true, OrderType::GTD, expiry};
    buyBook.addOrder(o);
    EXPECT_TRUE(buyBook.findOrder(11).has_value());
    std::this_thread::sleep_for(100ms);
    buyBook.checkExpiredOrders();
    EXPECT_FALSE(buyBook.findOrder(11).has_value());
}

// 5. Stop & StopLimit orders: handoff, conversion (engine does type/status change), multi-trigger
TEST_F(OrderBookTest, StopLimit_Conversion_HandOff) {
    Order sl{12, {75.0}, 2, true, OrderType::STOPLIMIT, std::nullopt, 25.0};
    buyBook.addStopOrder(sl);
    EXPECT_FALSE(buyBook.findOrder(12).has_value()); // not in price book yet
    buyBook.checkAndTrigger(25.0); // should move to triggered buffer
    auto triggered = buyBook.getAndClearTriggeredOrders();
    ASSERT_EQ(triggered.size(), 1);
    EXPECT_EQ(triggered[0].orderId, 12);
    // Type/status transition handled by engine
}

TEST_F(OrderBookTest, ExpiredStopRemoval) {
    auto expiry = std::chrono::system_clock::now() + 10ms;
    Order s{13, std::nullopt, 1, false, OrderType::STOP, expiry, 50.0};
    // Use sellBook for this test to ensure side doesn't matter for stop expiry
    sellBook.addStopOrder(s);
    std::this_thread::sleep_for(20ms);
    sellBook.removeExpiredStopOrders();
    auto all = sellBook.getAllOrders();
    EXPECT_TRUE(std::none_of(all.begin(), all.end(), [](auto &o){ return o.orderId == 13; }));
}


TEST_F(OrderBookTest, MultipleStopsTriggeredTogether) {
    Order s1{21, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 10.0};
    Order s2{22, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 10.0};
    buyBook.addStopOrder(s1);
    buyBook.addStopOrder(s2);
    buyBook.checkAndTrigger(10.0);
    auto triggered = buyBook.getAndClearTriggeredOrders();
    EXPECT_EQ(triggered.size(), 2);
    EXPECT_TRUE(std::any_of(triggered.begin(), triggered.end(), [](auto &o){ return o.orderId==21; }));
    EXPECT_TRUE(std::any_of(triggered.begin(), triggered.end(), [](auto &o){ return o.orderId==22; }));
}

// 6. Iceberg replenishment (test uses stable pointer, not temp vector element)
TEST_F(OrderBookTest, Iceberg_Replenish) {
    Order ib{14, {20.0}, 100, true, OrderType::ICE, std::nullopt, std::nullopt, 10, 10};
    buyBook.addOrder(ib);
    
    // Simulate visible depletion by directly modifying the order via pointer
    auto ptr = buyBook.findOrder(14);
    ASSERT_TRUE(ptr.has_value());
    (*ptr)->visibleQuantity = 0; // Simulate depletion

    buyBook.replenishIcebergOrder(*ptr); // Replenish

    // Re-fetch to check
    ptr = buyBook.findOrder(14);
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ((*ptr)->visibleQuantity.value(), 10);
}


TEST_F(OrderBookTest, IcebergPartialReplenish) {
    Order ib{23, {30.0}, 3, true, OrderType::ICE, std::nullopt, std::nullopt, 2, 2};
    buyBook.addOrder(ib);

    // Simulate partial fill leaving only 1 total quantity remaining
    auto ptr = buyBook.findOrder(23);
    ASSERT_TRUE(ptr.has_value());
    (*ptr)->filledQuantity = 2; // 2 filled
    (*ptr)->visibleQuantity = 0; // Visible depleted

    buyBook.replenishIcebergOrder(*ptr); // Try to replenish

    // Re-fetch to check
    ptr = buyBook.findOrder(23);
    ASSERT_TRUE(ptr.has_value());
    // Should replenish only the remaining amount (1), not the full replenishQty (2)
    EXPECT_EQ((*ptr)->visibleQuantity.value(), 1);
}


// 7. getAllOrders consistency (both price and stops)
TEST_F(OrderBookTest, GetAllOrders_ContainsBothPriceAndStops) {
    Order a{15, {5.0}, 1, true, OrderType::LIMIT};
    Order b{16, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 7.0};
    buyBook.addOrder(a);
    buyBook.addStopOrder(b);
    auto all = buyBook.getAllOrders();
    EXPECT_EQ(all.size(), 2);
    EXPECT_TRUE(std::any_of(all.begin(), all.end(), [](auto &o){ return o.orderId == 15; }));
    EXPECT_TRUE(std::any_of(all.begin(), all.end(), [](auto &o){ return o.orderId == 16; }));
}

// 8. Triggered stop-market hand-off
TEST_F(OrderBookTest, TriggeredStopMarketOrder_HandOff) {
    Order s{20, std::nullopt, 10, true, OrderType::STOP, std::nullopt, 55.0};
    buyBook.addStopOrder(s);
    buyBook.checkAndTrigger(56.0); // should trigger
    // Stop order should be gone from the stop book
    auto all_stops = buyBook.getAllOrders(); // Check via getAllOrders as findOrder only checks price book
    EXPECT_TRUE(std::none_of(all_stops.begin(), all_stops.end(), [](auto& o){ return o.orderId == 20 && o.stopPrice.has_value(); }));

    auto triggered = buyBook.getAndClearTriggeredOrders();
    ASSERT_EQ(triggered.size(), 1);
    EXPECT_EQ(triggered[0].orderId, 20);
    EXPECT_EQ(triggered[0].type, OrderType::STOP); // book does not transition type
    EXPECT_TRUE(buyBook.getAndClearTriggeredOrders().empty());
}


// 9. After clear, adding works
TEST_F(OrderBookTest, AddAfterClearWorks) {
    buyBook.addOrder({24, {1.0}, 1, true, OrderType::LIMIT});
    buyBook.clear();
    buyBook.addOrder({25, {2.0}, 1, true, OrderType::LIMIT});
    auto ptr = buyBook.findOrder(25);
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ((*ptr)->orderId, 25);
}

/* NEW: Best-of-book cache behavior */

// 10. Best cache updates when adding a superior price
TEST_F(OrderBookTest, BestCache_UpdatesOnSuperiorInsert) {
    buyBook.addOrder({31, 100.0, 1, true, OrderType::LIMIT});
    auto b1 = buyBook.getBestOrder();
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(b1->orderId, 31);

    buyBook.addOrder({32, 101.0, 1, true, OrderType::LIMIT}); // superior price
    auto b2 = buyBook.getBestOrder();
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(b2->orderId, 32);
}

// 11. Best cache re-computes when head level empties
TEST_F(OrderBookTest, BestCache_RecomputesOnHeadErase) {
    sellBook.addOrder({41, 10.0, 1, false, OrderType::LIMIT}); // best (lowest)
    sellBook.addOrder({42, 11.0, 1, false, OrderType::LIMIT});
    auto b1 = sellBook.getBestOrder();
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(b1->orderId, 41);

    ASSERT_TRUE(sellBook.cancelOrder(41)); // erase head level
    auto b2 = sellBook.getBestOrder();
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(b2->orderId, 42);
}


// 12. Inferior insert must NOT change the best-of-book
TEST_F(OrderBookTest, BestCache_NotChangedOnInferiorInsert) {
    buyBook.addOrder({4001, 105.0, 1, true, OrderType::LIMIT}); // best
    auto b1 = buyBook.getBestOrder();
    ASSERT_TRUE(b1.has_value());
    EXPECT_EQ(b1->orderId, 4001);

    buyBook.addOrder({4002, 100.0, 1, true, OrderType::LIMIT}); // inferior
    auto b2 = buyBook.getBestOrder();
    ASSERT_TRUE(b2.has_value());
    EXPECT_EQ(b2->orderId, 4001); // unchanged
}

// 13. SELL-side stops: trigger when lastPrice <= stop (inclusive)
TEST_F(OrderBookTest, SellSideStops_TriggerAtOrBelow) {
    OrderBook sellOnly{OrderBook::Side::SELL};
    Order s1{5001, std::nullopt, 1, false, OrderType::STOP, std::nullopt, 100.0};
    Order s2{5002, std::nullopt, 1, false, OrderType::STOP, std::nullopt, 100.0};
    sellOnly.addStopOrder(s1);
    sellOnly.addStopOrder(s2);

    // lastPrice exactly at 100 should trigger both
    sellOnly.checkAndTrigger(100.0);
    auto trig = sellOnly.getAndClearTriggeredOrders();
    ASSERT_EQ(trig.size(), 2u);
    std::unordered_set<int> ids; for (auto& o : trig) ids.insert(o.orderId);
    EXPECT_TRUE(ids.count(5001) && ids.count(5002));
}

// 14. BUY-side stops: trigger when lastPrice >= stop (inclusive)
// Added for completeness
TEST_F(OrderBookTest, BuySideStops_TriggerAtOrAbove) {
    OrderBook buyOnly{OrderBook::Side::BUY};
    Order s1{6001, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 200.0};
    Order s2{6002, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 200.0};
    Order s3{6003, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 201.0}; // Higher stop
    buyOnly.addStopOrder(s1);
    buyOnly.addStopOrder(s2);
    buyOnly.addStopOrder(s3);

    // lastPrice exactly at 200 should trigger s1 and s2
    buyOnly.checkAndTrigger(200.0);
    auto trig1 = buyOnly.getAndClearTriggeredOrders();
    ASSERT_EQ(trig1.size(), 2u);
    std::unordered_set<int> ids1; for (auto& o : trig1) ids1.insert(o.orderId);
    EXPECT_TRUE(ids1.count(6001) && ids1.count(6002));
    EXPECT_FALSE(ids1.count(6003));

    // lastPrice rising to 201 should trigger s3
    buyOnly.checkAndTrigger(201.0);
    auto trig2 = buyOnly.getAndClearTriggeredOrders();
     ASSERT_EQ(trig2.size(), 1u);
     EXPECT_EQ(trig2[0].orderId, 6003);
}

// 15. Cancel Stop Order test
TEST_F(OrderBookTest, CancelStopOrder) {
    Order s1{7001, std::nullopt, 1, true, OrderType::STOP, std::nullopt, 10.0};
    buyBook.addStopOrder(s1);
    
    // Check it's in the stop book via getAllOrders
    auto all1 = buyBook.getAllOrders();
    EXPECT_TRUE(std::any_of(all1.begin(), all1.end(), [](auto&o){ return o.orderId == 7001; }));

    EXPECT_TRUE(buyBook.cancelStopOrder(7001));

    // Check it's gone
    auto all2 = buyBook.getAllOrders();
    EXPECT_TRUE(std::none_of(all2.begin(), all2.end(), [](auto&o){ return o.orderId == 7001; }));

    // Cancel non-existent should fail
    EXPECT_FALSE(buyBook.cancelStopOrder(9999));
}
