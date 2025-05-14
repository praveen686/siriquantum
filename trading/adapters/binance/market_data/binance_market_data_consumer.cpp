#include "binance_market_data_consumer.h"
#include <iostream>
#include <thread>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <filesystem>
#include <boost/beast/version.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/error.hpp>
#include <boost/asio/ssl/stream.hpp>

namespace Trading {

// HttpClient implementation
HttpClient::HttpClient(net::io_context& ioc, ssl::context& ctx, const std::string& host, const std::string& port)
    : host_(host), port_(port), ioc_(ioc), ctx_(ctx) {
}

std::string HttpClient::get(const std::string& target, const std::map<std::string, std::string>& params) {
    try {
        // Create the query string from parameters
        std::string query_string;
        if (!params.empty()) {
            query_string = "?";
            for (auto it = params.begin(); it != params.end(); ++it) {
                if (it != params.begin()) query_string += "&";
                query_string += it->first + "=" + it->second;
            }
        }

        // The io_context is required for all I/O
        tcp::resolver resolver(ioc_);

        // These objects perform our I/O
        beast::ssl_stream<beast::tcp_stream> stream(ioc_, ctx_);

        // Set SNI Hostname (many hosts need this to handshake successfully)
        if(!SSL_set_tlsext_host_name(stream.native_handle(), host_.c_str())) {
            beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
            throw beast::system_error{ec};
        }

        // Look up the domain name
        auto const results = resolver.resolve(host_, port_);

        // Make the connection on the IP address we get from a lookup
        beast::get_lowest_layer(stream).connect(results);

        // Perform the SSL handshake
        stream.handshake(ssl::stream_base::client);

        // Set up an HTTP GET request message
        http::request<http::string_body> req{http::verb::get, target + query_string, 11};
        req.set(http::field::host, host_);
        req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

        // Send the HTTP request to the remote host
        http::write(stream, req);

        // This buffer is used for reading and must be persisted
        beast::flat_buffer buffer;

        // Declare a container to hold the response
        http::response<http::dynamic_body> res;

        // Receive the HTTP response
        http::read(stream, buffer, res);

        // Convert response to string
        std::string response_body = beast::buffers_to_string(res.body().data());

        // Gracefully close the stream
        beast::error_code ec;
        stream.shutdown(ec);
        if(ec == net::error::eof) {
            // Rationale: https://stackoverflow.com/questions/25587403/boost-asio-ssl-async-shutdown-always-finishes-with-eof-error
            ec = {};
        }
        if(ec) {
            logger_.log("%:% %() % Error shutting down SSL connection: %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), ec.message());
        }

        return response_body;
    }
    catch(std::exception const& e) {
        logger_.log("%:% %() % Error making HTTP request: %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), e.what());
        throw;
    }
}

// OrderBook implementation
void OrderBook::initializeWithSnapshot(const Json::Value& snapshot) {
    std::lock_guard<std::mutex> lock(mutex_);

    reset();

    // Get the last update ID from the snapshot
    last_update_id_ = snapshot["lastUpdateId"].asUInt64();

    // Process bids
    for (const auto& bid : snapshot["bids"]) {
        Common::Price price = stringToPrice(bid[0].asString());
        Common::Qty qty = stringToQty(bid[1].asString());

        if (qty > 0) {
            bids_[price] = qty;
        }
    }

    // Process asks
    for (const auto& ask : snapshot["asks"]) {
        Common::Price price = stringToPrice(ask[0].asString());
        Common::Qty qty = stringToQty(ask[1].asString());

        if (qty > 0) {
            asks_[price] = qty;
        }
    }

    initialized_ = true;
}

void OrderBook::addBidLevel(Common::Price price, Common::Qty qty) {
    std::lock_guard<std::mutex> lock(mutex_);
    bids_[price] = qty;
}

void OrderBook::addAskLevel(Common::Price price, Common::Qty qty) {
    std::lock_guard<std::mutex> lock(mutex_);
    asks_[price] = qty;
}

void OrderBook::setLastUpdateId(uint64_t update_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    last_update_id_ = update_id;
    initialized_ = true;
}

bool OrderBook::applyUpdate(const Json::Value& depthUpdate) {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return false;
    }

    const auto firstUpdateId = depthUpdate["U"].asUInt64();
    const auto lastUpdateId = depthUpdate["u"].asUInt64();

    // If the update is older than our current state, ignore it
    if (lastUpdateId < last_update_id_) {
        return false;
    }

    // If we missed any updates, we need to re-initialize
    if (firstUpdateId > last_update_id_ + 1) {
        initialized_ = false;
        return false;
    }

    // Process bid updates
    for (const auto& bid : depthUpdate["b"]) {
        Common::Price price = stringToPrice(bid[0].asString());
        Common::Qty qty = stringToQty(bid[1].asString());

        if (qty > 0) {
            bids_[price] = qty;
        } else {
            bids_.erase(price);
        }
    }

    // Process ask updates
    for (const auto& ask : depthUpdate["a"]) {
        Common::Price price = stringToPrice(ask[0].asString());
        Common::Qty qty = stringToQty(ask[1].asString());

        if (qty > 0) {
            asks_[price] = qty;
        } else {
            asks_.erase(price);
        }
    }

    // Update the last update ID
    last_update_id_ = lastUpdateId;

    return true;
}

std::pair<Common::Price, Common::Qty> OrderBook::getBestBid() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || bids_.empty()) {
        return {Common::Price_INVALID, Common::Qty_INVALID};
    }

