#include "zerodha_order_gateway_adapter.h"
#include <chrono>
#include <thread>
#include <sstream>

namespace Adapter {
namespace Zerodha {

// Constructor for the order gateway adapter
ZerodhaOrderGatewayAdapter::ZerodhaOrderGatewayAdapter(
    Common::Logger* logger,
    Common::ClientId client_id,
    ::Exchange::ClientRequestLFQueue* client_requests,
    ::Exchange::ClientResponseLFQueue* client_responses,
    const std::string& api_key,
    const std::string& api_secret) 
    : api_key_(api_key),
      api_secret_(api_secret),
      client_id_(client_id),
      logger_(logger),
      outgoing_requests_(client_requests),
      incoming_responses_(client_responses) {
    
    logger_->log("%:% %() % Initialized ZerodhaOrderGatewayAdapter with client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), client_id_);
}

// Destructor for the order gateway adapter
ZerodhaOrderGatewayAdapter::~ZerodhaOrderGatewayAdapter() {
    stop();
}

// Start the order gateway
auto ZerodhaOrderGatewayAdapter::start() -> void {
    if (run_) {
        logger_->log("%:% %() % Already running\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }
    
    run_ = true;
    
    // Start the main processing thread
    logger_->log("%:% %() % Starting order gateway thread for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), client_id_);
    
    processing_thread_ = std::thread(&ZerodhaOrderGatewayAdapter::runOrderGateway, this);
}

// Stop the order gateway
auto ZerodhaOrderGatewayAdapter::stop() -> void {
    if (!run_) {
        return;
    }
    
    logger_->log("%:% %() % Stopping order gateway for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), client_id_);
    
    run_ = false;
    
    // Wait for threads to terminate
    if (processing_thread_.joinable()) {
        processing_thread_.join();
    }
}

// Register a tradable instrument
auto ZerodhaOrderGatewayAdapter::registerInstrument(
    const std::string& zerodha_symbol, 
    Common::TickerId internal_ticker_id) -> void {
    
    std::lock_guard<std::mutex> lock(symbols_mutex_);
    
    ticker_to_symbol_map_[internal_ticker_id] = zerodha_symbol;
    symbol_to_ticker_map_[zerodha_symbol] = internal_ticker_id;
    
    logger_->log("%:% %() % Registered instrument %s (id: %) for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                zerodha_symbol.c_str(), internal_ticker_id, client_id_);
}

// Map Zerodha symbol to internal ticker ID
auto ZerodhaOrderGatewayAdapter::mapZerodhaSymbolToInternal(
    const std::string& zerodha_symbol) -> Common::TickerId {
    
    std::lock_guard<std::mutex> lock(symbols_mutex_);
    
    auto it = symbol_to_ticker_map_.find(zerodha_symbol);
    if (it != symbol_to_ticker_map_.end()) {
        return it->second;
    }
    
    logger_->log("%:% %() % ERROR: Unknown Zerodha symbol: %s for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                zerodha_symbol.c_str(), client_id_);
    
    return Common::TickerId_INVALID;
}

// Map internal ticker ID to Zerodha symbol
auto ZerodhaOrderGatewayAdapter::mapInternalToZerodhaSymbol(
    Common::TickerId ticker_id) -> std::string {
    
    std::lock_guard<std::mutex> lock(symbols_mutex_);
    
    auto it = ticker_to_symbol_map_.find(ticker_id);
    if (it != ticker_to_symbol_map_.end()) {
        return it->second;
    }
    
    logger_->log("%:% %() % ERROR: Unknown ticker_id: % for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                ticker_id, client_id_);
    
    return "";
}

// Main thread function for processing order requests and responses
auto ZerodhaOrderGatewayAdapter::runOrderGateway() -> void {
    logger_->log("%:% %() % Starting order gateway loop for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), client_id_);
    
    while (run_) {
        // Process any incoming order requests
        for (auto client_request = outgoing_requests_->getNextToRead();
             client_request;
             client_request = outgoing_requests_->getNextToRead()) {
            
            processOrderRequest(*client_request);
            outgoing_requests_->updateReadIndex();
        }
        
        // Small sleep to avoid busy-waiting
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

// Process outgoing order requests
auto ZerodhaOrderGatewayAdapter::processOrderRequest(
    const Exchange::MEClientRequest& request) -> void {
    
    logger_->log("%:% %() % Processing order request: % for client_id:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                request.toString().c_str(), client_id_);
    
    // Validate the request
    if (request.client_id_ != client_id_) {
        logger_->log("%:% %() % ERROR: Request client_id % does not match adapter client_id %\n", 
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_), 
                    request.client_id_, client_id_);
        return;
    }
    
    // Process based on request type
    switch (request.type_) {
        case Exchange::ClientRequestType::NEW:
            sendNewOrder(request);
            break;
            
        case Exchange::ClientRequestType::CANCEL:
            sendCancelOrder(request);
            break;
            
        default:
            logger_->log("%:% %() % ERROR: Unknown request type: % for client_id:%\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_), 
                        static_cast<int>(request.type_), client_id_);
            break;
    }
}

// Send a new order
auto ZerodhaOrderGatewayAdapter::sendNewOrder(
    const Exchange::MEClientRequest& request) -> void {
    
    std::string symbol = mapInternalToZerodhaSymbol(request.ticker_id_);
    
    logger_->log("%:% %() % Sending new order for client_id:% symbol:% price:% qty:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                client_id_, symbol.c_str(), request.price_, request.qty_);
    
    // Create a simulated acceptance response
    Exchange::MEClientResponse response;
    response.type_ = Exchange::ClientResponseType::ACCEPTED;
    response.client_id_ = request.client_id_;
    response.ticker_id_ = request.ticker_id_;
    response.order_id_ = request.order_id_;
    response.side_ = request.side_;
    response.price_ = request.price_;
    response.exec_qty_ = 0;
    response.leaves_qty_ = request.qty_;
    
    // Send the acceptance
    auto next_write = incoming_responses_->getNextToWriteTo();
    *next_write = response;
    incoming_responses_->updateWriteIndex();
    
    // In paper trading, we'll simulate a fill after a delay
    if (paper_trading_mode_) {
        // Simulate a fill in a separate thread
        std::thread([this, request, symbol]() {
            // Simulate execution latency (50-200ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(50 + rand() % 150));
            
            // Create a simulated fill response
            Exchange::MEClientResponse fill_response;
            fill_response.type_ = Exchange::ClientResponseType::FILLED;
            fill_response.client_id_ = request.client_id_;
            fill_response.ticker_id_ = request.ticker_id_;
            fill_response.order_id_ = request.order_id_;
            fill_response.side_ = request.side_;
            fill_response.price_ = request.price_;
            fill_response.exec_qty_ = request.qty_; // Full fill for simplicity
            fill_response.leaves_qty_ = 0;
            
            logger_->log("%:% %() % Simulated fill for client_id:% order_id:% symbol:% price:% qty:%\n", 
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_), 
                        client_id_, request.order_id_, symbol.c_str(), request.price_, request.qty_);
            
            // Send the fill
            auto next_write = incoming_responses_->getNextToWriteTo();
            *next_write = fill_response;
            incoming_responses_->updateWriteIndex();
        }).detach();
    }
}

// Send a cancel order
auto ZerodhaOrderGatewayAdapter::sendCancelOrder(
    const Exchange::MEClientRequest& request) -> void {
    
    std::string symbol = mapInternalToZerodhaSymbol(request.ticker_id_);
    
    logger_->log("%:% %() % Sending cancel for client_id:% order_id:% symbol:%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                client_id_, request.order_id_, symbol.c_str());
    
    // Create a simulated cancel response
    Exchange::MEClientResponse response;
    response.type_ = Exchange::ClientResponseType::CANCELED;
    response.client_id_ = request.client_id_;
    response.ticker_id_ = request.ticker_id_;
    response.order_id_ = request.order_id_;
    response.side_ = request.side_;
    response.price_ = request.price_;
    response.exec_qty_ = 0;
    response.leaves_qty_ = 0;
    
    // Send the cancellation
    auto next_write = incoming_responses_->getNextToWriteTo();
    *next_write = response;
    incoming_responses_->updateWriteIndex();
}

// Add logging when using the setters
void ZerodhaOrderGatewayAdapter::logSettings() {
    logger_->log("%:% %() % Current settings: paper_mode=%, fill_prob=%, latency=%-% ms, slippage=%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_), 
                paper_trading_mode_ ? "enabled" : "disabled",
                paper_trading_fill_probability_,
                paper_trading_min_latency_ms_, paper_trading_max_latency_ms_,
                paper_trading_slippage_factor_);
}

} // namespace Zerodha
} // namespace Adapter