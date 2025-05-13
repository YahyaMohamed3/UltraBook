#include "engine.hpp"
#include <iostream>
#include <algorithm> 
#include "types.hpp"
#include"helpers.cpp"

namespace ultraBook{

// Default constructor for the matching engine
MatchingEngine::MatchingEngine() = default;

/**
 * Add a basic limit order to the order book
 * 
 * Limit orders specify both price and quantity and are placed in the order book
 * at their specified price level, following price-time priority.
 * 
 * @param orderId Unique identifier for this order
 * @param price The limit price for this order
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addLimitOrder(int orderId , double price, int quantity , bool isBuy){
    // Validate order parameters
    if(quantity <= 0 || price <= 0){
        std::cerr <<"Invalid order: please make sure that the price and the quantity are greater than 0." << std::endl;
        return;
    }
    std::cout<<"[addLimitOrder] OrderID: "<<orderId
             <<" , Price: "<< price
             <<" , Qty: "<<quantity
             <<", Side: "<<(isBuy ? "Buy" : "Sell") << std::endl;

    // Create a new order object
    Order newOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::LIMIT);

    // Add to appropriate side of the order book and update the order map for quick access
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

/**
 * Add a market order that executes immediately at the best available price
 * 
 * Market orders execute immediately against the opposite side of the order book
 * at the best available prices until the quantity is filled or no more counterparties
 * are available. Any unfilled quantity is canceled.
 * 
 * @param orderId Unique identifier for this order
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 * @param isConverted Optional flag indicating if this is a converted stop order
 * @param existingOrder Optional pointer to an existing order (for converted orders)
 */
void MatchingEngine::addMarketOrder(int orderId, int quantity, bool isBuy, bool isConverted, Order* existingOrder) {
    // Validate order parameters
    if(quantity <= 0) {
        std::cerr << "Invalid order: Qty must be bigger than 0." << std::endl;
        return;
    }

    std::cout << "[Market Order] OrderID: " << orderId
              << ", quantity: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;

    // Order pointer setup - either create new order or use existing one
    Order* marketOrderPtr = nullptr;
    if (!isConverted) {
        // Create a new market order
        Order marketOrder(orderId, std::nullopt, quantity, isBuy, OrderType::MARKET);
        allOrders.push_back(marketOrder);
        marketOrderPtr = &allOrders.back();
    } else {
        // Use the existing order (e.g., converted stop order)
        marketOrderPtr = existingOrder;
    }

    // Track remaining quantity for execution
    int remainingQty = quantity;

    if (isBuy) {
        // Buy market order execution logic - match against sell orders starting with lowest price
        while(remainingQty > 0 && !sellOrders.empty()) {
            auto& sellQueue = sellOrders.begin()->second;
            Order& sellOrder = sellQueue.front();

            // Calculate trade quantity as the minimum of what's needed and what's available
            int tradeQty = std::min(remainingQty, sellOrder.getRemainingQuantity());
            if (tradeQty <= 0) {
                break;
            }
            
            double tradePrice = sellOrders.begin()->first;
            
            std::cout<<"[Market Buy] OrderID: "<<orderId
                     <<" matched with SellOrder "<<sellOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<< tradeQty<<std::endl;

            // Create and record the trade
            Trade trade(orderId, sellOrder.orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            // Update quantities and last traded price
            remainingQty -= tradeQty;
            sellOrder.filledQuantity += tradeQty;
            marketOrderPtr->filledQuantity += tradeQty;
            lastPrice = tradePrice;
            
            // Check if any stop orders should be triggered by this trade
            checkandTrigger(lastPrice);

            // Update sell order status and remove if completely filled
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
        // Sell market order execution logic - match against buy orders starting with highest price
        while(remainingQty > 0 && !buyOrders.empty()) {
            auto& buyQueue = buyOrders.begin()->second;
            Order& buyOrder = buyQueue.front();

            int tradeQty = std::min(remainingQty, buyOrder.getRemainingQuantity());
            if (tradeQty <= 0) {
                break;
            }
            
            double tradePrice = buyOrders.begin()->first;
            
            std::cout<<"Match [Market Sell] OrderID: "<<orderId
                     <<" matched with "<<buyOrder.orderId
                     <<" at price "<<tradePrice
                     <<" for Qty "<<tradeQty<<std::endl;

            // Create and record the trade
            Trade trade(buyOrder.orderId, orderId, tradePrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);

            // Update quantities and last traded price
            remainingQty -= tradeQty;
            buyOrder.filledQuantity += tradeQty;
            marketOrderPtr->filledQuantity += tradeQty;
            lastPrice = tradePrice;
            
            // Check if any stop orders should be triggered by this trade
            checkandTrigger(lastPrice);

            // Update buy order status and remove if completely filled
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

    // Update market order status based on execution
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

/**
 * Add an Immediate-or-Cancel (IOC) order
 * 
 * IOC orders attempt to execute immediately up to their specified price, 
 * and any unfilled portion is canceled rather than entering the order book.
 * 
 * @param orderId Unique identifier for this order
 * @param price The maximum (buy) or minimum (sell) price for execution
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addIOCOrder(int orderId, double price, int quantity, bool isBuy) {
    // Validate order parameters
    if(price <= 0 || quantity <= 0) {
        std::cerr << "[addIOCOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }

    // Create IOC order and store in allOrders
    Order iocOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::IOC);
    allOrders.push_back(iocOrder);
    Order* orderPtr = &allOrders.back();
    
    std::cout << "[IOC Order] OrderID: " << orderId
              << ", Price: " << price
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;

    int remainingQty = quantity;
    if(isBuy) {
        // Buy IOC order - match against sell orders at or below specified price
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

                // Create and record the trade
                Trade trade(orderId, sellOrder.orderId, lowestPrice, tradeQty);
                tradeLog.push_back(trade);
                tradesByOrderId[orderId].push_back(trade);
                tradesByOrderId[sellOrder.orderId].push_back(trade);
                lastPrice = lowestPrice;
                
                // Check if any stop orders should be triggered by this trade
                checkandTrigger(lastPrice);

                // Update sell order status and remove if completely filled
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
                break; // Price not favorable, cancel remaining quantity
            }
        }
    } else {
        // Sell IOC order - match against buy orders at or above specified price
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

                // Create and record the trade
                Trade trade(buyOrder.orderId, orderId, highestPrice, tradeQty);
                tradeLog.push_back(trade);
                tradesByOrderId[orderId].push_back(trade);
                tradesByOrderId[buyOrder.orderId].push_back(trade);
                lastPrice = highestPrice;
                
                // Check if any stop orders should be triggered by this trade
                checkandTrigger(lastPrice);

                // Update buy order status and remove if completely filled
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
                break; // Price not favorable, cancel remaining quantity
            }
        }
    }

    // Update IOC order status based on execution
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

/**
 * Add a Fill-or-Kill (FOK) order
 * 
 * FOK orders must be filled in their entirety immediately or canceled completely.
 * They will not execute if the full quantity cannot be filled at or better than
 * the specified price.
 * 
 * @param orderId Unique identifier for this order
 * @param price The maximum (buy) or minimum (sell) price for execution
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addFOKOrder(int orderId, double price, int quantity, bool isBuy) {
    // Validate order parameters
    if (price <= 0 || quantity <= 0) {
        throw OrderException("Invalid parameters for FOK order");
    }

    // Create FOK order and store in allOrders
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
        // For buy FOK, check if there are enough sell orders at or below the specified price
        for (auto sellIt = sellOrders.begin(); sellIt != sellOrders.end() && sellIt->first <= price; ++sellIt) {
            for (const auto& order : sellIt->second) {
                availableQty += order.getRemainingQuantity();
                if (availableQty >= quantity) break;
            }
            if (availableQty >= quantity) break;
        }
    } else {
        // For sell FOK, check if there are enough buy orders at or above the specified price
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

    // Execute the full quantity since we've verified it can be filled
    int remainingQty = quantity;
    
    if (isBuy) {
        // Buy FOK order - match against sell orders at or below specified price
        while (remainingQty > 0) {
            auto& sellQueue = sellOrders.begin()->second;
            Order& sellOrder = sellQueue.front();
            double execPrice = sellOrders.begin()->first;

            // Double-check price is still favorable (in case another thread changed the book)
            if (execPrice > price) {
                // Price moved unfavorably
                setOrderStatus(orderPtr, OrderStatus::CANCELED);
                std::cout << "[FOK Order] OrderID: " << orderId 
                         << " canceled - price moved unfavorably" << std::endl;
                return;
            }

            int tradeQty = std::min(remainingQty, sellOrder.getRemainingQuantity());
            
            // Create and record the trade
            Trade trade(orderId, sellOrder.orderId, execPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            // Update quantities and last traded price
            remainingQty -= tradeQty;
            sellOrder.filledQuantity += tradeQty;
            orderPtr->filledQuantity += tradeQty;
            lastPrice = execPrice;
            
            // Check if any stop orders should be triggered by this trade
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

            // Remove sell order if completely filled
            if (sellOrder.isComplete()) {
                orderMap.erase(sellOrder.orderId);
                sellQueue.pop_front();
                if (sellQueue.empty()) {
                    sellOrders.erase(sellOrders.begin());
                }
            }
        }
    } else {
        // Sell FOK order - match against buy orders at or above specified price
        while (remainingQty > 0) {
            auto& buyQueue = buyOrders.begin()->second;
            Order& buyOrder = buyQueue.front();
            double execPrice = buyOrders.begin()->first;

            // Double-check price is still favorable
            if (execPrice < price) {
                // Price moved unfavorably
                setOrderStatus(orderPtr, OrderStatus::CANCELED);
                std::cout << "[FOK Order] OrderID: " << orderId 
                         << " canceled - price moved unfavorably" << std::endl;
                return;
            }

            int tradeQty = std::min(remainingQty, buyOrder.getRemainingQuantity());
            
            // Create and record the trade
            Trade trade(buyOrder.orderId, orderId, execPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[orderId].push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);

            // Update quantities and last traded price
            remainingQty -= tradeQty;
            buyOrder.filledQuantity += tradeQty;
            orderPtr->filledQuantity += tradeQty;
            lastPrice = execPrice;
            
            // Check if any stop orders should be triggered by this trade
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

            // Remove buy order if completely filled
            if (buyOrder.isComplete()) {
                orderMap.erase(buyOrder.orderId);
                buyQueue.pop_front();
                if (buyQueue.empty()) {
                    buyOrders.erase(buyOrders.begin());
                }
            }
        }
    }
    
    // FOK is fully filled at this point
    setOrderStatus(orderPtr, OrderStatus::FILLED);
    std::cout << "[FOK Order] OrderID: " << orderId << " fully filled" << std::endl;
}

/**
 * Add a Good-Till-Canceled (GTC) order
 * 
 * GTC orders remain active in the order book until they are explicitly canceled.
 * They do not expire automatically.
 * 
 * @param orderId Unique identifier for this order
 * @param price The limit price for this order
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addGTCOrder(int orderId, double price , int quantity, bool isBuy){
    // Validate order parameters
    if(quantity <= 0 || price <= 0) {
        std::cerr << "[addGTCOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[GTC Order] OrderID: " << orderId
              << ", Price: " << price
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;
    
    // Create GTC order and store in allOrders and appropriate order book
    Order gtcOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::GTC);
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

/**
 * Add a Good-Till-Date (GTD) order
 * 
 * GTD orders remain active in the order book until they are either filled,
 * canceled explicitly, or the specified expiry time is reached.
 * 
 * @param orderId Unique identifier for this order
 * @param price The limit price for this order
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 * @param expiry The timestamp when this order should expire
 */
void MatchingEngine::addGTDOrder(int orderId, double price, int quantity, bool isBuy, std::chrono::system_clock::time_point expiry){
    // Validate order parameters
    if(quantity <= 0 || price <= 0) {
        std::cerr << "[addGTDOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[GTD Order] OrderID: " << orderId
              << ", Price: " << price
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") 
              << ", Expiry: " << std::chrono::system_clock::to_time_t(expiry) << std::endl;
    
    // Create GTD order and store in allOrders and appropriate order book
    Order gtdOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::GTD, expiry);
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

/**
 * Add a Stop order
 * 
 * Stop orders are initially inactive and become market orders once the market price
 * reaches or crosses the specified stop price. For buy stops, they trigger when price
 * rises to or above the stop price. For sell stops, they trigger when price falls
 * to or below the stop price.
 * 
 * @param orderId Unique identifier for this order
 * @param stopPrice The price at which the stop order is triggered
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addStopOrder(int orderId, double stopPrice, int quantity, bool isBuy){
    // Validate order parameters
    if(quantity <= 0 || stopPrice <= 0){
        std::cerr << "[addStopOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[Stop Order] OrderID: " << orderId
              << ", Stop Price: " << stopPrice
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;
    
    // Create Stop order and store in allOrders and appropriate stop order book
    Order stopOrder(orderId, std::nullopt, quantity, isBuy, OrderType::STOP, 
                    std::nullopt, std::optional<double>{stopPrice});
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
    // Order is set to inactive by default in the constructor
}

/**
 * Check and trigger stop orders based on the last traded price
 * 
 * This function is called after each trade to check if any stop orders
 * should be triggered based on the new market price.
 * 
 * @param lastprice The most recent trade price
 */
void MatchingEngine::checkandTrigger(double lastprice){
    std::cout<<"[checkandTrigger] Last Price: "<<lastprice<<std::endl;
    
    // Check buy stop orders (trigger when price rises above stop price)
    auto it = buyStopOrders.begin();
    while(it != buyStopOrders.end()){
        auto& orderQueue = it->second;
        Order& stopOrder = orderQueue.front();
        if(stopOrder.stopPrice && lastprice >= stopOrder.stopPrice.value()){
            stopOrder.status = OrderStatus::TRIGGERED;
            if(stopOrder.type == OrderType::STOP){
                std::cout<<"[checkandTrigger] Buy Stop OrderID: "<<stopOrder.orderId
                         <<" triggered at price "<<lastprice<<std::endl;
                // Don't erase from orderMap here - convertStopToMarket handles this
                convertStopToMarket(&stopOrder);
            }
            else if(stopOrder.type == OrderType::STOPLIMIT){
                std::cout<<"[checkandTrigger] Buy Stop Limit OrderID: "<<stopOrder.orderId
                         <<" triggered at price "<<lastprice<<std::endl;
                // Don't erase from orderMap here - convertStopToLimit handles this
                convertStopToLimit(&stopOrder);
            }
            // Remove from stop orders queue regardless of conversion
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
    
    // Check sell stop orders (trigger when price falls below stop price)
    it = sellStopOrders.begin();
    while(it != sellStopOrders.end()){
        auto& orderQueue = it->second;
        Order& stopOrder = orderQueue.front();
        if(stopOrder.stopPrice && lastprice <= stopOrder.stopPrice.value()){
            stopOrder.status = OrderStatus::TRIGGERED;
            if(stopOrder.type == OrderType::STOP){
                std::cout<<"[checkandTrigger] Sell Stop OrderID: "<<stopOrder.orderId
                         <<" triggered at price "<<lastprice<<std::endl;
                // Don't erase from orderMap here - convertStopToMarket handles this
                convertStopToMarket(&stopOrder);
            }
            else if(stopOrder.type == OrderType::STOPLIMIT){
                std::cout<<"[checkandTrigger] Sell Stop Limit OrderID: "<<stopOrder.orderId
                         <<" triggered at price "<<lastprice<<std::endl;
                // Don't erase from orderMap here - convertStopToLimit handles this
                convertStopToLimit(&stopOrder);
            }
            // Remove from stop orders queue regardless of conversion
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
}

/**
 * Add a Stop Limit order
 * 
 * Stop Limit orders become limit orders once the market price reaches or crosses
 * the specified stop price. The order is then placed in the book at the limit price.
 * 
 * @param orderId Unique identifier for this order
 * @param stopPrice The price at which the stop order is triggered
 * @param limitPrice The limit price once the order is activated
 * @param quantity The number of shares/contracts to buy or sell
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addStopLimitOrder(int orderId, double stopPrice, double limitPrice, int quantity, bool isBuy) {
    // Validate order parameters
    if (quantity <= 0 || stopPrice <= 0 || limitPrice <= 0) {
        std::cerr << "[addStopLimitOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout << "[Stop Limit Order] OrderID: " << orderId
              << ", Stop Price: " << stopPrice
              << ", Limit Price: " << limitPrice
              << ", Qty: " << quantity
              << ", Side: " << (isBuy ? "Buy" : "Sell") << std::endl;
    
    // Create Stop Limit order and store in allOrders and appropriate stop order book
    Order stopLimitOrder(orderId, std::optional<double>{limitPrice}, quantity, isBuy, OrderType::STOPLIMIT, 
                         std::nullopt, std::optional<double>{stopPrice});
    allOrders.push_back(stopLimitOrder);
    if(isBuy){
        buyStopOrders[stopPrice].push_back(stopLimitOrder);
        auto& orderQueue = buyStopOrders[stopPrice];
        orderMap[orderId] = &orderQueue.back();
    }
    else{
        sellStopOrders[stopPrice].push_back(stopLimitOrder);
        auto& orderQueue = sellStopOrders[stopPrice];
        orderMap[orderId] = &orderQueue.back();
    }
}

/**
 * Convert a triggered Stop order to a Market order
 * 
 * This function is called when a Stop order is triggered and converts
 * it to a Market order for immediate execution.
 * 
 * @param order Pointer to the Stop order to be converted
 */
void MatchingEngine::convertStopToMarket(Order* order) {
    if (order->status == OrderStatus::TRIGGERED) {
        std::cout << "[convertStopToMarket] Converting Stop OrderID: " << order->orderId << " to Market Order." << std::endl;
        
        order->type = OrderType::MARKET;  // Change the type to MARKET
        order->price = std::nullopt;      // No price for market orders
        
        // We'll keep track of the original quantity filled so far
        int originalFilledQty = order->filledQuantity;
        
        // Now execute as market order without creating a new order
        int orderId = order->orderId;
        int quantity = order->quantity - originalFilledQty;
        bool isBuy = order->isBuy;
        
        // Remove from orderMap temporarily to avoid double-processing
        orderMap.erase(orderId);
        
        // Call addMarketOrder with a flag to indicate this is a converted order
        // This avoids creating a new order in allOrders
        MatchingEngine::addMarketOrder(orderId, quantity, isBuy, true, order);
    }
}

/**
 * Convert a triggered Stop Limit order to a Limit order
 * 
 * This function is called when a Stop Limit order is triggered and
 * converts it to a regular Limit order at the specified limit price.
 * 
 * @param order Pointer to the Stop Limit order to be converted
 */
void MatchingEngine::convertStopToLimit(Order* order) {
    if (order->status == OrderStatus::TRIGGERED) {
        std::cout << "[convertStopToLimit] Converting Stop Limit OrderID: " << order->orderId 
                  << " to Limit Order at price: " << order->stopPrice.value() << std::endl;

        // Store original values we need to preserve
        int orderId = order->orderId;
        int quantity = order->quantity;
        int filledQuantity = order->filledQuantity;
        bool isBuy = order->isBuy;
        double limitPrice = order->stopPrice.value();
        
        // Update the order type and prices
        order->type = OrderType::LIMIT;
        order->price = order->stopPrice;
        order->stopPrice = std::nullopt;
        
        // Remove from orderMap temporarily
        orderMap.erase(orderId);
        
        // Instead of creating a new order, add this order to the appropriate book
        if (isBuy) {
            buyOrders[limitPrice].push_back(*order);
            auto& orderQueue = buyOrders[limitPrice];
            Order* orderPtr = &orderQueue.back();
            orderPtr->filledQuantity = filledQuantity; // Preserve filled quantity
            orderMap[orderId] = orderPtr;
        } else {
            sellOrders[limitPrice].push_back(*order);
            auto& orderQueue = sellOrders[limitPrice];
            Order* orderPtr = &orderQueue.back();
            orderPtr->filledQuantity = filledQuantity; // Preserve filled quantity
            orderMap[orderId] = orderPtr;
        }
        
        // Update status to active
        setOrderStatus(orderMap[orderId], OrderStatus::ACTIVE);
    }
}

/**
 * Add an Iceberg order
 * 
 * Iceberg orders allow traders to submit large orders while only displaying a
 * portion of the total quantity to the market. As the visible portion is filled,
 * it is automatically replenished from the hidden quantity.
 * 
 * @param orderId Unique identifier for this order
 * @param price The limit price for this order
 * @param quantity The total number of shares/contracts to buy or sell
 * @param visibleQuantity The quantity to display in the order book
 * @param replenishQuantity The quantity to replenish when visible portion is filled
 * @param isBuy True for buy orders, false for sell orders
 */
void MatchingEngine::addIcebergOrder(int orderId, double price , int quantity, int visibleQuantity, int replenishQuantity , bool isBuy){
    // Validate order parameters
    if(quantity <= 0 || visibleQuantity <= 0 || price <= 0 || replenishQuantity <= 0){
        std::cerr << "[addIcebergOrder] Invalid Order: please make sure price and quantity are bigger than 0." << std::endl;
        return;
    }
    std::cout<<"[Iceberg Order] OrderID: "<<orderId
                <<" , Price: "<<price
                <<" , Total Qty: "<<quantity
                <<" , Visible Qty: "<<visibleQuantity
                <<" , Replenish Qty: "<<replenishQuantity
                <<", Side: "<<(isBuy ? "Buy" : "Sell") << std::endl;
                
    // Create Iceberg order and store in allOrders and appropriate order book
    Order icebergOrder(orderId, std::optional<double>{price}, quantity, isBuy, OrderType::ICE, 
                    std::nullopt, std::nullopt, 
                    std::optional<int>{visibleQuantity}, 
                    std::optional<int>{replenishQuantity});
    allOrders.push_back(icebergOrder);
    if(isBuy){
        buyOrders[price].push_back(icebergOrder);
        auto& orderQueue = buyOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }
    else{
        sellOrders[price].push_back(icebergOrder);
        auto& orderQueue = sellOrders[price];
        orderMap[orderId] = &orderQueue.back();
    }
}

/**
 * Replenish the visible quantity of an Iceberg order
 * 
 * This function is called when the visible portion of an Iceberg order
 * is filled and needs to be replenished from the hidden quantity.
 * 
 * @param order Pointer to the Iceberg order to be replenished
 */
void MatchingEngine::replenishIcebergOrder(Order* order) {
    if (order->visibleQuantity && order->replenishQuantity) {
        int remainingQty = order->getRemainingQuantity();
        int replenishQty = order->replenishQuantity.value();

        // Always replenish and log if there's remaining quantity
        if (remainingQty > 0) {
            int newVisibleQty = std::min(replenishQty, remainingQty);
            order->visibleQuantity = newVisibleQty;
            
            // Make sure to update the copy in orderMap
            orderMap[order->orderId] = order;
           
            std::cout << "[Replenish Iceberg Order] OrderID: " << order->orderId 
                      << " replenished to " << newVisibleQty << " visible shares "
                      << "(hidden: " << (remainingQty - newVisibleQty) << ")" << std::endl;
        }
    }
}
/**
 * Print the trade log
 * 
 * Prints all trades that have occurred in the matching engine.
 */
void MatchingEngine::printTradelog(){
    for (const auto& trade : tradeLog) {
        std::cout << trade << '\n';
    }
}

/**
 * Cancel an existing order
 * 
 * Removes the specified order from the order book and updates its status to CANCELED.
 * 
 * @param orderId Unique identifier of the order to cancel
 */
void MatchingEngine::cancelOrder(int orderId) {
    auto it = orderMap.find(orderId);
    if (it != orderMap.end()) {
        Order* order = it->second;
        if(order->status == OrderStatus::FILLED) {
            std::cout << "[cancelOrder] OrderID: " << orderId << " is already filled." << std::endl;
            return;
        }
        
        order->status = OrderStatus::CANCELED;
        
        // Only attempt to remove from order books if the order has a price
        if (order->price.has_value()) {
            if (order->isBuy) {
                // Remove from buyOrders map
                auto& orderQueue = buyOrders[order->price.value()];
                orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                               [orderId](const Order& o) { return o.orderId == orderId; }),
                               orderQueue.end());
                
                // Clean up empty price levels
                if (orderQueue.empty()) {
                    buyOrders.erase(order->price.value());
                }
            } else {
                // Remove from sellOrders map
                auto& orderQueue = sellOrders[order->price.value()];
                orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                               [orderId](const Order& o) { return o.orderId == orderId; }),
                               orderQueue.end());
                
                // Clean up empty price levels
                if (orderQueue.empty()) {
                    sellOrders.erase(order->price.value());
                }
            }
        } else if (order->stopPrice.has_value()) {
            // Handle stop orders
            if (order->isBuy) {
                auto& orderQueue = buyStopOrders[order->stopPrice.value()];
                orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                               [orderId](const Order& o) { return o.orderId == orderId; }),
                               orderQueue.end());
                
                if (orderQueue.empty()) {
                    buyStopOrders.erase(order->stopPrice.value());
                }
            } else {
                auto& orderQueue = sellStopOrders[order->stopPrice.value()];
                orderQueue.erase(std::remove_if(orderQueue.begin(), orderQueue.end(),
                                               [orderId](const Order& o) { return o.orderId == orderId; }),
                               orderQueue.end());
                
                if (orderQueue.empty()) {
                    sellStopOrders.erase(order->stopPrice.value());
                }
            }
        }
        
        orderMap.erase(it);
        std::cout << "[cancelOrder] OrderID: " << orderId << " has been canceled." << std::endl;
    } else {
        std::cout << "[cancelOrder] OrderID: " << orderId << " not found." << std::endl;
    }
}

/**
 * Print a snapshot of the current order book
 * 
 * Displays all orders currently in the book, organized by price level,
 * with sell orders shown from highest to lowest price and buy orders
 * shown from highest to lowest price.
 */
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

/**
 * Main matching algorithm for the engine
 * 
 * Attempts to match orders in the book based on price-time priority.
 * Continues matching until no more matches are possible.
 */
void MatchingEngine::matchOrders() {
    std::cout << "[matchOrders] Attempting to match orders..." << std::endl;
    bool matchFound = true;

    while (matchFound) {
        matchFound = false;

        if (buyOrders.empty() || sellOrders.empty()) {
            break;
        }

        // Get highest buy price and lowest sell price
        double highestBuyPrice = buyOrders.begin()->first;
        double lowestSellPrice = sellOrders.begin()->first;

        // If highest buy price >= lowest sell price, we have a match
        if (highestBuyPrice >= lowestSellPrice) {
            auto buyIt = buyOrders.begin();
            auto sellIt = sellOrders.begin();
            auto& buyQueue = buyIt->second;
            auto& sellQueue = sellIt->second;
            Order& buyOrder = buyQueue.front();
            Order& sellOrder = sellQueue.front();

            // Check for expired GTD orders before matching
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

            // Calculate trade quantity based on order type
            int tradeQty = 0;
            if(buyOrder.type == OrderType::ICE && buyOrder.visibleQuantity.has_value() && buyOrder.replenishQuantity.has_value()) {
                 tradeQty = std::min(buyOrder.visibleQuantity.value(), sellOrder.getRemainingQuantity()); 
            }
            else if(sellOrder.type == OrderType::ICE && sellOrder.visibleQuantity.has_value() && sellOrder.replenishQuantity.has_value()) {
                 tradeQty = std::min(sellOrder.visibleQuantity.value(), buyOrder.getRemainingQuantity()); 
            }
            else {
                 tradeQty = std::min(buyOrder.getRemainingQuantity(), sellOrder.getRemainingQuantity());
            }

            std::cout << "[MATCH] BuyOrder " << buyOrder.orderId 
                      << " matched with SellOrder " << sellOrder.orderId
                      << " at price " << lowestSellPrice
                      << " for quantity " << tradeQty << std::endl;

            // Create and record the trade
            Trade trade(buyOrder.orderId, sellOrder.orderId, lowestSellPrice, tradeQty);
            tradeLog.push_back(trade);
            tradesByOrderId[buyOrder.orderId].push_back(trade);
            tradesByOrderId[sellOrder.orderId].push_back(trade);

            // Update order quantities and last traded price
            buyOrder.filledQuantity += tradeQty;
            sellOrder.filledQuantity += tradeQty;
            lastPrice = lowestSellPrice;
            
            // Check if any stop orders should be triggered by this trade
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
            
            // Handle iceberg order replenishment
            if (buyOrder.type == OrderType::ICE && buyOrder.getRemainingQuantity() > 0) {
                // If this was a partial fill and visible qty is now depleted
                if (buyOrder.visibleQuantity.value() <= tradeQty) {
                    replenishIcebergOrder(&buyOrder);
                } else {
                    // Decrement visible quantity
                    buyOrder.visibleQuantity = buyOrder.visibleQuantity.value() - tradeQty;
                }
            }
            if (sellOrder.type == OrderType::ICE && sellOrder.getRemainingQuantity() > 0) {
                // If this was a partial fill and visible qty is now depleted
                if (sellOrder.visibleQuantity.value() <= tradeQty) {
                    replenishIcebergOrder(&sellOrder);
                } else {
                    // Decrement visible quantity
                    sellOrder.visibleQuantity = sellOrder.visibleQuantity.value() - tradeQty;
                }
            }

            // Remove completed orders from the book
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

/**
 * Update the status of an order
 * 
 * Changes the status of the specified order and logs the change.
 * 
 * @param order Pointer to the order to update
 * @param newStatus The new status to set
 */
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

/**
 * Get the current status of an order
 * 
 * Retrieves the status of the specified order by ID.
 * 
 * @param orderId The ID of the order to check
 * @return The current status of the order
 */
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

/**
 * Check for expired GTD orders
 * 
 * Scans the order book for any GTD orders that have reached their
 * expiration time and removes them.
 */
void MatchingEngine::checkExpiredOrders() {
    std::cout << "[checkExpiredOrders] Checking for expired GTD orders..." << std::endl;
    
    auto currentTime = std::chrono::system_clock::now();
    bool expiredOrdersFound = false;
    
    // Check buy orders
    for (auto buyIt = buyOrders.begin(); buyIt != buyOrders.end();) {
        auto& orderQueue = buyIt->second;
        auto orderIt = orderQueue.begin();
        
        while (orderIt != orderQueue.end()) {
            if (orderIt->type == OrderType::GTD && orderIt->expiry.has_value() && 
                currentTime > orderIt->expiry.value()) {
                
                std::cout << "[GTD ORDER] OrderID: " << orderIt->orderId
                          << " expired at " << std::chrono::system_clock::to_time_t(orderIt->expiry.value()) << std::endl;
                
                Order* orderPtr = &(*orderIt);
                setOrderStatus(orderPtr, OrderStatus::EXPIRED);
                orderMap.erase(orderIt->orderId);
                
                orderIt = orderQueue.erase(orderIt);
                expiredOrdersFound = true;
            } else {
                ++orderIt;
            }
        }
        
        if (orderQueue.empty()) {
            buyIt = buyOrders.erase(buyIt);
        } else {
            ++buyIt;
        }
    }
    
    // Check sell orders
    for (auto sellIt = sellOrders.begin(); sellIt != sellOrders.end();) {
        auto& orderQueue = sellIt->second;
        auto orderIt = orderQueue.begin();
        
        while (orderIt != orderQueue.end()) {
            if (orderIt->type == OrderType::GTD && orderIt->expiry.has_value() && 
                currentTime > orderIt->expiry.value()) {
                
                std::cout << "[GTD ORDER] OrderID: " << orderIt->orderId
                          << " expired at " << std::chrono::system_clock::to_time_t(orderIt->expiry.value()) << std::endl;
                
                Order* orderPtr = &(*orderIt);
                setOrderStatus(orderPtr, OrderStatus::EXPIRED);
                orderMap.erase(orderIt->orderId);
                
                orderIt = orderQueue.erase(orderIt);
                expiredOrdersFound = true;
            } else {
                ++orderIt;
            }
        }
        
        if (orderQueue.empty()) {
            sellIt = sellOrders.erase(sellIt);
        } else {
            ++sellIt;
        }
    }
    
    if (!expiredOrdersFound) {
        std::cout << "[checkExpiredOrders] No expired orders found." << std::endl;
    }
}
} // namespace ultraBook
// engine.cpp
