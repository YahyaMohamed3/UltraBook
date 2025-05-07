#include "engine.hpp"
#include <iostream>
#include <algorithm>
#include <thread>
#include "types.hpp"
#include <oneapi/tbb/concurrent_unordered_map.h>

namespace ultraBook {

MatchingEngine::MatchingEngine() {
    // Initialize atomic price cache
    bestBidPrice.store(0.0, std::memory_order_release);
    bestAskPrice.store(std::numeric_limits<double>::max(), std::memory_order_release);
    
    // Initialize SIMD price levels with zero
    __m256d zeroPrices = _mm256_setzero_pd();
    __m256i zeroQtys = _mm256_setzero_si256();
    
    _mm256_store_pd(reinterpret_cast<double*>(&bestBuyPrices.prices), zeroPrices);
    _mm256_store_pd(reinterpret_cast<double*>(&bestSellPrices.prices), zeroPrices);
    
    lastUpdateTime_.store(getCurrentTimestamp(), std::memory_order_release);
}

MatchingEngine::~MatchingEngine() {
    // Process any remaining trades
    processTradeQueue();
}

void MatchingEngine::addLimitOrder(int orderId, double price, int quantity, bool isBuy) {
    uint64_t startTime = getCurrentTimestamp();
    
    try {
        // Validate inputs
        if (price <= 0.0 || quantity <= 0) {
            throw OrderException("Invalid price or quantity");
        }
        
        // Create order using memory pool
        Order* order = orderPool.allocate(orderId, price, quantity, isBuy, OrderType::LIMIT);
        if (!order) {
            throw OrderException("Failed to allocate order");
        }
        
        // Add to order map and update metrics
        orderMap.insert({orderId, order});
        metrics_.totalOrders.fetch_add(1, std::memory_order_relaxed);
        
        // Update price level cache for SIMD matching
        updatePriceLevelCache(price, quantity, isBuy);
        
        // Add to appropriate order book
        if (isBuy) {
            auto& level = buyOrders[price];
            if (!level) {
                level = std::make_unique<LockFreeLevel<Order>>(price);
            }
            level->addOrder(order);
            
            double currentBest = bestBidPrice.load(std::memory_order_acquire);
            if (price > currentBest) {
                bestBidPrice.store(price, std::memory_order_release);
            }
        } else {
            auto& level = sellOrders[price];
            if (!level) {
                level = std::make_unique<LockFreeLevel<Order>>(price);
            }
            level->addOrder(order);
            
            double currentBest = bestAskPrice.load(std::memory_order_acquire);
            if (price < currentBest) {
                bestAskPrice.store(price, std::memory_order_release);
            }
        }
        
        // Try matching
        matchOrders();
        
        // Update performance metrics
        uint64_t endTime = getCurrentTimestamp();
        uint64_t latency = (endTime - startTime) * 1000000000ULL / getCpuFrequency();
        metrics_.avgLatencyNs.store(
            (metrics_.avgLatencyNs.load(std::memory_order_relaxed) + latency) / 2,
            std::memory_order_relaxed
        );
        
    } catch (const std::exception& e) {
        std::cerr << "Error adding limit order: " << e.what() << std::endl;
        throw;
    }
}

void MatchingEngine::matchOrders() {
    // Fast path check using atomic price cache
    double bid = bestBidPrice.load(std::memory_order_acquire);
    double ask = bestAskPrice.load(std::memory_order_acquire);
    
    if (bid <= 0.0 || ask >= std::numeric_limits<double>::max() || bid < ask) {
        return;  // No matches possible
    }

    // Try batch matching first
    batchMatchOrders();
    
    // Continue with individual matches
    while (true) {
        auto buyIt = buyOrders.begin();
        auto sellIt = sellOrders.begin();
        
        if (buyIt == buyOrders.end() || sellIt == sellOrders.end()) {
            break;
        }
        
        if (buyIt->first < sellIt->first) {
            break;
        }
        
        auto buyLevel = buyIt->second.get();
        auto sellLevel = sellIt->second.get();
        
        if (!buyLevel || !sellLevel) {
            break;
        }
        
        Order* buyOrder = buyLevel->getFirstOrder();
        Order* sellOrder = sellLevel->getFirstOrder();
        
        if (!buyOrder || !sellOrder) {
            break;
        }
        
        int tradeQty = std::min(buyOrder->getRemainingQuantity(),
                               sellOrder->getRemainingQuantity());
        
        if (tradeQty > 0) {
            executeTrade(buyOrder, sellOrder, sellIt->first, tradeQty);
            
            if (buyOrder->getRemainingQuantity() == 0) {
                buyLevel->removeFirstOrder();
                if (buyLevel->empty()) {
                    double price = buyIt->first;
                    buyOrders[price].reset();  // Reset the unique_ptr
                    updateBestBidPrice();
                }
            }
            
            if (sellOrder->getRemainingQuantity() == 0) {
                sellLevel->removeFirstOrder();
                if (sellLevel->empty()) {
                    double price = sellIt->first;
                    sellOrders[price].reset();  // Reset the unique_ptr
                    updateBestAskPrice();
                }
            }
        } else {
            break;
        }
    }
}

void MatchingEngine::batchMatchOrders() {
    alignas(32) double buyPrices[4];
    alignas(32) double sellPrices[4];
    alignas(32) int buyQtys[4];
    alignas(32) int sellQtys[4];
    
    // Load top 4 price levels into SIMD registers
    int levelCount = 0;
    for (auto it = buyOrders.begin(); it != buyOrders.end() && levelCount < 4; ++it, ++levelCount) {
        buyPrices[levelCount] = it->first;
        buyQtys[levelCount] = it->second->getTotalQuantity();
    }
    
    levelCount = 0;
    for (auto it = sellOrders.begin(); it != sellOrders.end() && levelCount < 4; ++it, ++levelCount) {
        sellPrices[levelCount] = it->first;
        sellQtys[levelCount] = it->second->getTotalQuantity();
    }
    
    // Load into SIMD registers
    __m256d buyPricesVec = _mm256_load_pd(buyPrices);
    __m256d sellPricesVec = _mm256_load_pd(sellPrices);
    __m256i buyQtysVec = _mm256_load_si256(reinterpret_cast<const __m256i*>(buyQtys));
    __m256i sellQtysVec = _mm256_load_si256(reinterpret_cast<const __m256i*>(sellQtys));
    
    // Compare prices (buy >= sell)
    __m256d matchMask = _mm256_cmp_pd(buyPricesVec, sellPricesVec, _CMP_GE_OQ);
    int matches = _mm256_movemask_pd(matchMask);
    
    if (matches) {
        // Process matches
        for (int i = 0; i < 4; ++i) {
            if (matches & (1 << i)) {
                auto buyIt = buyOrders.find(buyPrices[i]);
                auto sellIt = sellOrders.find(sellPrices[i]);
                
                if (buyIt != buyOrders.end() && sellIt != sellOrders.end()) {
                    matchPriceLevels(buyIt->second.get(), sellIt->second.get(), sellPrices[i]);
                }
            }
        }
    }
}

void MatchingEngine::matchPriceLevels(LockFreeLevel<Order>* buyLevel, LockFreeLevel<Order>* sellLevel, double price) {
    if (!buyLevel || !sellLevel) return;
    
    Order* buyOrder;
    Order* sellOrder;
    
    while ((buyOrder = buyLevel->getFirstOrder()) && (sellOrder = sellLevel->getFirstOrder())) {
        int tradeQty = std::min(buyOrder->getRemainingQuantity(), sellOrder->getRemainingQuantity());
        if (tradeQty > 0) {
            executeTrade(buyOrder, sellOrder, price, tradeQty);
            
            if (buyOrder->getRemainingQuantity() == 0) {
                buyLevel->removeFirstOrder();
            }
            if (sellOrder->getRemainingQuantity() == 0) {
                sellLevel->removeFirstOrder();
            }
        } else {
            break;
        }
    }
}

void MatchingEngine::executeTrade(Order* buyOrder, Order* sellOrder, double price, int quantity) {
    // Update order quantities
    buyOrder->filledQuantity += quantity;
    sellOrder->filledQuantity += quantity;
    
    // Create trade record
    if (!tradeQueue.push(buyOrder->orderId, sellOrder->orderId, price, quantity)) {
        processTradeQueue();  // Queue full, process some trades
        tradeQueue.push(buyOrder->orderId, sellOrder->orderId, price, quantity);
    }
    
    // Update order statuses
    if (buyOrder->getRemainingQuantity() == 0) {
        buyOrder->status = OrderStatus::FILLED;
    } else {
        buyOrder->status = OrderStatus::PARTIALLY_FILLED;
    }
    
    if (sellOrder->getRemainingQuantity() == 0) {
        sellOrder->status = OrderStatus::FILLED;
    } else {
        sellOrder->status = OrderStatus::PARTIALLY_FILLED;
    }
    
    // Update metrics
    metrics_.totalTrades.fetch_add(1, std::memory_order_relaxed);
}

void MatchingEngine::updateOrderStatus(Order* order) {
    if (order->getRemainingQuantity() == 0) {
        setOrderStatus(order, OrderStatus::FILLED);
    } else if (order->filledQuantity > 0) {
        setOrderStatus(order, OrderStatus::PARTIALLY_FILLED);
    }
}

void MatchingEngine::setOrderStatus(Order* order, OrderStatus newStatus) {
    if (!order) return;
    
    OrderStatus oldStatus = order->status;
    order->status = newStatus;
    
    if (oldStatus != newStatus) {
        std::cout << "[Status Update] OrderID: " << order->orderId 
                  << " Status changed from " << oldStatus 
                  << " to " << newStatus << std::endl;
    }
}

void MatchingEngine::updateBestPrices() {
    if (!buyOrders.empty()) {
        bestBidPrice.store(buyOrders.begin()->first, std::memory_order_release);
    } else {
        bestBidPrice.store(0.0, std::memory_order_release);
    }
    
    if (!sellOrders.empty()) {
        bestAskPrice.store(sellOrders.begin()->first, std::memory_order_release);
    } else {
        bestAskPrice.store(std::numeric_limits<double>::max(), std::memory_order_release);
    }
    _mm_sfence();
}

void MatchingEngine::updateBestBidPrice() {
    double newBid = buyOrders.empty() ? 0.0 : buyOrders.begin()->first;
    bestBidPrice.store(newBid, std::memory_order_release);
}

void MatchingEngine::updateBestAskPrice() {
    double newAsk = sellOrders.empty() ? 
        std::numeric_limits<double>::max() : 
        sellOrders.begin()->first;
    bestAskPrice.store(newAsk, std::memory_order_release);
}

void MatchingEngine::updatePriceLevelCache(double price, int quantity, bool isBuy) {
    alignas(32) double prices[4] = {price, price, price, price};
    alignas(32) int quantities[4] = {quantity, quantity, quantity, quantity};
    
    auto& cache = isBuy ? bestBuyPrices : bestSellPrices;
    _mm256_store_pd(reinterpret_cast<double*>(&cache.prices), _mm256_load_pd(prices));
}

uint64_t MatchingEngine::getCurrentTimestamp() const {
    return rdtsc();
}

uint64_t MatchingEngine::getCpuFrequency() {
    static const uint64_t freq = []() {
        uint64_t start = rdtsc();
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        return (rdtsc() - start) * 10;  // Frequency in Hz
    }();
    return freq;
}

uint64_t MatchingEngine::getLatencyNs(uint64_t startTime) const {
    return (rdtsc() - startTime) * 1000000000ULL / getCpuFrequency();
}

void MatchingEngine::processTradeQueue() {
    Trade* trade = nullptr;
    while (tradeQueue.try_pop(trade)) {
        if (trade) {
            tradeLog.push_back(*trade);
        }
    }
}

void MatchingEngine::printOrderBook() const {
    std::cout << "\nOrder Book:\n";
    std::cout << "Bids:\n";
    for (const auto& [price, level] : buyOrders) {
        std::cout << price << ": " << level->getTotalQuantity() << "\n";
    }
    std::cout << "\nAsks:\n";
    for (const auto& [price, level] : sellOrders) {
        std::cout << price << ": " << level->getTotalQuantity() << "\n";
    }
    std::cout << std::endl;
}

} // namespace ultraBook