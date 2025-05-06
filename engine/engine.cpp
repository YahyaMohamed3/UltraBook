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
    
    setOrderStatus(orderMap[orderId], OrderStatus::ACTIVE);
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
    int remainingQty = quantity;

    if (isBuy) {
        while(remainingQty > 0 && !sellOrders.empty()) {
            auto& sellQueue = sellOrders.begin()->second;
            Order& sellOrder = sellQueue.front();

            int tradeQty = std::min(remainingQty, sellOrder.quantity);
            double tradePrice = sellOrders.begin()->first;
            
            std::cout<<"[Market Buy] OrderID: "<<orderId
                     <<" matched with SellOrder "<<sellOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<< tradeQty<<std::endl;

            Trade trade(orderId, sellOrder.orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);

            remainingQty -= tradeQty;
            sellOrder.filledQuantity += tradeQty;
            marketOrder.filledQuantity += tradeQty;

            // Update sell order status
            if (sellOrder.filledQuantity < sellOrder.quantity) {
                setOrderStatus(&sellOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&sellOrder, OrderStatus::FILLED);
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if(sellQueue.empty()) {
                    sellOrders.erase(sellOrders.begin());
                }
            }
        }
    } else {
        while(remainingQty > 0 && !buyOrders.empty()) {
            auto& buyQueue = buyOrders.begin()->second;
            Order& buyOrder = buyQueue.front();

            int tradeQty = std::min(remainingQty, buyOrder.quantity);
            double tradePrice = buyOrders.begin()->first;
            
            std::cout<<"Match [Market Sell] OrderID: "<<orderId
                     <<" matched with "<<buyOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<<tradeQty<<std::endl;

            Trade trade(buyOrder.orderId, orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);

            remainingQty -= tradeQty;
            buyOrder.filledQuantity += tradeQty;
            marketOrder.filledQuantity += tradeQty;

            // Update buy order status
            if (buyOrder.filledQuantity < buyOrder.quantity) {
                setOrderStatus(&buyOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&buyOrder, OrderStatus::FILLED);
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if(buyQueue.empty()) {
                    buyOrders.erase(buyOrders.begin());
                }
            }
        }
    }

    // Store and update market order status
    allOrders.push_back(marketOrder);
    Order* marketOrderPtr = &allOrders.back();
    
    if (remainingQty == quantity) {
        setOrderStatus(marketOrderPtr, OrderStatus::CANCELED);
        std::cout << "[Market Order] No matching orders found, order canceled." << std::endl;
    } else if (remainingQty > 0) {
        setOrderStatus(marketOrderPtr, OrderStatus::PARTIALLY_FILLED);
        std::cout << "[Market Order] Partially filled: " << (quantity - remainingQty) 
                 << " filled, " << remainingQty << " remaining (canceled)" << std::endl;
    } else {
        setOrderStatus(marketOrderPtr, OrderStatus::FILLED);
        std::cout << "[Market Order] Fully filled." << std::endl;
    }
}

