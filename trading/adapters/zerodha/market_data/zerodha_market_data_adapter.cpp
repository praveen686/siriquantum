#include "trading/adapters/zerodha/market_data/zerodha_market_data_adapter.h"
#include <nlohmann/json.hpp>

namespace Adapter {
namespace Zerodha {

ZerodhaMarketDataAdapter::ZerodhaMarketDataAdapter(Common::Logger* logger, 
                                               ExchangeNS::MEMarketUpdateLFQueue* market_updates,
                                               const std::string& config_file)
    : logger_(logger),
      market_updates_(market_updates),
      zerodha_updates_(ZERODHA_QUEUE_SIZE) {
    
    logger_->log("%:% %() % Initializing Zerodha Market Data Adapter with JSON config file: %\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                config_file.c_str());
                
    // Create environment config with JSON config only
    config_ = std::make_unique<EnvironmentConfig>(logger_, "", config_file);
    if (!config_->load()) {
        logger_->log("%:% %() % Failed to load configuration, cannot initialize\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }
    
    // Create authenticator from configuration
    authenticator_ = std::make_unique<ZerodhaAuthenticator>(
        ZerodhaAuthenticator::from_config(logger_, 
                                        *config_, 
                                        config_->getInstrumentsCacheDir()));
    
    // Initialize token manager
    initialize();
}

ZerodhaMarketDataAdapter::~ZerodhaMarketDataAdapter() {
    stop();
    
    using namespace std::literals::chrono_literals;
    std::this_thread::sleep_for(5s);
    
    logger_->log("%:% %() % Destroyed Zerodha Market Data Adapter\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
}

auto ZerodhaMarketDataAdapter::initialize() -> void {
    if (!authenticator_) {
        logger_->log("%:% %() % Cannot initialize: no authenticator available\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }
    
    logger_->log("%:% %() % Initializing InstrumentTokenManager\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
                
    // Create token manager
    token_manager_ = std::make_unique<InstrumentTokenManager>(
        authenticator_.get(),
        logger_,
        config_ ? config_->getInstrumentsCacheDir() : ".cache/zerodha"
    );
    
    // Pre-initialize token manager to download instrument data
    if (!token_manager_->initialize()) {
        logger_->log("%:% %() % Warning: Failed to initialize token manager\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
    }
}

auto ZerodhaMarketDataAdapter::start() -> void {
    logger_->log("%:% %() % Starting Zerodha Market Data Adapter\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    // First authenticate
    if (!authenticate()) {
        logger_->log("%:% %() % Failed to authenticate with Zerodha. Cannot start market data adapter.\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }
    
    // Get API key from configuration
    std::string api_key = config_->getApiKey();
    
    if (api_key.empty()) {
        logger_->log("%:% %() % No API key found in configuration for WebSocket client\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }
    
    websocket_client_ = std::make_unique<ZerodhaWebSocketClient>(
        api_key,
        authenticator_->get_access_token(),
        zerodha_updates_,
        logger_
    );
    
    // Start market data thread
    run_ = true;
    market_data_thread_ = std::thread([this]() { runMarketData(); });
}

auto ZerodhaMarketDataAdapter::stop() -> void {
    run_ = false;
    
    logger_->log("%:% %() % Stopping Zerodha Market Data Adapter\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
                
    // Disconnect WebSocket
    if (websocket_client_) {
        websocket_client_->disconnect();
    }
    
    // Wait for thread to finish
    if (market_data_thread_.joinable()) {
        market_data_thread_.join();
    }
}

auto ZerodhaMarketDataAdapter::subscribe(const std::string& zerodha_symbol, Common::TickerId internal_ticker_id) -> void {
    // Format and resolve symbol based on configuration
    std::string formatted_symbol = zerodha_symbol;
    if (config_) {
        formatted_symbol = config_->formatSymbol(zerodha_symbol);
        formatted_symbol = config_->resolveSymbol(formatted_symbol);
    }
    
    logger_->log("%:% %() % Subscribing to Zerodha symbol: % (internal ID: %)\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                formatted_symbol.c_str(), internal_ticker_id);
    
    // Thread-safe access to maps
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        symbol_map_[formatted_symbol] = internal_ticker_id;
        ticker_to_symbol_map_[internal_ticker_id] = formatted_symbol;
    }
    
    // Get instrument token using the token manager
    int32_t token = 0;
    if (token_manager_) {
        token = token_manager_->getInstrumentToken(formatted_symbol);
    }
    
    if (token == 0) {
        logger_->log("%:% %() % Could not find instrument token for symbol: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    formatted_symbol.c_str());
        return;
    }
    
    // Map token to ticker_id
    {
        std::lock_guard<std::mutex> lock(token_mutex_);
        token_to_ticker_map_[token] = internal_ticker_id;
    }
    
    // Create order book for this ticker
    {
        std::lock_guard<std::mutex> lock(order_book_mutex_);
        if (order_books_.find(internal_ticker_id) == order_books_.end()) {
            order_books_[internal_ticker_id] = std::make_unique<ZerodhaOrderBook>(internal_ticker_id, logger_);
            logger_->log("%:% %() % Created order book for symbol: % (ID: %)\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        formatted_symbol.c_str(), internal_ticker_id);
        }
    }
    
    logger_->log("%:% %() % Found token % for symbol %\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                token, formatted_symbol.c_str());
    
    // Subscribe to the token via WebSocket if connected
    if (websocket_client_ && websocket_client_->is_connected()) {
        websocket_client_->subscribe({token}, StreamingMode::FULL);
    } else {
        logger_->log("%:% %() % WebSocket not connected, queueing subscription for %\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    formatted_symbol.c_str());
    }
}

auto ZerodhaMarketDataAdapter::unsubscribe(const std::string& zerodha_symbol) -> void {
    // Format and resolve symbol based on configuration
    std::string formatted_symbol = zerodha_symbol;
    if (config_) {
        formatted_symbol = config_->formatSymbol(zerodha_symbol);
        formatted_symbol = config_->resolveSymbol(formatted_symbol);
    }
    
    logger_->log("%:% %() % Unsubscribing from Zerodha symbol: %\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                formatted_symbol.c_str());
    
    // Get the instrument token
    int32_t token = 0;
    if (token_manager_) {
        token = token_manager_->getInstrumentToken(formatted_symbol);
    }
    
    // Find internal ticker ID
    Common::TickerId ticker_id = Common::TickerId_INVALID;
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        auto symbol_it = symbol_map_.find(formatted_symbol);
        if (symbol_it != symbol_map_.end()) {
            ticker_id = symbol_it->second;
        }
    }
    
    // Remove order book
    if (ticker_id != Common::TickerId_INVALID) {
        std::lock_guard<std::mutex> lock(order_book_mutex_);
        order_books_.erase(ticker_id);
    }
    
    // Remove token mapping
    if (token != 0) {
        std::lock_guard<std::mutex> lock(token_mutex_);
        token_to_ticker_map_.erase(token);
    }
    
    // Remove from maps
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        
        // Find internal ticker ID
        auto symbol_it = symbol_map_.find(formatted_symbol);
        if (symbol_it != symbol_map_.end()) {
            Common::TickerId ticker_id = symbol_it->second;
            ticker_to_symbol_map_.erase(ticker_id);
        }
        
        symbol_map_.erase(formatted_symbol);
    }
    
    // Unsubscribe via WebSocket
    if (token != 0 && websocket_client_ && websocket_client_->is_connected()) {
        websocket_client_->unsubscribe({token});
    }
}

auto ZerodhaMarketDataAdapter::mapZerodhaSymbolToInternal(const std::string& zerodha_symbol) -> Common::TickerId {
    // Format and resolve symbol based on configuration
    std::string formatted_symbol = zerodha_symbol;
    if (config_) {
        formatted_symbol = config_->formatSymbol(zerodha_symbol);
        formatted_symbol = config_->resolveSymbol(formatted_symbol);
    }
    
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    auto it = symbol_map_.find(formatted_symbol);
    if (it != symbol_map_.end()) {
        return it->second;
    }
    return Common::TickerId_INVALID;
}

auto ZerodhaMarketDataAdapter::mapInternalToZerodhaSymbol(Common::TickerId ticker_id) -> std::string {
    std::lock_guard<std::mutex> lock(mapping_mutex_);
    auto it = ticker_to_symbol_map_.find(ticker_id);
    if (it != ticker_to_symbol_map_.end()) {
        return it->second;
    }
    return "";
}

auto ZerodhaMarketDataAdapter::getOrderBook(Common::TickerId ticker_id) -> ZerodhaOrderBook* {
    std::lock_guard<std::mutex> lock(order_book_mutex_);
    auto it = order_books_.find(ticker_id);
    if (it != order_books_.end()) {
        return it->second.get();
    }
    return nullptr;
}

auto ZerodhaMarketDataAdapter::mapZerodhaInstrumentToInternal(int32_t instrument_token) -> Common::TickerId {
    std::lock_guard<std::mutex> lock(token_mutex_);
    auto it = token_to_ticker_map_.find(instrument_token);
    if (it != token_to_ticker_map_.end()) {
        return it->second;
    }
    return Common::TickerId_INVALID;
}

auto ZerodhaMarketDataAdapter::isConnected() const -> bool {
    return websocket_client_ && websocket_client_->is_connected();
}

auto ZerodhaMarketDataAdapter::subscribeToTestSymbols() -> void {
    if (!config_) {
        logger_->log("%:% %() % Cannot subscribe to test symbols: no config available\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }
    
    const auto& test_symbols = config_->getTestSymbols();
    logger_->log("%:% %() % Subscribing to % test symbols\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                test_symbols.size());
    
    for (const auto& symbol : test_symbols) {
        // Create a deterministic ticker ID based on symbol hash
        auto ticker_id = static_cast<Common::TickerId>(std::hash<std::string>{}(symbol) % 10000);
        subscribe(symbol, ticker_id);
    }
}

auto ZerodhaMarketDataAdapter::authenticate() -> bool {
    logger_->log("%:% %() % Authenticating with Zerodha\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    if (!authenticator_) {
        logger_->log("%:% %() % No authenticator available\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    std::string access_token = authenticator_->authenticate();
    
    if (access_token.empty()) {
        logger_->log("%:% %() % Authentication failed\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    logger_->log("%:% %() % Successfully authenticated\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    return true;
}

auto ZerodhaMarketDataAdapter::runMarketData() -> void {
    logger_->log("%:% %() % Zerodha Market Data thread started\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    // Connect to WebSocket
    if (websocket_client_) {
        if (!websocket_client_->connect()) {
            logger_->log("%:% %() % Failed to connect to Zerodha WebSocket\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_));
            return;
        }
        
        logger_->log("%:% %() % Connected to Zerodha WebSocket\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        
        // Re-subscribe to all symbols
        std::vector<int32_t> tokens;
        {
            std::lock_guard<std::mutex> lock(mapping_mutex_);
            for (const auto& [symbol, ticker_id] : symbol_map_) {
                if (token_manager_) {
                    int32_t token = token_manager_->getInstrumentToken(symbol);
                    if (token != 0) {
                        tokens.push_back(token);
                    }
                }
            }
        }
        
        // Subscribe to all tokens at once
        if (!tokens.empty()) {
            logger_->log("%:% %() % Subscribing to % tokens\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        tokens.size());
            websocket_client_->subscribe(tokens, StreamingMode::FULL);
        }
    }
    
    // Process market data
    while (run_) {
        // Check if token manager needs refresh
        if (token_manager_ && token_manager_->shouldRefresh()) {
            logger_->log("%:% %() % Refreshing instrument token data\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_));
            token_manager_->updateInstrumentData();
        }
        
        // Process market updates
        processMarketUpdates();
        
        // Small sleep to prevent CPU spinning
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    
    logger_->log("%:% %() % Zerodha Market Data thread stopped\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
}

auto ZerodhaMarketDataAdapter::processMarketUpdates() -> void {
    // Process all available updates from the queue
    for (auto update = zerodha_updates_.getNextToRead(); 
         zerodha_updates_.size() > 0 && update != nullptr; 
         update = zerodha_updates_.getNextToRead()) {
        
        // Process the update
        processMarketUpdate(*update);
        
        // Update the read index
        zerodha_updates_.updateReadIndex();
    }
}

auto ZerodhaMarketDataAdapter::processMarketUpdate(const MarketUpdate& update) -> void {
    // Get ticker ID for this instrument
    auto ticker_id = mapZerodhaInstrumentToInternal(update.instrument_token);
    if (ticker_id == Common::TickerId_INVALID) {
        // Try to get instrument info and map from there
        if (token_manager_) {
            auto instrument_info_opt = token_manager_->getInstrumentInfo(update.instrument_token);
            if (instrument_info_opt.has_value()) {
                std::string symbol = instrument_info_opt->exchange != Adapter::Zerodha::Exchange::UNKNOWN ?
                    InstrumentTokenManager::exchangeToString(instrument_info_opt->exchange) + ":" + instrument_info_opt->trading_symbol :
                    instrument_info_opt->trading_symbol;
                
                ticker_id = mapZerodhaSymbolToInternal(symbol);
            }
        }
        
        // Still can't find ticker ID, skip this update
        if (ticker_id == Common::TickerId_INVALID) {
            return;
        }
    }
    
    // Get or create order book for this ticker
    ZerodhaOrderBook* order_book = nullptr;
    {
        std::lock_guard<std::mutex> lock(order_book_mutex_);
        auto it = order_books_.find(ticker_id);
        if (it == order_books_.end()) {
            // Create new order book
            order_books_[ticker_id] = std::make_unique<ZerodhaOrderBook>(ticker_id, logger_);
            order_book = order_books_[ticker_id].get();
            
            logger_->log("%:% %() % Created order book for ticker ID: %\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        ticker_id);
        } else {
            order_book = it->second.get();
        }
    }
    
    // Process update through order book
    if (order_book) {
        // Convert Zerodha timestamp to std::chrono::time_point
        auto update_time = std::chrono::system_clock::from_time_t(update.exchange_timestamp);
        auto current_time = std::chrono::system_clock::now();
        auto latency = std::chrono::duration_cast<std::chrono::milliseconds>(
            current_time - update_time).count();
        
        // Occasionally log the update (to avoid excessive logging)
        static int log_counter = 0;
        if (++log_counter % 100 == 0) {
            logger_->log("%:% %() % Received market update for % (ID: %): price=%, latency=%ms\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        mapInternalToZerodhaSymbol(ticker_id).c_str(), ticker_id,
                        update.last_price, latency);
        }
        
        // Process update and get generated events
        auto events = order_book->processMarketUpdate(update);
        
        // Push all generated events to the market updates queue
        for (auto* event : events) {
            auto next_write = market_updates_->getNextToWriteTo();
            *next_write = *event;
            market_updates_->updateWriteIndex();
            
            // Return the event to the memory pool
            // We don't have direct access to order_book's memory pool,
            // so we can just let the memory be reclaimed when we get another update
        }
    }
}

auto ZerodhaMarketDataAdapter::convertToInternalFormat(const MarketUpdate& update) -> std::vector<ExchangeNS::MEMarketUpdate*> {
    // Get ticker ID for this instrument
    auto ticker_id = mapZerodhaInstrumentToInternal(update.instrument_token);
    
    // If we can't map the token, try to find it through symbol mapping
    if (ticker_id == Common::TickerId_INVALID && token_manager_) {
        auto instrument_info_opt = token_manager_->getInstrumentInfo(update.instrument_token);
        if (instrument_info_opt.has_value()) {
            std::string symbol = instrument_info_opt->exchange != Adapter::Zerodha::Exchange::UNKNOWN ?
                InstrumentTokenManager::exchangeToString(instrument_info_opt->exchange) + ":" + instrument_info_opt->trading_symbol :
                instrument_info_opt->trading_symbol;
            
            ticker_id = mapZerodhaSymbolToInternal(symbol);
        }
    }
    
    // If still not found, return empty result
    if (ticker_id == Common::TickerId_INVALID) {
        return std::vector<ExchangeNS::MEMarketUpdate*>();
    }
    
    // Get or create order book for this ticker
    ZerodhaOrderBook* order_book = nullptr;
    {
        std::lock_guard<std::mutex> lock(order_book_mutex_);
        auto it = order_books_.find(ticker_id);
        if (it == order_books_.end()) {
            // Create new order book
            order_books_[ticker_id] = std::make_unique<ZerodhaOrderBook>(ticker_id, logger_);
            order_book = order_books_[ticker_id].get();
        } else {
            order_book = it->second.get();
        }
    }
    
    // Process update through order book
    if (order_book) {
        return order_book->processMarketUpdate(update);
    }
    
    return std::vector<ExchangeNS::MEMarketUpdate*>();
}

auto ZerodhaMarketDataAdapter::convertToInternalFormat(const std::string& zerodha_symbol, 
                                                    double price, 
                                                    double qty, 
                                                    bool is_bid,
                                                    bool is_trade) -> ExchangeNS::MEMarketUpdate {
    ExchangeNS::MEMarketUpdate update;
    
    // Format and resolve symbol based on configuration
    std::string formatted_symbol = zerodha_symbol;
    if (config_) {
        formatted_symbol = config_->formatSymbol(zerodha_symbol);
        formatted_symbol = config_->resolveSymbol(formatted_symbol);
    }
    
    // Set basic fields
    update.ticker_id_ = mapZerodhaSymbolToInternal(formatted_symbol);
    update.price_ = static_cast<Common::Price>(price * 100.0);  // Convert to price ticks (cents)
    update.qty_ = static_cast<Common::Qty>(qty);
    update.side_ = is_bid ? Common::Side::BUY : Common::Side::SELL;
    
    // Set update type based on whether it's a trade or order book update
    if (is_trade) {
        update.type_ = ExchangeNS::MarketUpdateType::TRADE;
    } else {
        update.type_ = ExchangeNS::MarketUpdateType::ADD;  // For simplicity, treat all as adds
    }
    
    // Generate a deterministic order ID based on symbol and price
    int32_t token = token_manager_ ? token_manager_->getInstrumentToken(formatted_symbol) : 0;
    if (token == 0) {
        std::hash<std::string> hash_fn;
        token = static_cast<int32_t>(hash_fn(formatted_symbol) % 1000000);
    }
    
    update.order_id_ = ZerodhaOrderBook::generateOrderId(
        update.ticker_id_, price, update.side_);
    
    // Set a default priority
    update.priority_ = 1;
    
    return update;
}

auto ZerodhaMarketDataAdapter::onReconnect() -> void {
    logger_->log("%:% %() % WebSocket reconnected. Clearing all order books...\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    // Clear all order books and generate CLEAR events
    {
        std::lock_guard<std::mutex> lock(order_book_mutex_);
        for (auto& [ticker_id, book] : order_books_) {
            auto clear_events = book->clear();
            
            // Push all clear events to the market updates queue
            for (auto* event : clear_events) {
                auto next_write = market_updates_->getNextToWriteTo();
                *next_write = *event;
                market_updates_->updateWriteIndex();
                
                // Memory will be reclaimed on next update
            }
        }
    }
    
    // Re-subscribe to all instruments
    std::vector<int32_t> tokens;
    {
        std::lock_guard<std::mutex> lock(mapping_mutex_);
        for (const auto& [symbol, ticker_id] : symbol_map_) {
            if (token_manager_) {
                int32_t token = token_manager_->getInstrumentToken(symbol);
                if (token != 0) {
                    tokens.push_back(token);
                }
            }
        }
    }
    
    // Re-subscribe in FULL mode
    if (!tokens.empty() && websocket_client_ && websocket_client_->is_connected()) {
        logger_->log("%:% %() % Re-subscribing to % tokens\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    tokens.size());
        websocket_client_->subscribe(tokens, StreamingMode::FULL);
    }
}

} // namespace Zerodha
} // namespace Adapter