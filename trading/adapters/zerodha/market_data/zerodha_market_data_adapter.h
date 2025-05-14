#pragma once

#include <string>
#include <functional>
#include <map>
#include <memory>
#include <thread>
#include <mutex>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/logging.h"
#include "common/types.h"

#include "exchange/market_data/market_update.h"

// Alias to avoid namespace confusion
namespace ExchangeNS = ::Exchange;
#include "trading/adapters/zerodha/auth/zerodha_authenticator.h"
#include "trading/adapters/zerodha/market_data/zerodha_websocket_client.h"
#include "trading/adapters/zerodha/market_data/instrument_token_manager.h"
#include "trading/adapters/zerodha/market_data/environment_config.h"
#include "trading/adapters/zerodha/market_data/orderbook/zerodha_order_book.h"

namespace Adapter {
namespace Zerodha {

/**
 * ZerodhaMarketDataAdapter receives market data from Zerodha and converts it 
 * to the internal exchange format used by the trading system.
 * 
 * This adapter:
 * - Connects to Zerodha WebSocket API
 * - Subscribes to market data for specified symbols
 * - Automatically handles token lookup using InstrumentTokenManager
 * - Converts Zerodha market data to internal exchange format
 * - Supports special handling for indices via .env configuration
 * - Maintains full limit order books for all subscribed instruments
 */
class ZerodhaMarketDataAdapter {
public:
    /**
     * Constructor using JSON configuration file
     * 
     * @param logger Logger for diagnostic messages
     * @param market_updates Output queue for processed market updates
     * @param config_file Path to JSON configuration file
     */
    ZerodhaMarketDataAdapter(Common::Logger* logger, 
                             ExchangeNS::MEMarketUpdateLFQueue* market_updates,
                             const std::string& config_file);

    /**
     * Destructor
     */
    ~ZerodhaMarketDataAdapter();

    /**
     * Start the market data adapter
     * 
     * Initializes the WebSocket connection and starts processing threads
     */
    auto start() -> void;
    
    /**
     * Stop the market data adapter
     * 
     * Closes the WebSocket connection and stops processing threads
     */
    auto stop() -> void;

    /**
     * Subscribe to market data for a specific symbol
     * 
     * @param zerodha_symbol Symbol to subscribe to (format: EXCHANGE:SYMBOL)
     * @param internal_ticker_id Internal ticker ID to map to this symbol
     */
    auto subscribe(const std::string& zerodha_symbol, Common::TickerId internal_ticker_id) -> void;
    
    /**
     * Unsubscribe from market data for a specific symbol
     * 
     * @param zerodha_symbol Symbol to unsubscribe from
     */
    auto unsubscribe(const std::string& zerodha_symbol) -> void;

    /**
     * Map Zerodha symbol to internal ticker ID
     * 
     * @param zerodha_symbol Zerodha symbol to map
     * @return Internal ticker ID or TickerId_INVALID if not found
     */
    auto mapZerodhaSymbolToInternal(const std::string& zerodha_symbol) -> Common::TickerId;
    
    /**
     * Map internal ticker ID to Zerodha symbol
     * 
     * @param ticker_id Internal ticker ID to map
     * @return Zerodha symbol or empty string if not found
     */
    auto mapInternalToZerodhaSymbol(Common::TickerId ticker_id) -> std::string;
    
    /**
     * Check if connected to Zerodha WebSocket
     * 
     * @return true if connected to Zerodha WebSocket
     */
    auto isConnected() const -> bool;
    
    /**
     * Subscribe to all test symbols defined in environment
     * 
     * Useful for testing the adapter with pre-configured symbols
     */
    auto subscribeToTestSymbols() -> void;

    /**
     * Get order book for a specific ticker
     * 
     * @param ticker_id Internal ticker ID
     * @return Pointer to order book or nullptr if not found
     */
    auto getOrderBook(Common::TickerId ticker_id) -> ZerodhaOrderBook*;
    
    /**
     * Get order book for a specific ticker (const version)
     * 
     * @param ticker_id Internal ticker ID
     * @return Const pointer to order book or nullptr if not found
     */
    auto getOrderBook(Common::TickerId ticker_id) const -> const ZerodhaOrderBook*;
    
    /**
     * Map Zerodha instrument token to internal ticker ID
     * 
     * @param instrument_token Zerodha instrument token
     * @return Internal ticker ID or TickerId_INVALID if not found
     */
    auto mapZerodhaInstrumentToInternal(int32_t instrument_token) -> Common::TickerId;

    // Deleted default, copy & move constructors and assignment-operators
    ZerodhaMarketDataAdapter() = delete;
    ZerodhaMarketDataAdapter(const ZerodhaMarketDataAdapter&) = delete;
    ZerodhaMarketDataAdapter(const ZerodhaMarketDataAdapter&&) = delete;
    ZerodhaMarketDataAdapter& operator=(const ZerodhaMarketDataAdapter&) = delete;
    ZerodhaMarketDataAdapter& operator=(const ZerodhaMarketDataAdapter&&) = delete;

private:
    // Common initialization logic
    auto initialize() -> void;
    
    // The main processing thread
    auto runMarketData() -> void;
    
    // Process market data updates from Zerodha
    auto processMarketUpdates() -> void;
    
    // Process a single market update
    auto processMarketUpdate(const MarketUpdate& update) -> void;
    
    // Authenticate with Zerodha API
    auto authenticate() -> bool;
    
    // Convert Zerodha market data to internal format
    auto convertToInternalFormat(const MarketUpdate& update) -> std::vector<ExchangeNS::MEMarketUpdate*>;
    auto convertToInternalFormat(const std::string& zerodha_symbol, 
                                double price, 
                                double qty, 
                                bool is_bid,
                                bool is_trade) -> ExchangeNS::MEMarketUpdate;

    // Handle WebSocket reconnection
    auto onReconnect() -> void;

private:
    // Logger
    Common::Logger* logger_ = nullptr;
    std::string time_str_;
    
    // Queue for processed market data updates
    ExchangeNS::MEMarketUpdateLFQueue* market_updates_ = nullptr;
    
    // LF Queue for Zerodha market updates from WebSocket
    Common::LFQueue<MarketUpdate> zerodha_updates_;
    
    // Map of Zerodha symbols to internal ticker IDs
    std::map<std::string, Common::TickerId> symbol_map_;
    std::map<Common::TickerId, std::string> ticker_to_symbol_map_;
    std::mutex mapping_mutex_;
    
    // Map of instrument tokens to internal ticker IDs
    std::map<int32_t, Common::TickerId> token_to_ticker_map_;
    std::mutex token_mutex_;
    
    // Order books for each subscribed instrument
    std::map<Common::TickerId, std::unique_ptr<ZerodhaOrderBook>> order_books_;
    mutable std::mutex order_book_mutex_;
    
    // Core components
    std::unique_ptr<EnvironmentConfig> config_;
    std::unique_ptr<ZerodhaAuthenticator> authenticator_;
    std::unique_ptr<InstrumentTokenManager> token_manager_;
    std::unique_ptr<ZerodhaWebSocketClient> websocket_client_;
    
    // Thread for processing market data
    std::thread market_data_thread_;
    
    // Run flag for the thread
    volatile bool run_ = false;
    
    // Queue size constants
    static constexpr size_t ZERODHA_QUEUE_SIZE = 10 * 1024;
};

} // namespace Zerodha
} // namespace Adapter