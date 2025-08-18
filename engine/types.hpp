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
        case OrderStatus::EXPIRED: return os << "EXPIRED";
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
    std::optional<std::chrono::system_clock::time_point> expiry = std::nullopt;
    std::optional<double> stopPrice;
    std::optional<int> visibleQuantity;          
    std::optional<int> replenishQuantity;              

    // Default constructor (needed for std::unordered_map)
    Order() : orderId(0), quantity(0), filledQuantity(0), isBuy(false), 
              type(OrderType::LIMIT), timestamp(std::chrono::high_resolution_clock::now()) {}

    Order(int id, std::optional<double> p, int q, bool side, OrderType orderType,
          std::optional<std::chrono::system_clock::time_point> exp = std::nullopt, 
          std::optional<double> stop = std::nullopt, 
          std::optional<int> vis = std::nullopt, 
          std::optional<int> hid = std::nullopt)
        // Initialize in the same order as declared in the struct
        : orderId(id), 
          price(p), 
          quantity(q), 
          filledQuantity(0), 
          isBuy(side), 
          type(orderType),
          status(orderType == OrderType::STOP || orderType == OrderType::STOPLIMIT ? OrderStatus::INACTIVE : OrderStatus::ACTIVE),
          timestamp(std::chrono::high_resolution_clock::now()),
          expiry(exp), 
          stopPrice(stop), 
          visibleQuantity(vis), 
          replenishQuantity(hid)
    {
        if (quantity <= 0) {
            throw OrderException("Invalid quantity: must be positive");
        }
        if ((type == OrderType::LIMIT || type == OrderType::GTC || type == OrderType::GTD || 
             type == OrderType::IOC || type == OrderType::FOK || type == OrderType::ICE) && 
            (!price || price.value() <= 0)) {
            throw OrderException("Limit orders must have valid positive price");
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



struct OrderModificationRequest{
    std::optional<double> newPrice;          // For LIMIT, STOP, STOP-LIMIT
    std::optional<int> newQuantity;          // All orders
    std::optional<OrderType> newTimeInForce; // Convert between GTC/GTD/IOC
    std::optional<std::chrono::system_clock::time_point> newExpiryTime; // For GTD orders
    std::optional<double> newStopPrice;      // For STOP, STOP-LIMIT
    std::optional<int> newDisplaySize;       // For Iceberg orders
    std::optional<int> newVisibleQuantity;   // For Iceberg orders (visible portion)
    std::optional<int> newReplenishQuantity; // For Iceberg orders (replenish amount)
    std::optional<std::chrono::system_clock::time_point> newExpiry; // Alternative name for expiry time
    std::optional<OrderStatus> newStatus;    // Status updates

    bool hasModifications() const{
        return newPrice.has_value() || 
           newQuantity.has_value() || 
           newTimeInForce.has_value() ||
           newExpiryTime.has_value() ||
           newStopPrice.has_value() ||
           newDisplaySize.has_value() ||
           newVisibleQuantity.has_value() ||
           newReplenishQuantity.has_value() ||
           newExpiry.has_value() ||
           newStatus.has_value();
    };
    bool validateForOrderType(OrderType type) const{
        switch(type){
            case OrderType::MARKET:
                // Market orders cannot have price modifications
                if (newPrice.has_value() || newStopPrice.has_value() || newTimeInForce.has_value() || 
                    newExpiryTime.has_value() || newDisplaySize.has_value()) {
                    return false;
                }
                break;

            case OrderType::LIMIT:
                // Limit orders can have price and quantity modifications
                // but cannot have stop price and display size
                if (newStopPrice.has_value() || newDisplaySize.has_value() ||
                    newVisibleQuantity.has_value() || newReplenishQuantity.has_value()) {
                    return false;
                }
                break;

            case OrderType::STOP:
                // Stop orders must have stop price, not regular price
                if (newPrice.has_value() || newDisplaySize.has_value() ||
                    newVisibleQuantity.has_value() || newReplenishQuantity.has_value()) {
                    return false;
                }
                break;

            case OrderType::STOPLIMIT:
                // Stop-limit orders can have both stop price and limit price
                if (newDisplaySize.has_value() || newVisibleQuantity.has_value() || 
                    newReplenishQuantity.has_value()) {
                    return false;
                }
                break;

            case OrderType::FOK:
                // Fill-or-Kill orders can have price and quantity
                // but not time-related parameters
                if (newStopPrice.has_value() || newTimeInForce.has_value() || 
                    newExpiryTime.has_value() || newDisplaySize.has_value() ||
                    newVisibleQuantity.has_value() || newReplenishQuantity.has_value() ||
                    newExpiry.has_value()) {
                    return false;
                }
                break;

            case OrderType::IOC:
                // Immediate-or-Cancel orders can have price and quantity
                // but not time-related parameters
                if (newStopPrice.has_value() || newTimeInForce.has_value() || 
                    newExpiryTime.has_value() || newDisplaySize.has_value() ||
                    newVisibleQuantity.has_value() || newReplenishQuantity.has_value() ||
                    newExpiry.has_value()) {
                    return false;
                }
                break;

            case OrderType::GTC:
                // Good-Till-Canceled orders can have price, quantity, and time in force
                // but not expiry time (that would make it GTD)
                if (newStopPrice.has_value() || newExpiryTime.has_value() || 
                    newDisplaySize.has_value() || newVisibleQuantity.has_value() || 
                    newReplenishQuantity.has_value() || newExpiry.has_value()) {
                    return false;
                }
                break;

            case OrderType::GTD:
                // Good-Till-Date orders can have all parameters except stop price
                // and display size
                if (newStopPrice.has_value() || newDisplaySize.has_value() ||
                    newVisibleQuantity.has_value() || newReplenishQuantity.has_value()) {
                    return false;
                }
                break;

            case OrderType::ICE:
                // Iceberg orders can have all parameters including display size
                // but not stop price
                if (newStopPrice.has_value()) {
                    return false;
                }
                break;

            default:
                // Unknown order type, reject modifications
                return false;
        }
        
        return true;
    };
};
struct PriceComp {
    bool reverse;
    PriceComp(bool rev = false) : reverse(rev) {}
    bool operator()(double a, double b) const {
        return reverse ? a > b : a < b;
    }
};

}// namespace ultraBook

#endif