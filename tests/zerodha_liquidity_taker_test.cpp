#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <atomic>
#include <filesystem>

#include "common/logging.h"
#include "common/macros.h"
#include "common/time_utils.h"
#include "common/lf_queue.h"
#include "common/thread_utils.h"
#include "common/types.h"

#include "exchange/market_data/market_update.h"
#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"

#include "trading/strategy/trade_engine.h"
#include "trading/strategy/market_order_book.h"
#include "trading/strategy/order_manager.h"
#include "trading/strategy/feature_engine.h"
#include "trading/strategy/risk_manager.h"

#include "trading/adapters/zerodha/auth/zerodha_authenticator.h"
#include "trading/adapters/zerodha/market_data/environment_config.h"
#include "trading/adapters/zerodha/market_data/zerodha_market_data_adapter.h"
#include "trading/adapters/zerodha/order_gw/zerodha_order_gateway_adapter.h"
#include "trading/adapters/zerodha/strategy/zerodha_liquidity_taker.h"

using namespace Common;
using namespace Exchange;
using namespace Trading;
using namespace Adapter::Zerodha;

// Global variables for cleanup
std::atomic<bool> running(true);
Logger* logger = nullptr;

// Signal handler to handle Ctrl+C
void signalHandler(int signal) {
    std::cout << "Caught signal " << signal << ". Exiting..." << std::endl;
    running = false;
}

