#include <gtest/gtest.h>
#include <thread>
#include <chrono>
#include "data/csv_feed.hpp"
#include "data/replay.hpp"
#include "engine/engine.hpp"

using namespace ultraBook;

TEST(Replay, SeedsDepthAndMatches) {
    MatchingEngine eng;

    // ensure some initial quotes so best-of-book exists even before replay
    eng.addLimitOrder(1, 100.00, 10, true);
    eng.addLimitOrder(2, 100.10, 10, false);

    CSVFeed feed;
    ASSERT_TRUE(feed.load("data/test_ticks.csv"));

    MarketReplay rp(eng);
    rp.setFeed(&feed);
    rp.start(1000.0); // super fast
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    rp.stop();

    auto bb = eng.getBuyBook().getBestOrder();
    auto ba = eng.getSellBook().getBestOrder();
    ASSERT_TRUE(bb.has_value());
    ASSERT_TRUE(ba.has_value());
    EXPECT_LT(bb->price.value(), ba->price.value());
}
