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

void printSeparator() {
    std::cout << "\n--------------------------------------------------" << std::endl;
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
    printHeader("TESTING MARKET BUY ORDER");
    engine.addMarketOrder(5, 200, true); // Buy 200 market
    engine.printOrderBook();
    engine.printTradelog();
    
    // Add more sell orders
    printHeader("ADDING MORE SELL ORDERS");
    engine.addLimitOrder(6, 10.2, 200, false); // Sell 200 @ $10.20
    engine.addLimitOrder(7, 10.3, 300, false); // Sell 300 @ $10.30
    engine.printOrderBook();
    
    // Test IOC order
    printHeader("TESTING IOC SELL ORDER");
    engine.addIOCOrder(8, 9.7, 250, false); // Sell 250 @ $9.70 IOC
    engine.printOrderBook();
    engine.printTradelog();
    
    // Test FOK order that succeeds
    printHeader("TESTING FOK BUY ORDER (SUCCESS)");
    engine.addFOKOrder(9, 10.3, 100, true); // Buy 100 @ $10.30 FOK
    engine.printOrderBook();
    
    // Test FOK order that fails
    printHeader("TESTING FOK BUY ORDER (FAILURE)");
    engine.addFOKOrder(10, 10.0, 1000, true); // Buy 1000 @ $10.00 FOK
    engine.printOrderBook();

    // Test GTC order
    printHeader("TESTING GTC ORDER");
    engine.addGTCOrder(11, 9.8, 150, true); // Buy 150 @ $9.80 GTC
    engine.printOrderBook();

    // Test GTD order (expires in 5 seconds)
    printHeader("TESTING GTD ORDER");
    auto expiry = std::chrono::system_clock::now() + 5s;
    engine.addGTDOrder(12, 9.7, 120, true, expiry); // Buy 120 @ $9.70 GTD
    engine.printOrderBook();
    
    std::cout << "\nWaiting 6 seconds for GTD order to expire..." << std::endl;
    std::this_thread::sleep_for(6s);
    
    // Call checkExpiredOrders to handle expired GTD orders directly
    printHeader("CHECKING EXPIRED ORDERS");
    engine.checkExpiredOrders();
    engine.printOrderBook();
    
    // Test Stop order
    printHeader("TESTING STOP ORDER");
    engine.addStopOrder(13, 10.4, 100, true); // Buy Stop @ $10.4, Qty 100
    engine.printOrderBook();
    
    printSeparator();
    std::cout << "Adding a trade to trigger stop order..." << std::endl;
    engine.addLimitOrder(14, 10.4, 50, false); // Sell 50 @ $10.4
    engine.addLimitOrder(15, 10.5, 100, true); // Buy 100 @ $10.5 (to match with sell order)
    engine.matchOrders(); // This should trigger the stop order
    engine.printOrderBook();
    engine.printTradelog();
    
    // Test StopLimit order
    printHeader("TESTING STOP LIMIT ORDER");
    engine.addStopLimitOrder(16, 10.6, 10.7, 80, true); // Buy StopLimit @ trigger $10.6, limit $10.7, Qty 80
    engine.printOrderBook();
    
    printSeparator();
    std::cout << "Adding a trade to trigger stop limit order..." << std::endl;
    engine.addLimitOrder(17, 10.6, 30, false); // Sell 30 @ $10.6
    engine.addLimitOrder(18, 10.6, 30, true);  // Buy 30 @ $10.6 (to match with sell order)
    engine.matchOrders(); // This should trigger the stop limit order
    engine.printOrderBook();
    engine.printTradelog();
    
    // Test Iceberg order
    printHeader("TESTING ICEBERG ORDER");
    engine.addIcebergOrder(19, 10.1, 500, 100, 100, true); // Buy 500 @ $10.1 with 100 visible
    engine.printOrderBook();
    
    printSeparator();
    std::cout << "Adding order to match with iceberg..." << std::endl;
    engine.addLimitOrder(20, 10.1, 250, false); // Sell 250 @ $10.1
    engine.matchOrders(); // Should match with iceberg and replenish visible quantity
    engine.printOrderBook();
    engine.printTradelog();
    
    // Test order cancellation
    printHeader("TESTING ORDER CANCELLATION");
    engine.cancelOrder(19); // Cancel iceberg order
    engine.printOrderBook();
    
    // Final order book and trade log
    printHeader("FINAL ORDER BOOK AND TRADE LOG");
    engine.printOrderBook();
    engine.printTradelog();
    
    std::cout << "\nAll tests completed successfully!" << std::endl;
    return 0;
}