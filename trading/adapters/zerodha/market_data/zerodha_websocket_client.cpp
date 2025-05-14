#include "zerodha_websocket_client.h"

// JSON library for message parsing and generation
#include <nlohmann/json.hpp>

// For endianness conversion
#include <arpa/inet.h>

// For base64 encoding
#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/buffer.h>
#include <openssl/err.h>

namespace Adapter {
namespace Zerodha {

// Base64 encoding functionality removed (was unused)

ZerodhaWebSocketClient::ZerodhaWebSocketClient(
    const std::string& api_key,
    const std::string& access_token,
    Common::LFQueue<MarketUpdate>& update_queue,
    Common::Logger* logger)
    : api_key_(api_key),
      access_token_(access_token),
      update_queue_(update_queue),
      logger_(logger)
{
    // Generate WebSocket URL with authentication parameters
    ws_url_ = "wss://ws.kite.trade/?api_key=" + api_key_ + "&access_token=" + access_token_;
    
    // Initialize Boost.Beast components
    std::string init_str;
    logger_->log("%:% %() % Initializing Boost.Beast WebSocket client\n", 
               __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&init_str));
    
    // Create an io_context
    ioc_ = std::make_unique<net::io_context>(1);
    
    // Create an SSL context
    ssl_ctx_ = std::make_unique<ssl::context>(ssl::context::tlsv12_client);
    
    // Load system root certificates
    ssl_ctx_->set_default_verify_paths();
    
    // Verify the remote server's certificate
    ssl_ctx_->set_verify_mode(ssl::verify_peer);
    
    // Create a resolver to look up DNS entries
    resolver_ = std::make_unique<tcp::resolver>(net::make_strand(*ioc_));
}

ZerodhaWebSocketClient::~ZerodhaWebSocketClient() {
    // Set flags to prevent new operations
    running_ = false;
    connected_ = false;
    
    try {
        // First, stop the IO context to interrupt any pending operations
        if (ioc_ && !ioc_->stopped()) {
            ioc_->stop();
        }
        
        // Add a delay to allow operations to cancel
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        // Safely close the WebSocket connection if it's open
        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            if (ws_ && ws_->is_open()) {
                beast::error_code ec;
                ws_->close(websocket::close_code::normal, ec);
                if (ec) {
                    std::string err_str;
                    logger_->log("%:% %() % Error closing WebSocket in destructor: %\n",
                               __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                               ec.message().c_str());
                }
                ws_open_ = false;
            }
        }
        
        // Wait for WebSocket thread to terminate with a timeout
        if (ws_thread_.joinable()) {
            // Use std::future with a timeout to safely join the thread
            std::future<void> future = std::async(std::launch::async, [this]() {
                if (ws_thread_.joinable()) {
                    ws_thread_.join();
                }
            });
            
            // Wait for up to 1 second
            if (future.wait_for(std::chrono::seconds(1)) == std::future_status::timeout) {
                // If the thread doesn't exit in time, detach it
                if (ws_thread_.joinable()) {
                    ws_thread_.detach();
                    if (logger_) {
                        std::string warn_str;
                        logger_->log("%:% %() % WebSocket thread join timed out in destructor, detaching\n",
                                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&warn_str));
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        if (logger_) {
            std::string err_str;
            logger_->log("%:% %() % Exception during destructor cleanup: %\n",
                       __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                       e.what());
        }
    } catch (...) {
        if (logger_) {
            std::string err_str;
            logger_->log("%:% %() % Unknown exception during destructor cleanup\n",
                       __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str));
        }
    }
}

bool ZerodhaWebSocketClient::connect() {
    if (connected_) {
        // Already connected
        return true;
    }
    
    std::string time_str;
    logger_->log("%:% %() % Connecting to Zerodha WebSocket: %\n",
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), ws_url_.c_str());
    
    // Reset connection state
    reconnect_attempt_ = 0;
    reconnect_delay_ = std::chrono::milliseconds(1000);
    running_ = true;
    
    // Parse URL components
    std::string url = ws_url_;
    
    if (url.substr(0, 6) == "wss://") {
        url = url.substr(6);
    } else if (url.substr(0, 5) == "ws://") {
        url = url.substr(5);
    }
    
    // Split into host and path
    size_t pathPos = url.find('/');
    std::string host = url.substr(0, pathPos);
    std::string path = pathPos != std::string::npos ? url.substr(pathPos) : "/";
    
    // Add query parameters if they're not in the path
    if (path.find('?') == std::string::npos && pathPos != std::string::npos) {
        path += "?api_key=" + api_key_ + "&access_token=" + access_token_;
    }
    
    // Log connection details
    std::string conn_str;
    logger_->log("%:% %() % Connection details: host='%', path='%', port=443, secure=true\n", 
              __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&conn_str),
              host.c_str(), path.c_str());
    
