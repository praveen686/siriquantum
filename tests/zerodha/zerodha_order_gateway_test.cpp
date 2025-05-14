#include <iostream>
#include <string>
#include <filesystem>
#include <thread>
#include <chrono>
#include <iomanip>
#include <atomic>

#include "common/logging.h"
#include "common/lf_queue.h"
#include "common/types.h"
#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"
#include "trading/adapters/zerodha/order_gw/zerodha_order_gateway_adapter.h"
#include "trading/adapters/zerodha/market_data/environment_config.h"

// Alias to avoid namespace confusion
namespace ExchangeNS = ::Exchange;

// Size of the order request and response queues
constexpr size_t ORDER_QUEUE_SIZE = 1024;

// Create a unique order ID for testing
Common::OrderId generateOrderId() {
    static Common::OrderId next_order_id = 1;
    return next_order_id++;
}

// Helper to create a new order request
ExchangeNS::MEClientRequest createNewOrderRequest(
    Common::ClientId client_id,
    Common::TickerId ticker_id,
    Common::Side side,
    Common::Price price,
    Common::Qty qty) {
    
    ExchangeNS::MEClientRequest request;
    request.type_ = ExchangeNS::ClientRequestType::NEW;
    request.client_id_ = client_id;
    request.ticker_id_ = ticker_id;
    request.order_id_ = generateOrderId();
    request.side_ = side;
    request.price_ = price;
    request.qty_ = qty;
    
    return request;
}

// Helper to create a cancel order request
ExchangeNS::MEClientRequest createCancelOrderRequest(
    Common::ClientId client_id,
    Common::TickerId ticker_id,
    Common::OrderId order_id,
    Common::Side side,
    Common::Price price,
    Common::Qty qty) {
    
    ExchangeNS::MEClientRequest request;
    request.type_ = ExchangeNS::ClientRequestType::CANCEL;
    request.client_id_ = client_id;
    request.ticker_id_ = ticker_id;
    request.order_id_ = order_id;
    request.side_ = side;
    request.price_ = price;
    request.qty_ = qty;
    
    return request;
}

// Helper to print a client response
void printClientResponse(const ExchangeNS::MEClientResponse& response, const std::string& symbol = "") {
    std::string response_type;
    switch (response.type_) {
        case ExchangeNS::ClientResponseType::ACCEPTED:
            response_type = "ACCEPTED";
            break;
        case ExchangeNS::ClientResponseType::CANCELED:
            response_type = "CANCELED";
            break;
        case ExchangeNS::ClientResponseType::FILLED:
            response_type = "FILLED";
            break;
        case ExchangeNS::ClientResponseType::CANCEL_REJECTED:
            response_type = "CANCEL_REJECTED";
            break;
        default:
            response_type = "INVALID";
            break;
    }
    
    std::cout << "Order response: " 
              << "type=" << response_type
              << ", client_id=" << response.client_id_
              << ", ticker=" << response.ticker_id_;
              
    if (!symbol.empty()) {
        std::cout << " (" << symbol << ")";
    }
    
    std::cout << ", order_id=" << response.order_id_
              << ", side=" << (response.side_ == Common::Side::BUY ? "BUY" : "SELL")
              << ", price=" << std::fixed << std::setprecision(2) << (static_cast<double>(response.price_) / 100.0)
              << ", exec_qty=" << response.exec_qty_
              << ", leaves_qty=" << response.leaves_qty_
              << std::endl;
}

