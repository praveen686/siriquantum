#pragma once

#include <string>
#include <functional>
#include <map>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/logging.h"
#include "common/types.h"

#include "exchange/market_data/market_update.h"

namespace Adapter {
namespace Binance {

/**
 * BinanceMarketDataAdapter receives market data from Binance WebSocket API 
 * and converts it to the internal exchange format used by the trading system.
 */
class BinanceMarketDataAdapter {
public:
    BinanceMarketDataAdapter(Common::Logger* logger, 
                              Exchange::MEMarketUpdateLFQueue* market_updates,
                              const std::string& api_key,
                              const std::string& api_secret);

    ~BinanceMarketDataAdapter();

    // Start and stop the market data consumer thread
    auto start() -> void;
    auto stop() -> void;

    // Subscribe to market data for a specific ticker
    auto subscribe(const std::string& binance_symbol, Common::TickerId internal_ticker_id) -> void;
    
    // Unsubscribe from market data for a specific ticker
    auto unsubscribe(const std::string& binance_symbol) -> void;

    // Symbol mapping functions
    auto mapBinanceSymbolToInternal(const std::string& binance_symbol) -> Common::TickerId;
    auto mapInternalToBinanceSymbol(Common::TickerId ticker_id) -> std::string;

    // Deleted default, copy & move constructors and assignment-operators
    BinanceMarketDataAdapter() = delete;
    BinanceMarketDataAdapter(const BinanceMarketDataAdapter&) = delete;
    BinanceMarketDataAdapter(const BinanceMarketDataAdapter&&) = delete;
    BinanceMarketDataAdapter& operator=(const BinanceMarketDataAdapter&) = delete;
    BinanceMarketDataAdapter& operator=(const BinanceMarketDataAdapter&&) = delete;

private:
    // API credentials
    std::string api_key_;
    std::string api_secret_;
    
    // Run flag for the thread
    volatile bool run_ = false;
    
    // Logger
    Common::Logger* logger_ = nullptr;
    std::string time_str_;
    
    // Queue for processed market data updates
    Exchange::MEMarketUpdateLFQueue* market_updates_ = nullptr;
    
    // Map of Binance symbols to internal ticker IDs
    std::map<std::string, Common::TickerId> symbol_map_;
    std::map<Common::TickerId, std::string> ticker_to_symbol_map_;
    
    // WebSocket connection status
    bool ws_connected_ = false;
    
    // The main processing thread
    auto runMarketData() -> void;
    
    // Initialize WebSocket connection
    auto initializeWebSocket() -> bool;
    
    // WebSocket message handler
    auto onWebSocketMessage(const std::string& message) -> void;
    
    // Process different types of market data from Binance
    auto processDepthUpdate(const std::string& json_data) -> void;
    auto processTrade(const std::string& json_data) -> void;
    
    // Convert Binance market data to internal format
    auto convertToInternalFormat(const std::string& binance_symbol, 
                                double price, 
                                double qty, 
                                bool is_bid,
                                bool is_trade) -> Exchange::MEMarketUpdate;
};

} // namespace Binance
} // namespace Adapter