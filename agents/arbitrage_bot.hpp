#pragma once
#include <atomic>
#include <thread>
#include <chrono>
#include "engine/engine.hpp"

namespace ultraBook {

class ArbitrageBot {
public:
    explicit ArbitrageBot(MatchingEngine& engine)
        : engine_(engine) {}

    ~ArbitrageBot() { stop(); }

    void start(double minSpread = 0.20, int lot = 5, int ms = 1500);
    void stop();

private:
    void run(double minSpread, int lot, int ms);

    MatchingEngine& engine_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextOrderId_{27000};
    std::thread th_;
};

} // namespace ultraBook
