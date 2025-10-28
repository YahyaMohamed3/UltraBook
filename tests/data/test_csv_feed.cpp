#include <gtest/gtest.h>
#include "data/csv_feed.hpp"

using namespace ultraBook;

TEST(CSVFeed, Loads) {
    CSVFeed f;
    ASSERT_TRUE(f.load("data/test_ticks.csv"));
    const auto& ticks = f.ticks();
    ASSERT_GE(ticks.size(), 3u);
    EXPECT_GT(ticks[0].ts, 0);
    EXPECT_GT(ticks[0].px, 0.0);
}
