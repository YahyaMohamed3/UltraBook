#ifndef TYPES_HPP
#define TYPES_HPP

#include <chrono>
#include <optional>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <vector>
#include <string>
#include <atomic>
#include <memory>
#include <array>
#include <mutex>
#include <shared_mutex>
#include <queue>

// SIMD and intrinsics includes
#ifdef _MSC_VER
    #include <intrin.h>
#else
    #include <immintrin.h>
#endif

// Add tbb for concurrent containers
#include <tbb/concurrent_unordered_map.h>
#include <tbb/concurrent_queue.h>

// Cache line size definition
#ifndef CACHE_LINE_SIZE
#define CACHE_LINE_SIZE 64
#endif

namespace ultraBook {

// Cross-platform CPU prefetch
#ifdef _MSC_VER
    #define CPU_PREFETCH(ptr) _mm_prefetch((const char*)(ptr), _MM_HINT_T0)
#else
    #define CPU_PREFETCH(ptr) __builtin_prefetch(ptr)
#endif

// Cross-platform high-precision timestamp
#ifdef _MSC_VER
inline uint64_t rdtsc() {
    return __rdtsc();
}
#else
inline uint64_t rdtsc() {
    unsigned int lo, hi;
    __asm__ __volatile__ ("rdtsc" : "=a" (lo), "=d" (hi));
    return ((uint64_t)hi << 32) | lo;
}
#endif

// SIMD-aligned price level structure
struct alignas(32) PriceLevel {
    alignas(32) __m256d prices;      // 4 doubles for price levels
    alignas(32) __m256i quantities;  // 4 ints for quantities
    
    void prefetchForRead() const {
        CPU_PREFETCH(this);
        CPU_PREFETCH(&prices);
        CPU_PREFETCH(&quantities);
    }
    
    double getTotalValue() const {
        // Calculate price * quantity using SIMD
        __m256d priceQty = _mm256_mul_pd(
            prices, 
            _mm256_cvtepi32_pd(_mm256_castsi256_si128(quantities))
        );
        
        // Horizontal sum
        __m256d sum = _mm256_hadd_pd(priceQty, priceQty);
        __m128d high = _mm256_extractf128_pd(sum, 1);
        __m128d low = _mm256_castpd256_pd128(sum);
        __m128d total = _mm_add_pd(high, low);
        
        return _mm_cvtsd_f64(total);
    }
};

// Memory pool with SIMD alignment
template<typename T>
class alignas(CACHE_LINE_SIZE) MemoryPool {
private:
    static constexpr size_t POOL_SIZE = 1 << 16;
    static constexpr size_t NUM_CHUNKS = 8;
    
    struct alignas(CACHE_LINE_SIZE) Chunk {
        alignas(32) std::array<T, POOL_SIZE> data;
        alignas(CACHE_LINE_SIZE) std::atomic<size_t> nextFree{0};
        alignas(CACHE_LINE_SIZE) std::atomic<bool> active{true};
    };
    
    alignas(CACHE_LINE_SIZE) std::array<Chunk, NUM_CHUNKS> chunks_{};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> currentChunk_{0};

public:
    MemoryPool() {
        for (auto& chunk : chunks_) {
            chunk.nextFree.store(0, std::memory_order_relaxed);
            chunk.active.store(true, std::memory_order_relaxed);
        }
    }

    template<typename... Args>
    T* allocate(Args&&... args) {
        size_t chunkIdx = currentChunk_.load(std::memory_order_relaxed);
        
        for (size_t i = 0; i < NUM_CHUNKS; ++i) {
            auto& chunk = chunks_[chunkIdx];
            if (chunk.active.load(std::memory_order_acquire)) {
                size_t index = chunk.nextFree.fetch_add(1, std::memory_order_acquire);
                if (index < POOL_SIZE) {
                    return new (&chunk.data[index]) T(std::forward<Args>(args)...);
                }
                chunk.active.store(false, std::memory_order_release);
            }
            chunkIdx = (chunkIdx + 1) % NUM_CHUNKS;
            currentChunk_.store(chunkIdx, std::memory_order_release);
        }
        return nullptr;
    }

