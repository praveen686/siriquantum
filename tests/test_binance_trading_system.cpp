#include <iostream>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <signal.h>
#include <iomanip>
#include <filesystem>

#include "common/logging.h"
#include "common/macros.h"
#include "common/lf_queue.h"
#include "common/types.h"

#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"
#include "exchange/market_data/market_update.h"

#include "trading/adapters/binance/order_gw/binance_order_gateway_adapter.h"
#include "trading/adapters/binance/market_data/binance_market_data_consumer.h"
#include "trading/strategy/market_order_book.h"
#include "trading/strategy/binance_market_order_book.h"
#include "trading/strategy/trade_engine.h"

/// Main components.
Common::Logger *logger = nullptr;
Trading::TradeEngine *trade_engine = nullptr;
Trading::BinanceMarketDataConsumer *market_data_consumer = nullptr;
Trading::BinanceOrderGatewayAdapter *order_gateway = nullptr;

volatile bool running = true;

void signal_handler(int signal) {
    std::cout << "Caught signal " << signal << ", shutting down..." << std::endl;
    running = false;
}

int main(int argc, char **argv) {
    // Register signal handler
    signal(SIGINT, signal_handler);
    
    if (argc < 3) {
        std::cerr << "Usage: " << argv[0] << " <client_id> <symbol> [price_threshold] [order_size]" << std::endl;
        std::cerr << "Example: " << argv[0] << " 1 BTCUSDT 1.0 0.001" << std::endl;
        std::cerr << "  price_threshold: Threshold for market maker algorithm (default: 1.0)" << std::endl;
        std::cerr << "  order_size: Order size in base currency units (default: 0.001)" << std::endl;
        return 1;
    }
    
    const Common::ClientId client_id = atoi(argv[1]);
    std::string symbol = argv[2];
    double price_threshold = (argc > 3) ? std::stod(argv[3]) : 1.0;
    double order_size = (argc > 4) ? std::stod(argv[4]) : 0.001;

    std::vector<std::string> symbols = {symbol};

    std::cout << "Testing Trading System with Binance..." << std::endl;
    std::cout << "Config file: Using standard trading.json" << std::endl;
    std::cout << "Client ID: " << client_id << std::endl;
    std::cout << "Symbol: " << symbol << std::endl;
    std::cout << "Price Threshold: " << price_threshold << std::endl;
    std::cout << "Order Size: " << order_size << " " << symbol.substr(0, symbol.length() - 4) << std::endl;
    
    try {
        // Create log directory if it doesn't exist
        std::filesystem::create_directories("/home/praveen/om/siriquantum/ida/logs/binance/");
        
        // Create logger with path in logs/binance directory
        logger = new Common::Logger("/home/praveen/om/siriquantum/ida/logs/binance/test_binance_system.log");
        std::string time_str;
        logger->log("%:% %() % Starting Trading System test\n", __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str));
        
        // Load the Binance config from the standard trading.json config file
        Trading::BinanceConfig config;
        try {
            config = Trading::BinanceMarketDataConsumer::loadConfig("/home/praveen/om/siriquantum/ida/config/trading.json");
            std::cout << "Successfully loaded config from trading.json. Using " 
                     << (config.use_testnet ? "TESTNET" : "PRODUCTION") 
                     << " environment." << std::endl;
            
            logger->log("%:% %() % Successfully loaded config from trading.json. Using %\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str),
                     config.use_testnet ? "TESTNET" : "PRODUCTION");
        } catch (const std::exception& e) {
            std::cerr << "Failed to load config from trading.json: " << e.what() << std::endl;
            logger->log("%:% %() % Failed to load config from trading.json: %\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str), e.what());
            return 1;
        }
        
        // Create the lock free queues to facilitate communication
        Exchange::ClientRequestLFQueue client_requests(100);
        Exchange::ClientResponseLFQueue client_responses(100);
        Exchange::MEMarketUpdateLFQueue market_updates(100);
        
        // Setup ticker configuration
        // For this test, we'll use simple configuration for a single symbol
        TradeEngineCfgHashMap ticker_cfg;
        
        // Use bracket operator [] instead of at() to ensure proper initialization of the array element
        ticker_cfg[0] = {
            static_cast<Common::Qty>(100), // Clip size - use fixed value instead of multiplying by order_size
            0.1,                           // Reduced threshold to make market maker more aggressive
            {
                static_cast<Common::Qty>(100), // Max order size - use fixed value
                static_cast<Common::Qty>(1000), // Larger max position - use fixed value
                -10000.0                        // Larger max loss allowance
            }
        };
        
        // Log the ticker configuration to verify it's correct
        logger->log("%:% %() % Initialized ticker configuration: %\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str),
                 ticker_cfg[0].toString().c_str());
        
        // Create TradeEngine
        logger->log("%:% %() % Starting Trade Engine...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        trade_engine = new Trading::TradeEngine(client_id, Common::AlgoType::MAKER,
                                             ticker_cfg,
                                             &client_requests,
                                             &client_responses,
                                             &market_updates);
        trade_engine->start();
        
        // Create BinanceOrderGatewayAdapter
        logger->log("%:% %() % Starting Order Gateway Adapter...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        order_gateway = new Trading::BinanceOrderGatewayAdapter(client_id, &client_requests, &client_responses, config, symbols);
        order_gateway->start();
        
        // Create BinanceMarketDataConsumer
        logger->log("%:% %() % Starting Market Data Consumer...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        market_data_consumer = new Trading::BinanceMarketDataConsumer(client_id, &market_updates, symbols, config);
        market_data_consumer->start();
        
        // Wait for initial market data
        std::cout << "Waiting for market data initialization..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));

        // Initialize last event time
        trade_engine->initLastEventTime();

        // How price determination works:
        // 1. MarketDataConsumer receives market updates and places them in market_updates queue
        // 2. TradeEngine processes these updates and sends them to appropriate MarketOrderBook
        // 3. MarketOrderBook maintains the order book and updates BBO (Best Bid/Offer)
        // 4. TradeEngine.onOrderBookUpdate calls FeatureEngine.onOrderBookUpdate
        // 5. FeatureEngine calculates a fair price based on BBO (bid_price * ask_qty + ask_price * bid_qty) / (bid_qty + ask_qty)
        // 6. Market Maker algorithm uses this fair price to determine bid/ask prices

        // For this test, we'll create a simple order after 5 seconds
        std::cout << "Waiting for market data and order generation..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(10));

        // NOTE: We do NOT need to create orders manually
        // The TradeEngine will determine prices based on market data via the FeatureEngine
        // Then the MarketMaker algorithm will call OrderManager.moveOrders() to place appropriate orders
        // This follows the proper architecture where price determination and order placement
        // are handled by the trading algorithms

        // Add more debug logging
        logger->log("%:% %() % Explicitly checking if orders are getting placed...\n",
                 __FILE__, __LINE__, __FUNCTION__,
                 Common::getCurrentTimeStr(&time_str));

        std::cout << "System running - watching for automatic order generation from TradeEngine for 20 seconds..." << std::endl;

        // Wait longer for orders to be placed
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        logger->log("%:% %() % TradeEngine is running with market data. Watching for orders generated by MarketMaker algorithm.\n",
                 __FILE__, __LINE__, __FUNCTION__,
                 Common::getCurrentTimeStr(&time_str));
        
        // Main loop - wait for signals or market data to process
        std::cout << "Test running. Press Ctrl+C to exit." << std::endl;
        
        int response_count = 0;
        while (running && trade_engine->silentSeconds() < 60) {
            if (client_responses.size() > 0) {
                auto response = client_responses.getNextToRead();
                if (response) {
                    response_count++;

                    // Print response details
                    std::cout << "\nReceived response #" << response_count << ":" << std::endl;
                    std::cout << "Client Response: Type=" << Exchange::clientResponseTypeToString(response->type_)
                             << ", OrderId=" << response->order_id_
                             << ", TickerId=" << static_cast<int>(response->ticker_id_)
                             << ", Side=" << Common::sideToString(response->side_)
                             << ", Price=" << Common::priceToString(response->price_)
                             << ", ExecQty=" << Common::qtyToString(response->exec_qty_)
                             << ", LeavesQty=" << Common::qtyToString(response->leaves_qty_)
                             << std::endl;

                    // Check if this is a NEW order response with a price
                    if (response->type_ == Exchange::ClientResponseType::ACCEPTED &&
                        response->price_ != Common::Price_INVALID) {
                        std::cout << "*** TradeEngine determined price: "
                                 << Common::priceToString(response->price_)
                                 << " for " << Common::sideToString(response->side_)
                                 << " order ***" << std::endl;
                    }
                    
                    // Log response
                    logger->log("%:% %() % Received response: Type=%, OrderId=%, Price=%\n",
                             __FILE__, __LINE__, __FUNCTION__,
                             Common::getCurrentTimeStr(&time_str),
                             Exchange::clientResponseTypeToString(response->type_),
                             response->order_id_,
                             Common::priceToString(response->price_));
                    
                    client_responses.updateReadIndex();
                }
            }
            
            logger->log("%:% %() % Waiting till no activity, been silent for % seconds...\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str), trade_engine->silentSeconds());
            
            using namespace std::literals::chrono_literals;
            std::this_thread::sleep_for(5s);
        }
        
        // Cleanup
        std::cout << "Test completed. Shutting down..." << std::endl;
        
        trade_engine->stop();
        market_data_consumer->stop();
        order_gateway->stop();
        
        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(10s);
        
        delete logger;
        logger = nullptr;
        delete trade_engine;
        trade_engine = nullptr;
        delete market_data_consumer;
        market_data_consumer = nullptr;
        delete order_gateway;
        order_gateway = nullptr;
        
        std::this_thread::sleep_for(10s);
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}