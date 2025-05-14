#include <cstdlib>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <iomanip>
#include <filesystem>

#include "common/logging.h"
#include "common/lf_queue.h"
#include "exchange/market_data/market_update.h"
#include "trading/adapters/zerodha/market_data/zerodha_market_data_adapter.h"
#include "trading/adapters/zerodha/market_data/orderbook/zerodha_order_book.h"

// Alias to avoid namespace confusion
namespace ExchangeNS = ::Exchange;

// Helper function to print order book
void printOrderBook(const Adapter::Zerodha::ZerodhaOrderBook* book, const std::string& symbol) {
    if (!book) {
        std::cout << "Order book not found for " << symbol << std::endl;
        return;
    }
    
    // Get depth
    auto [bid_depth, ask_depth] = book->getDepth();
    
    std::cout << "Order Book for " << symbol << ": " << bid_depth << " bids, " << ask_depth << " asks" << std::endl;
    
    // Print BBO
    const auto& bbo = book->getBBO();
    std::cout << "BBO: " << bbo.bid_quantity << " @ " << std::fixed << std::setprecision(2) << bbo.bid_price 
              << " / " << bbo.ask_price << " @ " << bbo.ask_quantity << std::endl;
    
    // Print bid side (top 5)
    std::cout << "Bids:" << std::endl;
    int count = 0;
    for (const auto& [price, level] : book->getBids()) {
        std::cout << "  " << level.quantity << " @ " << std::fixed << std::setprecision(2) << level.price 
                  << " (" << level.orders << " orders)" << std::endl;
        if (++count >= 5) break;
    }
    
    // Print ask side (top 5)
    std::cout << "Asks:" << std::endl;
    count = 0;
    for (const auto& [price, level] : book->getAsks()) {
        std::cout << "  " << level.quantity << " @ " << std::fixed << std::setprecision(2) << level.price 
                  << " (" << level.orders << " orders)" << std::endl;
        if (++count >= 5) break;
    }
    
    std::cout << std::endl;
}

// Process and print market updates
void processMarketUpdates(ExchangeNS::MEMarketUpdateLFQueue* queue, Common::Logger* logger,
                         Adapter::Zerodha::ZerodhaMarketDataAdapter* adapter) {
    static int update_count = 0;
    
    while (auto* update = queue->getNextToRead()) {
        update_count++;
        
        // Log every 100th update to avoid excessive output
        if (update_count % 100 == 0) {
            // Get symbol from ticker ID
            std::string symbol = adapter->mapInternalToZerodhaSymbol(update->ticker_id_);
            
            // Log the update
            logger->log("Received market update #%: % for %, type=%, price=%, qty=%\n", 
                      update_count, 
                      update->toString().c_str(),
                      symbol.c_str(),
                      ExchangeNS::marketUpdateTypeToString(update->type_).c_str(),
                      update->price_ / 100.0,  // Convert to rupees
                      update->qty_);
            
            // Print order book state
            auto* book = adapter->getOrderBook(update->ticker_id_);
            if (book) {
                printOrderBook(book, symbol);
            }
        }
        
        // Update read index
        queue->updateReadIndex();
    }
}

int main(int /* argc */, char** /* argv */) {
    // Set up logs directory path for Zerodha
    std::filesystem::path logs_dir = "/home/praveen/om/siriquantum/ida/logs/zerodha";
    std::filesystem::create_directories(logs_dir); // Ensure directory exists
    
    // Initialize logger with path to logs directory
    Common::Logger logger((logs_dir / "order_book_test.log").string());
    
    std::string time_str;
    logger.log("%:% %() % Starting Zerodha Order Book Test...\n", 
               __FILE__, __LINE__, __FUNCTION__, 
               Common::getCurrentTimeStr(&time_str));
    
    // Note: The Logger in this implementation doesn't have log levels
    // We'll log everything and rely on the Common::LogLevel system used by the adapter
    
    // Create market updates queue
    ExchangeNS::MEMarketUpdateLFQueue market_updates(1024);
    
    // Create market data adapter with configuration file
    std::string config_file = "/home/praveen/om/siriquantum/ida/config/trading.json";
    std::cout << "Loading configuration from " << config_file << std::endl;
    
    Adapter::Zerodha::ZerodhaMarketDataAdapter market_data_adapter(
        &logger, &market_updates, config_file);
    
    // Start the adapter
    market_data_adapter.start();
    
    // Subscribe to test symbols
    market_data_adapter.subscribeToTestSymbols();
    
    // Let the connections initialize
    std::this_thread::sleep_for(std::chrono::seconds(2));
    
    // Load configuration to get test symbols
    Adapter::Zerodha::EnvironmentConfig config(&logger, "", config_file);
    if (config.load()) {
        const auto& test_symbols = config.getTestSymbols();
        std::cout << "Found " << test_symbols.size() << " test symbols in configuration" << std::endl;
        
        // Process test symbols from config
        for (const auto& symbol : test_symbols) {
            std::cout << "Subscribing to symbol: " << symbol << std::endl;
            
            // Create a deterministic ticker ID
            auto ticker_id = static_cast<Common::TickerId>(std::hash<std::string>{}(symbol) % 10000);
            market_data_adapter.subscribe(symbol, ticker_id);
        }
    } else {
        std::cerr << "Failed to load configuration from " << config_file << std::endl;
    }
    
    // Process market updates
    std::cout << "Waiting for market updates. Press Ctrl+C to stop." << std::endl;
    
    // Loop to process market updates
    while (true) {
        processMarketUpdates(&market_updates, &logger, &market_data_adapter);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    
    return 0;
}