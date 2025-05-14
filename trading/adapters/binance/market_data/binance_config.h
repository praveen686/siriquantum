#pragma once

#include <string>
#include <algorithm>

namespace Trading {

struct BinanceConfig {
    std::string api_key;
    std::string api_secret;
    bool use_testnet = false;
    
    // API endpoints
    std::string rest_base_url() const {
        return use_testnet ? "https://testnet.binance.vision" : "https://api.binance.com";
    }
    
    std::string ws_host() const {
        return use_testnet ? "stream.testnet.binance.vision" : "stream.binance.com";
    }
    
    std::string ws_port() const {
        return use_testnet ? "443" : "9443";
    }
    
    std::string ws_target(const std::string& symbol, const std::string& stream_type) const {
        std::string lower_symbol = symbol;
        std::transform(lower_symbol.begin(), lower_symbol.end(), lower_symbol.begin(), ::tolower);
        return "/ws/" + lower_symbol + "@" + stream_type;
    }
};

} // namespace Trading