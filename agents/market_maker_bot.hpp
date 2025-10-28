#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include <optional>
#include "engine/engine.hpp"

namespace ultraBook {

class MarketMakerBot {
public:
    explicit MarketMakerBot(MatchingEngine& engine)
        : engine_(engine) {}

    ~MarketMakerBot() { stop(); }

    void start(double spread = 0.10, int qty = 50, int ms = 2000);
    void stop();

private:
    void run(double spread, int qty, int ms);

    MatchingEngine& engine_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextOrderId_{20000};
    std::thread th_;
};

} // namespace ultraBook
