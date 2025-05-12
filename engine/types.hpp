#ifndef TYPES_HPP
#define TYPES_HPP

#include <chrono>
#include <optional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>

namespace ultraBook {

class OrderException : public std::runtime_error {
public:
    explicit OrderException(const std::string& message) 
        : std::runtime_error(message) {}
};

enum class OrderType {
    LIMIT,
    MARKET,
    STOP,
    STOPLIMIT,
    FOK,
    IOC,
    GTC,
    GTD,
    ICE
};

enum class OrderStatus {
    ACTIVE,
    INACTIVE,
    TRIGGERED,
    PARTIALLY_FILLED,
    FILLED,
    CANCELED,
    EXPIRED
};

inline std::ostream& operator<<(std::ostream& os, const OrderStatus& status) {
    switch(status) {
        case OrderStatus::ACTIVE: return os << "ACTIVE";
        case OrderStatus::INACTIVE: return os << "INACTIVE";
        case OrderStatus::TRIGGERED: return os << "TRIGGERED";
        case OrderStatus::PARTIALLY_FILLED: return os << "PARTIALLY_FILLED";
        case OrderStatus::FILLED: return os << "FILLED";
        case OrderStatus::CANCELED: return os << "CANCELED";
        default: return os << "UNKNOWN";
    }
}

struct Order {
    int orderId;
    std::optional<double> price;
    int quantity;
    int filledQuantity;
    bool isBuy;
    OrderType type;
    OrderStatus status{OrderStatus::ACTIVE};
    std::chrono::high_resolution_clock::time_point timestamp;
    std::optional<double> stopPrice;             
    std::optional<std::chrono::system_clock::time_point> expiry = std::nullopt ;
    std::optional<int> visibleQuantity;          
    std::optional<int> hiddenQuantity;              

    Order(int id, std::optional<double> p, int q, bool side, OrderType orderType,std::optional<std::chrono::system_clock::time_point> exp = std::nullopt, std::optional<double> stop = std::nullopt, std::optional<int> vis = std::nullopt, std::optional<int> hid = std::nullopt)
        : orderId(id), price(p), quantity(q), isBuy(side), type(orderType),
          timestamp(std::chrono::high_resolution_clock::now()) , expiry(exp), stopPrice(stop), visibleQuantity(vis), hiddenQuantity(hid){
        if (quantity <= 0) {
            throw OrderException("Invalid quantity: must be positive");
        }
        if (type == OrderType::LIMIT && (!price || price.value() <= 0)) {
            throw OrderException("Limit orders must have valid positive price");
        }
        if( type == OrderType::STOP){
            status = OrderStatus::INACTIVE;
        }
    }

    bool isComplete() const {
        return status == OrderStatus::FILLED || status == OrderStatus::CANCELED;
    }

    int getRemainingQuantity() const {
        return quantity - filledQuantity;
    }

    friend std::ostream& operator<<(std::ostream& os, const Order& order) {
        os << "Order{ID:" << order.orderId 
           << ", Type:" << (order.type == OrderType::LIMIT ? "LIMIT" : "MARKET")
           << ", Side:" << (order.isBuy ? "Buy" : "Sell")
           << ", Price:" << (order.price ? std::to_string(*order.price) : "MARKET")
           << ", Qty:" << order.quantity
           << ", Filled:" << order.filledQuantity
           << ", Status:" << order.status 
           << "}";
        return os;
    }
};

struct Trade {
    int buyOrderId;
    int sellOrderId;
    double price;
    int quantity;
    std::chrono::high_resolution_clock::time_point timestamp;

    Trade(int buyId, int sellId, double p, int q)
        : buyOrderId(buyId), sellOrderId(sellId), price(p), quantity(q),
          timestamp(std::chrono::high_resolution_clock::now())
    {
        if (price <= 0 || quantity <= 0) {
            throw OrderException("Invalid trade parameters");
        }
    }
};

inline std::ostream& operator<<(std::ostream& os, const Trade& t) {
    auto ms = std::chrono::duration_cast<std::chrono::microseconds>(t.timestamp.time_since_epoch()).count();
    os << "Trade | BuyID: " << t.buyOrderId
       << ", SellID: " << t.sellOrderId
       << ", Price: " << std::fixed << std::setprecision(2) << t.price
       << ", Qty: " << t.quantity
       << ", Timestamp: " << ms << "µs";
    return os;
}

} // namespace ultraBook

#endif