#ifndef ENGINE_HPP
#define ENGINE_HPP

#include "../orderbook/orderbook.hpp"
#include "types.hpp"
#include <unordered_map>
#include <vector>
#include <chrono>
#include <optional>

namespace ultraBook {

class MatchingEngine {
public:
    
    MatchingEngine();

    // 1) Order submission methods
    void addLimitOrder(int orderId, double price, int quantity, bool isBuy);
    void addGTCOrder(int orderId, double price, int quantity, bool isBuy);
    void addGTDOrder(int orderId, double price, int quantity, bool isBuy,
                     std::chrono::system_clock::time_point expiry);
    void addIcebergOrder(int orderId, double price, int totalQty, int visibleQty,
                         int replenishQty, bool isBuy);
    void addStopOrder(int orderId, double stopPrice, int quantity, bool isBuy);
    void addStopLimitOrder(int orderId, double stopPrice, double limitPrice,
                           int quantity, bool isBuy);

    // Fast paths (do not rest the taker)
    void addMarketOrder(int orderId, int quantity, bool isBuy);
    void addIOCOrder(int orderId, double price, int quantity, bool isBuy);
    void addFOKOrder(int orderId, double price, int quantity, bool isBuy);

    // 2) Core matching and maintenance
    void matchOrders();
    void checkExpiredOrders();

    // 3) Utility
    void cancelOrder(int orderId);
    void modifyOrder(int orderId, const OrderModificationRequest& req);

    // 4) Introspection
    OrderStatus getOrderStatus(int orderId) const;
    void printOrderBook() const;
    void printTradelog() const;
    const OrderBook& getBuyBook() const { return buyBook; }
    const OrderBook& getSellBook() const { return sellBook; }
    const std::vector<Trade>& getTradeLog() const { return tradeLog; }

private:
    void refreshLastPriceFromBook();
    void processTriggeredOrders();

    // Internal: sweep opposite book up to 'remaining'; optional limit price.
    // Returns filled quantity.
    int sweepOpposite(int takerId, int remaining, bool isBuy,
                      std::optional<double> limitPrice);

    double lastPrice{0.0};
    OrderBook buyBook{ OrderBook::Side::BUY };
    OrderBook sellBook{ OrderBook::Side::SELL };

    // keep a history of every order for status & audit
    std::unordered_map<int, Order> allOrdersMap;
    std::vector<Trade> tradeLog;
    std::unordered_map<int, std::vector<Trade>> tradesByOrderId;
};

} // namespace ultraBook

#endif // ENGINE_HPP