    // Start a thread for the WebSocket connection
    ws_thread_ = std::thread([this, host, path]() {
        try {
            std::string thread_str;
            logger_->log("%:% %() % Starting WebSocket client thread\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&thread_str));
            
            // Create a TCP socket and connect to the host
            auto socket = std::make_unique<tcp::socket>(net::make_strand(*ioc_));
            
            // Resolve the host name and port
            std::string port_str = "443"; // Always use 443 for wss://
            auto const results = resolver_->resolve(host, port_str);
            
            // Create the WebSocket SSL stream
            auto ssl_stream = beast::ssl_stream<tcp::socket>(std::move(*socket), *ssl_ctx_);
            ws_ = std::make_unique<websocket::stream<beast::ssl_stream<tcp::socket>>>(std::move(ssl_stream));
            
            // Set SNI hostname (required for SSL)
            // https://en.wikipedia.org/wiki/Server_Name_Indication
            if (!SSL_set_tlsext_host_name(ws_->next_layer().native_handle(), host.c_str())) {
                throw beast::system_error(
                    beast::error_code(
                        static_cast<int>(::ERR_get_error()),
                        net::error::get_ssl_category()),
                    "Failed to set SNI hostname");
            }
            
            // Connect to the host
            boost::asio::connect(beast::get_lowest_layer(*ws_), results);
            
            // Perform the SSL handshake
            ws_->next_layer().handshake(ssl::stream_base::client);
            
            // Set suggested timeout settings for the websocket
            ws_->set_option(websocket::stream_base::timeout::suggested(
                beast::role_type::client));
            
            // Set a decorator to change the User-Agent of the handshake
            ws_->set_option(websocket::stream_base::decorator(
                [](websocket::request_type& req) {
                    req.set(http::field::user_agent, 
                        std::string(BOOST_BEAST_VERSION_STRING) + 
                        " kite-websocket-client");
                }));
            
            // Perform the websocket handshake
            std::string url_host = host + ":" + port_str;
            ws_->handshake(url_host, path);
            
            // Connection established successfully
            ws_open_ = true;
            connected_ = true;
            
            std::string success_str;
            logger_->log("%:% %() % WebSocket connection established\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&success_str));
            
            // Notify that we're connected
            this->on_connect();
            
            // Now start receiving messages
            beast::flat_buffer buffer;
            // Set up a timeout for reads to allow for cancellation
            ws_->set_option(websocket::stream_base::timeout::suggested(
                beast::role_type::client));
                
            while (running_) {
                try {
                    // Check if the WebSocket is still open and we're still running
                    bool is_open = false;
                    {
                        std::lock_guard<std::mutex> lock(ws_mutex_);
                        is_open = ws_ && ws_->is_open();
                    }
                    
                    if (!is_open || !running_) {
                        break;  // Exit the loop if WebSocket closed or not running
                    }
                    
                    // Use an asynchronous read with a timeout to ensure we can interrupt it
                    beast::error_code ec;
                    {
                        std::lock_guard<std::mutex> lock(ws_mutex_);
                        
                        // Only read if we're still running and connected
                        if (ws_ && ws_->is_open() && running_) {
                            // Set a non-blocking timeout
                            ws_->set_option(websocket::stream_base::timeout::suggested(
                                beast::role_type::client));
                                
                            // Use a short read timeout to allow for cancellation
                            ws_->read(buffer, ec);
                            
                            // If we have an error check if it's due to timeout or cancellation
                            if (ec) {
                                if (ec == beast::error::timeout || 
                                    ec == beast::websocket::error::closed ||
                                    ec == boost::asio::error::operation_aborted) {
                                    // These are expected errors during shutdown
                                    if (!running_) {
                                        break;  // Exit if we're shutting down
                                    }
                                    // Otherwise continue the loop to check running flag again
                                    continue;
                                } else {
                                    // Unexpected error
                                    throw beast::system_error{ec};
                                }
                            }
                            
                            // Extract the message
                            auto msg = beast::buffers_to_string(buffer.data());
                            
                            // Determine if this is binary or text
                            bool is_binary = ws_->got_binary();
                            
                            // Process the message
                            this->on_message(msg.data(), msg.size(), is_binary);
                            
                            // Clear the buffer
                            buffer.consume(buffer.size());
                        }
                    }
                    
                    // Add a short sleep to avoid spinning CPU if reads are very fast
                    if (running_) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(10));
                    }
                } catch (const beast::system_error& e) {
                    if (e.code() == beast::websocket::error::closed) {
                        // Normal closure
                        std::string close_str;
                        logger_->log("%:% %() % WebSocket closed normally\n", 
                                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&close_str));
                        
                        // Notify about disconnection
                        this->on_disconnect(websocket::close_code::normal, "Connection closed normally");
                        break;
                    } else {
                        // Abnormal closure
                        std::string err_str;
                        logger_->log("%:% %() % WebSocket error: %\n", 
                                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                                    e.what());
                        
                        // Notify about error
                        this->on_error(e.what());
                        break;
                    }
                } catch (const std::exception& e) {
                    // Handle any other exceptions
                    std::string err_str;
                    logger_->log("%:% %() % Exception during WebSocket read: %\n", 
                               __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                               e.what());
                    break;
                }
            }
            
            // If we're still connected, close gracefully
            {
                std::lock_guard<std::mutex> lock(ws_mutex_);
                if (ws_ && ws_->is_open()) {
                    beast::error_code ec;
                    ws_->close(websocket::close_code::normal, ec);
                    if (ec) {
                        std::string err_str;
                        logger_->log("%:% %() % Error closing WebSocket: %\n", 
                                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                                    ec.message().c_str());
                    }
                }
            }
            
            // Cleanup
            ws_open_ = false;
            connected_ = false;
            
        } catch (const std::exception& e) {
            std::string err_str;
            logger_->log("%:% %() % Exception in WebSocket thread: %\n", 
                       __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                       e.what());
            
            // Notify about error
            this->on_error(e.what());
        }
        
        std::string exit_str;
        logger_->log("%:% %() % WebSocket thread exiting\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&exit_str));
    });
    