    void deallocate(T* ptr) {
        if (!ptr) return;
        ptr->~T();
        // Memory is reclaimed when chunk is reused
    }
};

// Lock-free price level container
template<typename T>
class alignas(CACHE_LINE_SIZE) LockFreeLevel {
private:
    double price_;
    alignas(CACHE_LINE_SIZE) tbb::concurrent_queue<T*> orders_;
    alignas(CACHE_LINE_SIZE) std::atomic<int> totalQuantity_{0};

public:
    explicit LockFreeLevel(double price) : price_(price) {}

    void addOrder(T* order) {
        if (order) {
            orders_.push(order);
            totalQuantity_.fetch_add(order->getRemainingQuantity(), std::memory_order_release);
        }
    }

    T* getFirstOrder() {
        T* order = nullptr;
        orders_.try_pop(order);
        return order;
    }

    void removeFirstOrder() {
        T* order = nullptr;
        if (orders_.try_pop(order) && order) {
            totalQuantity_.fetch_sub(order->getRemainingQuantity(), std::memory_order_release);
        }
    }

    bool empty() const {
        return orders_.empty();
    }

    int getTotalQuantity() const {
        return totalQuantity_.load(std::memory_order_acquire);
    }
};

// Trade structure with atomic operations
struct alignas(CACHE_LINE_SIZE) Trade {
    int buyOrderId;
    int sellOrderId;
    double price;
    int quantity;
    uint64_t timestamp;

    Trade(int buyId, int sellId, double p, int qty) 
        : buyOrderId(buyId), sellOrderId(sellId), price(p), quantity(qty),
          timestamp(rdtsc()) {}

    friend std::ostream& operator<<(std::ostream& os, const Trade& trade) {
        os << "Trade(Buy:" << trade.buyOrderId 
           << " Sell:" << trade.sellOrderId 
           << " Price:" << trade.price 
           << " Qty:" << trade.quantity << ")";
        return os;
    }
};

// Zero-copy trade queue
template<typename T, size_t Size = 1024>
class alignas(CACHE_LINE_SIZE) ZeroCopyQueue {
private:
    static constexpr size_t CACHE_LINE_SIZE = 64;
    alignas(CACHE_LINE_SIZE) std::array<T, Size> buffer_;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> head_{0};
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> tail_{0};

public:
    ZeroCopyQueue() : head_(0), tail_(0) {}  // Add explicit default constructor

    bool push(int buyId, int sellId, double price, int quantity) {
        size_t current = tail_.load(std::memory_order_relaxed);
        size_t next = (current + 1) % Size;
        
        if (next == head_.load(std::memory_order_acquire)) {
            return false;  // Queue is full
        }
        
        buffer_[current] = T(buyId, sellId, price, quantity);
        tail_.store(next, std::memory_order_release);
        return true;
    }

    bool try_pop(T* output) {
        size_t current = head_.load(std::memory_order_relaxed);
        if (current == tail_.load(std::memory_order_acquire)) {
            return false;  // Queue is empty
        }
        
        *output = std::move(buffer_[current]);
        head_.store((current + 1) % Size, std::memory_order_release);
        return true;
    }

    bool empty() const {
        return head_.load(std::memory_order_acquire) == 
               tail_.load(std::memory_order_acquire);
    }
};

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
    CANCELED
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
    int filledQuantity{0};
    bool isBuy;
    OrderType type;
    OrderStatus status{OrderStatus::ACTIVE};
    std::chrono::high_resolution_clock::time_point timestamp;
    std::optional<double> stopPrice;             
    std::optional<std::chrono::system_clock::time_point> expiry; 
    std::optional<int> visibleQuantity;          
    std::optional<int> minQuantity;              

    Order(int id, std::optional<double> p, int q, bool side, OrderType orderType)
        : orderId(id), price(p), quantity(q), isBuy(side), type(orderType),
          timestamp(std::chrono::high_resolution_clock::now()) 
    {
        if (quantity <= 0) {
            throw OrderException("Invalid quantity: must be positive");
        }
        if (type == OrderType::LIMIT && (!price || price.value() <= 0)) {
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

} // namespace ultraBook

#endif