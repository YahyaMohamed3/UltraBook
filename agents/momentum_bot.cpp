#include "momentum_bot.hpp"

namespace ultraBook {

void MomentumBot::start(int lookback, int lot, int ms, double thresh) {
    if (running_) return;
    running_ = true;
    th_ = std::thread(&MomentumBot::run, this, lookback, lot, ms, thresh);
}

void MomentumBot::stop() {
    running_ = false;
    if (th_.joinable()) th_.join();
}

void MomentumBot::run(int lookback, int lot, int ms, double thresh) {
    using namespace std::chrono;
    std::vector<double> hist; hist.reserve(lookback + 2);

    while (running_) {
        auto bestBid = engine_.getBuyBook().getBestOrder();
        auto bestAsk = engine_.getSellBook().getBestOrder();
        if (bestBid && bestAsk) {
            const double mid = (bestBid->price.value() + bestAsk->price.value()) * 0.5; // :contentReference[oaicite:5]{index=5}
            hist.push_back(mid);
            if ((int)hist.size() > lookback) hist.erase(hist.begin());

            if ((int)hist.size() >= 5) {
                double recent = 0.5 * (hist[hist.size()-1] + hist[hist.size()-2]);
                double older  = 0.5 * (hist[0] + hist[1]);
                if (recent > older + thresh) {
                    engine_.addMarketOrder(nextOrderId_++, lot, true);   // buy momentum :contentReference[oaicite:6]{index=6}
                } else if (recent < older - thresh) {
                    engine_.addMarketOrder(nextOrderId_++, lot, false);  // sell momentum :contentReference[oaicite:7]{index=7}
                }
            }
        }
        std::this_thread::sleep_for(milliseconds(ms));
    }
}

} // namespace ultraBook
