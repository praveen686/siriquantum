#include <csignal>
#include <string>
#include <memory>

#include "strategy/trade_engine.h"
#include "order_gw/order_gateway.h"
#include "market_data/market_data_consumer.h"

#include "adapters/adapter_factory.h"
#include "adapters/zerodha/market_data/zerodha_market_data_adapter.h"
#include "adapters/zerodha/order_gw/zerodha_order_gateway_adapter.h"
#include "adapters/binance/market_data/binance_market_data_adapter.h"
#include "adapters/binance/order_gw/binance_order_gateway_adapter.h"

#include "common/logging.h"

// Alias to avoid namespace confusion - same as in zerodha_market_data_adapter.h
namespace ExchangeNS = ::Exchange;

/// Main components.
Common::Logger *logger = nullptr;
Trading::TradeEngine *trade_engine = nullptr;

// Use std::unique_ptr for proper memory management of the adapters
std::unique_ptr<void> market_data_adapter = nullptr;
std::unique_ptr<void> order_gateway_adapter = nullptr;

// Function to clean up resources
void cleanup() {
    if (trade_engine) {
        trade_engine->stop();
        delete trade_engine;
        trade_engine = nullptr;
    }
    
    // Adapters will be automatically cleaned up by unique_ptr
    market_data_adapter.reset();
    order_gateway_adapter.reset();
    
    if (logger) {
        delete logger;
        logger = nullptr;
    }
}

// Signal handler
void signalHandler(int signal) {
    logger->log("%:% %() % Received signal %. Shutting down...\n", __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&std::string()), signal);
    cleanup();
    exit(EXIT_SUCCESS);
}

