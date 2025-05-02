#include "Engine.hpp"
#include <iostream>
#include <algorithm> 

namespace ultraBook{

MatchingEngine::MatchingEngine() {
    // Initialize internal data structures here if needed
}

//Add limit order to the book 
void MatchingEngine::addLimitOrder(int orderId , double price , int quantity , bool isBuy){
    std::cout<<"[addLimitOrder] OrderId: "<< orderId
             <<", price: "<< price
             <<", Qty: "<<quantity
             <<", Side: "<<(isBuy ? "Buy" : "Sell") << std::endl;
    
    Order newOrder (orderId, price, quantity, isBuy);

    if(isBuy){
        buyOrders[price].push_back(newOrder);
        orderMap[orderId] = &buyOrders[price].back();
    }
    else{
        sellOrders[price].push_back(newOrder);
        orderMap[orderId] = &sellOrders[price].back();
    }
}


void MatchingEngine::addMarketOrder(){






}

void MatchingEngine::cancelOrder(int orderId) {
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        Order* order = it->second;
        if (order->isBuy) {
            // Remove from buyOrders map
            auto& orderQueue = buyOrders[order->price];
            orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                           [orderId](const Order& o) { return o.orderId == orderId; }),
                           orderQueue.end());
        } else {
            // Remove from sellOrders map
            auto& orderQueue = sellOrders[order->price];
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
        
        // Check if there are any buy and sell orders
        if (buyOrders.empty() || sellOrders.empty()) {
            break;
        }
        
        // Get the highest buy price and lowest sell price
        double highestBuyPrice = buyOrders.begin()->first;
        double lowestSellPrice = sellOrders.begin()->first;
        
        // If highest buy price >= lowest sell price, we have a match
        if (highestBuyPrice >= lowestSellPrice) {
            auto& buyQueue = buyOrders.begin()->second;
            auto& sellQueue = sellOrders.begin()->second;
            
            // Get the first orders in the queue (oldest at that price)
            Order& buyOrder = buyQueue.front();
            Order& sellOrder = sellQueue.front();
            
            // Calculate trade quantity
            int tradeQty = std::min(buyOrder.quantity, sellOrder.quantity);
            
            std::cout << "[MATCH] BuyOrder " << buyOrder.orderId 
                      << " matched with SellOrder " << sellOrder.orderId
                      << " at price " << lowestSellPrice
                      << " for quantity " << tradeQty << std::endl;

            Trade trade(buyOrder.orderId , sellOrder.orderId, lowestSellPrice , tradeQty);
            tradeLog.push_back(trade)
            
            
            // Update order quantities
            buyOrder.quantity -= tradeQty;
            sellOrder.quantity -= tradeQty;
            
            // Remove filled orders
            if (buyOrder.quantity == 0) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                
                // If no more Buy orders at this price, remove the price level
                if (buyQueue.empty()) {
                    buyOrders.erase(buyOrders.begin());
                }
            }
            
            if (sellOrder.quantity == 0) {
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                
                // If no more Sell orders at this price, remove the price level
                if (sellQueue.empty()) {
                    sellOrders.erase(sellOrders.begin());
                }
            }
            
            matchFound = true;
        } else {
            // No matches possible
            break;
        }
    }
    
    if (!matchFound) {
        std::cout << "[matchOrders] No matches found." << std::endl;
    }
}
}