    // First element in bids_ is the highest bid (since greater<Price> comparator is used)
    return *bids_.begin();
}

std::pair<Common::Price, Common::Qty> OrderBook::getBestAsk() const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_ || asks_.empty()) {
        return {Common::Price_INVALID, Common::Qty_INVALID};
    }

    // First element in asks_ is the lowest ask
    return *asks_.begin();
}

void OrderBook::reset() {
    bids_.clear();
    asks_.clear();
    last_update_id_ = 0;
    initialized_ = false;
}

void OrderBook::generateMarketUpdates(Common::TickerId ticker_id, std::vector<Exchange::MEMarketUpdate>& updates, Common::OrderId& next_order_id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    if (!initialized_) {
        return;
    }

    // First, clear the old book
    Exchange::MEMarketUpdate clear_update;
    clear_update.type_ = Exchange::MarketUpdateType::CLEAR;
    clear_update.ticker_id_ = ticker_id;
    updates.push_back(clear_update);

    // Generate updates for bids (buy side)
    for (const auto& [price, qty] : bids_) {
        Exchange::MEMarketUpdate update;
        update.type_ = Exchange::MarketUpdateType::ADD;
        update.ticker_id_ = ticker_id;
        update.order_id_ = next_order_id++;
        update.price_ = price;
        update.qty_ = qty;
        update.side_ = Common::Side::BUY;
        update.priority_ = 1; // Top priority
        updates.push_back(update);
    }

    // Generate updates for asks (sell side)
    for (const auto& [price, qty] : asks_) {
        Exchange::MEMarketUpdate update;
        update.type_ = Exchange::MarketUpdateType::ADD;
        update.ticker_id_ = ticker_id;
        update.order_id_ = next_order_id++;
        update.price_ = price;
        update.qty_ = qty;
        update.side_ = Common::Side::SELL;
        update.priority_ = 1; // Top priority
        updates.push_back(update);
    }
}

// WebSocketConnection implementation
WebSocketConnection::WebSocketConnection(net::io_context& ioc, 
                                       ssl::context& ctx,
                                       const std::string& host,
                                       const std::string& port,
                                       const std::string& target,
                                       const std::string& symbol,
                                       const std::string& stream_type,
                                       std::function<void(const std::string&, const std::string&, const std::string&)> on_message_cb,
                                       Common::Logger* logger)
    : resolver_(net::make_strand(ioc)),
      ws_(net::make_strand(ioc), ctx),
      host_(host),
      port_(port),
      target_(target),
      symbol_(symbol),
      stream_type_(stream_type),
      on_message_cb_(on_message_cb),
      logger_(logger) {
}