    return true;
}

void ZerodhaWebSocketClient::disconnect() {
    // Set flags for graceful shutdown FIRST
    running_ = false;
    connected_ = false;
    
    try {
        std::string time_str;
        logger_->log("%:% %() % Disconnecting from WebSocket\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        
        // Make a copy of the client pointer to allow safe access outside the lock
        std::unique_ptr<websocket::stream<beast::ssl_stream<tcp::socket>>> ws_copy;
        
        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            // Swap our ws_ with ws_copy to safely access it outside the lock
            if (ws_ && ws_open_) {
                ws_copy = std::move(ws_);
                ws_open_ = false;
            }
        }
        
        // First, try to close the WebSocket connection cleanly (if we have one)
        if (ws_copy) {
            try {
                // Set very short timeouts for the closing operations
                ws_copy->set_option(websocket::stream_base::timeout::suggested(
                    beast::role_type::client));
                
                // Cancel any pending operations
                boost::beast::get_lowest_layer(*ws_copy).cancel();
                
                // Try to close gracefully with a short timeout
                beast::error_code ec;
                ws_copy->close(websocket::close_code::going_away, ec);
                
                if (ec) {
                    logger_->log("%:% %() % Non-critical error during WebSocket close: %\n", 
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                            ec.message().c_str());
                }
            }
            catch (const std::exception& e) {
                logger_->log("%:% %() % Exception during WebSocket close: %\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                        e.what());
            }
        }
        
        // Stop the IO context to force any pending operations to complete
        if (ioc_ && !ioc_->stopped()) {
            try {
                ioc_->stop();
            }
            catch (const std::exception& e) {
                logger_->log("%:% %() % Exception stopping I/O context: %\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                        e.what());
            }
        }
        
        // Give operations a moment to complete
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        
        // Wait for WebSocket thread to exit with a timeout
        if (ws_thread_.joinable()) {
            // Use a safer approach with a timeout
            // Try to join for 2 seconds, then detach if it takes too long
            std::future<void> future = std::async(std::launch::async, [this]() {
                if (ws_thread_.joinable()) {
                    ws_thread_.join();
                }
            });
            
            // Wait for up to 2 seconds for the thread to finish
            if (future.wait_for(std::chrono::seconds(2)) == std::future_status::timeout) {
                // If it takes too long, detach the thread rather than waiting forever
                // This is safer than continuing to wait when shutting down
                if (ws_thread_.joinable()) {
                    ws_thread_.detach();
                    std::string warn_str;
                    logger_->log("%:% %() % WebSocket thread join timed out, detaching\n", 
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&warn_str));
                }
            }
        }
    }
    catch (const std::exception& e) {
        std::string err_str;
        logger_->log("%:% %() % Exception during disconnect: %\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str), 
                e.what());
    }
    catch (...) {
        std::string err_str;
        logger_->log("%:% %() % Unknown exception during disconnect\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str));
    }
}

bool ZerodhaWebSocketClient::is_connected() const {
    return connected_;
}

bool ZerodhaWebSocketClient::subscribe(const std::vector<int32_t>& instrument_tokens,
                         StreamingMode mode) {
    if (!connected_) {
        std::string time_str;
        logger_->log("%:% %() % Cannot subscribe, not connected\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        return false;
    }
    
    // First subscribe to the instruments
    if (!send_subscription(instrument_tokens, "subscribe")) {
        return false;
    }
    
    // Then set the desired mode
    if (!send_mode_change(instrument_tokens, mode)) {
        return false;
    }
    
    // Update subscription state
    {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        for (auto token : instrument_tokens) {
            subscribed_tokens_.insert(token);
            token_modes_[token] = mode;
        }
    }
    
    return true;
}

bool ZerodhaWebSocketClient::unsubscribe(const std::vector<int32_t>& instrument_tokens) {
    if (!connected_) {
        std::string time_str;
        logger_->log("%:% %() % Cannot unsubscribe, not connected\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        return false;
    }
    
    if (!send_subscription(instrument_tokens, "unsubscribe")) {
        return false;
    }
    
    // Update subscription state
    {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        for (auto token : instrument_tokens) {
            subscribed_tokens_.erase(token);
            token_modes_.erase(token);
        }
    }
    
    return true;
}

bool ZerodhaWebSocketClient::set_mode(const std::vector<int32_t>& instrument_tokens,
                        StreamingMode mode) {
    if (!connected_) {
        std::string time_str;
        logger_->log("%:% %() % Cannot set mode, not connected\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        return false;
    }
    
    if (!send_mode_change(instrument_tokens, mode)) {
        return false;
    }
    
    // Update mode state
    {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        for (auto token : instrument_tokens) {
            if (subscribed_tokens_.count(token) > 0) {
                token_modes_[token] = mode;
            }
        }
    }
    
    return true;
}

void ZerodhaWebSocketClient::on_connect() {
    std::string time_str;
    logger_->log("%:% %() % Connected to Zerodha WebSocket\n",
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    
    connected_ = true;
    reconnect_attempt_ = 0;
    reconnect_delay_ = std::chrono::milliseconds(1000);
    
    // Resubscribe to tokens if reconnecting
    std::unordered_map<StreamingMode, std::vector<int32_t>> mode_tokens;
    
    {
        std::lock_guard<std::mutex> lock(subscription_mutex_);
        if (!subscribed_tokens_.empty()) {
            // Group tokens by mode
            for (const auto& token : subscribed_tokens_) {
                auto mode = token_modes_[token];
                mode_tokens[mode].push_back(token);
            }
        }
    }
    
    // Resubscribe by mode group
    for (const auto& [mode, tokens] : mode_tokens) {
        subscribe(tokens, mode);
    }
}

void ZerodhaWebSocketClient::on_disconnect(int code, const std::string& message) {
    std::string time_str;
    logger_->log("%:% %() % Disconnected from Zerodha WebSocket: code=%, message=%\n",
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                code, message.c_str());
    
    connected_ = false;
    
    // Attempt reconnection if still running
    if (running_ && !reconnecting_) {
        reconnect();
    }
}

void ZerodhaWebSocketClient::on_message(const char* data, size_t length, bool is_binary) {
    std::string time_str;
    
    // Log information about the received message
    if (is_binary) {
        logger_->log("%:% %() % Received binary message of length %\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                   length);
                   
        // Process binary market data
        parse_binary_message(data, length);
    } else {
        // Show first 100 chars of text message (or less if shorter)
        std::string preview;
        if (length > 100) {
            preview = std::string(data, 100) + "...";
        } else {
            preview = std::string(data, length);
        }
        
        logger_->log("%:% %() % Received text message: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                   preview.c_str());
                   
        // Process JSON message
        std::string text_message(data, length);
        handle_text_message(text_message);
    }
}

void ZerodhaWebSocketClient::on_error(const std::string& error) {
    std::string time_str;
    logger_->log("%:% %() % WebSocket error: %\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), error.c_str());
    
    // Attempt reconnection if still running
    if (running_ && !reconnecting_ && !connected_) {
        reconnect();
    }
}

void ZerodhaWebSocketClient::parse_binary_message(const char* data, size_t length) {
    std::string time_str;
    
    if (length < 4) {
        logger_->log("%:% %() % Binary message too short: % bytes\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                   length);
        return;
    }
    
    // Read number of packets (first 2 bytes)
    int16_t num_packets = ntohs_manual(*reinterpret_cast<const int16_t*>(data));
    
    logger_->log("%:% %() % Processing % packets from binary message\n", 
               __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
               num_packets);
    
    // Process each packet
    size_t offset = 2; // Start after num_packets
    int processed_packets = 0;
    
    for (int i = 0; i < num_packets && offset + 2 < length; i++) {
        // Read packet length (2 bytes)
        int16_t packet_length = ntohs_manual(*reinterpret_cast<const int16_t*>(data + offset));
        offset += 2;
        
        // Ensure we have enough bytes
        if (offset + packet_length > length) {
            logger_->log("%:% %() % Packet % truncated, need % bytes but only % remaining\n", 
                       __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                       i, packet_length, length - offset);
            break;
        }
        
        // Get next slot in queue
        MarketUpdate* update = update_queue_.getNextToWriteTo();
        if (update) {
            const char* packet_data = data + offset;
            
            // Set timestamp
            update->timestamp = Common::getCurrentNanos();
            
            // Parse packet into update
            parse_packet(packet_data, packet_length, update);
            
            // Commit the update
            update_queue_.updateWriteIndex();
            processed_packets++;
        } else {
            logger_->log("%:% %() % Queue full, cannot process packet %\n", 
                       __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                       i);
        }
        
        // Move to next packet
        offset += packet_length;
    }
    
    logger_->log("%:% %() % Successfully processed % of % packets\n", 
               __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
               processed_packets, num_packets);
}

void ZerodhaWebSocketClient::parse_packet(const char* data, size_t packet_length, MarketUpdate* update) {
    // Read instrument token (first 4 bytes)
    int32_t token = ntohl_manual(*reinterpret_cast<const int32_t*>(data));
    update->instrument_token = token;
    
    // Determine packet type based on length and token
    if (is_index_token(token)) {
        // Index packet
        update->type = MarketUpdateType::INDEX;
        parse_index_packet(data, packet_length, update);
    } else if (packet_length == 8) {
        // LTP packet (mode = "ltp")
        update->type = MarketUpdateType::LTP;
        parse_ltp_packet(data, update);
    } else if (packet_length == 44) {
        // Quote packet (mode = "quote")
        update->type = MarketUpdateType::QUOTE;
        parse_quote_packet(data, update);
    } else if (packet_length == 184) {
        // Full packet (mode = "full")
        update->type = MarketUpdateType::FULL;
        parse_full_packet(data, update);
    } else {
        // Unknown packet type
        std::string time_str;
        logger_->log("%:% %() % Unknown packet type: token=%, length=%\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                    token, packet_length);
    }
}

void ZerodhaWebSocketClient::parse_ltp_packet(const char* data, MarketUpdate* update) {
    // LTP packet format:
    // Bytes 0-3: Instrument token (already read)
    // Bytes 4-7: Last traded price
    
    // Read last traded price (bytes 4-7)
    int32_t last_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 4));
    
    // Convert price from paise to rupees (divide by 100)
    update->last_price = last_price / 100.0;
}

void ZerodhaWebSocketClient::parse_quote_packet(const char* data, MarketUpdate* update) {
    // Quote packet format:
    // Bytes 0-3: Instrument token (already read)
    // Bytes 4-7: Last traded price
    // Bytes 8-11: Last traded quantity
    // Bytes 12-15: Average traded price
    // Bytes 16-19: Volume traded for the day
    // Bytes 20-23: Total buy quantity
    // Bytes 24-27: Total sell quantity
    // Bytes 28-31: Open price of the day
    // Bytes 32-35: High price of the day
    // Bytes 36-39: Low price of the day
    // Bytes 40-43: Close price
    
    // Parse all fields
    int32_t last_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 4));
    int32_t last_quantity = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 8));
    int32_t average_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 12));
    int32_t volume = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 16));
    int32_t buy_quantity = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 20));
    int32_t sell_quantity = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 24));
    int32_t open_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 28));
    int32_t high_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 32));
    int32_t low_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 36));
    int32_t close_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 40));
    
    // Convert all prices from paise to rupees (divide by 100)
    update->last_price = last_price / 100.0;
    update->last_quantity = last_quantity;
    update->average_price = average_price / 100.0;
    update->volume = volume;
    update->buy_quantity = buy_quantity;
    update->sell_quantity = sell_quantity;
    update->open_price = open_price / 100.0;
    update->high_price = high_price / 100.0;
    update->low_price = low_price / 100.0;
    update->close_price = close_price / 100.0;
}