int main([[maybe_unused]] int argc, [[maybe_unused]] char** argv) {
    // Register signal handler
    std::signal(SIGINT, signalHandler);
    
    // Set up logs directory path for Zerodha
    std::filesystem::path logs_dir = "/home/praveen/om/siriquantum/ida/logs/zerodha";
    std::filesystem::create_directories(logs_dir); // Ensure directory exists
    
    // Create a logger - make sure directory exists first
    std::string log_file = (logs_dir / "liquidity_taker_test.log").string();
    std::cout << "Creating logger with log file: " << log_file << std::endl;
    std::string time_str;
    logger = new Logger(log_file);
    
    // Test immediate logging to ensure it's working
    logger->log("%:% %() % LOGGER TEST - VERIFY LOGGING IS WORKING\n", 
               __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
    
    std::cout << "Starting Zerodha Liquidity Taker test..." << std::endl;
    logger->log("%:% %() % Starting Zerodha Liquidity Taker test...\n", 
               __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
    
    try {
        // Load environment configuration - match exactly how market_data_test does it
        std::cout << "Loading environment configuration..." << std::endl;
        logger->log("%:% %() % Loading environment configuration...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        const std::string config_file = "/home/praveen/om/siriquantum/ida/config/trading.json";
        
        // Create configuration exactly like in market_data_test.cpp lines 56-57
        std::unique_ptr<Adapter::Zerodha::EnvironmentConfig> env_config = 
            std::make_unique<Adapter::Zerodha::EnvironmentConfig>(logger, "", config_file);
        
        // Load config using the method in market_data_test.cpp
        if (!env_config->load()) {
            logger->log("%:% %() % Failed to load config file: %\n", 
                       __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str), config_file.c_str());
            return EXIT_FAILURE;
        }
        
        // Initialize Zerodha Authenticator
        std::cout << "Initializing Zerodha authenticator..." << std::endl;
        logger->log("%:% %() % Initializing Zerodha authenticator...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        // Create the authenticator but don't call authenticate() on it directly
        // In market_data_test.cpp, authentication is done inside the adapter's start() method
        
        // Create an environment variable to specify cache directory
        std::string cache_dir = env_config->getInstrumentsCacheDir();
        std::filesystem::create_directories(cache_dir);
        
        std::cout << "Using cache directory: " << cache_dir << std::endl;
        logger->log("%:% %() % Using cache directory: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str), cache_dir.c_str());
        
        // Create the authenticator for use with other components, but DON'T call authenticate() on it
        auto authenticator = std::make_unique<ZerodhaAuthenticator>(
            ZerodhaAuthenticator::from_config(
                logger,
                *env_config,
                cache_dir
            )
        );
        
        // Authentication will be done by market_data_adapter->start()
        std::cout << "Zerodha authentication will be done by market data adapter" << std::endl;
        logger->log("%:% %() % Zerodha authentication will be done by market data adapter\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        // Create LF queues for market data and order gateway
        const size_t market_update_queue_size = 1024;
        const size_t client_request_queue_size = 128;
        const size_t client_response_queue_size = 128;
        
        auto market_updates = std::make_unique<LFQueue<MEMarketUpdate>>(market_update_queue_size);
        auto client_requests = std::make_unique<LFQueue<MEClientRequest>>(client_request_queue_size);
        auto client_responses = std::make_unique<LFQueue<MEClientResponse>>(client_response_queue_size);
        
        // Create position keeper first
        auto position_keeper = std::make_unique<PositionKeeper>(logger);
        
        // Create order book manager
        auto order_books = std::make_unique<MarketOrderBookHashMap>();
        
        // Create trading configuration for tickers
        auto ticker_cfg = std::make_unique<TradeEngineCfgHashMap>();
        
        // Add test symbols to ticker configuration
        for (const auto& symbol_info : env_config->getInstruments()) {
            TickerId ticker_id = symbol_info.ticker_id;
            TradeEngineCfg cfg;
            cfg.clip_ = symbol_info.clip;
            cfg.threshold_ = symbol_info.threshold;
            
            // Configure risk settings
            cfg.risk_cfg_.max_position_ = symbol_info.max_position;
            cfg.risk_cfg_.max_loss_ = symbol_info.max_loss;
            
            logger->log("%:% %() % Adding ticker config: % (clip: %, threshold: %, max_pos: %)\n", 
                       __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str),
                       symbol_info.symbol.c_str(), symbol_info.clip, 
                       symbol_info.threshold, symbol_info.max_position);
            
            (*ticker_cfg)[ticker_id] = cfg;
            
            // Register instrument with order gateway
            // Will move this after order_gateway_adapter is created
            
            // Create an empty order book for this ticker
            auto order_book = new MarketOrderBook(ticker_id, logger);
            (*order_books)[ticker_id] = order_book;
        }
        
        // Create risk manager with position keeper
        auto risk_manager = std::make_unique<RiskManager>(logger, position_keeper.get(), *ticker_cfg);
        
        // Create trade engine with proper parameters
        const ClientId client_id = 1;
        auto trade_engine = std::make_unique<TradeEngine>(
            client_id,
            AlgoType::TAKER, // Liquidity taker strategy
            *ticker_cfg,
            client_requests.get(),
            client_responses.get(),
            market_updates.get()
        );
        
        // Create order manager
        auto order_manager = std::make_unique<OrderManager>(
            logger,
            trade_engine.get(),
            *risk_manager
        );
        
        // Create feature engine
        auto feature_engine = std::make_unique<FeatureEngine>(
            logger
        );
        
        // Initialize Zerodha Market Data Adapter using EXACTLY the same pattern as market_data_test.cpp
        logger->log("%:% %() % Creating market data adapter with JSON configuration\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        std::cout << "Initializing Zerodha market data adapter..." << std::endl;
        
        // Create market data adapter using EXACTLY the same code from market_data_test.cpp lines 82-83
        std::unique_ptr<Adapter::Zerodha::ZerodhaMarketDataAdapter> market_data_adapter =
            std::make_unique<Adapter::Zerodha::ZerodhaMarketDataAdapter>(
                logger, 
                market_updates.get(),
                config_file
            );
        
        // Initialize Zerodha Order Gateway Adapter
        std::cout << "Initializing Zerodha order gateway adapter..." << std::endl;
        logger->log("%:% %() % Initializing Zerodha order gateway adapter...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        auto order_gateway_adapter = std::make_unique<ZerodhaOrderGatewayAdapter>(
            logger,
            client_id,
            client_requests.get(),
            client_responses.get(),
            env_config->getApiKey(),
            env_config->getApiSecret()
        );
        
        // Set paper trading mode
        order_gateway_adapter->setPaperTradingMode(true);
        order_gateway_adapter->setPaperTradingFillProbability(0.9);
        order_gateway_adapter->setPaperTradingLatencyRange(10.0, 50.0);
        
        // Register instruments with order gateway
        for (const auto& symbol_info : env_config->getInstruments()) {
            TickerId ticker_id = symbol_info.ticker_id;
            order_gateway_adapter->registerInstrument(symbol_info.symbol, ticker_id);
        }
        
        // Initialize Zerodha Liquidity Taker strategy configuration
        std::cout << "Initializing Zerodha liquidity taker strategy..." << std::endl;
        logger->log("%:% %() % Initializing Zerodha liquidity taker strategy...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        ZerodhaLiquidityTaker::Config zerodha_config;
        zerodha_config.use_vwap_filter = true;
        zerodha_config.vwap_threshold = 0.02;
        zerodha_config.enforce_circuit_limits = true;
        zerodha_config.use_bracket_orders = true;
        zerodha_config.stop_loss_percent = 0.5;
        zerodha_config.target_percent = 1.0;
        zerodha_config.min_volume_percentile = 50;
        zerodha_config.enforce_trading_hours = true;
        zerodha_config.trading_start_time = "09:15:00";
        zerodha_config.trading_end_time = "15:15:00";
        
        // Don't create the strategy until after WebSocket connection is confirmed
        // We'll create this after the WebSocket connection is established
        std::unique_ptr<ZerodhaLiquidityTaker> liquidity_taker;
        
        // Important: We need to match the exact sequence from market_data_test that is known to work
        
        // First, use the market data adapter's convenience method to subscribe to test symbols
        // This is the exact approach used in market_data_test.cpp line 125
        std::cout << "Subscribing to test symbols using subscribeToTestSymbols API..." << std::endl;
        logger->log("%:% %() % Subscribing to test symbols using the adapter API...\n", 
                  __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
                   
        // Call the same method used in market_data_test.cpp
        market_data_adapter->subscribeToTestSymbols();
        
        // Also add the NIFTY 50 index subscription, exactly like in market_data_test.cpp lines 133-134
        logger->log("%:% %() % Also subscribing to NIFTY 50 index...\n", 
                  __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
                   
        auto nifty_ticker_id = static_cast<Common::TickerId>(std::hash<std::string>{}("NSE:NIFTY 50") % 10000);
        market_data_adapter->subscribe("NSE:NIFTY 50", nifty_ticker_id);
        
        // Key problem: In liquidity_taker_test, other components might be interfering
        // with the WebSocket connection process
        
        // Start ONLY the market data adapter first and wait for connection before
        // initializing other components - this should match the market_data_test approach
        std::cout << "Starting market data adapter..." << std::endl;
        logger->log("%:% %() % Starting market data adapter...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        // Start the adapter
        market_data_adapter->start();
        
        // Allow time for connection to establish (like in the market data test)
        std::cout << "Waiting for WebSocket connection to establish..." << std::endl;
        logger->log("%:% %() % Waiting for WebSocket connection to establish...\n",
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
                   
        // Wait longer initially as it seems 5 seconds might not be enough
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        // Check if connected
        if (!market_data_adapter->isConnected()) {
            std::cout << "WARNING: Not connected to Zerodha WebSocket, waiting 15 more seconds..." << std::endl;
            logger->log("%:% %() % Failed to connect to Zerodha WebSocket, waiting longer...\n",
                       __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
                       
            // Wait longer for connection
            std::this_thread::sleep_for(std::chrono::seconds(15));
            
            if (!market_data_adapter->isConnected()) {
                std::cerr << "ERROR: Failed to connect to Zerodha WebSocket after waiting" << std::endl;
                logger->log("%:% %() % Failed to connect to Zerodha WebSocket after waiting\n",
                           __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
                return EXIT_FAILURE;
            }
        }
        
        std::cout << "Connected to Zerodha WebSocket!" << std::endl;
        logger->log("%:% %() % Connected to Zerodha WebSocket\n",
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        // Very important: After WebSocket connection is established,
        // now it's safe to proceed with initializing other components
        std::cout << "WebSocket connection successful - continuing with other components" << std::endl;
        
        // Start order gateway adapter
        std::cout << "Starting order gateway adapter..." << std::endl;
        logger->log("%:% %() % Starting order gateway adapter...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        order_gateway_adapter->start();
        
        // Create the Zerodha Liquidity Taker strategy AFTER websocket is connected
        std::cout << "Creating liquidity taker strategy..." << std::endl;
        logger->log("%:% %() % Creating liquidity taker strategy...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        liquidity_taker = std::make_unique<ZerodhaLiquidityTaker>(
            logger,
            trade_engine.get(),
            feature_engine.get(),
            order_manager.get(),
            *ticker_cfg,
            zerodha_config,
            market_data_adapter.get(),
            order_gateway_adapter.get(),
            client_requests.get(),
            client_id
        );
        
        // Start trade engine
        std::cout << "Starting trade engine..." << std::endl;
        logger->log("%:% %() % Starting trade engine...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        std::thread trade_engine_thread([&trade_engine]() {
            trade_engine->run();
        });
        
        // Run the test for a fixed duration or until interrupted
        std::cout << "Test running. Press Ctrl+C to stop..." << std::endl;
        logger->log("%:% %() % Test running. Press Ctrl+C to stop...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        const auto start_time = getCurrentNanos();
        const auto test_duration_ns = 60ULL * 1'000'000'000ULL; // 60 seconds in nanoseconds
        
        while (running && (static_cast<uint64_t>(getCurrentNanos() - start_time) < test_duration_ns)) {
            // Print stats every 5 seconds
            if ((getCurrentNanos() - start_time) % (5ULL * 1'000'000'000ULL) < 100'000'000ULL) {
                logger->log("%:% %() % Test running for % seconds...\n", 
                           __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str),
                           (getCurrentNanos() - start_time) / 1'000'000'000ULL);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        // Cleanup
        std::cout << "Test complete. Cleaning up..." << std::endl;
        logger->log("%:% %() % Test complete. Cleaning up...\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        // Stop components in reverse order
        running = false;
        
        // Join the trade engine thread
        if (trade_engine_thread.joinable()) {
            trade_engine_thread.join();
        }
        
        // Stop order gateway adapter
        order_gateway_adapter->stop();
        
        // Stop market data adapter
        market_data_adapter->stop();
        
        std::cout << "Zerodha Liquidity Taker test completed successfully" << std::endl;
        logger->log("%:% %() % Zerodha Liquidity Taker test completed successfully\n", 
                   __FILE__, __LINE__, __FUNCTION__, getCurrentTimeStr(&time_str));
        
        return EXIT_SUCCESS;
    } catch (const std::exception& e) {
        logger->log("%:% %() % Exception: %\n", __FILE__, __LINE__, __FUNCTION__,
                   getCurrentTimeStr(&time_str), e.what());
        return EXIT_FAILURE;
    } catch (...) {
        logger->log("%:% %() % Unknown exception\n", __FILE__, __LINE__, __FUNCTION__,
                   getCurrentTimeStr(&time_str));
        return EXIT_FAILURE;
    }
    
    // Clean up logger
    if (logger) {
        delete logger;
        logger = nullptr;
    }
    
    return EXIT_SUCCESS;
}