/// ./trading_main CLIENT_ID ALGO_TYPE EXCHANGE_TYPE API_KEY API_SECRET [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...
int main(int argc, char **argv) {
    if(argc < 6) {
        std::cerr << "USAGE trading_main CLIENT_ID ALGO_TYPE EXCHANGE_TYPE API_KEY API_SECRET [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...\n";
        std::cerr << "EXCHANGE_TYPE can be ZERODHA or BINANCE\n";
        return EXIT_FAILURE;
    }

    // Register signal handlers
    signal(SIGINT, signalHandler);
    signal(SIGTERM, signalHandler);

    const Common::ClientId client_id = atoi(argv[1]);
    srand(client_id);

    const auto algo_type = Common::stringToAlgoType(argv[2]);
    
    // Parse exchange type
    std::string exchange_type_str = argv[3];
    Adapter::ExchangeType exchange_type;
    
    if (exchange_type_str == "ZERODHA") {
        exchange_type = Adapter::ExchangeType::ZERODHA;
    } else if (exchange_type_str == "BINANCE") {
        exchange_type = Adapter::ExchangeType::BINANCE;
    } else {
        std::cerr << "Invalid EXCHANGE_TYPE. Must be ZERODHA or BINANCE\n";
        return EXIT_FAILURE;
    }
    
    // API credentials
    std::string api_key = argv[4];
    std::string api_secret = argv[5];

    logger = new Common::Logger("trading_main_" + std::to_string(client_id) + ".log");

    // The lock free queues to facilitate communication between order gateway <-> trade engine and market data consumer -> trade engine.
    ExchangeNS::ClientRequestLFQueue client_requests(Common::ME_MAX_CLIENT_UPDATES);
    ExchangeNS::ClientResponseLFQueue client_responses(Common::ME_MAX_CLIENT_UPDATES);
    ExchangeNS::MEMarketUpdateLFQueue market_updates(Common::ME_MAX_MARKET_UPDATES);

    std::string time_str;

    Common::TradeEngineCfgHashMap ticker_cfg;

    // Parse and initialize the TradeEngineCfgHashMap above from the command line arguments.
    // [CLIP_1 THRESH_1 MAX_ORDER_SIZE_1 MAX_POS_1 MAX_LOSS_1] [CLIP_2 THRESH_2 MAX_ORDER_SIZE_2 MAX_POS_2 MAX_LOSS_2] ...
    size_t next_ticker_id = 0;
    for (int i = 6; i < argc; i += 5, ++next_ticker_id) {
        ticker_cfg.at(next_ticker_id) = {static_cast<Common::Qty>(std::atoi(argv[i])), std::atof(argv[i + 1]),
                                      {static_cast<Common::Qty>(std::atoi(argv[i + 2])),
                                       static_cast<Common::Qty>(std::atoi(argv[i + 3])),
                                       std::atof(argv[i + 4])}};
    }

    logger->log("%:% %() % Starting Trade Engine...\n", __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    trade_engine = new Trading::TradeEngine(client_id, algo_type,
                                          ticker_cfg,
                                          &client_requests,
                                          &client_responses,
                                          &market_updates);
    trade_engine->start();

    logger->log("%:% %() % Starting Order Gateway Adapter for %...\n", __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str), exchange_type_str.c_str());
    
    // Create and start the order gateway adapter
    order_gateway_adapter = Adapter::AdapterFactory::createOrderGatewayAdapter(
        exchange_type, 
        logger, 
        client_id, 
        &client_requests, 
        &client_responses, 
        api_key, 
        api_secret
    );
    
    // Need to cast to the correct type
    if (exchange_type == Adapter::ExchangeType::ZERODHA) {
        auto* zerodha_adapter = static_cast<Adapter::Zerodha::ZerodhaOrderGatewayAdapter*>(order_gateway_adapter.get());
        zerodha_adapter->start();
        
        // Register instruments - in a real implementation, we would read this from a config file
        zerodha_adapter->registerInstrument("NIFTY-FUT", 0);
        zerodha_adapter->registerInstrument("RELIANCE-EQ", 1);
        zerodha_adapter->registerInstrument("HDFC-EQ", 2);
    } else if (exchange_type == Adapter::ExchangeType::BINANCE) {
        auto* binance_adapter = static_cast<Adapter::Binance::BinanceOrderGatewayAdapter*>(order_gateway_adapter.get());
        binance_adapter->start();
        
        // Register instruments - in a real implementation, we would read this from a config file
        binance_adapter->registerInstrument("BTCUSDT", 0);
        binance_adapter->registerInstrument("ETHUSDT", 1);
        binance_adapter->registerInstrument("XRPUSDT", 2);
    }

    logger->log("%:% %() % Starting Market Data Adapter for %...\n", __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str), exchange_type_str.c_str());
    
    // Create and start the market data adapter
    market_data_adapter = Adapter::AdapterFactory::createMarketDataAdapter(
        exchange_type,
        logger,
        &market_updates,
        api_key,
        api_secret
    );
    
    // Need to cast to the correct type
    if (exchange_type == Adapter::ExchangeType::ZERODHA) {
        auto* zerodha_adapter = static_cast<Adapter::Zerodha::ZerodhaMarketDataAdapter*>(market_data_adapter.get());
        zerodha_adapter->start();
        
        // Subscribe to market data - in a real implementation, we would read this from a config file
        zerodha_adapter->subscribe("NIFTY-FUT", 0);
        zerodha_adapter->subscribe("RELIANCE-EQ", 1);
        zerodha_adapter->subscribe("HDFC-EQ", 2);
    } else if (exchange_type == Adapter::ExchangeType::BINANCE) {
        auto* binance_adapter = static_cast<Adapter::Binance::BinanceMarketDataAdapter*>(market_data_adapter.get());
        binance_adapter->start();
        
        // Subscribe to market data - in a real implementation, we would read this from a config file
        binance_adapter->subscribe("BTCUSDT", 0);
        binance_adapter->subscribe("ETHUSDT", 1);
        binance_adapter->subscribe("XRPUSDT", 2);
    }

    // Wait for market data and order adapters to initialize
    usleep(10 * 1000 * 1000);

    trade_engine->initLastEventTime();

    // Main loop to keep the application running
    while (trade_engine->silentSeconds() < 60) {
        logger->log("%:% %() % Waiting till no activity, been silent for % seconds...\n", 
                    __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str), trade_engine->silentSeconds());

        using namespace std::literals::chrono_literals;
        std::this_thread::sleep_for(30s);
    }

    // Clean up resources
    cleanup();

    return EXIT_SUCCESS;
}