void ZerodhaWebSocketClient::parse_full_packet(const char* data, MarketUpdate* update) {
    // First parse all the quote fields (first 44 bytes)
    parse_quote_packet(data, update);
    
    // Full packet format (additional fields):
    // Bytes 44-47: Last traded timestamp
    // Bytes 48-51: Open Interest
    // Bytes 52-55: Open Interest Day High
    // Bytes 56-59: Open Interest Day Low
    // Bytes 60-63: Exchange timestamp
    // Bytes 64-183: Market depth entries (10 entries, 12 bytes each)
    
    // Parse additional fields
    int32_t last_trade_time = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 44));
    int32_t open_interest = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 48));
    int32_t oi_day_high = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 52));
    int32_t oi_day_low = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 56));
    int32_t exchange_timestamp = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 60));
    
    update->last_trade_time = last_trade_time;
    update->open_interest = open_interest;
    update->open_interest_day_high = oi_day_high;
    update->open_interest_day_low = oi_day_low;
    update->exchange_timestamp = exchange_timestamp;
    
    // Parse market depth (5 bid and 5 ask levels)
    for (int i = 0; i < 5; i++) {
        // Bids (first 5 entries)
        int offset = 64 + (i * 12); // Each entry is 12 bytes
        
        // Each entry has: quantity (4 bytes), price (4 bytes), orders (2 bytes), padding (2 bytes)
        int32_t quantity = ntohl_manual(*reinterpret_cast<const int32_t*>(data + offset));
        int32_t price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + offset + 4));
        int16_t orders = ntohs_manual(*reinterpret_cast<const int16_t*>(data + offset + 8));
        
        update->bids[i].quantity = quantity;
        update->bids[i].price = price;
        update->bids[i].orders = orders;
        update->bids[i].padding = 0; // Skip padding
    }
    
    for (int i = 0; i < 5; i++) {
        // Asks (next 5 entries)
        int offset = 124 + (i * 12); // 64 + (5 * 12) = 124 (start of asks)
        
        int32_t quantity = ntohl_manual(*reinterpret_cast<const int32_t*>(data + offset));
        int32_t price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + offset + 4));
        int16_t orders = ntohs_manual(*reinterpret_cast<const int16_t*>(data + offset + 8));
        
        update->asks[i].quantity = quantity;
        update->asks[i].price = price;
        update->asks[i].orders = orders;
        update->asks[i].padding = 0; // Skip padding
    }
}

