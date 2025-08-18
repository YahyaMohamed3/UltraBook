#ifndef ENGINE_HPP
#define ENGINE_HPP

#include "../orderbook/orderbook.hpp"
#include "types.hpp"
#include <unordered_map>
#include <vector>
#include <chrono>

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
    void addMarketOrder(int orderId, int quantity, bool isBuy);
    void addIOCOrder(int orderId, double price, int quantity, bool isBuy);
    void addFOKOrder(int orderId, double price, int quantity, bool isBuy);

    // process triggered stop/stop-limit orders

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


private:
    void refreshLastPriceFromBook();
    void processTriggeredOrders(); // <-- Declaration only

    double lastPrice{0.0};
    OrderBook buyBook{ OrderBook::Side::BUY };
    OrderBook sellBook{ OrderBook::Side::SELL };

    // keep a history of every order for status lookups & audit
    std::unordered_map<int, Order> allOrdersMap;
    std::vector<Trade> tradeLog;
    std::unordered_map<int, std::vector<Trade>> tradesByOrderId;
};

} // namespace ultraBook

#endif // ENGINE_HPP
