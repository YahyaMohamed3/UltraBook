#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "agents/momentum_bot.hpp"
#include "engine/engine.hpp"

using namespace ultraBook;

static void feed_mid(MatchingEngine& e, double bid, double ask) {
    static int id = 2000;
    e.addLimitOrder(id++, bid, 10, true);
    e.addLimitOrder(id++, ask, 10, false);
}

TEST(Momentum, ReactsToTrend) {
    MatchingEngine eng;
    feed_mid(eng, 100.00, 100.10);
    feed_mid(eng, 100.02, 100.12);
    feed_mid(eng, 100.05, 100.15);

    MomentumBot mo(eng);
    mo.start(10, 5, 20, 0.01); // lookback, lot, ms, thresh
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    mo.stop();

    auto bb = eng.getBuyBook().getBestOrder();
    auto ba = eng.getSellBook().getBestOrder();
    ASSERT_TRUE(bb.has_value());
    ASSERT_TRUE(ba.has_value());
}
