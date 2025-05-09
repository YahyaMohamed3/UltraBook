#include "engine.hpp"
#include <iostream>
#include <algorithm> 
#include "types.hpp"
#include"helpers.cpp"

namespace ultraBook{

MatchingEngine::MatchingEngine() = default;

//Add limit order to the book 
void MatchingEngine::addLimitOrder(int orderId , double price, int quantity , bool isBuy){
    if(quantity <= 0 || price <= 0){
        std::cerr <<"Invalid order: please make sure that the price and the quantity are greater than 0." << std::endl;
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
    
    // Order is already set to ACTIVE in the constructor
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

            int tradeQty = std::min(remainingQty, sellOrder.getRemainingQuantity());
            // Skip if trade quantity is zero
            if (tradeQty <= 0) {
                break;
            }
            
            double tradePrice = sellOrders.begin()->first;
            
            std::cout<<"[Market Buy] OrderID: "<<orderId
                     <<" matched with SellOrder "<<sellOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<< tradeQty<<std::endl;

            Trade trade(orderId, sellOrder.orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            remainingQty -= tradeQty;
            sellOrder.filledQuantity += tradeQty;
            marketOrder.filledQuantity += tradeQty;
            lastPrice = tradePrice;
            checkandTrigger(lastPrice);

            if (sellOrder.isComplete()) {
                setOrderStatus(&sellOrder, OrderStatus::FILLED);
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if(sellQueue.empty()) {
                    sellOrders.erase(sellOrders.begin());
                }
            } else {
                setOrderStatus(&sellOrder, OrderStatus::PARTIALLY_FILLED);
            }
        }
    } else {
        while(remainingQty > 0 && !buyOrders.empty()) {
            auto& buyQueue = buyOrders.begin()->second;
            Order& buyOrder = buyQueue.front();

            int tradeQty = std::min(remainingQty, buyOrder.getRemainingQuantity());
            // Skip if trade quantity is zero
            if (tradeQty <= 0) {
                break;
            }
            
            double tradePrice = buyOrders.begin()->first;
            
            std::cout<<"Match [Market Sell] OrderID: "<<orderId
                     <<" matched with "<<buyOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<<tradeQty<<std::endl;

            Trade trade(buyOrder.orderId, orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);

            remainingQty -= tradeQty;
            buyOrder.filledQuantity += tradeQty;
            marketOrder.filledQuantity += tradeQty;
            lastPrice = tradePrice;
            checkandTrigger(lastPrice);

            if (buyOrder.isComplete()) {
                setOrderStatus(&buyOrder, OrderStatus::FILLED);
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if(buyQueue.empty()) {
                    buyOrders.erase(buyOrders.begin());
                }
            } else {
                setOrderStatus(&buyOrder, OrderStatus::PARTIALLY_FILLED);
            }
        }
    }

    // Store and update market order status
    allOrders.push_back(marketOrder);
    Order* marketOrderPtr = &allOrders.back();
    
    if (remainingQty == quantity) {
        setOrderStatus(marketOrderPtr, OrderStatus::CANCELED);
        std::cout << "[Market Order] No matching orders found, Order: "<<orderId<<" canceled." << std::endl;
    } else if (remainingQty > 0) {
        setOrderStatus(marketOrderPtr, OrderStatus::PARTIALLY_FILLED);
        std::cout << "[Market Order] OrderID: "<<orderId<<" Partially filled: " << (quantity - remainingQty) 
                 << " filled, " << remainingQty << " remaining (canceled)" << std::endl;
    } else {
        setOrderStatus(marketOrderPtr, OrderStatus::FILLED);
        std::cout << "[Market Order] OrderID: "<<orderId<<" Fully filled." << std::endl;
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
    std::cout << "[IOC Order] OrderID: " << orderId
              << ", Price: " << price
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;

    int remainingQty = quantity;
    if(isBuy) {
        while(remainingQty > 0 && !sellOrders.empty()) {
            double lowestPrice = sellOrders.begin()->first;
            if(lowestPrice <= price) {
                auto& sellQueue = sellOrders.begin()->second;
                Order& sellOrder = sellQueue.front();

                int tradeQty = std::min(remainingQty, sellOrder.getRemainingQuantity());
                remainingQty -= tradeQty;
                sellOrder.filledQuantity += tradeQty;
                orderPtr->filledQuantity += tradeQty;

                std::cout << "[IOC Match] OrderID: " << orderId
                         << " matched with sellOrder: " << sellOrder.orderId
                         << " for QTY: " << tradeQty
                         << " at price: " << lowestPrice << std::endl;

                Trade trade(orderId, sellOrder.orderId, lowestPrice, tradeQty);
                tradeLog.push_back(trade);
                tradesByOrderId[orderId].push_back(trade);
                tradesByOrderId[sellOrder.orderId].push_back(trade);
                lastPrice = lowestPrice;
                checkandTrigger(lastPrice);


                if(sellOrder.getRemainingQuantity() > 0) {
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

                int tradeQty = std::min(remainingQty, buyOrder.getRemainingQuantity());
                remainingQty -= tradeQty;
                buyOrder.filledQuantity += tradeQty;
                orderPtr->filledQuantity += tradeQty;

                std::cout << "[IOC Match] OrderID: " << orderId
                         << " matched with buyOrder: " << buyOrder.orderId
                         << " for QTY: " << tradeQty
                         << " at price: " << highestPrice << std::endl;

                Trade trade(buyOrder.orderId, orderId, highestPrice, tradeQty);
                tradeLog.push_back(trade);
                tradesByOrderId[orderId].push_back(trade);
                tradesByOrderId[buyOrder.orderId].push_back(trade);
                lastPrice = highestPrice;
                checkandTrigger(lastPrice);

                if(buyOrder.getRemainingQuantity() > 0) {
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

    if(orderPtr->filledQuantity == 0) {
        setOrderStatus(orderPtr, OrderStatus::CANCELED);
        std::cout << "[IOC Order] OrderID: " << orderId << " canceled - no matches at specified price" << std::endl;
    } else if(orderPtr->getRemainingQuantity() > 0) {
        setOrderStatus(orderPtr, OrderStatus::PARTIALLY_FILLED);
        std::cout << "[IOC Order] OrderID: " << orderId 
                 << " partially filled: " << orderPtr->filledQuantity 
                 << " of " << quantity << " shares. Remaining canceled." << std::endl;
    } else {
        setOrderStatus(orderPtr, OrderStatus::FILLED);
        std::cout << "[IOC Order] OrderID: " << orderId << " fully filled." << std::endl;
    }
}

void MatchingEngine::addFOKOrder(int orderId, double price, int quantity, bool isBuy) {
    if (price <= 0 || quantity <= 0) {
        throw OrderException("Invalid parameters for FOK order");
    }

    Order fokOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::FOK);
    allOrders.push_back(fokOrder);
    Order* orderPtr = &allOrders.back();
    
    std::cout<<"[FOK Order] OrderID: "<<orderId
             <<", Price: "<<price
             <<", Qty: "<<quantity
             <<", Side: "<<(isBuy ? "Buy" : "Sell") << std::endl;

    // First check if full quantity can be executed at the specified price
    int availableQty = 0;

    if (isBuy) {
        for (auto sellIt = sellOrders.begin(); sellIt != sellOrders.end() && sellIt->first <= price; ++sellIt) {
            for (const auto& order : sellIt->second) {
                availableQty += order.getRemainingQuantity();
                if (availableQty >= quantity) break;
            }
            if (availableQty >= quantity) break;
        }
    } else {
        for (auto buyIt = buyOrders.begin(); buyIt != buyOrders.end() && buyIt->first >= price; ++buyIt) {
            for (const auto& order : buyIt->second) {
                availableQty += order.getRemainingQuantity();
                if (availableQty >= quantity) break;
            }
            if (availableQty >= quantity) break;
        }
    }

    // If can't fill entire quantity, cancel the order
    if (availableQty < quantity) {
        setOrderStatus(orderPtr, OrderStatus::CANCELED);
        std::cout << "[FOK Order] OrderID: " << orderId 
                 << " canceled - insufficient quantity available at price " << price << std::endl;
        return;
    }

    // Execute the full quantity
    int remainingQty = quantity;
    
    if (isBuy) {
        while (remainingQty > 0) {
            auto& sellQueue = sellOrders.begin()->second;
            Order& sellOrder = sellQueue.front();
            double execPrice = sellOrders.begin()->first;

            if (execPrice > price) {
                // Price moved unfavorably
                setOrderStatus(orderPtr, OrderStatus::CANCELED);
                std::cout << "[FOK Order] OrderID: " << orderId 
                         << " canceled - price moved unfavorably" << std::endl;
                return;
            }

            int tradeQty = std::min(remainingQty, sellOrder.getRemainingQuantity());
            
            Trade trade(orderId, sellOrder.orderId, execPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            remainingQty -= tradeQty;
            sellOrder.filledQuantity += tradeQty;
            orderPtr->filledQuantity += tradeQty;
            lastPrice = execPrice;
            checkandTrigger(lastPrice);

            std::cout << "[FOK Match] Buy OrderID: " << orderId 
                     << " matched with Sell OrderID: " << sellOrder.orderId
                     << " for " << tradeQty << " at " << execPrice << std::endl;

            // Update sell order status
            if (sellOrder.getRemainingQuantity() > 0) {
                setOrderStatus(&sellOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&sellOrder, OrderStatus::FILLED);
            }

            if (sellOrder.isComplete()) {
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if (sellQueue.empty()) {
                    sellOrders.erase(sellOrders.begin());
                }
            }
        }
    } else {
        while (remainingQty > 0) {
            auto& buyQueue = buyOrders.begin()->second;
            Order& buyOrder = buyQueue.front();
            double execPrice = buyOrders.begin()->first;

            if (execPrice < price) {
                // Price moved unfavorably
                setOrderStatus(orderPtr, OrderStatus::CANCELED);
                std::cout << "[FOK Order] OrderID: " << orderId 
                         << " canceled - price moved unfavorably" << std::endl;
                return;
            }

            int tradeQty = std::min(remainingQty, buyOrder.getRemainingQuantity());
            
            Trade trade(buyOrder.orderId, orderId, execPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);

            remainingQty -= tradeQty;
            buyOrder.filledQuantity += tradeQty;
            orderPtr->filledQuantity += tradeQty;
            lastPrice = execPrice;
            checkandTrigger(lastPrice);

            std::cout << "[FOK Match] Sell OrderID: " << orderId 
                     << " matched with Buy OrderID: " << buyOrder.orderId
                     << " for " << tradeQty << " at " << execPrice << std::endl;

            // Update buy order status
            if (buyOrder.getRemainingQuantity() > 0) {
                setOrderStatus(&buyOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&buyOrder, OrderStatus::FILLED);
            }

            if (buyOrder.isComplete()) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if (buyQueue.empty()) {
                    buyOrders.erase(buyOrders.begin());
                }
            }
        }
    }
    setOrderStatus(orderPtr, OrderStatus::FILLED);
    std::cout << "[FOK Order] OrderID: " << orderId << " fully filled" << std::endl;
}

void MatchingEngine::addGTCOrder(int orderId, double price , int quantity, bool isBuy){
    if(quantity <= 0 || price <= 0) {
        std::cerr << "[addGTCOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[GTC Order] OrderID: " << orderId
              << ", Price: " << price
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;
    Order gtcOrder(orderId, price, quantity, isBuy, OrderType::GTC);
    allOrders.push_back(gtcOrder);
    if(isBuy){
        buyOrders[price].push_back(gtcOrder);
        auto& orderQueue = buyOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }
    else{
        sellOrders[price].push_back(gtcOrder);
        auto& orderQueue = sellOrders[price];
        orderMap[orderId] = &orderQueue.back();

    }
    
}

void MatchingEngine::addGTDOrder(int orderId, double price, int quantity, bool isBuy, std::chrono::system_clock::time_point expiry){
if(quantity <= 0 || price <= 0) {
        std::cerr << "[addGTDOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[GTD Order] OrderID: " << orderId
              << ", Price: " << price
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") 
              << ", Expiry: " << std::chrono::system_clock::to_time_t(expiry) << std::endl;
    Order gtdOrder(orderId, price, quantity, isBuy, OrderType::GTD, expiry);
    allOrders.push_back(gtdOrder);
    if(isBuy){
        buyOrders[price].push_back(gtdOrder);
        auto& orderQueue = buyOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }
    else{
        sellOrders[price].push_back(gtdOrder);
        auto& orderQueue = sellOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }

}

void MatchingEngine::addStopOrder(int orderId, double stopPrice, int quantity, bool isBuy){
    if(quantity <= 0 || stopPrice <= 0){
        std::cerr << "[addStopOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[Stop Order] OrderID: " << orderId
              << ", Stop Price: " << stopPrice
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;
    Order stopOrder(orderId, stopPrice, quantity, isBuy, OrderType::STOP);
    allOrders.push_back(stopOrder);
    if(isBuy){
        buyStopOrders[stopPrice].push_back(stopOrder);
        auto& orderQueue = buyStopOrders[stopPrice];
        orderMap[orderId] = &orderQueue.back();
    }
    else{
        sellStopOrders[stopPrice].push_back(stopOrder);
        auto& orderQueue = sellStopOrders[stopPrice];
        orderMap[orderId] = &orderQueue.back();
    }
    //order set to inactive by default in the constructor
}

void MatchingEngine::checkandTrigger(double lastprice){
    std::cout<<"[checkandTrigger] Last Price: "<<lastprice<<std::endl;
    auto it = buyStopOrders.begin();
    while(it != buyStopOrders.end()){
        auto& orderQueue = it->second;
        Order& stopOrder = orderQueue.front();
        if(stopOrder.stopPrice && lastprice >= stopOrder.stopPrice.value()){
            std::cout<<"[checkandTrigger] Buy Stop OrderID: "<<stopOrder.orderId
                     <<" triggered at price "<<lastprice<<std::endl;
            stopOrder.status = OrderStatus::TRIGGERED;
            convertStopToMarket(&stopOrder);
            orderMap.erase(stopOrder.orderId);
            orderQueue.pop_front();
            if(orderQueue.empty()){
                buyStopOrders.erase(it++);
            }else{
                ++it;
            }
        }else{
            ++it;
        }
    }
    it = sellStopOrders.begin();
    while(it != sellStopOrders.end()){
        auto& orderQueue = it->second;
        Order& stopOrder = orderQueue.front();
        if(stopOrder.stopPrice && lastprice <= stopOrder.stopPrice.value()){
            std::cout<<"[checkandTrigger] Sell Stop OrderID: "<<stopOrder.orderId
                     <<" triggered at price "<<lastprice<<std::endl;
            stopOrder.status = OrderStatus::TRIGGERED;
            convertStopToMarket(&stopOrder);
            orderMap.erase(stopOrder.orderId);
            orderQueue.pop_front();
            if(orderQueue.empty()){
                sellStopOrders.erase(it++);
            }else{
                ++it;
            }
        }else{
            ++it;
        }
    }
    // Check for triggered orders and convert to market orders





}
void MatchingEngine::convertStopToMarket(Order* order) {
    if (order->status == OrderStatus::TRIGGERED) {
        std::cout << "[convertStopToMarket] Converting Stop OrderID: " << order->orderId << " to Market Order." << std::endl;
        order->type = OrderType::MARKET;
        order->status = OrderStatus::ACTIVE; // Set to active for market order
        order->price = std::nullopt; // No price for market orders
        orderMap[order->orderId] = order; // Update map with new type

        MatchingEngine::addMarketOrder(order->orderId, order->quantity, order->isBuy);
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
            std::cout<<"(OrderID "<<order.orderId << ", "<< order.quantity<< " shares) ";
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
            std::cout<<"(OrderID "<<order.orderId<<", "<< order.quantity<<" shares) ";
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

            if(buyOrder.type == OrderType::GTD && buyOrder.expiry.has_value()) {
                if (std::chrono::system_clock::now() > buyOrder.expiry.value()) {
                    setOrderStatus(&buyOrder, OrderStatus::EXPIRED);
                    std::cout<<"[GTD ORDER] OrderID: "<<buyOrder.orderId
                             <<" expired at "<<std::chrono::system_clock::to_time_t(buyOrder.expiry.value())<<std::endl;
                    orderMap.erase(buyOrder.orderId);
                    buyQueue.pop_front();
                    if(buyQueue.empty()) {
                        buyOrders.erase(buyIt);
                    }
                    continue; // Skip to next iteration
                }
            }
            if(sellOrder.type == OrderType::GTD && sellOrder.expiry.has_value()) {
                if (std::chrono::system_clock::now() > sellOrder.expiry.value()) {
                    setOrderStatus(&sellOrder, OrderStatus::EXPIRED);
                    std::cout<<"[GTD ORDER] OrderID: "<<sellOrder.orderId
                             <<" expired at "<<std::chrono::system_clock::to_time_t(sellOrder.expiry.value())<<std::endl;
                    orderMap.erase(sellOrder.orderId);
                    sellQueue.pop_front();
                    if(sellQueue.empty()) {
                        sellOrders.erase(sellIt);
                    }
                    continue; // Skip to next iteration
                }
            }

            // Use getRemainingQuantity() helper
            int tradeQty = std::min(buyOrder.getRemainingQuantity(), sellOrder.getRemainingQuantity());

            std::cout << "[MATCH] BuyOrder " << buyOrder.orderId 
                      << " matched with SellOrder " << sellOrder.orderId
                      << " at price " << lowestSellPrice
                      << " for quantity " << tradeQty << std::endl;

            Trade trade(buyOrder.orderId, sellOrder.orderId, lowestSellPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            buyOrder.filledQuantity += tradeQty;
            sellOrder.filledQuantity += tradeQty;
            lastPrice = lowestSellPrice;
            checkandTrigger(lastPrice);
            
            // Update order statuses
            if (buyOrder.getRemainingQuantity() > 0) {
                setOrderStatus(&buyOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&buyOrder, OrderStatus::FILLED);
            }
            
            if (sellOrder.getRemainingQuantity() > 0) {
                setOrderStatus(&sellOrder, OrderStatus::PARTIALLY_FILLED);
            } else {
                setOrderStatus(&sellOrder, OrderStatus::FILLED);
            }

            // Use isComplete() helper
            if (buyOrder.isComplete()) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if(buyQueue.empty()) {
                    buyOrders.erase(buyIt);
                }
            }

            if (sellOrder.isComplete()) {
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if(sellQueue.empty()) {
                    sellOrders.erase(sellIt);
                }
            }

            matchFound = true;
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

OrderStatus MatchingEngine::getOrderStatus(int orderId) const {
    // First check if the order is in the active order map
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        return it->second->status;
    }
    
    // If not active, search in allOrders for completed/canceled orders
    for (const auto& order : allOrders) {
        if (order.orderId == orderId) {
            return order.status;
        }
    }
    
    // If order not found, return CANCELED as default
    return OrderStatus::CANCELED;
}
} // namespace ultraBook
// engine.cpp
