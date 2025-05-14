#include "trading/adapters/binance/order_gw/binance_order_gateway.h"
#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>    // For fmod and floor
#include <openssl/hmac.h>
#include <openssl/sha.h>

namespace Trading {

OrderGateway::OrderGateway(Common::ClientId client_id,
                         Exchange::ClientRequestLFQueue *client_requests,
                         Exchange::ClientResponseLFQueue *client_responses,
                         const BinanceConfig& config,
                         const std::vector<std::string>& symbols)
    : client_id_(client_id),
      config_(config),
      incoming_requests_(client_requests),  // Renamed from outgoing_requests_ - these are requests FROM TradeEngine
      outgoing_responses_(client_responses), // Renamed from incoming_responses_ - these are responses TO TradeEngine
      symbols_(symbols),
      logger_("logs/trading_binance_order_gateway_" + std::to_string(client_id) + ".log") {
    
    // Initialize symbol mappings
    for (size_t i = 0; i < symbols_.size() && i < Common::ME_MAX_TICKERS; ++i) {
        symbol_to_ticker_id_[symbols_[i]] = i;
        ticker_id_to_symbol_[i] = symbols_[i];
    }
    
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_ALL);
    curl_ = curl_easy_init();
    
    if (!curl_) {
        logger_.log("%:% %() % Failed to initialize CURL\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_));
        FATAL("Failed to initialize CURL");
    }
}

OrderGateway::~OrderGateway() {
    stop();
    
    if (curl_) {
        curl_easy_cleanup(curl_);
        curl_ = nullptr;
    }
    
    curl_global_cleanup();
}

void OrderGateway::start() {
    run_ = true;
    
    // Start the main thread
    ASSERT(Common::createAndStartThread(-1, "Trading/BinanceOrderGateway", [this] { run(); }) != nullptr, 
           "Failed to start BinanceOrderGateway thread.");
    
    // Start order status polling thread
    order_status_thread_ = std::thread(&OrderGateway::pollOrderStatuses, this);
}

void OrderGateway::stop() {
    run_ = false;
    
    // Wait for order status thread to finish
    if (order_status_thread_.joinable()) {
        order_status_thread_.join();
    }
}

size_t OrderGateway::curlWriteCallback(char *ptr, size_t size, size_t nmemb, std::string *data) {
    data->append(ptr, size * nmemb);
    return size * nmemb;
}

std::string OrderGateway::generateTimestamp() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    
    return std::to_string(now);
}

std::string OrderGateway::createSignature(const std::string& query_string) {
    unsigned char* digest = HMAC(
        EVP_sha256(),
        config_.api_secret.c_str(), config_.api_secret.length(),
        reinterpret_cast<const unsigned char*>(query_string.c_str()), query_string.length(),
        nullptr, nullptr
    );
    
    char signature[65];
    for (int i = 0; i < 32; i++) {
        sprintf(&signature[i*2], "%02x", digest[i]);
    }
    signature[64] = 0;
    
    return std::string(signature);
}

