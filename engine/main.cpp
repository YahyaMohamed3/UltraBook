#include <iostream>
#include "engine.hpp"  // Adjust this path as needed
#include "types.hpp"
using namespace ultraBook;

int main() {
    MatchingEngine engine;

    std::cout << "---- TEST 1: Add Basic Limit Orders ----\n";
    engine.addLimitOrder(1, 100.0, 10, true);   // Buy order
    engine.addLimitOrder(2, 101.0, 5, false);   // Sell order
    engine.printOrderBook();

    std::cout << "\n---- TEST 2: Add Matching Market Order ----\n";
    engine.addMarketOrder(3, 5, true);  // Buy market — should match with sell order
    engine.printTradelog();

    std::cout << "\n---- TEST 3: Partial Fill ----\n";
    engine.addLimitOrder(4, 102.0, 8, false); // New sell
    engine.addMarketOrder(5, 10, true);       // Buy market, partially fills
    engine.printTradelog();

    std::cout << "\n---- TEST 4: Invalid Orders ----\n";
    engine.addLimitOrder(6, -50.0, 10, true); // Invalid price
    engine.addLimitOrder(7, 99.0, -5, true);  // Invalid quantity
    engine.addMarketOrder(8, -3, false);      // Invalid market order
    engine.printOrderBook();

    std::cout << "\n---- TEST 5: Same Price, FIFO Priority ----\n";
    engine.addLimitOrder(9, 100.0, 5, true);
    engine.addLimitOrder(10, 100.0, 5, true);
    engine.addLimitOrder(11, 100.0, 5, false); // Will match both above in order
    engine.addMarketOrder(12, 10, false);      // Market sell, tests FIFO match
    engine.printTradelog();

    std::cout << "\n---- FINAL ORDER BOOK STATE ----\n";
    engine.printOrderBook();

    return 0;
}
