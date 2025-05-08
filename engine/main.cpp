#include "engine.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <iomanip>

using namespace ultraBook;
using namespace std::chrono_literals;

void printHeader(const std::string& title) {
    std::cout << "\n\n================================================" << std::endl;
    std::cout << title << std::endl;
    std::cout << "================================================" << std::endl;
}

int main() {
    MatchingEngine engine;
    
    printHeader("INITIAL EMPTY ORDER BOOK");
    engine.printOrderBook();
    
    // Add some limit orders to populate the book
    printHeader("ADDING LIMIT ORDERS");
    engine.addLimitOrder(1, 10.0, 100, true);  // Buy 100 @ $10.00
    engine.addLimitOrder(2, 9.5, 200, true);   // Buy 200 @ $9.50
    engine.addLimitOrder(3, 10.5, 150, false); // Sell 150 @ $10.50
    engine.addLimitOrder(4, 11.0, 300, false); // Sell 300 @ $11.00
    engine.printOrderBook();
    
    // Match orders
    printHeader("MATCHING EXISTING ORDERS");
    engine.matchOrders();
    engine.printOrderBook();
    
    // Testing market order that will partially match
    printHeader("TESTING MARKET BUY ORDER (PARTIAL FILL)");
    engine.addMarketOrder(5, 500, true); // Buy 500 market
    engine.printOrderBook();
    engine.printTradelog();
    
    // Add more sell orders
    printHeader("ADDING MORE SELL ORDERS");
    engine.addLimitOrder(6, 10.2, 200, false); // Sell 200 @ $10.20
    engine.addLimitOrder(7, 10.3, 300, false); // Sell 300 @ $10.30
    engine.printOrderBook();
    
    // Test IOC order
    printHeader("TESTING IOC SELL ORDER");
    engine.addIOCOrder(8, 9.7, 250, false); // Sell 250 @ $9.70 IOC - should match with existing buy orders
    engine.printOrderBook();
    engine.printTradelog();
    
    // Test FOK order that succeeds
    printHeader("TESTING FOK BUY ORDER (SUCCESS)");
    engine.addFOKOrder(9, 10.3, 100, true); // Buy 100 @ $10.30 FOK - should fill completely
    engine.printOrderBook();
    
    // Test FOK order that fails
    printHeader("TESTING FOK BUY ORDER (FAILURE)");
    engine.addFOKOrder(10, 10.0, 1000, true); // Buy 1000 @ $10.00 FOK - should fail (not enough liquidity)
    engine.printOrderBook();
    
    // Test order cancellation
    printHeader("TESTING ORDER CANCELLATION");
    engine.cancelOrder(6); // Cancel sell order ID 6
    engine.printOrderBook();
    
    // Final order book and trade log
    printHeader("FINAL ORDER BOOK AND TRADE LOG");
    engine.printOrderBook();
    engine.printTradelog();
    
    std::cout << "\nAll tests completed successfully!" << std::endl;
    return 0;
}