int main(int argc, char** argv) {
    // Set up logs directory path for Zerodha
    std::filesystem::path logs_dir = "/home/praveen/om/siriquantum/ida/logs/zerodha";
    std::filesystem::create_directories(logs_dir); // Ensure directory exists
    
    // Initialize logger with path to logs directory
    Common::Logger logger((logs_dir / "order_gateway_test.log").string());
    
    std::string time_str;
    logger.log("%:% %() % Starting Zerodha Order Gateway Adapter Test...\n", 
               __FILE__, __LINE__, __FUNCTION__, 
               Common::getCurrentTimeStr(&time_str));
    
    try {
        std::cout << "Starting Zerodha Order Gateway Adapter Test..." << std::endl;
        
        // Default config file path if not provided on command line
        const std::string default_config_file = "/home/praveen/om/siriquantum/ida/config/trading.json";
        
        // Path to JSON config file (if provided as command line argument)
        const std::string config_file = (argc > 1) ? argv[1] : default_config_file;
        std::cout << "Using config file: " << config_file << std::endl;
        logger.log("%:% %() % Using config file: %\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str),
                 config_file.c_str());
        
        // Check if we should use paper trading mode (default to true for safety)
        bool use_paper_trading = true;
        if (argc > 2) {
            std::string mode_arg = argv[2];
            if (mode_arg == "live") {
                use_paper_trading = false;
            }
        }
        
        std::cout << "Running in " << (use_paper_trading ? "PAPER" : "LIVE") << " trading mode." << std::endl;
        logger.log("%:% %() % Running in % trading mode\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str),
                 use_paper_trading ? "PAPER" : "LIVE");
        
        // Skip loading the configuration - we'll use hardcoded values for test
        logger.log("%:% %() % Using hardcoded configuration for test\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str));
        
        // Create the lock-free queues for order requests and responses
        ExchangeNS::ClientRequestLFQueue client_requests(ORDER_QUEUE_SIZE);
        ExchangeNS::ClientResponseLFQueue client_responses(ORDER_QUEUE_SIZE);
        
        // Client ID for this test
        Common::ClientId client_id = 1;
        
        // Create test symbols with ticker IDs
        struct TestSymbol {
            std::string symbol;
            Common::TickerId ticker_id;
        };
        
        std::vector<TestSymbol> test_symbols;
        
        // Use hardcoded test symbols instead of trying to parse from config
        test_symbols.push_back({"NSE:RELIANCE", 1001});
        test_symbols.push_back({"NSE:INFY", 1002});
        test_symbols.push_back({"NSE:NIFTY50-FUT", 1003});
        
        // Display test symbols
        std::cout << "Using test symbols:" << std::endl;
        for (const auto& symbol : test_symbols) {
            std::cout << "  - " << symbol.symbol << " (ticker_id: " << symbol.ticker_id << ")" << std::endl;
        }
        
        // Create the API key and secret (in a real implementation these would be loaded from config)
        // These are placeholders - real tests would use valid credentials loaded from config
        std::string api_key = "your_api_key";
        std::string api_secret = "your_api_secret";
        
        // Attempt to get any zerodha credentials (keeping this simple for test purposes)
        // In a real system, we would extract these from a proper config
        try {
            // Just use defaults for testing
            api_key = "test_api_key";
            api_secret = "test_api_secret";
            
            logger.log("%:% %() % Using sample credentials for testing\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str));
        } catch (const std::exception& e) {
            logger.log("%:% %() % Error getting credentials: %s\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str), e.what());
        }
        
        logger.log("%:% %() % Using credentials for API access\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str));
        
        // Create the order gateway adapter
        logger.log("%:% %() % Creating Zerodha order gateway adapter\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str));
        
        Adapter::Zerodha::ZerodhaOrderGatewayAdapter order_gateway(
            &logger,
            client_id, 
            &client_requests,
            &client_responses,
            api_key,
            api_secret
        );
        
        // Configure the adapter for paper or live trading
        order_gateway.setPaperTradingMode(use_paper_trading);
        
        if (use_paper_trading) {
            // Configure paper trading parameters
            order_gateway.setPaperTradingFillProbability(0.9);  // 90% fill probability
            order_gateway.setPaperTradingLatencyRange(50, 200); // 50-200ms latency
            order_gateway.setPaperTradingSlippageFactor(0.001); // 0.1% slippage
            
            logger.log("%:% %() % Configured paper trading parameters\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str));
        } else {
            // Configure live trading parameters
            order_gateway.setOrderStatusPollInterval(1000); // 1 second polling
            
            logger.log("%:% %() % Configured live trading parameters\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str));
        }
        
        // Register the test symbols
        for (const auto& symbol : test_symbols) {
            order_gateway.registerInstrument(symbol.symbol, symbol.ticker_id);
            
            logger.log("%:% %() % Registered instrument: % (ticker_id: %)\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str),
                     symbol.symbol.c_str(), symbol.ticker_id);
        }
        
        // Start the order gateway
        logger.log("%:% %() % Starting order gateway adapter\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str));
        std::cout << "Starting order gateway adapter..." << std::endl;
        
        order_gateway.start();
        
        // Allow time for setup
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Track if we've seen any response
        std::atomic<bool> received_response(false);
        
        // Start a background thread to monitor responses
        std::thread response_thread([&]() {
            logger.log("%:% %() % Starting response monitoring thread\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str));
            
            // Monitor for responses
            while (true) {
                // Check for responses in the lock-free queue
                if (client_responses.size() > 0) {
                    // Process all available responses using the LFQueue pattern
                    for (auto response = client_responses.getNextToRead(); 
                         client_responses.size() > 0 && response != nullptr; 
                         response = client_responses.getNextToRead()) {
                        
                        received_response = true;
                        
                        // Get the symbol for this ticker ID (if available)
                        std::string symbol;
                        for (const auto& test_symbol : test_symbols) {
                            if (test_symbol.ticker_id == response->ticker_id_) {
                                symbol = test_symbol.symbol;
                                break;
                            }
                        }
                        
                        // Log the response
                        logger.log("%:% %() % Received order response: type=%, client_id=%, ticker=%, order_id=%, side=%, price=%, exec_qty=%, leaves_qty=%\n", 
                                 __FILE__, __LINE__, __FUNCTION__,
                                 Common::getCurrentTimeStr(&time_str),
                                 static_cast<int>(response->type_),
                                 response->client_id_,
                                 response->ticker_id_,
                                 response->order_id_,
                                 static_cast<int>(response->side_),
                                 static_cast<double>(response->price_) / 100.0,
                                 response->exec_qty_,
                                 response->leaves_qty_);
                        
                        // Print the response
                        printClientResponse(*response, symbol);
                        
                        // Move to next response in queue
                        client_responses.updateReadIndex();
                    }
                }
                
                // Small sleep to prevent CPU spinning
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        response_thread.detach(); // Let it run independently
        
        // Create and send test orders
        if (!test_symbols.empty()) {
            const auto& test_symbol = test_symbols[0];
            
            logger.log("%:% %() % Sending test orders for symbol % (ticker_id: %)\n", 
                     __FILE__, __LINE__, __FUNCTION__, 
                     Common::getCurrentTimeStr(&time_str),
                     test_symbol.symbol.c_str(), test_symbol.ticker_id);
            
            std::cout << "\nTest 1: Sending a market buy order" << std::endl;
            // Create a market buy order (price 0 indicates market order)
            auto market_buy = createNewOrderRequest(
                client_id,
                test_symbol.ticker_id,
                Common::Side::BUY,
                0,  // Market order (price 0)
                10  // Quantity
            );
            
            // Send the order
            std::cout << "Sending market buy order for " << test_symbol.symbol 
                      << " (ticker_id: " << test_symbol.ticker_id << ")" << std::endl;
            
            auto next_write = client_requests.getNextToWriteTo();
            *next_write = market_buy;
            client_requests.updateWriteIndex();
            
            // Wait for responses (more time if in live mode)
            std::this_thread::sleep_for(std::chrono::seconds(use_paper_trading ? 5 : 10));
            
            std::cout << "\nTest 2: Sending a limit buy order" << std::endl;
            // Create a limit buy order
            auto limit_buy = createNewOrderRequest(
                client_id,
                test_symbol.ticker_id,
                Common::Side::BUY,
                10000,  // Limit price (100.00 in price ticks)
                5       // Quantity
            );
            
            // Send the order
            std::cout << "Sending limit buy order for " << test_symbol.symbol 
                      << " at price " << std::fixed << std::setprecision(2)
                      << (static_cast<double>(limit_buy.price_) / 100.0) << std::endl;
            
            next_write = client_requests.getNextToWriteTo();
            *next_write = limit_buy;
            client_requests.updateWriteIndex();
            
            // Save the order ID for cancellation
            Common::OrderId limit_order_id = limit_buy.order_id_;
            
            // Wait for responses (more time if in live mode)
            std::this_thread::sleep_for(std::chrono::seconds(use_paper_trading ? 5 : 10));
            
            std::cout << "\nTest 3: Cancelling the limit order" << std::endl;
            // Create a cancel order request
            auto cancel_request = createCancelOrderRequest(
                client_id,
                test_symbol.ticker_id,
                limit_order_id,
                Common::Side::BUY,
                limit_buy.price_,
                limit_buy.qty_
            );
            
            // Send the cancel request
            std::cout << "Sending cancel request for order_id: " << limit_order_id << std::endl;
            
            next_write = client_requests.getNextToWriteTo();
            *next_write = cancel_request;
            client_requests.updateWriteIndex();
            
            // Wait for responses (more time if in live mode)
            std::this_thread::sleep_for(std::chrono::seconds(use_paper_trading ? 5 : 10));
            
            std::cout << "\nTest 4: Sending a limit sell order" << std::endl;
            // Create a limit sell order
            auto limit_sell = createNewOrderRequest(
                client_id,
                test_symbol.ticker_id,
                Common::Side::SELL,
                10500,  // Limit price (105.00 in price ticks)
                3       // Quantity
            );
            
            // Send the order
            std::cout << "Sending limit sell order for " << test_symbol.symbol 
                      << " at price " << std::fixed << std::setprecision(2)
                      << (static_cast<double>(limit_sell.price_) / 100.0) << std::endl;
            
            next_write = client_requests.getNextToWriteTo();
            *next_write = limit_sell;
            client_requests.updateWriteIndex();
            
            // Wait for responses (more time if in live mode)
            std::this_thread::sleep_for(std::chrono::seconds(use_paper_trading ? 5 : 10));
        }
        
        // Stop the order gateway
        logger.log("%:% %() % Stopping order gateway adapter\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str));
        std::cout << "\nStopping order gateway adapter..." << std::endl;
        
        order_gateway.stop();
        
        // Wait for any final processing
        std::this_thread::sleep_for(std::chrono::seconds(1));
        
        // Final report
        logger.log("%:% %() % Test complete. Result: %\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str),
                 received_response ? "SUCCESS" : "NO RESPONSES RECEIVED");
        
        std::cout << "\nTest complete. Result: " 
                  << (received_response ? "SUCCESS" : "NO RESPONSES RECEIVED") << std::endl;
        
        return received_response ? 0 : 1;
        
    } catch (const std::exception& e) {
        logger.log("%:% %() % ERROR: %\n", 
                 __FILE__, __LINE__, __FUNCTION__, 
                 Common::getCurrentTimeStr(&time_str),
                 e.what());
        std::cerr << "\nERROR: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}