void MatchingEngine::addIOCOrder(int orderId, double price, int quantity, bool isBuy) {
    if(price <= 0 || quantity <= 0) {
        std::cerr << "[addIOCOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }

    Order iocOrder(orderId, price, quantity, isBuy, OrderType::IOC);
    allOrders.push_back(iocOrder);
    Order* orderPtr = &allOrders.back();
    setOrderStatus(orderPtr, OrderStatus::ACTIVE);

    int remainingQty = quantity;
    if(isBuy) {
        while(remainingQty > 0 && !sellOrders.empty()) {
            double lowestPrice = sellOrders.begin()->first;
            if(lowestPrice <= price) {
                auto& sellQueue = sellOrders.begin()->second;
                Order& sellOrder = sellQueue.front();

                int tradeQty = std::min(remainingQty, sellOrder.quantity);
                remainingQty -= tradeQty;
                sellOrder.filledQuantity += tradeQty;
                orderPtr->filledQuantity += tradeQty;

                std::cout << "[IOC Match] OrderID: " << orderId
                         << " matched with sellOrder: " << sellOrder.orderId
                         << " for QTY: " << tradeQty
                         << " at price: " << lowestPrice << std::endl;

                Trade trade(orderId, sellOrder.orderId, lowestPrice, tradeQty);
                tradeLog.push_back(trade);

                if(sellOrder.filledQuantity < sellOrder.quantity) {
                    setOrderStatus(&sellOrder, OrderStatus::PARTIALLY_FILLED);
                } else {
                    setOrderStatus(&sellOrder, OrderStatus::FILLED);
                    orderMap.erase(sellOrder.orderId);
                    sellQueue.pop_front();
                    if(sellQueue.empty()) {
                        sellOrders.erase(sellOrders.begin());
                    }
                }
            } else {
                break; // Price not favorable
            }
        }
    } else {
        while(remainingQty > 0 && !buyOrders.empty()) {
            double highestPrice = buyOrders.begin()->first;
            if(highestPrice >= price) {
                auto& buyQueue = buyOrders.begin()->second;
                Order& buyOrder = buyQueue.front();

                int tradeQty = std::min(remainingQty, buyOrder.quantity);
                remainingQty -= tradeQty;
                buyOrder.filledQuantity += tradeQty;
                orderPtr->filledQuantity += tradeQty;

                std::cout << "[IOC Match] OrderID: " << orderId
                         << " matched with buyOrder: " << buyOrder.orderId
                         << " for QTY: " << tradeQty
                         << " at price: " << highestPrice << std::endl;

                Trade trade(buyOrder.orderId, orderId, highestPrice, tradeQty);
                tradeLog.push_back(trade);

                if(buyOrder.filledQuantity < buyOrder.quantity) {
                    setOrderStatus(&buyOrder, OrderStatus::PARTIALLY_FILLED);
                } else {
                    setOrderStatus(&buyOrder, OrderStatus::FILLED);
                    orderMap.erase(buyOrder.orderId);
                    buyQueue.pop_front();
                    if(buyQueue.empty()) {
                        buyOrders.erase(buyOrders.begin());
                    }
                }
            } else {
                break; // Price not favorable
            }
        }
    }

    // Update IOC order final status
    if(orderPtr->filledQuantity == 0) {
        setOrderStatus(orderPtr, OrderStatus::CANCELED);
        std::cout << "[IOC Order] OrderID: " << orderId << " canceled - no matches at specified price" << std::endl;
    } else if(orderPtr->filledQuantity < quantity) {
        setOrderStatus(orderPtr, OrderStatus::PARTIALLY_FILLED);
        std::cout << "[IOC Order] OrderID: " << orderId 
                 << " partially filled: " << orderPtr->filledQuantity 
                 << " of " << quantity << " shares. Remaining canceled." << std::endl;
    } else {
        setOrderStatus(orderPtr, OrderStatus::FILLED);
        std::cout << "[IOC Order] OrderID: " << orderId << " fully filled." << std::endl;
    }
}

void MatchingEngine::addFOKOrder(int orderId , double price , int quantity, bool isBuy){
    if(price <= 0 || quantity <= 0 ){
        std::cerr<<"Please make sure that the price and the  quantity are bigger than zero";
        return;
    }
    Order FOCOrder(orderId , price , quantity , isBuy , OrderType::FOC);
    allOrders.push_back(FOCOrder);
    Order* orderPtr = &allOrders.back();
    setOrderStatus(orderPtr , OrderStatus::ACTIVE);
    int availble = 0;

    if(isBuy){
        auto it = sellOrders.begin();
        double lowestPrice = it->first;
        if(lowestPrice <= price){
            auto& sellQueue = it->second;
            //scan if there are enough orders to immdielty fill 
            for(const auto& order : sellQueue) {
                if(availble >= quantity) {
                    break;  // We have enough quantity to fill the order
                }
                availble += order.quantity;
            }
            if(availble < quantity){
                setOrderStatus(orderPtr, OrderStatus::CANCELED);
                std::cout<<"[FOK Order] Order has been killed - insufficient quantity"<<std::endl;
                return;
            }

        }







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
        order->status = OrderStatus::CANCELED;
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
            std::cout<<"(OrderID "<<order.orderId 
                    << ", "<< order.quantity<< " shares"
                    << ", Status: " << order.status << ") ";
        }
        std::cout <<std::endl;
    }
    
    // Print buy orders (highest to lowest)
    std::cout<<"BUY ORDERS"<<std::endl;
    for(const auto& [price , orders] : buyOrders){
        std::cout<<"Price: "<<price<<": ";
        for(const auto& order: orders){
            std::cout<<"(OrderID "<<order.orderId
                    <<", "<< order.quantity<<" shares"
                    << ", Status: " << order.status << ") ";
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
            
            buyOrder.filledQuantity += tradeQty;
            sellOrder.filledQuantity += tradeQty;
            
            // Update order statuses
            if (buyOrder.filledQuantity < buyOrder.quantity) {
                setOrderStatus(&buyOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&buyOrder, OrderStatus::FILLED);
            }
            
            if (sellOrder.filledQuantity < sellOrder.quantity) {
                setOrderStatus(&sellOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&sellOrder, OrderStatus::FILLED);
            }
            
            if (buyOrder.status == OrderStatus::FILLED) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if(buyQueue.empty()) {
                    buyOrders.erase(buyIt);
                }
            }
            
            if (sellOrder.status == OrderStatus::FILLED) {
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

void MatchingEngine::setOrderStatus(Order* order, OrderStatus newStatus) {
    if (!order) {
        std::cerr << "Error: Cannot set status on null order" << std::endl;
        return;
    }

    OrderStatus oldStatus = order->status;
    order->status = newStatus;
    
    std::cout << "[Status Update] OrderID: " << order->orderId 
              << " Status changed from " << oldStatus 
              << " to " << newStatus << std::endl;
}

}