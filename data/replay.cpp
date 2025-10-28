#include "replay.hpp"

namespace ultraBook {

void MarketReplay::start(double speed) {
    if (running_ || !feed_ || feed_->ticks().empty()) return;
    running_ = true;
    th_ = std::thread(&MarketReplay::loop, this, speed);
}

void MarketReplay::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void MarketReplay::loop(double speed) {
    using namespace std::chrono;
    const auto& T = feed_->ticks();
    for (size_t i = 0; i < T.size() && running_; ++i) {
        const auto& tick = T[i];
        seedBook(tick.px, tick.vol);               // generate depth + some marketable flow
        engine_.matchOrders();                     // run the cross using your engine core
        if (i > 0) {
            auto dt  = T[i].ts - T[i-1].ts;        // assume microseconds
            int ms   = (int)((dt / 1000.0) / speed);
            if (ms <= 0 || ms > 5000) ms = 100;
            std::this_thread::sleep_for(milliseconds(ms));
        }
    }
}

void MarketReplay::seedBook(double px, double vol) {
    std::random_device rd; std::mt19937 g(rd());
    std::uniform_int_distribution<> qty(5, 30);

    // 10 levels each side around px
    for (int i = 1; i <= 10; ++i) {
        double bid = px - i * 0.01;
        double ask = px + i * 0.01;
        if (bid > 0) engine_.addLimitOrder(nextOrderId_++, bid, qty(g), true);   // buy  :contentReference[oaicite:10]{index=10}
        engine_.addLimitOrder(nextOrderId_++, ask, qty(g), false);                // sell :contentReference[oaicite:11]{index=11}
    }
    // activity proportional to volume
    if (vol > 1.0) engine_.addMarketOrder(nextOrderId_++, (int)(vol * 2), g()%2==0); // :contentReference[oaicite:12]{index=12}
}

} // namespace ultraBook