Json::Value OrderGateway::sendRequest(const std::string& endpoint, const std::string& query_string, bool is_post) {
    Json::Value result;

    try {
        std::lock_guard<std::mutex> lock(curl_mutex_);

        // Set up the request
        std::string url = config_.rest_base_url() + endpoint;
        std::string request_log_url = url; // For logging (will add query string for GET requests)

        if (!is_post && !query_string.empty()) {
            url += "?" + query_string;
            request_log_url = url; // Include query string in log for GET requests
        }

        // Log the request
        logger_.log("%:% %() % Sending % request to %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   (is_post ? "POST" : "GET"), request_log_url.c_str());

        if (is_post) {
            logger_.log("%:% %() % POST data: %\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       query_string.c_str());
        }

        // Reset curl options
        curl_easy_reset(curl_);

        // Set the URL
        curl_easy_setopt(curl_, CURLOPT_URL, url.c_str());

        // Enable verbose mode for more detailed curl debug output
        curl_easy_setopt(curl_, CURLOPT_VERBOSE, 1L);

        // Set up headers
        struct curl_slist *headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
        headers = curl_slist_append(headers, ("X-MBX-APIKEY: " + config_.api_key).c_str());
        curl_easy_setopt(curl_, CURLOPT_HTTPHEADER, headers);

        // Set up timeout
        curl_easy_setopt(curl_, CURLOPT_TIMEOUT, 30L); // 30 seconds timeout

        // Set up POST data if needed
        if (is_post) {
            curl_easy_setopt(curl_, CURLOPT_POST, 1L);
            curl_easy_setopt(curl_, CURLOPT_POSTFIELDS, query_string.c_str());
        }

        // Prepare for response
        std::string response_string;
        curl_easy_setopt(curl_, CURLOPT_WRITEFUNCTION, curlWriteCallback);
        curl_easy_setopt(curl_, CURLOPT_WRITEDATA, &response_string);

        // Start timing the request
        auto start_time = std::chrono::high_resolution_clock::now();

        // Perform the request
        CURLcode res = curl_easy_perform(curl_);

        // End timing
        auto end_time = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time).count();

        // Clean up headers
        curl_slist_free_all(headers);

        // Log request timing
        logger_.log("%:% %() % Request completed in % ms\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), duration);

        if (res != CURLE_OK) {
            logger_.log("%:% %() % curl_easy_perform() failed: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), curl_easy_strerror(res));

            Json::Value error;
            error["curl_error"] = curl_easy_strerror(res);
            return error;
        }

        // Get HTTP status code
        long http_code = 0;
        curl_easy_getinfo(curl_, CURLINFO_RESPONSE_CODE, &http_code);

        // Log response data
        logger_.log("%:% %() % HTTP status: %, Response size: % bytes\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   http_code, response_string.length());

        // For small responses, log the entire response
        if (response_string.length() < 1000) {
            logger_.log("%:% %() % Response: %\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       response_string.c_str());
        } else {
            // For larger responses, log just the beginning
            logger_.log("%:% %() % Response (first 1000 chars): %...\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       response_string.substr(0, 1000).c_str());
        }

        if (http_code >= 400) {
            logger_.log("%:% %() % HTTP error %: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), http_code, response_string);

            // Try to parse the error response as JSON
            Json::CharReaderBuilder builder;
            std::string errors;
            std::istringstream error_stream(response_string);
            Json::Value error_json;

            if (Json::parseFromStream(builder, error_stream, &error_json, &errors)) {
                error_json["http_code"] = static_cast<int>(http_code);
                return error_json;
            } else {
                // If parsing fails, return a basic error
                Json::Value error;
                error["http_code"] = static_cast<int>(http_code);
                error["http_error"] = response_string;
                return error;
            }
        }

        // Parse JSON response
        Json::CharReaderBuilder builder;
        std::string errors;
        std::istringstream response_stream(response_string);

        if (!Json::parseFromStream(builder, response_stream, &result, &errors)) {
            logger_.log("%:% %() % Failed to parse JSON response: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), errors);

            Json::Value error;
            error["json_error"] = "Failed to parse JSON response";
            error["error_details"] = errors;
            error["raw_response"] = response_string;
            return error;
        }

        logger_.log("%:% %() % Successfully parsed JSON response\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_));

        return result;
    } catch (const std::exception& e) {
        // Catch any unexpected exceptions
        logger_.log("%:% %() % EXCEPTION in sendRequest: %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), e.what());

        Json::Value error;
        error["exception"] = e.what();
        return error;
    }
}

Json::Value OrderGateway::sendNewOrder(Common::TickerId ticker_id, Common::Side side, Common::Price price, Common::Qty qty, Common::OrderId order_id) {
    try {
        if (!ticker_id_to_symbol_.count(ticker_id)) {
            logger_.log("%:% %() % Unknown ticker ID: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), ticker_id);
            return Json::Value();
        }

        std::string symbol = ticker_id_to_symbol_[ticker_id];

        // Convert internal price/qty to Binance format
        double binance_price = price / 100.0; // Convert from internal price format (x100)
        double binance_qty = qty / 100.0;     // Convert from internal qty format (x100)

        // Check for valid price and quantity
        if (binance_price <= 0.0) {
            logger_.log("%:% %() % ERROR: Invalid price (%.8f) for order_id=%\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), binance_price, order_id);
            Json::Value error;
            error["error"] = "Invalid price";
            return error;
        }

        if (binance_qty <= 0.0) {
            logger_.log("%:% %() % ERROR: Invalid quantity (%.8f) for order_id=%\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), binance_qty, order_id);
            Json::Value error;
            error["error"] = "Invalid quantity";
            return error;
        }

        logger_.log("%:% %() % Processing order: ticker_id=%, symbol=%, side=%, internal_price=%, binance_price=%, internal_qty=%, binance_qty=%\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   ticker_id, symbol.c_str(),
                   Common::sideToString(side).c_str(),
                   price, binance_price,
                   qty, binance_qty);

        // Check if price passes the percent price filter
        if (!checkPercentPriceFilter(symbol, side, binance_price)) {
            // Price does not meet filter requirements - get current price
            double current_price = getCurrentPrice(symbol);
            if (current_price > 0) {
                // Adjust price to be within allowed range
                // For BUY orders: use 1% below market price
                // For SELL orders: use 1% above market price
                double old_price = binance_price;
                if (side == Common::Side::BUY) {
                    binance_price = current_price * 0.99;
                } else {
                    binance_price = current_price * 1.01;
                }

                logger_.log("%:% %() % Adjusted price to pass filter: old_price=%, new_price=%, current_market_price=%\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           old_price, binance_price, current_price);
            } else {
                logger_.log("%:% %() % ERROR: Price filter check failed and couldn't get current price. Cannot place order.\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                Json::Value error;
                error["error"] = "Price filter check failed";
                return error;
            }
        }

        // Get exchange info to check quantity requirements
        Json::Value symbol_info = getExchangeInfo(symbol);
        if (!symbol_info.isNull() && symbol_info.isMember("filters") && symbol_info["filters"].isArray()) {
            // Check LOT_SIZE filter (min/max quantity and step size)
            for (const auto& filter : symbol_info["filters"]) {
                if (filter.isMember("filterType") && filter["filterType"].asString() == "LOT_SIZE") {
                    double minQty = std::stod(filter["minQty"].asString());
                    double maxQty = std::stod(filter["maxQty"].asString());
                    double stepSize = std::stod(filter["stepSize"].asString());

                    logger_.log("%:% %() % LOT_SIZE filter: minQty=%, maxQty=%, stepSize=%, our_qty=%\n",
                               __FILE__, __LINE__, __FUNCTION__,
                               Common::getCurrentTimeStr(&time_str_),
                               minQty, maxQty, stepSize, binance_qty);

                    if (binance_qty < minQty) {
                        logger_.log("%:% %() % ERROR: Quantity too small. Minimum allowed: %\n",
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_), minQty);
                        Json::Value error;
                        error["error"] = "Quantity too small";
                        error["minQty"] = minQty;
                        return error;
                    }

                    if (binance_qty > maxQty) {
                        logger_.log("%:% %() % ERROR: Quantity too large. Maximum allowed: %\n",
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_), maxQty);
                        Json::Value error;
                        error["error"] = "Quantity too large";
                        error["maxQty"] = maxQty;
                        return error;
                    }

                    // Check step size (Binance requires quantities to be multiples of the step size)
                    if (stepSize > 0) {
                        double remainder = fmod(binance_qty, stepSize);
                        if (remainder > 0.0000001) { // Add small epsilon for floating point comparison
                            double adjusted_qty = floor(binance_qty / stepSize) * stepSize;

                            logger_.log("%:% %() % Adjusting quantity to match step size: old=%, new=%, step=%\n",
                                       __FILE__, __LINE__, __FUNCTION__,
                                       Common::getCurrentTimeStr(&time_str_),
                                       binance_qty, adjusted_qty, stepSize);

                            binance_qty = adjusted_qty;

                            // Check if adjusted quantity is now too small
                            if (binance_qty < minQty) {
                                logger_.log("%:% %() % ERROR: Adjusted quantity too small. Cannot place order.\n",
                                           __FILE__, __LINE__, __FUNCTION__,
                                           Common::getCurrentTimeStr(&time_str_));
                                Json::Value error;
                                error["error"] = "Adjusted quantity too small";
                                return error;
                            }
                        }
                    }
                    break;
                }
            }
        }

        // Create timestamp
        std::string timestamp = generateTimestamp();

        // Prepare query string
        std::ostringstream query_ss;
        query_ss << "symbol=" << symbol
                 << "&side=" << (side == Common::Side::BUY ? "BUY" : "SELL")
                 << "&type=LIMIT"
                 << "&timeInForce=GTC"
                 << "&quantity=" << std::fixed << std::setprecision(8) << binance_qty
                 << "&price=" << std::fixed << std::setprecision(8) << binance_price
                 << "&timestamp=" << timestamp;

        std::string query_string = query_ss.str();

        // Create signature
        std::string signature = createSignature(query_string);
        query_string += "&signature=" + signature;

        // Log the full request being sent
        logger_.log("%:% %() % Sending order request to Binance: %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), query_string.c_str());

        // Send the request
        Json::Value result = sendRequest("/api/v3/order", query_string, true);

        // Log raw response
        logger_.log("%:% %() % Received Binance response: %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), result.toStyledString().c_str());

        if (!result.isNull() && result.isMember("orderId")) {
            std::string binance_order_id = result["orderId"].asString();

            // Store mapping from internal to Binance order ID
            std::lock_guard<std::mutex> lock(order_map_mutex_);
            order_id_to_binance_id_[order_id] = binance_order_id;

            logger_.log("%:% %() % New order placed: % -> Binance ID: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), order_id, binance_order_id);
        } else if (result.isMember("code") && result.isMember("msg")) {
            // Log specific Binance API error
            logger_.log("%:% %() % Binance API error: code=%, msg=%\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       result["code"].asInt(), result["msg"].asString().c_str());
        } else {
            logger_.log("%:% %() % Failed to place order: %\n", __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_), result.toStyledString());
        }

        return result;
    } catch (const std::exception& e) {
        // Catch any unexpected exceptions
        logger_.log("%:% %() % EXCEPTION in sendNewOrder: %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), e.what());

        Json::Value error;
        error["error"] = "Exception";
        error["message"] = e.what();
        return error;
    }
}

Json::Value OrderGateway::cancelOrder(Common::TickerId ticker_id, Common::OrderId order_id, const std::string& binance_order_id) {
    if (!ticker_id_to_symbol_.count(ticker_id)) {
        logger_.log("%:% %() % Unknown ticker ID: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ticker_id);
        return Json::Value();
    }

    std::string symbol = ticker_id_to_symbol_[ticker_id];

    // Create timestamp
    std::string timestamp = generateTimestamp();

    // Prepare query string
    std::ostringstream query_ss;
    query_ss << "symbol=" << symbol
             << "&orderId=" << binance_order_id
             << "&timestamp=" << timestamp;

    std::string query_string = query_ss.str();

    // Create signature
    std::string signature = createSignature(query_string);
    query_string += "&signature=" + signature;

    // Send the request
    Json::Value result = sendRequest("/api/v3/order", query_string, true);

    if (!result.isNull() && result.isMember("orderId")) {
        // Remove mapping from internal to Binance order ID
        std::lock_guard<std::mutex> lock(order_map_mutex_);
        order_id_to_binance_id_.erase(order_id);

        logger_.log("%:% %() % Order canceled: % (Binance ID: %)\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), order_id, binance_order_id);
    } else {
        logger_.log("%:% %() % Failed to cancel order: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), result.toStyledString());
    }

    return result;
}

Json::Value OrderGateway::getOrderStatus(Common::TickerId ticker_id, const std::string& binance_order_id) {
    if (!ticker_id_to_symbol_.count(ticker_id)) {
        logger_.log("%:% %() % Unknown ticker ID: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), ticker_id);
        return Json::Value();
    }

    std::string symbol = ticker_id_to_symbol_[ticker_id];

    // Create timestamp
    std::string timestamp = generateTimestamp();

    // Prepare query string
    std::ostringstream query_ss;
    query_ss << "symbol=" << symbol
             << "&orderId=" << binance_order_id
             << "&timestamp=" << timestamp;

    std::string query_string = query_ss.str();

    // Create signature
    std::string signature = createSignature(query_string);
    query_string += "&signature=" + signature;

    // Send the request
    return sendRequest("/api/v3/order", query_string, false);
}

void OrderGateway::pollOrderStatuses() {
    logger_.log("%:% %() % Starting order status polling thread\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_));

    while (run_) {
        std::map<Common::OrderId, std::string> order_map_copy;

        {
            std::lock_guard<std::mutex> lock(order_map_mutex_);
            order_map_copy = order_id_to_binance_id_;
        }

        for (const auto& [order_id, binance_order_id] : order_map_copy) {
            // Skip if we can't match to a ticker ID
            // This is a simple approach - in production you might want to store ticker_id with order_id
            bool found = false;
            Common::TickerId ticker_id = 0;

            for (const auto& [symbol, id] : symbol_to_ticker_id_) {
                for (const auto& [tid, sym] : ticker_id_to_symbol_) {
                    if (sym == symbol) {
                        ticker_id = tid;
                        found = true;
                        break;
                    }
                }
                if (found) break;
            }

            if (!found) continue;

            Json::Value order_status = getOrderStatus(ticker_id, binance_order_id);
            if (!order_status.isNull()) {
                handleOrderQueryResponse(order_status, order_id, ticker_id);
            }

            // Sleep between requests to avoid rate limiting
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }

        // Sleep before next polling cycle
        std::this_thread::sleep_for(std::chrono::seconds(5));
    }

    logger_.log("%:% %() % Order status polling thread stopped\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_));
}

void OrderGateway::handleOrderQueryResponse(const Json::Value& response, Common::OrderId order_id, Common::TickerId /* ticker_id */) {
    if (!response.isMember("status")) {
        return;
    }

    std::string status = response["status"].asString();
    std::string binance_order_id = response["orderId"].asString();

    Exchange::ClientResponseType type;

    if (status == "FILLED") {
        type = Exchange::ClientResponseType::FILLED;

        // Remove from order map as it's completely filled
        std::lock_guard<std::mutex> lock(order_map_mutex_);
        order_id_to_binance_id_.erase(order_id);

    } else if (status == "PARTIALLY_FILLED") {
        // For partial fills, we can use FILLED with partial quantity
        type = Exchange::ClientResponseType::FILLED;
    } else if (status == "CANCELED") {
        type = Exchange::ClientResponseType::CANCELED;

        // Remove from order map as it's canceled
        std::lock_guard<std::mutex> lock(order_map_mutex_);
        order_id_to_binance_id_.erase(order_id);

    } else if (status == "REJECTED") {
        // For rejected orders, use CANCEL_REJECTED
        type = Exchange::ClientResponseType::CANCEL_REJECTED;

        // Remove from order map as it's rejected
        std::lock_guard<std::mutex> lock(order_map_mutex_);
        order_id_to_binance_id_.erase(order_id);

    } else {
        // Other statuses (NEW, PENDING_CANCEL) - continue tracking
        return;
    }

    createClientResponse(response, nullptr, type);
}

double OrderGateway::getCurrentPrice(const std::string& symbol) {
    std::string url = "/api/v3/ticker/price?symbol=" + symbol;

    Json::Value result = sendRequest(url, "", false);

    if (!result.isNull() && result.isMember("price")) {
        double price = std::stod(result["price"].asString());
        logger_.log("%:% %() % Current price for %: %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol, price);
        return price;
    }

    logger_.log("%:% %() % Failed to get current price for %\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), symbol);
    return 0.0;
}

Json::Value OrderGateway::getExchangeInfo(const std::string& symbol) {
    std::string url = "/api/v3/exchangeInfo?symbol=" + symbol;

    Json::Value result = sendRequest(url, "", false);

    if (!result.isNull() && result.isMember("symbols") && result["symbols"].isArray() && result["symbols"].size() > 0) {
        logger_.log("%:% %() % Retrieved exchange info for %\n", __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol);
        return result["symbols"][0];
    }

    logger_.log("%:% %() % Failed to get exchange info for %\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), symbol);
    return Json::Value();
}

bool OrderGateway::checkPercentPriceFilter(const std::string& symbol, Common::Side side, double price) {
    // Get current market price
    double current_price = getCurrentPrice(symbol);
    if (current_price <= 0) {
        logger_.log("%:% %() % WARNING: Cannot check percent price filter - no current price for %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol);
        return false;
    }

    // Get exchange info to find the filter parameters
    Json::Value symbol_info = getExchangeInfo(symbol);
    if (symbol_info.isNull()) {
        logger_.log("%:% %() % WARNING: Cannot check percent price filter - no exchange info for %\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_), symbol);
        return false;
    }

    // Find the PERCENT_PRICE_BY_SIDE filter
    double bidMultiplierUp = 5.0;
    double bidMultiplierDown = 0.2;
    double askMultiplierUp = 5.0;
    double askMultiplierDown = 0.2;

    if (symbol_info.isMember("filters") && symbol_info["filters"].isArray()) {
        for (const auto& filter : symbol_info["filters"]) {
            if (filter.isMember("filterType") && filter["filterType"].asString() == "PERCENT_PRICE_BY_SIDE") {
                bidMultiplierUp = std::stod(filter["bidMultiplierUp"].asString());
                bidMultiplierDown = std::stod(filter["bidMultiplierDown"].asString());
                askMultiplierUp = std::stod(filter["askMultiplierUp"].asString());
                askMultiplierDown = std::stod(filter["askMultiplierDown"].asString());
                break;
            }
        }
    }

    // Apply the filter based on order side
    if (side == Common::Side::BUY) {
        double min_price = current_price * bidMultiplierDown;
        double max_price = current_price * bidMultiplierUp;

        bool valid = (price >= min_price && price <= max_price);

        logger_.log("%:% %() % BUY order price check: price=%, current=%, range=[% - %], valid=%\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   price, current_price, min_price, max_price, (valid ? "true" : "false"));

        return valid;
    } else {
        double min_price = current_price * askMultiplierDown;
        double max_price = current_price * askMultiplierUp;

        bool valid = (price >= min_price && price <= max_price);

        logger_.log("%:% %() % SELL order price check: price=%, current=%, range=[% - %], valid=%\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   price, current_price, min_price, max_price, (valid ? "true" : "false"));

        return valid;
    }
}

void OrderGateway::processClientRequest(const Exchange::MEClientRequest* request) {
    logger_.log("%:% %() % Processing client request: type=%, ticker_id=%, order_id=%, side=%, price=%, qty=%\n",
               __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_),
               Exchange::clientRequestTypeToString(request->type_),
               request->ticker_id_, request->order_id_,
               Common::sideToString(request->side_),
               Common::priceToString(request->price_),
               Common::qtyToString(request->qty_));

    if (request->type_ == Exchange::ClientRequestType::NEW) {
        // New order request
        if (!ticker_id_to_symbol_.count(request->ticker_id_)) {
            logger_.log("%:% %() % ERROR: Unknown ticker ID: % for order_id=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      request->ticker_id_, request->order_id_);

            Json::Value empty;
            createClientResponse(empty, request, Exchange::ClientResponseType::CANCEL_REJECTED);
            return;
        }

        std::string symbol = ticker_id_to_symbol_[request->ticker_id_];
        double binance_price = request->price_ / 100.0; // Convert from internal price format
        double binance_qty = request->qty_ / 100.0;     // Convert from internal qty format

        logger_.log("%:% %() % Submitting order to Binance: symbol=%, side=%, price=%, qty=%\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   symbol.c_str(),
                   Common::sideToString(request->side_).c_str(),
                   binance_price, binance_qty);

        if (binance_qty <= 0.0) {
            logger_.log("%:% %() % ERROR: Invalid quantity (%.8f) for order_id=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      binance_qty, request->order_id_);

            Json::Value empty;
            createClientResponse(empty, request, Exchange::ClientResponseType::CANCEL_REJECTED);
            return;
        }

        Json::Value response = sendNewOrder(request->ticker_id_, request->side_,
                                           request->price_, request->qty_, request->order_id_);

        if (!response.isNull()) {
            logger_.log("%:% %() % Binance response: %\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       response.toStyledString().c_str());

            if (response.isMember("orderId")) {
                logger_.log("%:% %() % Order successfully placed: order_id=%, binance_order_id=%\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           request->order_id_, response["orderId"].asString().c_str());

                createClientResponse(response, request, Exchange::ClientResponseType::ACCEPTED);
            } else if (response.isMember("code") && response.isMember("msg")) {
                logger_.log("%:% %() % Order rejected by Binance: code=%, message=%\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           response["code"].asInt(), response["msg"].asString().c_str());

                createClientResponse(response, request, Exchange::ClientResponseType::CANCEL_REJECTED);
            } else {
                logger_.log("%:% %() % Invalid or unexpected response from Binance\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));

                createClientResponse(response, request, Exchange::ClientResponseType::CANCEL_REJECTED);
            }
        } else {
            logger_.log("%:% %() % Empty response from Binance for order_id=%\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       request->order_id_);

            createClientResponse(response, request, Exchange::ClientResponseType::CANCEL_REJECTED);
        }
    } else if (request->type_ == Exchange::ClientRequestType::CANCEL) {
        // Cancel order request
        std::string binance_order_id;

        {
            std::lock_guard<std::mutex> lock(order_map_mutex_);
            auto it = order_id_to_binance_id_.find(request->order_id_);

            if (it != order_id_to_binance_id_.end()) {
                binance_order_id = it->second;
                logger_.log("%:% %() % Found Binance order ID % for order_id=%\n",
                          __FILE__, __LINE__, __FUNCTION__,
                          Common::getCurrentTimeStr(&time_str_),
                          binance_order_id.c_str(), request->order_id_);
            } else {
                logger_.log("%:% %() % Cannot find Binance order ID for order_id=%\n",
                          __FILE__, __LINE__, __FUNCTION__,
                          Common::getCurrentTimeStr(&time_str_),
                          request->order_id_);
            }
        }

        if (!binance_order_id.empty()) {
            Json::Value response = cancelOrder(request->ticker_id_, request->order_id_, binance_order_id);

            if (!response.isNull()) {
                logger_.log("%:% %() % Binance cancel response: %\n",
                          __FILE__, __LINE__, __FUNCTION__,
                          Common::getCurrentTimeStr(&time_str_),
                          response.toStyledString().c_str());

                if (response.isMember("orderId")) {
                    createClientResponse(response, request, Exchange::ClientResponseType::CANCELED);
                } else if (response.isMember("code") && response.isMember("msg")) {
                    logger_.log("%:% %() % Cancel rejected by Binance: code=%, message=%\n",
                              __FILE__, __LINE__, __FUNCTION__,
                              Common::getCurrentTimeStr(&time_str_),
                              response["code"].asInt(), response["msg"].asString().c_str());

                    createClientResponse(response, request, Exchange::ClientResponseType::CANCEL_REJECTED);
                } else {
                    // Order may have already been filled or canceled
                    createClientResponse(response, request, Exchange::ClientResponseType::CANCEL_REJECTED);
                }
            } else {
                logger_.log("%:% %() % Empty response from Binance for cancel order_id=%\n",
                          __FILE__, __LINE__, __FUNCTION__,
                          Common::getCurrentTimeStr(&time_str_),
                          request->order_id_);

                createClientResponse(response, request, Exchange::ClientResponseType::CANCEL_REJECTED);
            }
        } else {
            // Order ID not found in our mapping
            logger_.log("%:% %() % Cannot cancel - no Binance order ID for order_id=%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_),
                      request->order_id_);

            Json::Value empty;
            createClientResponse(empty, request, Exchange::ClientResponseType::CANCEL_REJECTED);
        }
    }
}

void OrderGateway::createClientResponse(const Json::Value& response, const Exchange::MEClientRequest* request, Exchange::ClientResponseType type) {
    Exchange::MEClientResponse client_response;

    if (request) {
        // For direct responses to client requests
        client_response.client_id_ = request->client_id_;
        client_response.ticker_id_ = request->ticker_id_;
        client_response.client_order_id_ = request->order_id_;
        client_response.price_ = request->price_;
        client_response.exec_qty_ = 0;                 // Will be set for fills only
        client_response.side_ = request->side_;
    } else {
        // For asynchronous updates (e.g., from order status polling)
        if (!response.isMember("orderId") || !response.isMember("side")) {
            return;  // Insufficient data
        }

        // Extract order details from response
        std::string binance_order_id = response["orderId"].asString();
        std::string symbol = response["symbol"].asString();

        // Find internal order ID and ticker ID
        Common::OrderId order_id = 0;
        Common::TickerId ticker_id = 0;
        bool found_order = false, found_ticker = false;

        // Look up order ID
        for (const auto& [oid, boid] : order_id_to_binance_id_) {
            if (boid == binance_order_id) {
                order_id = oid;
                found_order = true;
                break;
            }
        }

        // Look up ticker ID
        for (const auto& [sym, tid] : symbol_to_ticker_id_) {
            if (sym == symbol) {
                ticker_id = tid;
                found_ticker = true;
                break;
            }
        }

        if (!found_order || !found_ticker) {
            return;  // Can't match to our internal IDs
        }

        client_response.client_id_ = client_id_;
        client_response.ticker_id_ = ticker_id;
        client_response.client_order_id_ = order_id;
        client_response.side_ = response["side"].asString() == "BUY" ? Common::Side::BUY : Common::Side::SELL;
    }

    client_response.type_ = type;

    // For fills, extract executed price and quantity
    if (type == Exchange::ClientResponseType::FILLED) {
        if (response.isMember("executedQty") && response.isMember("price")) {
            double executed_qty = std::stod(response["executedQty"].asString());
            double price = std::stod(response["price"].asString());

            // Convert to internal format
            client_response.exec_qty_ = static_cast<Qty>(executed_qty * 100);
            client_response.price_ = static_cast<Price>(price * 100);
        }
    }

    // Although MEClientResponse doesn't natively have seq_num_, we're tracking it here
    // for our internal OrderGateway implementation
    client_response.seq_num_ = next_outgoing_seq_num_++;

    // Push response to queue (to be read by TradeEngine)
    auto next_write = outgoing_responses_->getNextToWriteTo();
    *next_write = client_response;
    outgoing_responses_->updateWriteIndex();

    logger_.log("%:% %() % Sent client response: type=%, ticker_id=%, order_id=%, seq_num=%\n",
               __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_),
               Exchange::clientResponseTypeToString(client_response.type_),
               client_response.ticker_id_, client_response.client_order_id_, client_response.seq_num_);
}

auto OrderGateway::run() noexcept -> void {
    logger_.log("%:% %() % Starting OrderGateway loop\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_));

    // Debug: Check queue size at start
    logger_.log("%:% %() % DEBUG: Queue size at start: %\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), incoming_requests_->size());

    // Debug: Output queue pointer address indirectly as hexadecimal value
    unsigned long queue_addr = reinterpret_cast<unsigned long>(incoming_requests_);
    logger_.log("%:% %() % DEBUG: Queue address: 0x%\n", __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), queue_addr);

    // No capacity() method available in LFQueue

    // No getReadIndex() or getWriteIndex() methods available in LFQueue

    int empty_count = 0;
    int check_count = 0;
    int processed_count = 0;
    auto last_dump_time = std::chrono::steady_clock::now();

    while (run_) {
        check_count++;
        auto current_time = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(current_time - last_dump_time).count();

        // Dump detailed diagnostics every 5 seconds
        if (elapsed >= 5) {
            logger_.log("%:% %() % DIAGNOSTIC STATUS - Queue size: %, Processed: %, Empty checks: %\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       incoming_requests_->size(), processed_count, empty_count);

            last_dump_time = current_time;
        }

        // Debug: Periodically log queue size
        if (check_count % 100 == 0) {
            logger_.log("%:% %() % DEBUG: Queue check #% - Queue size: %\n",
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       check_count, incoming_requests_->size());
        }

        // Read incoming client requests from the TradeEngine
        auto next_request = incoming_requests_->getNextToRead();

        if (!next_request) {
            // No new requests, count empty checks and sleep briefly
            empty_count++;
            if (empty_count % 1000 == 0) {
                logger_.log("%:% %() % DEBUG: No requests found after % checks, Queue size: %\n",
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           empty_count, incoming_requests_->size());
            }

            std::this_thread::sleep_for(std::chrono::microseconds(200));
            continue;
        }

        // We've got a valid request to process, log it
        logger_.log("%:% %() % Successfully read request from queue, size=%\n",
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   incoming_requests_->size());

        // Reset empty count
        empty_count = 0;
        processed_count++;

        // Log that we've received a request
        logger_.log("%:% %() % Received client request #% from queue: %\n", __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_), processed_count, next_request->toString().c_str());

        // Enhanced logging for queue state
        logger_.log("%:% %() % QUEUE STATE before processing: size=%\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  incoming_requests_->size());

        // Process the client request
        try {
            processClientRequest(next_request);

            // Log successful processing
            logger_.log("%:% %() % Successfully processed request #%\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), processed_count);
        }
        catch (const std::exception& e) {
            // Log any exceptions during processing
            logger_.log("%:% %() % EXCEPTION while processing request #%: %\n",
                      __FILE__, __LINE__, __FUNCTION__,
                      Common::getCurrentTimeStr(&time_str_), processed_count, e.what());
        }

        // Update the read index to mark this request as processed
        incoming_requests_->updateReadIndex();

        // Log queue state after processing
        logger_.log("%:% %() % QUEUE STATE after processing: size=%\n",
                  __FILE__, __LINE__, __FUNCTION__,
                  Common::getCurrentTimeStr(&time_str_),
                  incoming_requests_->size());
    }

    logger_.log("%:% %() % OrderGateway loop stopped - Processed % requests in total\n",
               __FILE__, __LINE__, __FUNCTION__,
               Common::getCurrentTimeStr(&time_str_), processed_count);
}

} // namespace Trading
