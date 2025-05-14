#pragma once

#include <string>
#include <functional>
#include <map>

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
    
    // Map of internal order IDs to Zerodha order IDs
    std::map<Common::OrderId, std::string> order_id_map_;
    
    // The main processing thread
    auto runOrderGateway() -> void;
    
    // Process outgoing order requests
    auto processOrderRequest(const Exchange::MEClientRequest& request) -> void;
    
    // Convert internal order request to Zerodha format and send
    auto sendNewOrder(const Exchange::MEClientRequest& request) -> void;
    auto sendCancelOrder(const Exchange::MEClientRequest& request) -> void;
    
    // Process order responses from Zerodha and convert to internal format
    auto processOrderResponses() -> void;
    auto convertResponseToInternal(const std::string& zerodha_resp, 
                                  const std::string& zerodha_order_id,
                                  Common::OrderId internal_order_id) -> Exchange::MEClientResponse;
};

} // namespace Zerodha
} // namespace Adapter