void WebSocketConnection::connect() {
    // Set SNI Hostname (many hosts need this to handshake successfully)
    if(!SSL_set_tlsext_host_name(ws_.next_layer().native_handle(), host_.c_str())) {
        beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
        logger_->log("%:% %() % SSL error: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ec.message());
        return;
    }
    
    // Look up the domain name
    logger_->log("%:% %() % Resolving: % %\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), host_, port_);
               
    resolver_.async_resolve(
        host_,
        port_,
        beast::bind_front_handler(
            &WebSocketConnection::on_resolve,
            shared_from_this()));
}

void WebSocketConnection::on_resolve(beast::error_code ec, tcp::resolver::results_type results) {
    if(ec) {
        logger_->log("%:% %() % Resolve error: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ec.message());
        return;
    }

    // Set a timeout on the operation
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

    // Make the connection on the IP address we get from a lookup
    beast::get_lowest_layer(ws_).async_connect(
        results,
        [self = shared_from_this()](beast::error_code ec, auto) {
            self->on_connect(ec, {});
        });
}

void WebSocketConnection::on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type) {
    if(ec) {
        logger_->log("%:% %() % Connect error: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ec.message());
        return;
    }

    // Set a timeout on the operation
    beast::get_lowest_layer(ws_).expires_after(std::chrono::seconds(30));

    // Perform the SSL handshake
    ws_.next_layer().async_handshake(
        ssl::stream_base::client,
        beast::bind_front_handler(
            &WebSocketConnection::on_ssl_handshake,
            shared_from_this()));
}

void WebSocketConnection::on_ssl_handshake(beast::error_code ec) {
    if(ec) {
        logger_->log("%:% %() % SSL handshake error: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ec.message());
        return;
    }
    
    // Turn off the timeout on the tcp_stream, because
    // the websocket stream has its own timeout system.
    beast::get_lowest_layer(ws_).expires_never();
    
    // Set suggested timeout settings for the websocket
    ws_.set_option(
        websocket::stream_base::timeout::suggested(
            beast::role_type::client));
    
    // Set a decorator to change the User-Agent of the handshake
    ws_.set_option(websocket::stream_base::decorator(
        [](websocket::request_type& req) {
            req.set(http::field::user_agent,
                std::string(BOOST_BEAST_VERSION_STRING) +
                    " binance-trading-client");
        }));
    
    // Perform the websocket handshake
    logger_->log("%:% %() % Performing WebSocket handshake on %\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), target_);
               
    ws_.async_handshake(host_, target_,
        beast::bind_front_handler(
            &WebSocketConnection::on_handshake,
            shared_from_this()));
}

void WebSocketConnection::on_handshake(beast::error_code ec) {
    if(ec) {
        logger_->log("%:% %() % WebSocket handshake error: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ec.message());
        return;
    }
    
    is_open_ = true;
    logger_->log("%:% %() % WebSocket connection established for % %\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), symbol_, stream_type_);
    
    // Start reading
    do_read();
}

void WebSocketConnection::do_read() {
    // Read a message
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &WebSocketConnection::on_read,
            shared_from_this()));
}

void WebSocketConnection::on_read(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);
    
    if(ec) {
        if(ec != websocket::error::closed) {
            logger_->log("%:% %() % WebSocket read error: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), ec.message());
        }
        is_open_ = false;
        return;
    }
    
    // Process the message
    std::string message = beast::buffers_to_string(buffer_.data());
    buffer_.consume(buffer_.size());
    
    // Call the message handler
    on_message_cb_(message, symbol_, stream_type_);
    
    // Read another message
    do_read();
}

void WebSocketConnection::close() {
    if (!is_open_) {
        return;
    }
    
    beast::error_code ec;
    ws_.close(websocket::close_code::normal, ec);
    
    if(ec) {
        logger_->log("%:% %() % WebSocket close error: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ec.message());
    }
    
    is_open_ = false;
}

bool WebSocketConnection::is_open() const {
    return is_open_;
}

// BinanceMarketDataConsumer implementation
BinanceMarketDataConsumer::BinanceMarketDataConsumer(Common::ClientId client_id,
                                     Exchange::MEMarketUpdateLFQueue *market_updates,
                                     const std::vector<std::string>& symbols,
                                     const BinanceConfig& config)
    : client_id_(client_id),
      logger_("/home/praveen/om/siriquantum/ida/logs/binance/binance_md_consumer_" + std::to_string(client_id) + ".log"),
      config_(config),
      incoming_md_updates_(market_updates),
      symbols_(symbols) {
    
    // Create log directory if it doesn't exist
    std::filesystem::create_directories("/home/praveen/om/siriquantum/ida/logs/binance/");

    // Initialize ticker mapping and order books
    for (size_t i = 0; i < symbols_.size() && i < Common::ME_MAX_TICKERS; ++i) {
        symbol_to_ticker_id_[symbols_[i]] = i;
        // Create order book entry for each symbol (using emplace which doesn't require copy/move assignment)
        order_books_.emplace(symbols_[i], OrderBook());
        // Initialize buffer for depth updates
        buffered_updates_[symbols_[i]] = std::vector<Json::Value>();
    }

    // Set up SSL context
    ctx_.set_default_verify_paths();
    ctx_.set_verify_mode(ssl::verify_peer);
}

BinanceMarketDataConsumer::~BinanceMarketDataConsumer() {
    stop();
}

void BinanceMarketDataConsumer::start() {
    run_ = true;

    // Start IO context in a separate thread
    ioc_thread_ = std::thread([this]() {
        logger_.log("%:% %() % Starting IO context thread\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_));

        try {
            ioc_.run();
        } catch (const std::exception& e) {
            logger_.log("%:% %() % IO context error: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), e.what());
        }

        logger_.log("%:% %() % IO context thread stopped\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_));
    });

    // Per Binance recommendation:
    // 1. Connect to depth streams first to start buffering
    // 2. Get snapshots and initialize order books
    // 3. Process buffered updates

    // Connect to depth and trade streams for each symbol
    for (const auto& symbol : symbols_) {
        try {
            logger_.log("%:% %() % Initializing streams for symbol %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);

            // First connect to depth stream to start buffering
            connectToDepthStream(symbol);

            // Also connect to trade stream
            connectToTradeStream(symbol);

            // Allow some time for initial depth messages to arrive
            std::this_thread::sleep_for(std::chrono::seconds(1));

            // Get order book snapshot and initialize
            getOrderBookSnapshot(symbol);

            // Process any buffered updates
            processBufferedUpdates(symbol);

            logger_.log("%:% %() % Successfully initialized order book for %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);

        } catch (const std::exception& e) {
            logger_.log("%:% %() % Failed to initialize streams for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol, e.what());
        }
    }
}

void BinanceMarketDataConsumer::stop() {
    run_ = false;
    
    // Close all connections
    for (auto& conn : connections_) {
        if (conn && conn->is_open()) {
            conn->close();
        }
    }
    
    connections_.clear();
    
    // Stop IO context
    ioc_.stop();
    
    // Wait for thread to finish
    if (ioc_thread_.joinable()) {
        ioc_thread_.join();
    }
}

void BinanceMarketDataConsumer::connectToDepthStream(const std::string& symbol) {
    try {
        // Connect to depth stream for this symbol
        auto depthConn = std::make_shared<WebSocketConnection>(
            ioc_, ctx_,
            config_.ws_host(), config_.ws_port(),
            config_.ws_target(symbol, "depth"),
            symbol, "depth",
            std::bind(&BinanceMarketDataConsumer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            &logger_);

        connections_.push_back(depthConn);
        depthConn->connect();

        logger_.log("%:% %() % Connected to depth stream for %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol);

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error connecting to depth stream for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
        throw; // Rethrow to allow caller to handle
    }
}

void BinanceMarketDataConsumer::connectToTradeStream(const std::string& symbol) {
    try {
        // Connect to trade stream for this symbol
        auto tradeConn = std::make_shared<WebSocketConnection>(
            ioc_, ctx_,
            config_.ws_host(), config_.ws_port(),
            config_.ws_target(symbol, "trade"),
            symbol, "trade",
            std::bind(&BinanceMarketDataConsumer::onMessage, this, std::placeholders::_1, std::placeholders::_2, std::placeholders::_3),
            &logger_);

        connections_.push_back(tradeConn);
        tradeConn->connect();

        logger_.log("%:% %() % Connected to trade stream for %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol);

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error connecting to trade stream for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
        throw; // Rethrow to allow caller to handle
    }
}

void BinanceMarketDataConsumer::getOrderBookSnapshot(const std::string& symbol) {
    // Following Binance's documentation:
    // 1. Get a depth snapshot from REST API
    // 2. If lastUpdateId in the snapshot is <= first update ID from the depth stream, repeat
    // 3. Process buffered events where u > lastUpdateId from the snapshot

    if (!symbol_to_ticker_id_.count(symbol)) {
        logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol);
        return;
    }

    while (run_) {
        try {
            logger_.log("%:% %() % Getting depth snapshot for %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);

            // Make the REST API call
            std::string url = "/api/v3/depth";
            std::string params = "?symbol=" + symbol + "&limit=1000"; // Get up to 1000 levels

            // Construct full URL
            std::string host = config_.use_testnet ? "testnet.binance.vision" : "api.binance.com";
            std::string port = "443";

            // Open TCP connection
            net::io_context ioc;
            ssl::context ctx(ssl::context::tlsv12_client);
            ctx.set_default_verify_paths();
            ctx.set_verify_mode(ssl::verify_peer);

            tcp::resolver resolver(ioc);
            beast::ssl_stream<beast::tcp_stream> stream(ioc, ctx);

            // Set SNI Hostname
            if(!SSL_set_tlsext_host_name(stream.native_handle(), host.c_str())) {
                beast::error_code ec{static_cast<int>(::ERR_get_error()), net::error::get_ssl_category()};
                throw beast::system_error(ec);
            }

            // Resolve the host
            auto const results = resolver.resolve(host, port);

            // Connect to the host
            beast::get_lowest_layer(stream).connect(results);

            // Perform SSL handshake
            stream.handshake(ssl::stream_base::client);

            // Set up an HTTP GET request
            http::request<http::string_body> req{http::verb::get, url + params, 11};
            req.set(http::field::host, host);
            req.set(http::field::user_agent, BOOST_BEAST_VERSION_STRING);

            // Send the request
            http::write(stream, req);

            // Receive the response
            beast::flat_buffer buffer;
            http::response<http::dynamic_body> res;
            http::read(stream, buffer, res);

            // Convert to string
            std::string response_body = beast::buffers_to_string(res.body().data());

            // Close connection gracefully
            beast::error_code ec;
            stream.shutdown(ec);
            if(ec == net::error::eof) {
                ec = {}; // eof is expected
            }

            // Parse the JSON response
            Json::CharReaderBuilder builder;
            Json::Value snapshot;
            std::string errors;
            std::istringstream iss(response_body);
            if (!Json::parseFromStream(builder, iss, &snapshot, &errors)) {
                logger_.log("%:% %() % Error parsing snapshot JSON: %\n", __FILE__, __LINE__, __FUNCTION__,
                          Common::getCurrentTimeStr(&time_str_), errors);
                continue;
            }

            // Get last update ID from snapshot
            uint64_t last_update_id = snapshot["lastUpdateId"].asUInt64();

            // Check if we need to get a new snapshot
            // According to Binance docs:
            // - If we haven't received any depth updates yet, proceed with this snapshot
            // - If we have received updates, check if this snapshot's lastUpdateId is >= first update's U value
            std::unique_lock<std::mutex> lock(buffer_mutex_);

            if (buffered_updates_[symbol].empty()) {
                // No updates received yet, use this snapshot
                lock.unlock();
                initializeOrderBook(symbol, snapshot);
                return;
            } else {
                // Check if snapshot is newer than first buffered update
                uint64_t first_update_u = buffered_updates_[symbol].front()["U"].asUInt64();

                if (last_update_id >= first_update_u) {
                    // Snapshot is newer than or equal to first buffered update, use it
                    lock.unlock();
                    initializeOrderBook(symbol, snapshot);
                    return;
                } else {
                    // Snapshot is older, get a new one
                    logger_.log("%:% %() % Snapshot too old for %: lastUpdateId=% < firstUpdateId=%\n",
                              __FILE__, __LINE__, __FUNCTION__,
                              Common::getCurrentTimeStr(&time_str_),
                              symbol, last_update_id, first_update_u);
                }
            }

            // Sleep briefly before retrying
            std::this_thread::sleep_for(std::chrono::milliseconds(500));

        } catch (const std::exception& e) {
            logger_.log("%:% %() % Error getting snapshot for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol, e.what());

            // Sleep before retrying
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    }
}

void BinanceMarketDataConsumer::initializeOrderBook(const std::string& symbol, const Json::Value& snapshot) {
    try {
        if (!symbol_to_ticker_id_.count(symbol)) {
            logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        Common::TickerId ticker_id = symbol_to_ticker_id_[symbol];

        // Get the last update ID from the snapshot
        uint64_t last_update_id = snapshot["lastUpdateId"].asUInt64();

        // Reset the order book for this symbol
        auto& order_book = order_books_[symbol];
        order_book.reset();

        // First, send a CLEAR update to the trading engine
        Exchange::MEMarketUpdate clear_update;
        clear_update.type_ = Exchange::MarketUpdateType::CLEAR;
        clear_update.ticker_id_ = ticker_id;

        auto next_write = incoming_md_updates_->getNextToWriteTo();
        *next_write = clear_update;
        incoming_md_updates_->updateWriteIndex();

        logger_.log("%:% %() % Sent CLEAR update for %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol);

        // Process bids (buy side)
        int bid_count = 0;
        for (const auto& bid : snapshot["bids"]) {
            Common::Price price = static_cast<Common::Price>(std::stod(bid[0].asString()) * 100.0);
            Common::Qty qty = static_cast<Common::Qty>(std::stod(bid[1].asString()) * 100.0);

            if (qty > 0) {
                // Add the price level to the local order book
                order_book.addBidLevel(price, qty);

                // Send ADD update to the trading engine
                Exchange::MEMarketUpdate update;
                update.type_ = Exchange::MarketUpdateType::ADD;
                update.ticker_id_ = ticker_id;
                update.order_id_ = next_sequence_num_++;
                update.price_ = price;
                update.qty_ = qty;
                update.side_ = Common::Side::BUY;
                update.priority_ = 1; // Top priority

                auto next_write = incoming_md_updates_->getNextToWriteTo();
                *next_write = update;
                incoming_md_updates_->updateWriteIndex();

                bid_count++;
            }
        }

        // Process asks (sell side)
        int ask_count = 0;
        for (const auto& ask : snapshot["asks"]) {
            Common::Price price = static_cast<Common::Price>(std::stod(ask[0].asString()) * 100.0);
            Common::Qty qty = static_cast<Common::Qty>(std::stod(ask[1].asString()) * 100.0);

            if (qty > 0) {
                // Add the price level to the local order book
                order_book.addAskLevel(price, qty);

                // Send ADD update to the trading engine
                Exchange::MEMarketUpdate update;
                update.type_ = Exchange::MarketUpdateType::ADD;
                update.ticker_id_ = ticker_id;
                update.order_id_ = next_sequence_num_++;
                update.price_ = price;
                update.qty_ = qty;
                update.side_ = Common::Side::SELL;
                update.priority_ = 1; // Top priority

                auto next_write = incoming_md_updates_->getNextToWriteTo();
                *next_write = update;
                incoming_md_updates_->updateWriteIndex();

                ask_count++;
            }
        }

        // Mark the order book as initialized with the correct lastUpdateId
        order_book.setLastUpdateId(last_update_id);

        logger_.log("%:% %() % Initialized order book for % with lastUpdateId=%. Added % bids and % asks.\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  symbol, last_update_id, bid_count, ask_count);

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error initializing order book for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
        throw;
    }
}

void BinanceMarketDataConsumer::processBufferedUpdates(const std::string& symbol) {
    try {
        // Lock the buffer
        std::unique_lock<std::mutex> lock(buffer_mutex_);

        if (!order_books_.count(symbol) || !buffered_updates_.count(symbol)) {
            logger_.log("%:% %() % No order book or buffer for symbol %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        auto& order_book = order_books_[symbol];
        auto& buffer = buffered_updates_[symbol];

        if (buffer.empty()) {
            logger_.log("%:% %() % No buffered updates for %\n", __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        uint64_t last_update_id = order_book.getLastUpdateId();

        logger_.log("%:% %() % Processing % buffered updates for %, starting from lastUpdateId=%\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  buffer.size(), symbol, last_update_id);

        // Process each buffered update
        int applied_count = 0;
        for (auto it = buffer.begin(); it != buffer.end(); ) {
            const auto& update = *it;

            uint64_t first_update_id = update["U"].asUInt64();
            uint64_t final_update_id = update["u"].asUInt64();

            // 1. If u <= lastUpdateId from snapshot, ignore
            if (final_update_id <= last_update_id) {
                // This update is already included in the snapshot, discard it
                it = buffer.erase(it);
                continue;
            }

            // 2. If U > lastUpdateId+1, we missed some updates, need to re-initialize
            if (first_update_id > last_update_id + 1) {
                logger_.log("%:% %() % Gap detected in updates for %: expected U<=%, but got %\n",
                          __FILE__, __LINE__, __FUNCTION__,
                          Common::getCurrentTimeStr(&time_str_),
                          symbol, last_update_id + 1, first_update_id);

                // Clear the buffer and trigger a re-initialization
                buffer.clear();
                lock.unlock();

                // Get a new snapshot and start over
                getOrderBookSnapshot(symbol);
                return;
            }

            // 3. Otherwise, apply the update
            applyDepthUpdate(symbol, update);
            applied_count++;

            // Update lastUpdateId
            last_update_id = final_update_id;
            order_book.setLastUpdateId(last_update_id);

            // Remove this update from the buffer
            it = buffer.erase(it);
        }

        logger_.log("%:% %() % Applied % buffered updates for %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  applied_count, symbol);

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error processing buffered updates for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
    }
}

void BinanceMarketDataConsumer::onMessage(const std::string& payload, const std::string& symbol, const std::string& stream_type) {
    try {
        // Parse JSON message
        Json::CharReaderBuilder builder;
        Json::Value root;
        std::string errors;
        std::istringstream iss(payload);
        if (!Json::parseFromStream(builder, iss, &root, &errors)) {
            logger_.log("%:% %() % Error parsing JSON: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), errors);
            return;
        }

        // For streams with wrapping data structure (depth, trade), extract the data field
        Json::Value data;
        if (root.isMember("data")) {
            data = root["data"];
        } else {
            data = root; // For bookTicker, the data is at the root level
        }

        // Process based on stream type
        if (stream_type == "depth") {
            // Handle depth stream updates
            onDepthUpdate(symbol, data);
        } else if (stream_type == "trade") {
            // Handle trade stream updates
            onTradeUpdate(symbol, data);
        } else if (stream_type == "bookTicker") {
            // Legacy handler for bookTicker stream
            processBinanceBookUpdate(data, symbol);
        }

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error processing message: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), e.what());
    }
}

void BinanceMarketDataConsumer::onDepthUpdate(const std::string& symbol, const Json::Value& data) {
    try {
        if (!symbol_to_ticker_id_.count(symbol)) {
            logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        // Check if the order book is initialized
        auto& order_book = order_books_[symbol];
        if (!order_book.isInitialized()) {
            // Buffer the update until the order book is initialized
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffered_updates_[symbol].push_back(data);

            logger_.log("%:% %() % Buffered depth update for % (orderbook not initialized yet)\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        // Apply the depth update
        applyDepthUpdate(symbol, data);

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error processing depth update for %: %\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
    }
}

void BinanceMarketDataConsumer::applyDepthUpdate(const std::string& symbol, const Json::Value& data) {
    try {
        if (!symbol_to_ticker_id_.count(symbol)) {
            logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        auto& order_book = order_books_[symbol];
        if (!order_book.isInitialized()) {
            logger_.log("%:% %() % Cannot apply update - order book not initialized for %\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        Common::TickerId ticker_id = symbol_to_ticker_id_[symbol];

        // Check the update IDs to ensure we're applying updates in sequence
        uint64_t first_update_id = data["U"].asUInt64();
        uint64_t final_update_id = data["u"].asUInt64();
        uint64_t last_update_id = order_book.getLastUpdateId();

        // Per Binance docs:
        // 1. If final update ID <= lastUpdateId from snapshot, ignore this update
        if (final_update_id <= last_update_id) {
            logger_.log("%:% %() % Ignoring outdated update for %: update_id=% <= last_update_id=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      symbol, final_update_id, last_update_id);
            return;
        }

        // 2. If first update ID > lastUpdateId+1, we missed some updates
        if (first_update_id > last_update_id + 1) {
            logger_.log("%:% %() % Gap detected in updates for %: expected first_update_id<=%, got %\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      symbol, last_update_id + 1, first_update_id);

            // We need to get a new snapshot and restart
            order_book.reset();

            // Clear buffered updates
            std::lock_guard<std::mutex> lock(buffer_mutex_);
            buffered_updates_[symbol].clear();

            // Buffer the current update
            buffered_updates_[symbol].push_back(data);

            // Get a new snapshot
            getOrderBookSnapshot(symbol);
            return;
        }

        // 3. Apply the update - process bids (buy side)
        int bid_count = 0;
        for (const auto& bid : data["b"]) {
            Common::Price price = static_cast<Common::Price>(std::stod(bid[0].asString()) * 100.0);
            Common::Qty qty = static_cast<Common::Qty>(std::stod(bid[1].asString()) * 100.0);

            Exchange::MEMarketUpdate update;
            if (qty > 0) {
                // Add or update price level
                update.type_ = Exchange::MarketUpdateType::ADD;
                update.ticker_id_ = ticker_id;
                update.order_id_ = next_sequence_num_++;
                update.price_ = price;
                update.qty_ = qty;
                update.side_ = Common::Side::BUY;
                update.priority_ = 1; // Top priority

                bid_count++;
            } else {
                // Remove price level
                update.type_ = Exchange::MarketUpdateType::CANCEL;
                update.ticker_id_ = ticker_id;
                update.order_id_ = next_sequence_num_++;
                update.price_ = price;
                update.qty_ = 0;
                update.side_ = Common::Side::BUY;
            }

            // Send update to trading engine
            auto next_write = incoming_md_updates_->getNextToWriteTo();
            *next_write = update;
            incoming_md_updates_->updateWriteIndex();
        }

        // Process asks (sell side)
        int ask_count = 0;
        for (const auto& ask : data["a"]) {
            Common::Price price = static_cast<Common::Price>(std::stod(ask[0].asString()) * 100.0);
            Common::Qty qty = static_cast<Common::Qty>(std::stod(ask[1].asString()) * 100.0);

            Exchange::MEMarketUpdate update;
            if (qty > 0) {
                // Add or update price level
                update.type_ = Exchange::MarketUpdateType::ADD;
                update.ticker_id_ = ticker_id;
                update.order_id_ = next_sequence_num_++;
                update.price_ = price;
                update.qty_ = qty;
                update.side_ = Common::Side::SELL;
                update.priority_ = 1; // Top priority

                ask_count++;
            } else {
                // Remove price level
                update.type_ = Exchange::MarketUpdateType::CANCEL;
                update.ticker_id_ = ticker_id;
                update.order_id_ = next_sequence_num_++;
                update.price_ = price;
                update.qty_ = 0;
                update.side_ = Common::Side::SELL;
            }

            // Send update to trading engine
            auto next_write = incoming_md_updates_->getNextToWriteTo();
            *next_write = update;
            incoming_md_updates_->updateWriteIndex();
        }

        // Update the local order book
        bool success = order_book.applyUpdate(data);
        if (success) {
            logger_.log("%:% %() % Applied depth update for %: updateId=%->% (%d bids, %d asks)\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      symbol, first_update_id, final_update_id, bid_count, ask_count);
        } else {
            logger_.log("%:% %() % Failed to apply depth update for %: updateId=%->%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      symbol, first_update_id, final_update_id);

            // We need to get a new snapshot
            getOrderBookSnapshot(symbol);
        }

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error applying depth update for %: %\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
    }
}

void BinanceMarketDataConsumer::onTradeUpdate(const std::string& symbol, const Json::Value& data) {
    try {
        if (!symbol_to_ticker_id_.count(symbol)) {
            logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), symbol);
            return;
        }

        Common::TickerId ticker_id = symbol_to_ticker_id_[symbol];

        // Extract trade information
        Common::Price price = static_cast<Common::Price>(std::stod(data["p"].asString()) * 100.0);
        Common::Qty qty = static_cast<Common::Qty>(std::stod(data["q"].asString()) * 100.0);
        bool is_buyer_maker = data["m"].asBool(); // true if buyer is the maker (SELL trade)

        Common::Side side = is_buyer_maker ? Common::Side::SELL : Common::Side::BUY;

        // Create trade update
        Exchange::MEMarketUpdate trade_update;
        trade_update.type_ = Exchange::MarketUpdateType::TRADE;
        trade_update.ticker_id_ = ticker_id;
        trade_update.order_id_ = next_sequence_num_++;
        trade_update.price_ = price;
        trade_update.qty_ = qty;
        trade_update.side_ = side;

        // Send update to trading engine
        auto next_write = incoming_md_updates_->getNextToWriteTo();
        *next_write = trade_update;
        incoming_md_updates_->updateWriteIndex();

        logger_.log("%:% %() % Processed trade for %: % % @ %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   symbol, Common::sideToString(side),
                   Common::qtyToString(qty), Common::priceToString(price));

    } catch (const std::exception& e) {
        logger_.log("%:% %() % Error processing trade update for %: %\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), symbol, e.what());
    }
}

void BinanceMarketDataConsumer::processBinanceBookUpdate(const Json::Value& data, const std::string& symbol) {
    if (!symbol_to_ticker_id_.count(symbol)) {
        logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol);
        return;
    }
    
    Common::TickerId ticker_id = symbol_to_ticker_id_[symbol];
    
    // In bookTicker stream, the format is:
    // {"u":400900217,"s":"BNBUSDT","b":"240.40000000","B":"6.35796000","a":"240.50000000","A":"5.52504000"}

    // Extract bid/ask price and quantity
    Common::Price bid_price = std::stod(data["b"].asString()) * 100; // Convert to internal price format (x100)
    Common::Qty bid_qty = std::stod(data["B"].asString()) * 100;     // Convert to internal qty format (x100)
    Common::Price ask_price = std::stod(data["a"].asString()) * 100;
    Common::Qty ask_qty = std::stod(data["A"].asString()) * 100;

    // Sanity check - make sure bid < ask
    if (bid_price >= ask_price) {
        logger_.log("%:% %() % WARNING: Received inverted prices from Binance: bid=%s >= ask=%s\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  Common::priceToString(bid_price).c_str(),
                  Common::priceToString(ask_price).c_str());

        // Skip this update if it would create an inverted book
        return;
    }
    
    // Process BID update - according to Binance documentation
    if (bid_price > 0) {
        Exchange::MEMarketUpdate bid_update;

        if (bid_qty > 0) {
            // Add/update the price level with new quantity
            bid_update.type_ = Exchange::MarketUpdateType::ADD; // Use ADD for new or updated levels
            bid_update.ticker_id_ = ticker_id;
            bid_update.order_id_ = next_sequence_num_++;
            bid_update.price_ = bid_price;
            bid_update.qty_ = bid_qty;
            bid_update.side_ = Common::Side::BUY;
            bid_update.priority_ = 1; // Top priority

            // Push update to queue
            auto next_write = incoming_md_updates_->getNextToWriteTo();
            *next_write = bid_update;
            incoming_md_updates_->updateWriteIndex();

            logger_.log("%:% %() % Sent ADD for BID: price=% qty=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      Common::priceToString(bid_price),
                      Common::qtyToString(bid_qty));
        } else {
            // Remove price level if quantity is 0
            bid_update.type_ = Exchange::MarketUpdateType::CANCEL;
            bid_update.ticker_id_ = ticker_id;
            bid_update.order_id_ = next_sequence_num_++;
            bid_update.price_ = bid_price;
            bid_update.qty_ = 0;
            bid_update.side_ = Common::Side::BUY;

            // Push update to queue
            auto next_write = incoming_md_updates_->getNextToWriteTo();
            *next_write = bid_update;
            incoming_md_updates_->updateWriteIndex();

            logger_.log("%:% %() % Sent CANCEL for BID: price=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      Common::priceToString(bid_price));
        }
    }

    // Process ASK update - according to Binance documentation
    if (ask_price > 0) {
        Exchange::MEMarketUpdate ask_update;

        if (ask_qty > 0) {
            // Add/update the price level with new quantity
            ask_update.type_ = Exchange::MarketUpdateType::ADD; // Use ADD for new or updated levels
            ask_update.ticker_id_ = ticker_id;
            ask_update.order_id_ = next_sequence_num_++;
            ask_update.price_ = ask_price;
            ask_update.qty_ = ask_qty;
            ask_update.side_ = Common::Side::SELL;
            ask_update.priority_ = 1; // Top priority

            // Push update to queue
            auto next_write = incoming_md_updates_->getNextToWriteTo();
            *next_write = ask_update;
            incoming_md_updates_->updateWriteIndex();

            logger_.log("%:% %() % Sent ADD for ASK: price=% qty=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      Common::priceToString(ask_price),
                      Common::qtyToString(ask_qty));
        } else {
            // Remove price level if quantity is 0
            ask_update.type_ = Exchange::MarketUpdateType::CANCEL;
            ask_update.ticker_id_ = ticker_id;
            ask_update.order_id_ = next_sequence_num_++;
            ask_update.price_ = ask_price;
            ask_update.qty_ = 0;
            ask_update.side_ = Common::Side::SELL;

            // Push update to queue
            auto next_write = incoming_md_updates_->getNextToWriteTo();
            *next_write = ask_update;
            incoming_md_updates_->updateWriteIndex();

            logger_.log("%:% %() % Sent CANCEL for ASK: price=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      Common::priceToString(ask_price));
        }
    }
    
    logger_.log("%:% %() % Processed book update for % - BID: % @ %, ASK: % @ %\n", 
               __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), 
               symbol, 
               Common::qtyToString(bid_qty), Common::priceToString(bid_price),
               Common::priceToString(ask_price), Common::qtyToString(ask_qty));
}

void BinanceMarketDataConsumer::processBinanceTrade(const Json::Value& data, const std::string& symbol) {
    if (!symbol_to_ticker_id_.count(symbol)) {
        logger_.log("%:% %() % Unknown symbol: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol);
        return;
    }
    
    Common::TickerId ticker_id = symbol_to_ticker_id_[symbol];
    
    // In trade stream, the format is:
    // {"e":"trade","E":1678741852345,"s":"BNBUSDT","t":12345,"p":"240.50000000","q":"1.23400000","b":12345,"a":12345,"T":1678741852345,"m":true,"M":true}
    
    // Extract trade price, quantity and side
    Common::Price price = std::stod(data["p"].asString()) * 100; // Convert to internal price format (x100)
    Common::Qty qty = std::stod(data["q"].asString()) * 100;     // Convert to internal qty format (x100)
    bool is_buyer_maker = data["m"].asBool();                    // true if buyer is maker (SELL trade)
    
    Common::Side side = is_buyer_maker ? Common::Side::SELL : Common::Side::BUY;
    
    // Create trade update
    Exchange::MEMarketUpdate trade_update;
    trade_update.type_ = Exchange::MarketUpdateType::TRADE;
    trade_update.ticker_id_ = ticker_id;
    trade_update.order_id_ = next_sequence_num_++;
    trade_update.side_ = side;
    trade_update.price_ = price;
    trade_update.qty_ = qty;
    
    // Push update to queue
    auto next_write = incoming_md_updates_->getNextToWriteTo();
    *next_write = trade_update;
    incoming_md_updates_->updateWriteIndex();
    
    logger_.log("%:% %() % Processed trade for %: % % @ %\n", 
               __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), 
               symbol, 
               Common::sideToString(side),
               Common::qtyToString(qty), 
               Common::priceToString(price));
}

// Static helper to load configuration from file
BinanceConfig BinanceMarketDataConsumer::loadConfig(const std::string& config_path) {
    BinanceConfig config;
    
    try {
        // Parse the standard trading.json config file
        std::string trading_config_path = config_path.empty() ? "/home/praveen/om/siriquantum/ida/config/trading.json" : config_path;
        
        std::ifstream config_file(trading_config_path);
        if (!config_file.is_open()) {
            throw std::runtime_error("Cannot open trading config file: " + trading_config_path);
        }
        
        nlohmann::json root;
        config_file >> root;
        
        // Check if Binance config exists
        if (!root.contains("exchanges") || !root["exchanges"].contains("BINANCE")) {
            throw std::runtime_error("Binance configuration not found in trading.json");
        }
        
        // Extract Binance-specific configuration
        const auto& binance_config = root["exchanges"]["BINANCE"];
        
        // Get API credentials
        if (binance_config.contains("api_credentials")) {
            const auto& credentials = binance_config["api_credentials"];
            
            if (credentials.contains("api_key")) {
                config.api_key = credentials["api_key"].get<std::string>();
            }
            
            if (credentials.contains("api_secret")) {
                config.api_secret = credentials["api_secret"].get<std::string>();
            }
            
            if (credentials.contains("testnet")) {
                config.use_testnet = credentials["testnet"].get<bool>();
            }
        }
        
        // Validate configuration
        if (config.api_key.empty()) {
            throw std::runtime_error("API key is missing in Binance configuration");
        }
        
        if (config.api_secret.empty()) {
            throw std::runtime_error("API secret is missing in Binance configuration");
        }
        
        // Log configuration source and key details (mask sensitive data)
        std::string masked_key = config.api_key.substr(0, 4) + "..." + 
                                config.api_key.substr(config.api_key.length() - 4);
        std::cerr << "Loaded Binance configuration from " << trading_config_path << std::endl;
        std::cerr << "API Key: " << masked_key << ", Testnet: " << (config.use_testnet ? "Yes" : "No") << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Error loading Binance configuration: " << e.what() << std::endl;
        throw; // Re-throw to let the caller handle it
    }
    
    return config;
}

} // namespace Trading