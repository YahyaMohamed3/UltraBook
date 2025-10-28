#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "agents/market_maker_bot.hpp"
#include "engine/engine.hpp"

using namespace ultraBook;

static void prime_mid(MatchingEngine& e, double bid, double ask) {
    static int id = 1000;
    e.addLimitOrder(id++, bid, 10, true);
    e.addLimitOrder(id++, ask, 10, false);
}

TEST(MarketMaker, QuotesAroundMid) {
    MatchingEngine eng;
    prime_mid(eng, 100.00, 100.10);

    MarketMakerBot mm(eng);
    mm.start(0.10, 50, 20); // spread, qty, ms
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mm.stop();

    auto bb = eng.getBuyBook().getBestOrder();
    auto ba = eng.getSellBook().getBestOrder();
    ASSERT_TRUE(bb.has_value());
    ASSERT_TRUE(ba.has_value());
    double mid = (100.00 + 100.10) * 0.5;
    EXPECT_LE(bb->price.value(), mid);
    EXPECT_GE(ba->price.value(), mid);
}
