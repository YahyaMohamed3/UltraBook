#include "engine.hpp"
#include <iostream>
#include <algorithm> 
#include "types.hpp"

namespace ultraBook{

MatchingEngine::MatchingEngine() = default;

//Add limit order to the book 
void MatchingEngine::addLimitOrder(int orderId , double price, int quantity , bool isBuy){
    if(quantity <= 0){
        std::cerr <<"Invalid order: Quantity must be bigger than 0 ." << std::endl;
        return;
    }
    if(price <= 0){
        std::cerr <<"Invalid order: Price must be bigger than 0" << std::endl;
        return;
    }
    std::cout<<"[addLimitOrder] OrderID: "<<orderId
             <<" , Price: "<< price
             <<" , Qty: "<<quantity
             <<", Side: "<<(isBuy ? "Buy" : "Sell") << std::endl;
    
    Order newOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::LIMIT);

    if(isBuy){
        buyOrders[price].push_back(newOrder);
        auto& orderQueue = buyOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }
    else{
        sellOrders[price].push_back(newOrder);
        auto& orderQueue = sellOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }
}

//implement market order 
void MatchingEngine::addMarketOrder(int orderId, int quantity, bool isBuy) {
    if(quantity <= 0) {
        std::cerr << "Invalid order: Qty must be bigger than 0." << std::endl;
        return;
    }

    std::cout << "[Market Order] OrderID: " << orderId
              << ", quantity: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;

    Order marketOrder(orderId, std::nullopt, quantity, isBuy, OrderType::MARKET);

    if (isBuy) {
        // Match against best sell orders (lowest price)
        while(quantity > 0 && !sellOrders.empty()) {
            auto& sellQueue = sellOrders.begin()->second;
            Order& sellOrder = sellQueue.front();

            int tradeQty = std::min(quantity, sellOrder.quantity);
            double tradePrice = sellOrders.begin()->first;
            std::cout<<"[Market Buy] OrderID: "<<orderId
                     <<" matched with SellOrder "<<sellOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<< tradeQty<<std::endl;

            Trade trade(orderId, sellOrder.orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            quantity -= tradeQty;
            sellOrder.quantity -= tradeQty;

            if(sellOrder.quantity == 0) {
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if(sellQueue.empty()) sellOrders.erase(sellOrders.begin());
            }
        }
    } else {
        // Match against best buy orders (highest price)
        while(quantity > 0 && !buyOrders.empty()) {
            auto& buyQueue = buyOrders.begin()->second;
            Order& buyOrder = buyQueue.front();

            int tradeQty = std::min(quantity, buyOrder.quantity);
            double tradePrice = buyOrders.begin()->first;
            std::cout<<"Match [Market Sell] OrderID: "<<orderId
                     <<" matched with "<<buyOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<<tradeQty<<std::endl;

            Trade trade(buyOrder.orderId, orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);

            quantity -= tradeQty;
            buyOrder.quantity -= tradeQty;

            if(buyOrder.quantity == 0) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if(buyQueue.empty()) buyOrders.erase(buyOrders.begin());
            }
        }
    }

    if (quantity > 0) {
        std::cout << "[Market Order Unfilled] Quantity left unfilled: " << quantity << std::endl;
        std::cout << "[Market Order] Remaining portion was discarded." << std::endl;
    }
}

void MatchingEngine::printTradelog(){
    for (const auto& trade : tradeLog) {
        std::cout << trade << '\n';
    }
}

void MatchingEngine::cancelOrder(int orderId) {
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        Order* order = it->second;
        if (order->isBuy) {
            // Remove from buyOrders map
            auto& orderQueue = buyOrders[order->price.value()];
            orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                           [orderId](const Order& o) { return o.orderId == orderId; }),
                           orderQueue.end());
        } else {
            // Remove from sellOrders map
            auto& orderQueue = sellOrders[order->price.value()];
            orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                           [orderId](const Order& o) { return o.orderId == orderId; }),
                           orderQueue.end());
        }
        orderMap.erase(it);
        std::cout << "[cancelOrder] OrderID: " << orderId << " has been canceled." << std::endl;
    } else {
        std::cout << "[cancelOrder] OrderID: " << orderId << " not found." << std::endl;
    }
}
//OrderBook snapshot
void MatchingEngine::printOrderBook() const {
    std::cout << "====== ORDER BOOK ======" << std::endl;
    
    // Print sell orders (highest to lowest)
    std::cout<<"SELL ORDERS"<<std::endl;
    for(auto it = sellOrders.rbegin(); it != sellOrders.rend(); ++it){
        std::cout<<"Price: "<< it->first<<": ";
        for(const auto& order : it->second){
            std::cout<<"(OrderID "<<order.orderId << ", "<< order.quantity<< " shares) ";
        }
        std::cout <<std::endl;
    }
    
    // Print buy orders (highest to lowest)
    std::cout<<"BUY ORDERS"<<std::endl;
    for(const auto& [price , orders] : buyOrders){
        std::cout<<"Price: "<<price<<": ";
        for(const auto& order: orders){
            std::cout<<"(OrderID "<<order.orderId<<", "<< order.quantity<<" shares) ";
        }
        std::cout<<std::endl;
    }
    
    std::cout << "======================" << std::endl;
}

void MatchingEngine::matchOrders() {
    std::cout << "[matchOrders] Attempting to match orders..." << std::endl;
    
    bool matchFound = true;
    
    while (matchFound) {
        matchFound = false; 
        
        if (buyOrders.empty() || sellOrders.empty()) {
            break;
        }
        
        double highestBuyPrice = buyOrders.begin()->first;
        double lowestSellPrice = sellOrders.begin()->first;
        
        if (highestBuyPrice >= lowestSellPrice) {
            auto buyIt = buyOrders.begin();
            auto sellIt = sellOrders.begin();
            auto& buyQueue = buyIt->second;
            auto& sellQueue = sellIt->second;
            
            Order& buyOrder = buyQueue.front();
            Order& sellOrder = sellQueue.front();
            
            int tradeQty = std::min(buyOrder.quantity, sellOrder.quantity);
            
            std::cout << "[MATCH] BuyOrder " << buyOrder.orderId 
                      << " matched with SellOrder " << sellOrder.orderId
                      << " at price " << lowestSellPrice
                      << " for quantity " << tradeQty << std::endl;

            Trade trade(buyOrder.orderId, sellOrder.orderId, lowestSellPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);
            
            buyOrder.quantity -= tradeQty;
            sellOrder.quantity -= tradeQty;
            
            if (buyOrder.quantity == 0) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if(buyQueue.empty()) {
                    buyOrders.erase(buyIt);
                }
            }
            
            if (sellOrder.quantity == 0) {
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if(sellQueue.empty()) {
                    sellOrders.erase(sellIt);
                }
            }
            
            matchFound = true;
        } else {
            break;  // No matches possible at current price levels
        }
    }
    
    if (!matchFound) {
        std::cout << "[matchOrders] No matches found." << std::endl;
    }
}
}