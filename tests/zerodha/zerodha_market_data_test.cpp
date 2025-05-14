#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>

#include "common/logging.h"
#include "common/lf_queue.h"
#include "exchange/market_data/market_update.h"
#include "trading/adapters/zerodha/market_data/zerodha_market_data_adapter.h"
#include "trading/adapters/zerodha/market_data/environment_config.h"
#include "trading/adapters/zerodha/market_data/instrument_token_manager.h"

// Alias to avoid namespace confusion - same as in zerodha_market_data_adapter.h
namespace ExchangeNS = ::Exchange;

// Size of the market updates queue
constexpr size_t MARKET_UPDATES_QUEUE_SIZE = 1024 * 1024;

int main(int argc, char** argv) {
    // Set up logs directory path for Zerodha
    std::filesystem::path logs_dir = "/home/praveen/om/siriquantum/ida/logs/zerodha";
    std::filesystem::create_directories(logs_dir); // Ensure directory exists
    
    // Initialize logger with path to logs directory
    Common::Logger logger((logs_dir / "market_data_test.log").string());
    
    std::string time_str;
    logger.log("%:% %() % Starting Enhanced Zerodha Market Data Adapter Test...\n", 
               __FILE__, __LINE__, __FUNCTION__, 
               Common::getCurrentTimeStr(&time_str));
    
    try {
        std::cout << "Starting Enhanced Zerodha Market Data Adapter Test..." << std::endl;
        
        // Default config file path if not provided on command line
        const std::string default_config_file = "/home/praveen/om/siriquantum/ida/config/trading.json";
        
        // Path to JSON config file (if provided as command line argument)
        const std::string config_file = (argc > 1) ? argv[1] : default_config_file;
        std::cout << "Using config file: " << config_file << std::endl;
        logger.log("%:% %() % Using config file: %\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str),
                 config_file.c_str());
        
        // Queue for market updates - using the lock-free queue
        ExchangeNS::MEMarketUpdateLFQueue market_updates(MARKET_UPDATES_QUEUE_SIZE);
        
        // Create and initialize the market data adapter
        logger.log("%:% %() % Creating market data adapter with JSON configuration\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        
        // Load configuration to display info before creating adapter
        std::unique_ptr<Adapter::Zerodha::EnvironmentConfig> config = 
            std::make_unique<Adapter::Zerodha::EnvironmentConfig>(&logger, "", config_file);
        
        if (config->load()) {
            logger.log("%:% %() % Successfully loaded configuration from %\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str),
                     config_file.c_str());
            
            // Log trading mode and instruments to verify configuration
            logger.log("%:% %() % Trading mode: %\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str),
                     config->isPaperTrading() ? "PAPER" : "LIVE");
            
            std::cout << "Trading mode: " << (config->isPaperTrading() ? "PAPER" : "LIVE") << std::endl;
            std::cout << "Configured instruments: " << config->getInstruments().size() << std::endl;
        } else {
            logger.log("%:% %() % Failed to load configuration from %\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str),
                     config_file.c_str());
            return 1;
        }
        
        // Create the market data adapter with JSON config
        std::unique_ptr<Adapter::Zerodha::ZerodhaMarketDataAdapter> market_data_adapter =
            std::make_unique<Adapter::Zerodha::ZerodhaMarketDataAdapter>(&logger, &market_updates, config_file);
        
        logger.log("%:% %() % Starting market data adapter\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        std::cout << "Starting market data adapter..." << std::endl;
        
        market_data_adapter->start();
        
        // Allow time for connection to establish and instrument data to load
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Check if connected
        if (!market_data_adapter->isConnected()) {
            logger.log("%:% %() % Failed to connect to Zerodha WebSocket, waiting longer...\n", 
                       __FILE__, __LINE__, __FUNCTION__, 
                       Common::getCurrentTimeStr(&time_str));
            std::cout << "WARNING: Not connected to Zerodha WebSocket, waiting 10 seconds..." << std::endl;
            
            // Wait longer for connection
            std::this_thread::sleep_for(std::chrono::seconds(10));
            
            if (!market_data_adapter->isConnected()) {
                logger.log("%:% %() % Failed to connect to Zerodha WebSocket after waiting\n", 
                           __FILE__, __LINE__, __FUNCTION__, 
                           Common::getCurrentTimeStr(&time_str));
                std::cerr << "ERROR: Failed to connect to Zerodha WebSocket after waiting" << std::endl;
                return 1;
            }
        }
        
        logger.log("%:% %() % Connected to Zerodha WebSocket\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        std::cout << "Connected to Zerodha WebSocket!" << std::endl;
        
        // Use the adapter's built-in test symbol subscription
        logger.log("%:% %() % Subscribing to test symbols from environment configuration\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        std::cout << "Subscribing to test symbols..." << std::endl;
        
        market_data_adapter->subscribeToTestSymbols();
        
        // Also subscribe to an index symbol to test futures handling
        logger.log("%:% %() % Subscribing to NIFTY 50 index to test futures handling\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        std::cout << "Subscribing to NIFTY 50 index to test futures handling..." << std::endl;
        
        auto nifty_ticker_id = static_cast<Common::TickerId>(std::hash<std::string>{}("NSE:NIFTY 50") % 10000);
        market_data_adapter->subscribe("NSE:NIFTY 50", nifty_ticker_id);
        
        // Wait for and process market updates
        logger.log("%:% %() % Waiting for market data updates (30 seconds)...\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        std::cout << "Waiting for market data updates (30 seconds)..." << std::endl;
        std::cout << "Market data will be printed below as it arrives:" << std::endl;
        
        auto start_time = std::chrono::steady_clock::now();
        size_t update_count = 0;
        
        while (std::chrono::steady_clock::now() - start_time < std::chrono::seconds(30)) {
            // Check for updates in the lock-free queue
            if (market_updates.size() > 0) {
                // Process all available updates using the LFQueue pattern
                for (auto update = market_updates.getNextToRead(); 
                     market_updates.size() > 0 && update != nullptr; 
                     update = market_updates.getNextToRead()) {
                    
                    update_count++;
                    
                    // Log every update to see all market data
                    {
                        // Get the symbol for this ticker ID
                        std::string symbol = market_data_adapter->mapInternalToZerodhaSymbol(update->ticker_id_);
                        
                        logger.log("%:% %() % Market update: ticker=% (%), price=%, qty=%, side=%, type=%\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str),
                                   update->ticker_id_,
                                   symbol.c_str(),
                                   static_cast<double>(update->price_) / 100.0,  // Convert from price ticks to dollars
                                   update->qty_,
                                   update->side_ == Common::Side::BUY ? "BUY" : "SELL",
                                   static_cast<int>(update->type_));
                                   
                        std::cout << "Market update: ticker=" << update->ticker_id_ 
                                  << " (" << symbol << ")"
                                  << ", price=" << static_cast<double>(update->price_) / 100.0
                                  << ", qty=" << update->qty_
                                  << ", side=" << (update->side_ == Common::Side::BUY ? "BUY" : "SELL")
                                  << ", type=" << static_cast<int>(update->type_)
                                  << std::endl;
                    }
                    
                    // Move to next update in queue
                    market_updates.updateReadIndex();
                }
            }
            
            // Small sleep to prevent CPU spinning (similar to what the logger does)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        
        // Stop the market data adapter
        logger.log("%:% %() % Stopping market data adapter...\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str));
        std::cout << "Stopping market data adapter..." << std::endl;
        
        market_data_adapter->stop();
        
        // Wait for any final processing
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Final report
        logger.log("%:% %() % Test complete. Processed % market updates.\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str),
                   update_count);
        std::cout << "Test complete. Processed " << update_count << " market updates." << std::endl;
        
        if (update_count > 0) {
            logger.log("%:% %() % SUCCESS: Enhanced Zerodha Market Data Adapter is working!\n", 
                       __FILE__, __LINE__, __FUNCTION__, 
                       Common::getCurrentTimeStr(&time_str));
            std::cout << "SUCCESS: Enhanced Zerodha Market Data Adapter is working!" << std::endl;
            return 0;
        } else {
            logger.log("%:% %() % WARNING: No market updates received. This could be normal if the market is closed.\n", 
                       __FILE__, __LINE__, __FUNCTION__, 
                       Common::getCurrentTimeStr(&time_str));
            std::cout << "WARNING: No market updates received. This could be normal if the market is closed." << std::endl;
            return 0;
        }
        
    } catch (const std::exception& e) {
        logger.log("%:% %() % ERROR: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, 
                   Common::getCurrentTimeStr(&time_str),
                   e.what());
        std::cerr << "ERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}