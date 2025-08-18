#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <thread>
#include "../orderbook/orderbook.hpp"
#include "types.hpp"

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
    EXPECT_EQ(best->orderId, 2); // Best price for BUY is highest
}

TEST_F(OrderBookTest, GetBestOrder_SellSide) {
    sellBook.addOrder({3, 30.0,  50, false, OrderType::LIMIT});
    sellBook.addOrder({4, 15.0,  50, false, OrderType::LIMIT});
    auto best = sellBook.getBestOrder();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->orderId, 4); // Best price for SELL is lowest
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
    EXPECT_FALSE(buyBook.cancelOrder(9999)); // Should fail for non-existent order
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

// 3. ModifyOrder: price-time priority & repositioning
TEST_F(OrderBookTest, ModifyOrder_Reposition) {
    buyBook.addOrder({9, {10.0}, 5, true, OrderType::LIMIT});
    buyBook.addOrder({10, {20.0}, 5, true, OrderType::LIMIT});
    OrderModificationRequest req;
    req.newPrice = 30.0;
    req.newQuantity = 8;
    buyBook.modifyOrder(9, req);
    auto best = buyBook.getBestOrder();
    ASSERT_TRUE(best.has_value());
    EXPECT_EQ(best->orderId, 9);
    EXPECT_DOUBLE_EQ(best->price.value(), 30.0);
    EXPECT_EQ(best->quantity, 8);
}

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

// 5. Stop & StopLimit orders: handoff, conversion (no type/status change in book) , multiple stop orders trigerred
TEST_F(OrderBookTest, StopLimit_Conversion_HandOff) {
    // STOPLIMIT @ stop=25 → expect handoff to triggeredOrders, not type transition here
    Order sl{12, {75.0}, 2, true, OrderType::STOPLIMIT, std::nullopt, 25.0};
    buyBook.addStopOrder(sl);
    EXPECT_FALSE(buyBook.findOrder(12).has_value()); // Not in price book yet
    buyBook.checkAndTrigger(25.0); // Should move to triggered
    auto triggered = buyBook.getAndClearTriggeredOrders();
    ASSERT_EQ(triggered.size(), 1);
    EXPECT_EQ(triggered[0].orderId, 12);
    // Type/status transition is now the MatchingEngine's job!
}

TEST_F(OrderBookTest, ExpiredStopRemoval) {
    auto expiry = std::chrono::system_clock::now() + 10ms;
    Order s{13, std::nullopt, 1, false, OrderType::STOP, expiry, 50.0};
    buyBook.addStopOrder(s);
    std::this_thread::sleep_for(20ms);
    buyBook.removeExpiredStopOrders();
    auto all = buyBook.getAllOrders();
    EXPECT_TRUE(std::none_of(all.begin(), all.end(), [](auto &o){ return o.orderId == 13; }));
}

TEST_F(OrderBookTest, MultipleStopsTriggeredTogether) {
    // Both stop at 10
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

// 6. Iceberg replenishment (book should not manage status/fill, just update visible) & icberg partial replenishment
TEST_F(OrderBookTest, Iceberg_Replenish) {
    Order ib{14, {20.0}, 100, true, OrderType::ICE, std::nullopt, std::nullopt, 10, 10};
    buyBook.addOrder(ib);
    OrderModificationRequest r;
    r.newQuantity = 0; // Simulate visible depletion
    buyBook.modifyOrder(14, r);
    buyBook.replenishIcebergOrder(&buyBook.getAllOrders()[0]);
    auto ptr = buyBook.findOrder(14);
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ((*ptr)->visibleQuantity.value(), 10);
}


TEST_F(OrderBookTest, IcebergPartialReplenish) {
    Order ib{23, {30.0}, 3, true, OrderType::ICE, std::nullopt, std::nullopt, 2, 2};
    buyBook.addOrder(ib);
    OrderModificationRequest r;
    r.newQuantity = 1; // Only 1 left, less than replenishQty
    buyBook.modifyOrder(23, r);
    auto ptr = buyBook.findOrder(23);
    ASSERT_TRUE(ptr.has_value());
    buyBook.replenishIcebergOrder(*ptr); // Pass the real pointer
    ptr = buyBook.findOrder(23); // get again in case of iterator invalidation
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ((*ptr)->visibleQuantity.value(), 1); // Only 1 replenished, not 2
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

// 8. Simple concurrency sanity check
TEST_F(OrderBookTest, ConcurrentAdditionsAndCancellations) {
    constexpr int N = 1000;
    std::vector<std::thread> th;
    for(int t=0;t<4;t++){
        th.emplace_back([&](){
            for(int i=100;i<100+N;i++){
                buyBook.addOrder({i, double(i), 1, true, OrderType::LIMIT});
            }
        });
    }
    for(int t=0;t<4;t++){
        th.emplace_back([&](){
            for(int i=100;i<100+N;i++){
                buyBook.cancelOrder(i);
            }
        });
    }
    for(auto &t:th) t.join();
    auto all = buyBook.getAllOrders();
    for(auto &o: all){
        EXPECT_GE(o.orderId, 100);
        EXPECT_LT(o.orderId, 100+N);
    }
}

// 9. triggered stop-market hand-off
TEST_F(OrderBookTest, TriggeredStopMarketOrder_HandOff) {
    Order s{20, std::nullopt, 10, true, OrderType::STOP, std::nullopt, 55.0};
    buyBook.addStopOrder(s);
    buyBook.checkAndTrigger(56.0); // Should trigger
    EXPECT_FALSE(buyBook.findOrder(20).has_value());
    auto triggered = buyBook.getAndClearTriggeredOrders();
    ASSERT_EQ(triggered.size(), 1);
    EXPECT_EQ(triggered[0].orderId, 20);
    EXPECT_EQ(triggered[0].type, OrderType::STOP); // Book does not transition type!
    EXPECT_TRUE(buyBook.getAndClearTriggeredOrders().empty());
}

// 10. After clean book, adding orders should work
TEST_F(OrderBookTest, AddAfterClearWorks) {
    buyBook.addOrder({24, {1.0}, 1, true, OrderType::LIMIT});
    buyBook.clear();
    buyBook.addOrder({25, {2.0}, 1, true, OrderType::LIMIT});
    auto ptr = buyBook.findOrder(25);
    ASSERT_TRUE(ptr.has_value());
    EXPECT_EQ((*ptr)->orderId, 25);
}



int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
