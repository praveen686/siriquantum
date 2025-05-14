#pragma once

#include <functional>
#include <string>
#include <ctime>
#include <map>
#include <vector>
#include <atomic>
#include <thread>
#include <mutex>
#include <curl/curl.h>
#include <jsoncpp/json/json.h>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/logging.h"
#include "common/types.h"

#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"
#include "trading/adapters/binance/market_data/binance_config.h"

namespace Trading {

class OrderGateway {
public:
    OrderGateway(Common::ClientId client_id,
               Exchange::ClientRequestLFQueue *client_requests,
               Exchange::ClientResponseLFQueue *client_responses,
               const BinanceConfig& config,
               const std::vector<std::string>& symbols);

    ~OrderGateway();

    // Start and stop the order gateway
    auto start() -> void;
    auto stop() -> void;

    // Get current market price for a symbol
    double getCurrentPrice(const std::string& symbol);

    // Get exchange info including filters
    Json::Value getExchangeInfo(const std::string& symbol);

    // Check if an order price passes the PERCENT_PRICE_BY_SIDE filter
    bool checkPercentPriceFilter(const std::string& symbol, Common::Side side, double price);

    // Deleted default, copy & move constructors and assignment-operators
    OrderGateway() = delete;
    OrderGateway(const OrderGateway &) = delete;
    OrderGateway(const OrderGateway &&) = delete;
    OrderGateway &operator=(const OrderGateway &) = delete;
    OrderGateway &operator=(const OrderGateway &&) = delete;

private:
    const Common::ClientId client_id_;
    BinanceConfig config_;

    // Lock free queues for client requests and responses
    Exchange::ClientRequestLFQueue *incoming_requests_ = nullptr; // Renamed from outgoing_requests_ for clarity - these are requests FROM TradeEngine
    Exchange::ClientResponseLFQueue *outgoing_responses_ = nullptr; // Renamed from incoming_responses_ for clarity - these are responses TO TradeEngine

    // API interaction
    CURL *curl_ = nullptr;
    std::mutex curl_mutex_; // Protects curl_ for thread safety
    
    // Mapping between symbols and ticker IDs
    std::vector<std::string> symbols_;
    std::map<Common::TickerId, std::string> ticker_id_to_symbol_;
    std::map<std::string, Common::TickerId> symbol_to_ticker_id_;
    
    // Mapping from OrderId to Binance order ID for cancellations
    std::map<Common::OrderId, std::string> order_id_to_binance_id_;
    std::mutex order_map_mutex_; // Protects order_id_to_binance_id_

    volatile bool run_ = false;
    std::string time_str_;
    Common::Logger logger_;

    // Sequence numbers for requests and responses
    std::atomic<size_t> next_outgoing_seq_num_ = 1;
    std::atomic<size_t> next_exp_seq_num_ = 1;

    // Helper functions for API
    static size_t curlWriteCallback(char *ptr, size_t size, size_t nmemb, std::string *data);
    std::string createSignature(const std::string& query_string);
    std::string generateTimestamp();
    
    // HTTP request helper
    Json::Value sendRequest(const std::string& endpoint, const std::string& query_string, bool is_post);
    
    // REST API endpoints
    Json::Value sendNewOrder(Common::TickerId ticker_id, Common::Side side, Common::Price price, Common::Qty qty, Common::OrderId order_id);
    Json::Value cancelOrder(Common::TickerId ticker_id, Common::OrderId order_id, const std::string& binance_order_id);
    Json::Value getOrderStatus(Common::TickerId ticker_id, const std::string& binance_order_id);
    
    // Process client requests and responses
    void processClientRequest(const Exchange::MEClientRequest* request);
    void createClientResponse(const Json::Value& response, const Exchange::MEClientRequest* request, Exchange::ClientResponseType type);
    
    // Handle order query responses
    void handleOrderQueryResponse(const Json::Value& response, Common::OrderId order_id, Common::TickerId ticker_id);
    
    // Order status polling thread
    std::thread order_status_thread_;
    void pollOrderStatuses();
    
    // Main run loop
    auto run() noexcept -> void;
};

} // namespace Trading