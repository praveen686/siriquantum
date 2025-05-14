#pragma once

#include <functional>
#include <map>
#include <string>
#include <vector>
#include <atomic>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/beast/http.hpp>
#include <jsoncpp/json/json.h>
#include <nlohmann/json.hpp>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/logging.h"
#include "common/types.h"

#include "exchange/market_data/market_update.h"
#include "trading/adapters/binance/market_data/binance_config.h"

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace Trading {

// WebSocket connection class
class WebSocketConnection : public std::enable_shared_from_this<WebSocketConnection> {
public:
    WebSocketConnection(net::io_context& ioc,
                       ssl::context& ctx,
                       const std::string& host,
                       const std::string& port,
                       const std::string& target,
                       const std::string& symbol,
                       const std::string& stream_type,
                       std::function<void(const std::string&, const std::string&, const std::string&)> on_message_cb,
                       Common::Logger* logger);

    void connect();
    void close();
    bool is_open() const;

private:
    void on_resolve(beast::error_code ec, tcp::resolver::results_type results);
    void on_connect(beast::error_code ec, tcp::resolver::results_type::endpoint_type);
    void on_ssl_handshake(beast::error_code ec);
    void on_handshake(beast::error_code ec);
    void on_read(beast::error_code ec, std::size_t bytes_transferred);
    void do_read();

    tcp::resolver resolver_;
    websocket::stream<beast::ssl_stream<beast::tcp_stream>> ws_;
    beast::flat_buffer buffer_;
    std::string host_;
    std::string port_;
    std::string target_;
    std::string symbol_;
    std::string stream_type_;
    std::function<void(const std::string&, const std::string&, const std::string&)> on_message_cb_;
    Common::Logger* logger_;
    std::string time_str_;
    bool is_open_ = false;
};

// HTTP client for REST API calls
class HttpClient {
public:
    HttpClient(net::io_context& ioc, ssl::context& ctx, const std::string& host, const std::string& port);

    // Make a synchronous HTTP GET request
    std::string get(const std::string& target, const std::map<std::string, std::string>& params = {});

private:
    std::string host_;
    std::string port_;
    net::io_context& ioc_;
    ssl::context& ctx_;
    std::string time_str_;
    Common::Logger logger_{"/home/praveen/om/siriquantum/ida/logs/binance/http_client.log"};
};

// Order book data structure - based on Binance depth documentation
class OrderBook {
public:
    OrderBook() = default;

    // Explicitly define move constructor
    OrderBook(OrderBook&& other) noexcept {
        // Acquire the lock on the source object
        std::lock_guard<std::mutex> lock(other.mutex_);

        // Move data members
        bids_ = std::move(other.bids_);
        asks_ = std::move(other.asks_);
        last_update_id_ = other.last_update_id_;
        initialized_ = other.initialized_;

        // Reset source object's state
        other.last_update_id_ = 0;
        other.initialized_ = false;
    }

    // Explicitly define move assignment operator
    OrderBook& operator=(OrderBook&& other) noexcept {
        if (this != &other) {
            // Lock both objects to prevent data races
            // Using std::lock to avoid potential deadlock
            std::lock(mutex_, other.mutex_);
            std::lock_guard<std::mutex> lock_this(mutex_, std::adopt_lock);
            std::lock_guard<std::mutex> lock_other(other.mutex_, std::adopt_lock);

            // Move data members
            bids_ = std::move(other.bids_);
            asks_ = std::move(other.asks_);
            last_update_id_ = other.last_update_id_;
            initialized_ = other.initialized_;

            // Reset source object's state
            other.last_update_id_ = 0;
            other.initialized_ = false;
        }
        return *this;
    }

    // Initialize the order book with a snapshot
    void initializeWithSnapshot(const Json::Value& snapshot);

    // Apply a depth update to the order book
    bool applyUpdate(const Json::Value& depthUpdate);

    // Check if the book is initialized
    bool isInitialized() const { return initialized_; }

    // Get the current best bid price and quantity
    std::pair<Common::Price, Common::Qty> getBestBid() const;

    // Get the current best ask price and quantity
    std::pair<Common::Price, Common::Qty> getBestAsk() const;

    // Get the last update ID
    uint64_t getLastUpdateId() const { return last_update_id_; }

    // Set the last update ID and mark the book as initialized
    void setLastUpdateId(uint64_t update_id);

    // Add a bid level to the order book
    void addBidLevel(Common::Price price, Common::Qty qty);

    // Add an ask level to the order book
    void addAskLevel(Common::Price price, Common::Qty qty);

    // Reset the order book
    void reset();

    // Generate market updates for the current state
    void generateMarketUpdates(Common::TickerId ticker_id, std::vector<Exchange::MEMarketUpdate>& updates, Common::OrderId& next_order_id) const;

private:
    std::map<Common::Price, Common::Qty, std::greater<Common::Price>> bids_; // Bids sorted high to low
    std::map<Common::Price, Common::Qty> asks_; // Asks sorted low to high
    uint64_t last_update_id_ = 0;
    bool initialized_ = false;
    mutable std::mutex mutex_;

    // Helper to convert string price/qty to internal format
    Common::Price stringToPrice(const std::string& price_str) const {
        return static_cast<Common::Price>(std::stod(price_str) * 100.0); // Convert to internal price format
    }

    Common::Qty stringToQty(const std::string& qty_str) const {
        return static_cast<Common::Qty>(std::stod(qty_str) * 100.0); // Convert to internal qty format
    }
};

class BinanceMarketDataConsumer {
public:
    BinanceMarketDataConsumer(Common::ClientId client_id,
                      Exchange::MEMarketUpdateLFQueue *market_updates,
                      const std::vector<std::string>& symbols,
                      const BinanceConfig& config);

    ~BinanceMarketDataConsumer();

    // Start and stop the market data consumer
    auto start() -> void;
    auto stop() -> void;

    // Deleted default, copy & move constructors and assignment-operators
    BinanceMarketDataConsumer() = delete;
    BinanceMarketDataConsumer(const BinanceMarketDataConsumer &) = delete;
    BinanceMarketDataConsumer(const BinanceMarketDataConsumer &&) = delete;
    BinanceMarketDataConsumer &operator=(const BinanceMarketDataConsumer &) = delete;
    BinanceMarketDataConsumer &operator=(const BinanceMarketDataConsumer &&) = delete;

    // Static helper to load configuration from file
    static BinanceConfig loadConfig(const std::string& config_path);

private:
    Common::ClientId client_id_;
    volatile bool run_ = false;
    std::string time_str_;
    Common::Logger logger_;
    BinanceConfig config_;

    // Lock free queue to push market updates to trade engine
    Exchange::MEMarketUpdateLFQueue *incoming_md_updates_ = nullptr;

    // Asio components
    net::io_context ioc_;
    ssl::context ctx_{ssl::context::tlsv12_client};
    std::thread ioc_thread_;

    // WebSocket connections
    std::vector<std::shared_ptr<WebSocketConnection>> connections_;
    std::vector<std::string> symbols_;

    // Ticker ID mapping (symbol -> internal ticker ID)
    std::map<std::string, Common::TickerId> symbol_to_ticker_id_;
    std::atomic<size_t> next_sequence_num_ = 1;

    // Order book management
    std::unordered_map<std::string, OrderBook> order_books_;
    std::unordered_map<std::string, std::vector<Json::Value>> buffered_updates_;
    std::mutex buffer_mutex_; // Mutex for thread-safe access to buffered_updates_

    // Initialize order books with snapshots
    void getOrderBookSnapshot(const std::string& symbol);
    void initializeOrderBook(const std::string& symbol, const Json::Value& snapshot);
    void processBufferedUpdates(const std::string& symbol);

    // Process market data from Binance
    void onMessage(const std::string& payload, const std::string& symbol, const std::string& stream_type);
    void onDepthUpdate(const std::string& symbol, const Json::Value& data);
    void applyDepthUpdate(const std::string& symbol, const Json::Value& data);
    void onTradeUpdate(const std::string& symbol, const Json::Value& data);

    // Connect to depth and trade streams
    void connectToDepthStream(const std::string& symbol);
    void connectToTradeStream(const std::string& symbol);

    // Legacy methods (keeping for compatibility)
    void processBinanceBookUpdate(const Json::Value& data, const std::string& symbol);
    void processBinanceTrade(const Json::Value& data, const std::string& symbol);
};

} // namespace Trading