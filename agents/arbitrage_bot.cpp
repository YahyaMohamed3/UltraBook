#include "arbitrage_bot.hpp"

namespace ultraBook {

void ArbitrageBot::start(double minSpread, int lot, int ms) {
    if (running_) return;
    running_ = true;
    th_ = std::thread(&ArbitrageBot::run, this, minSpread, lot, ms);
}

void ArbitrageBot::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void ArbitrageBot::run(double minSpread, int lot, int ms) {
    using namespace std::chrono;
    while (running_) {
        auto bestBid = engine_.getBuyBook().getBestOrder();
        auto bestAsk = engine_.getSellBook().getBestOrder();
        if (bestBid && bestAsk) {
            const double bid = bestBid->price.value();
            const double ask = bestAsk->price.value();
            if (ask - bid > minSpread) {
                // Grab the spread: cross the ask, then place a passive sell a bit above bid
                engine_.addMarketOrder(nextOrderId_++, lot, true);                 // take ask :contentReference[oaicite:8]{index=8}
                engine_.addLimitOrder(nextOrderId_++, bid + 0.05, lot, false);     // rest at bid+tick :contentReference[oaicite:9]{index=9}
            }
        }
        std::this_thread::sleep_for(milliseconds(ms));
    }
}

} // namespace ultraBook
