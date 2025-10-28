#pragma once
#include <thread>
#include <atomic>
#include <random>
#include <chrono>
#include "engine/engine.hpp"
#include "data/feed.hpp"

namespace ultraBook {

class MarketReplay {
public:
    explicit MarketReplay(MatchingEngine& eng) : engine_(eng) {}
    ~MarketReplay() { stop(); }

    void setFeed(const IMarketDataFeed* feed) { feed_ = feed; }
    void start(double speed = 10.0);
    void stop();

private:
    void loop(double speed);
    void seedBook(double px, double vol);

    MatchingEngine& engine_;
    const IMarketDataFeed* feed_{nullptr};
    std::thread th_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextOrderId_{40000};
};

} // namespace ultraBook
