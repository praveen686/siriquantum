#pragma once

#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <atomic>
#include <thread>
#include <mutex>
#include <chrono>
#include <array>
#include <algorithm>
#include <memory>
#include <future>

// Common utilities
#include "common/logging.h"
#include "common/lf_queue.h"
#include "common/time_utils.h"

// Boost.Beast includes
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/strand.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ip/tcp.hpp>

namespace beast = boost::beast;
namespace http = beast::http;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = boost::asio::ssl;
using tcp = boost::asio::ip::tcp;

namespace Adapter {
namespace Zerodha {

// Market data structures
struct MarketDepthEntry {
    int32_t quantity;  // Order quantity
    int32_t price;     // Order price (in paise)
    int16_t orders;    // Number of orders at this level
    uint16_t padding;  // 2-byte padding to skip
};

// Supported streaming modes
enum class StreamingMode {
    LTP,    // Last trade price only
    QUOTE,  // Market quotes excluding depth
    FULL    // Full market quotes with depth
};

// Type of market update
enum class MarketUpdateType {
    LTP,     // Last trade price update
    QUOTE,   // Market quote update
    FULL,    // Full market update
    INDEX    // Index update
};

// Market update structure that will be published via LFQueue
struct MarketUpdate {
    int32_t instrument_token;      // Instrument identifier
    uint64_t timestamp;            // Update timestamp (microseconds)
    MarketUpdateType type;         // LTP, QUOTE, or FULL
    double last_price;             // Last traded price
    
    // Fields for QUOTE and FULL modes
    double last_quantity;          // Last traded quantity
    double average_price;          // Average traded price
    double volume;                 // Volume traded for the day
    double buy_quantity;           // Total buy quantity
    double sell_quantity;          // Total sell quantity
    double open_price;             // Open price
    double high_price;             // High price
    double low_price;              // Low price
    double close_price;            // Close price
    
    // Additional fields for FULL mode
    uint64_t last_trade_time;      // Last trade timestamp
    double open_interest;          // Open interest
    double open_interest_day_high; // Open interest day high
    double open_interest_day_low;  // Open interest day low
    uint64_t exchange_timestamp;   // Exchange timestamp
    
    // Market depth (only for FULL mode)
    std::array<MarketDepthEntry, 5> bids;  // Bid depth levels
    std::array<MarketDepthEntry, 5> asks;  // Ask depth levels
};

/**
 * ZerodhaWebSocketClient - High-performance client for Zerodha Kite WebSocket API
 *
 * This class handles:
 * - Connecting to the Zerodha WebSocket API
 * - Subscribing to market data for instruments
 * - Parsing binary market data messages
 * - Publishing updates to a lock-free queue
 * - Handling reconnection and error recovery
 */
// Forward declaration for WebSocket type
struct WebSocketUserData;

class ZerodhaWebSocketClient {
public:
    /**
     * Constructor
     * 
     * @param api_key Zerodha API key
     * @param access_token Authentication token from ZerodhaAuthenticator
     * @param update_queue Lock-free queue for publishing market updates
     * @param logger Logger for diagnostic messages
     */
    ZerodhaWebSocketClient(
        const std::string& api_key,
        const std::string& access_token,
        Common::LFQueue<MarketUpdate>& update_queue,
        Common::Logger* logger
    );

    /**
     * Destructor - Disconnects and cleans up resources
     */
    ~ZerodhaWebSocketClient();
    
    /**
     * Connect to the Kite WebSocket API
     * 
     * @return true if connection was initiated successfully
     */
    bool connect();
    
    /**
     * Disconnect from the WebSocket API
     */
    void disconnect();
    
    /**
     * Check if currently connected
     * 
     * @return true if connected to the WebSocket
     */
    bool is_connected() const;
    
    /**
     * Subscribe to market data for instruments
     * 
     * @param instrument_tokens Vector of instrument tokens to subscribe to
     * @param mode Streaming mode (LTP, QUOTE, or FULL)
     * @return true if subscription message was sent successfully
     */
    bool subscribe(const std::vector<int32_t>& instrument_tokens, 
                   StreamingMode mode = StreamingMode::FULL);
    
    /**
     * Unsubscribe from market data for instruments
     * 
     * @param instrument_tokens Vector of instrument tokens to unsubscribe from
     * @return true if unsubscription message was sent successfully
     */
    bool unsubscribe(const std::vector<int32_t>& instrument_tokens);
    
    /**
     * Change streaming mode for instruments
     * 
     * @param instrument_tokens Vector of instrument tokens to change mode for
     * @param mode New streaming mode
     * @return true if mode change message was sent successfully
     */
    bool set_mode(const std::vector<int32_t>& instrument_tokens, 
                  StreamingMode mode);

private:
    // WebSocket event handlers
    void on_connect();
    void on_disconnect(int code, const std::string& message);
    void on_message(const char* data, size_t length, bool is_binary);
    void on_error(const std::string& error);
    
    // Binary message parsing methods
    void parse_binary_message(const char* data, size_t length);
    void parse_packet(const char* data, size_t packet_length, MarketUpdate* update);
    void parse_ltp_packet(const char* data, MarketUpdate* update);
    void parse_quote_packet(const char* data, MarketUpdate* update);
    void parse_full_packet(const char* data, MarketUpdate* update);
    void parse_index_packet(const char* data, size_t length, MarketUpdate* update);
    
    // JSON message handling
    void handle_text_message(const std::string& message);
    
    // Subscription helpers
    bool send_subscription(const std::vector<int32_t>& tokens, const std::string& action);
    bool send_mode_change(const std::vector<int32_t>& tokens, StreamingMode mode);
    
    // Convert mode enum to string
    std::string mode_to_string(StreamingMode mode) const;
    
    // WebSocket loop and reconnection
    void reconnect();
    
    // Helper to check if a token is an index
    bool is_index_token(int32_t token) const;
    
    // Convert network-byte-order (big endian) to host byte order
    int32_t ntohl_manual(int32_t value) const;
    int16_t ntohs_manual(int16_t value) const;
    
    // Connection details
    std::string api_key_;
    std::string access_token_;
    std::string ws_url_;
    
    // Output queue and logger
    Common::LFQueue<MarketUpdate>& update_queue_;
    Common::Logger* logger_;
    
    // Boost.Beast WebSocket client components
    std::unique_ptr<net::io_context> ioc_;
    std::unique_ptr<ssl::context> ssl_ctx_;
    std::unique_ptr<tcp::resolver> resolver_;
    std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_;
    std::atomic<bool> ws_open_{false};
    
    // Connection state
    std::atomic<bool> connected_{false};
    std::atomic<bool> running_{false};
    std::atomic<bool> reconnecting_{false};
    
    // Synchronization
    std::mutex ws_mutex_; // Protects WebSocket access
    
    // Subscription state
    std::unordered_set<int32_t> subscribed_tokens_;
    std::unordered_map<int32_t, StreamingMode> token_modes_;
    std::mutex subscription_mutex_;
    
    // WebSocket thread
    std::thread ws_thread_;
    
    // Reconnection parameters
    uint32_t reconnect_attempt_{0};
    std::chrono::milliseconds reconnect_delay_{1000};
    const std::chrono::milliseconds max_reconnect_delay_{30000};
};

} // namespace Zerodha
} // namespace Adapter