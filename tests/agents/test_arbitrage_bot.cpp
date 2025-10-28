#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "agents/arbitrage_bot.hpp"
#include "engine/engine.hpp"

using namespace ultraBook;

TEST(Arbitrage, FiresOnWideSpread) {
    MatchingEngine eng;
    eng.addLimitOrder(1, 100.00, 10, true);
    eng.addLimitOrder(2, 100.50, 10, false);

    ArbitrageBot ab(eng);
    ab.start(0.20, 5, 20); // minSpread, lot, ms
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    ab.stop();

    auto bb = eng.getBuyBook().getBestOrder();
    auto ba = eng.getSellBook().getBestOrder();
    ASSERT_TRUE(bb.has_value());
    ASSERT_TRUE(ba.has_value());
    EXPECT_LT(bb->price.value(), ba->price.value());
}
