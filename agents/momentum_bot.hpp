#pragma once
#include <atomic>
#include <thread>
#include <vector>
#include <chrono>
#include "engine/engine.hpp"

namespace ultraBook {

class MomentumBot {
public:
    explicit MomentumBot(MatchingEngine& engine)
        : engine_(engine) {}

    ~MomentumBot() { stop(); }

    void start(int lookback = 10, int lot = 10, int ms = 3000, double thresh = 0.05);
    void stop();

private:
    void run(int lookback, int lot, int ms, double thresh);

    MatchingEngine& engine_;
    std::atomic<bool> running_{false};
    std::atomic<int> nextOrderId_{25000};
    std::thread th_;
};

} // namespace ultraBook