void ZerodhaWebSocketClient::parse_index_packet(const char* data, size_t length, MarketUpdate* update) {
    // Index packet format:
    // Bytes 0-3: Token (already read)
    // Bytes 4-7: Last traded price
    // Bytes 8-11: High of the day
    // Bytes 12-15: Low of the day
    // Bytes 16-19: Open of the day
    // Bytes 20-23: Close of the day
    // Bytes 24-27: Price change (If mode is quote, the packet ends here)
    // Bytes 28-31: Exchange timestamp (only in full mode)
    
    // Parse fields
    int32_t last_price = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 4));
    int32_t high = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 8));
    int32_t low = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 12));
    int32_t open = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 16));
    int32_t close = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 20));
    
    // Convert all prices from paise to rupees (divide by 100)
    update->last_price = last_price / 100.0;
    update->high_price = high / 100.0;
    update->low_price = low / 100.0;
    update->open_price = open / 100.0;
    update->close_price = close / 100.0;
    
    // Check if we have the extended fields
    if (length >= 32) {
        // Full mode includes exchange timestamp
        int32_t exchange_timestamp = ntohl_manual(*reinterpret_cast<const int32_t*>(data + 28));
        update->exchange_timestamp = exchange_timestamp;
    }
}

void ZerodhaWebSocketClient::handle_text_message(const std::string& message) {
    try {
        // Parse JSON message
        auto json = nlohmann::json::parse(message);
        
        if (json.contains("type")) {
            std::string type = json["type"];
            
            if (type == "order") {
                // Order postback
                std::string time_str;
                logger_->log("%:% %() % Received order postback: %\n", 
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                            message.c_str());
                
                // TODO: Handle order postbacks if needed
            } else if (type == "error") {
                // Error message
                std::string error_message = json["data"];
                std::string time_str;
                logger_->log("%:% %() % Received error from Zerodha WebSocket: %\n",
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                            error_message.c_str());
            } else if (type == "message") {
                // System message
                std::string system_message = json["data"];
                std::string time_str;
                logger_->log("%:% %() % Received system message from Zerodha WebSocket: %\n",
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                            system_message.c_str());
            }
        }
    } catch (const std::exception& e) {
        std::string time_str;
        logger_->log("%:% %() % Error parsing JSON message: %, message: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                    e.what(), message.c_str());
    }
}

bool ZerodhaWebSocketClient::send_subscription(const std::vector<int32_t>& tokens, const std::string& action) {
    if (!connected_ || !ws_open_ || !ws_) {
        return false;
    }
    
    // Create subscription message
    nlohmann::json message = {
        {"a", action},
        {"v", tokens}
    };
    
    std::string json_str = message.dump();
    
    std::string time_str;
    logger_->log("%:% %() % Sending subscription: action=%, tokens=%\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                action.c_str(), json_str.c_str());
    
    try {
        // Send the message using Boost.Beast with thread safety
        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            if (ws_ && ws_->is_open()) {
                beast::error_code ec;
                ws_->write(net::buffer(json_str), ec);
                
                if (ec) {
                    logger_->log("%:% %() % Error sending subscription: %\n", 
                                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                                ec.message().c_str());
                    return false;
                }
            } else {
                logger_->log("%:% %() % Cannot send subscription, WebSocket not open\n", 
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        logger_->log("%:% %() % Exception sending subscription: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                    e.what());
        return false;
    }
}

bool ZerodhaWebSocketClient::send_mode_change(const std::vector<int32_t>& tokens, StreamingMode mode) {
    if (!connected_ || !ws_open_ || !ws_) {
        return false;
    }
    
    // Convert mode enum to string
    std::string mode_str = mode_to_string(mode);
    
    // Create mode change message
    nlohmann::json message = {
        {"a", "mode"},
        {"v", {mode_str, tokens}}
    };
    
    std::string json_str = message.dump();
    
    std::string time_str;
    logger_->log("%:% %() % Sending mode change: mode=%, tokens=%\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                mode_str.c_str(), json_str.c_str());
    
    try {
        // Send the message using Boost.Beast with thread safety
        {
            std::lock_guard<std::mutex> lock(ws_mutex_);
            if (ws_ && ws_->is_open()) {
                beast::error_code ec;
                ws_->write(net::buffer(json_str), ec);
                
                if (ec) {
                    logger_->log("%:% %() % Error sending mode change: %\n", 
                                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                                ec.message().c_str());
                    return false;
                }
            } else {
                logger_->log("%:% %() % Cannot send mode change, WebSocket not open\n", 
                            __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
                return false;
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        logger_->log("%:% %() % Exception sending mode change: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str),
                    e.what());
        return false;
    }
}

std::string ZerodhaWebSocketClient::mode_to_string(StreamingMode mode) const {
    switch (mode) {
        case StreamingMode::LTP:   return "ltp";
        case StreamingMode::QUOTE: return "quote";
        case StreamingMode::FULL:  return "full";
        default:                   return "full";  // Default to full mode
    }
}


void ZerodhaWebSocketClient::reconnect() {
    // Use a mutex to prevent concurrent reconnection attempts
    static std::mutex reconnect_mutex;
    std::unique_lock<std::mutex> lock(reconnect_mutex, std::try_to_lock);
    
    // If we can't acquire the lock, another reconnection is in progress
    if (!lock.owns_lock() || reconnecting_) {
        return;
    }
    
    reconnecting_ = true;
    
    try {
        // Calculate reconnect delay with exponential backoff
        std::chrono::milliseconds delay = reconnect_delay_;
        reconnect_attempt_++;
        
        // Double the delay for next time, up to max_reconnect_delay_
        reconnect_delay_ = std::min(
            reconnect_delay_ * 2,
            max_reconnect_delay_
        );
        
        std::string time_str;
        logger_->log("%:% %() % Reconnecting in % ms (attempt %)\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), 
                    delay.count(), reconnect_attempt_);
        
        // Wait before reconnecting
        std::this_thread::sleep_for(delay);
        
        // Reset WebSocket state first
        ws_open_ = false;
        
        // Flag that we're shutting down cleanly
        running_ = false;
        
        // Close any existing connection
        if (ws_ && ws_->is_open()) {
            beast::error_code ec;
            ws_->close(websocket::close_code::normal, ec);
            if (ec) {
                std::string err_str;
                logger_->log("%:% %() % Error closing WebSocket: %\n",
                           __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str),
                           ec.message().c_str());
            }
        }
        
        // Reset the WebSocket object
        ws_.reset();
        
        // Wait for the WebSocket thread to exit, with a timeout
        if (ws_thread_.joinable()) {
            // Try to join with a timeout
            std::thread joiner([this]() {
                if (ws_thread_.joinable()) {
                    ws_thread_.join();
                }
            });
            
            // Give it 2 seconds to join
            if (joiner.joinable()) {
                joiner.join();
            }
        }
        
        // Set running back to true for the new connection
        running_ = true;
        
        // Try to establish a new connection
        std::string msg_str;
        logger_->log("%:% %() % Creating new connection (attempt %)\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&msg_str), 
                   reconnect_attempt_);
                   
        // Start the connection process
        if (!connect()) {
            logger_->log("%:% %() % Reconnection attempt % failed\n", 
                       __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&msg_str), 
                       reconnect_attempt_);
        }
    }
    catch (const std::exception& e) {
        std::string err_str;
        logger_->log("%:% %() % Exception during reconnection: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str), 
                   e.what());
    }
    catch (...) {
        std::string err_str;
        logger_->log("%:% %() % Unknown exception during reconnection\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&err_str));
    }
    
    reconnecting_ = false;
}

bool ZerodhaWebSocketClient::is_index_token(int32_t token) const {
    // Zerodha uses specific ranges for index tokens
    // This is a simplified check - adjust based on actual token ranges
    return token >= 100000 && token <= 300000;
}

int32_t ZerodhaWebSocketClient::ntohl_manual(int32_t value) const {
    return ntohl(value);
}

int16_t ZerodhaWebSocketClient::ntohs_manual(int16_t value) const {
    return ntohs(value);
}

} // namespace Zerodha
} // namespace Adapter