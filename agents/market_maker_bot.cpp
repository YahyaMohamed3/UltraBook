#include "market_maker_bot.hpp"

namespace ultraBook {

void MarketMakerBot::start(double spread, int qty, int ms) {
    if (running_) return;
    running_ = true;
    th_ = std::thread(&MarketMakerBot::run, this, spread, qty, ms);
}

void MarketMakerBot::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void MarketMakerBot::run(double spread, int qty, int ms) {
    using namespace std::chrono;
    while (running_) {
        // Use top-of-book snapshots to approximate mid; your OrderBook provides getBestOrder()
        auto bestBid = engine_.getBuyBook().getBestOrder();   // snapshot copy
        auto bestAsk = engine_.getSellBook().getBestOrder();  // snapshot copy

        if (bestBid && bestAsk) { // price-time priority handled by engine add* calls
            const double mid = (bestBid->price.value() + bestAsk->price.value()) * 0.5; // :contentReference[oaicite:2]{index=2}
            engine_.addLimitOrder(nextOrderId_++, mid - spread * 0.5, qty, true);  // buy  :contentReference[oaicite:3]{index=3}
            engine_.addLimitOrder(nextOrderId_++, mid + spread * 0.5, qty, false); // sell :contentReference[oaicite:4]{index=4}
        }

        std::this_thread::sleep_for(milliseconds(ms));
    }
}

} // namespace ultraBook
