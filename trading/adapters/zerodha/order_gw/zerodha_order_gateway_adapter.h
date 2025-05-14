#pragma once

#include <string>
#include <functional>
#include <map>
#include <queue>
#include <mutex>
#include <thread>
#include <random>

#include "common/thread_utils.h"
#include "common/lf_queue.h"
#include "common/macros.h"
#include "common/logging.h"
#include "common/types.h"

#include "exchange/order_server/client_request.h"
#include "exchange/order_server/client_response.h"

namespace Adapter {
namespace Zerodha {

/**
 * ZerodhaOrderGatewayAdapter converts internal order requests to Zerodha API calls
 * and translates responses back to the internal format.
 *
 * Supports both paper trading and live trading modes:
 * - Paper trading: Simulates order execution with realistic latency and fill models
 * - Live trading: Connects to Zerodha Kite API for real order placement
 */
class ZerodhaOrderGatewayAdapter {
public:
    ZerodhaOrderGatewayAdapter(Common::Logger* logger,
                              Common::ClientId client_id,
                              Exchange::ClientRequestLFQueue* client_requests,
                              Exchange::ClientResponseLFQueue* client_responses,
                              const std::string& api_key,
                              const std::string& api_secret);

    ~ZerodhaOrderGatewayAdapter();

    // Start and stop the order gateway thread
    auto start() -> void;
    auto stop() -> void;

    // Symbol mapping functions
    auto mapZerodhaSymbolToInternal(const std::string& zerodha_symbol) -> Common::TickerId;
    auto mapInternalToZerodhaSymbol(Common::TickerId ticker_id) -> std::string;

    // Register trading instruments
    auto registerInstrument(const std::string& zerodha_symbol, Common::TickerId internal_ticker_id) -> void;
    
    // Configuration
    void setPaperTradingMode(bool enable) { paper_trading_mode_ = enable; logSettings(); }
    void setPaperTradingFillProbability(double probability) { paper_trading_fill_probability_ = probability; logSettings(); }
    void setPaperTradingLatencyRange(double min_ms, double max_ms) {
        paper_trading_min_latency_ms_ = min_ms;
        paper_trading_max_latency_ms_ = max_ms;
        logSettings();
    }
    void setPaperTradingSlippageFactor(double factor) { paper_trading_slippage_factor_ = factor; logSettings(); }
    void setOrderStatusPollInterval(int interval_ms) { order_status_poll_interval_ms_ = interval_ms; logSettings(); }
    
    // Helper for logging settings
    void logSettings();

    // Deleted default, copy & move constructors and assignment-operators
    ZerodhaOrderGatewayAdapter() = delete;
    ZerodhaOrderGatewayAdapter(const ZerodhaOrderGatewayAdapter&) = delete;
    ZerodhaOrderGatewayAdapter(const ZerodhaOrderGatewayAdapter&&) = delete;
    ZerodhaOrderGatewayAdapter& operator=(const ZerodhaOrderGatewayAdapter&) = delete;
    ZerodhaOrderGatewayAdapter& operator=(const ZerodhaOrderGatewayAdapter&&) = delete;

private:
    // API credentials
    std::string api_key_;
    std::string api_secret_;
    
    // Client ID
    const Common::ClientId client_id_;
    
    // Run flag for the thread
    volatile bool run_ = false;
    
    // Logger
    Common::Logger* logger_ = nullptr;
    std::string time_str_;
    
    // Queues for order requests and responses
    Exchange::ClientRequestLFQueue* outgoing_requests_ = nullptr;
    Exchange::ClientResponseLFQueue* incoming_responses_ = nullptr;
    
    // Map of internal ticker IDs to Zerodha symbols
    std::map<Common::TickerId, std::string> ticker_to_symbol_map_;
    std::map<std::string, Common::TickerId> symbol_to_ticker_map_;
    std::mutex symbols_mutex_;
    
    // Map of internal order IDs to Zerodha order IDs
    std::map<Common::OrderId, std::string> order_id_map_;
    std::mutex orders_mutex_;
    
    // Last known status for each order (for detecting changes)
    std::map<Common::OrderId, std::pair<Exchange::ClientResponseType, Common::Qty>> last_order_status_;
    std::mutex last_order_status_mutex_;
    
    // Threads
    std::thread processing_thread_;
    std::thread status_thread_;
    
    // Paper trading settings
    bool paper_trading_mode_ = false;
    double paper_trading_fill_probability_ = 0.9;
    double paper_trading_min_latency_ms_ = 10.0;
    double paper_trading_max_latency_ms_ = 100.0;
    double paper_trading_slippage_factor_ = 0.0005; // 0.05%
    
    // Paper trading data structures
    struct PaperTradingOrder {
        Exchange::MEClientRequest internal_order;
        std::string zerodha_symbol;
        std::string zerodha_order_id;
        uint64_t execution_time;       // When the order will execute
        double fill_probability;       // Probability of order being filled
        double execution_price;        // Price including slippage
    };
    
    std::queue<PaperTradingOrder> pending_paper_orders_;
    std::mutex paper_orders_mutex_;
    std::mt19937 paper_trading_rng_;
    uint64_t next_paper_order_id_ = 1;
    
    // Live trading settings
    int order_status_poll_interval_ms_ = 2000;
    uint64_t next_zerodha_order_id_ = 100000; // For simulated responses
    
    // The main processing thread
    auto runOrderGateway() -> void;
    
    // Process outgoing order requests
    auto processOrderRequest(const Exchange::MEClientRequest& request) -> void;
    
    // Convert internal order request to Zerodha format and send
    auto sendNewOrder(const Exchange::MEClientRequest& request) -> void;
    auto sendCancelOrder(const Exchange::MEClientRequest& request) -> void;
    
    // Handle paper trading orders
    auto handlePaperTradeNewOrder(const Exchange::MEClientRequest& request, const std::string& zerodha_symbol) -> void;
    auto handlePaperTradeCancelOrder(const Exchange::MEClientRequest& request, const std::string& zerodha_order_id) -> void;
    auto processPaperOrders() -> void;
    
    // Handle live trading orders
    auto handleLiveTradeNewOrder(const Exchange::MEClientRequest& request, const std::string& zerodha_symbol) -> void;
    auto handleLiveTradeCancelOrder(const Exchange::MEClientRequest& request, const std::string& zerodha_order_id) -> void;
    auto pollOrderStatus() -> void;
    
    // Helper functions for paper trading
    auto simulateOrderLatency() -> uint64_t;
    auto shouldFillPaperOrder(double probability) -> bool;
    auto simulateSlippage() -> double;
    auto simulateNetworkLatency() -> int;
    auto simulateRandomFailure(double probability) -> bool;
    
    // HTTP API helpers
    auto sendOrderRequest(const std::string& endpoint, const std::string& params, 
                         std::string& response, bool is_delete = false) -> bool;
    
    // Status conversion
    auto convertZerodhaStatusToInternal(const std::string& zerodha_status) -> Exchange::ClientResponseType;
    auto shouldPropagateOrderUpdate(Common::OrderId order_id, Exchange::ClientResponseType new_status, Common::Qty new_filled_qty) -> bool;
    auto updateLastKnownOrderStatus(Common::OrderId order_id, Exchange::ClientResponseType status, Common::Qty filled_qty) -> void;
    
    // Convert responses
    auto convertResponseToInternal(const std::string& zerodha_resp, 
                                 const std::string& zerodha_order_id,
                                 Common::OrderId internal_order_id) -> Exchange::MEClientResponse;
};

} // namespace Zerodha
} // namespace Adapter