#pragma once

#include <string>
#include <chrono>
#include <optional>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include "common/logging.h"
#include "trading/adapters/zerodha/market_data/environment_config.h"
#include "totp.h"

namespace Adapter {
namespace Zerodha {

// Struct to hold authentication session data
struct ZerodhaSession {
    std::string access_token;
    std::chrono::system_clock::time_point expiry_time;
    
    // Serialization to JSON
    nlohmann::json to_json() const {
        nlohmann::json j;
        j["access_token"] = access_token;
        j["expiry"] = std::chrono::system_clock::to_time_t(expiry_time);
        return j;
    }
    
    // Deserialization from JSON
    static std::optional<ZerodhaSession> from_json(const nlohmann::json& j) {
        ZerodhaSession session;
        try {
            session.access_token = j["access_token"].get<std::string>();
            session.expiry_time = std::chrono::system_clock::from_time_t(j["expiry"].get<time_t>());
            return session;
        } catch (const std::exception&) {
            return std::nullopt;
        }
    }
    
    bool is_valid() const {
        return !access_token.empty() && 
               (expiry_time > std::chrono::system_clock::now());
    }
};

class ZerodhaAuthenticator {
public:
    // Constructor accepting credentials
    ZerodhaAuthenticator(
        const std::string& api_key,
        const std::string& api_secret,
        const std::string& user_id,
        const std::string& password,
        const std::string& totp_key,
        Common::Logger* logger,
        const std::string& cache_dir = ".cache"
    );
    
    // Constructor that loads credentials from EnvironmentConfig
    static ZerodhaAuthenticator from_config(Common::Logger* logger, 
                                         const Adapter::Zerodha::EnvironmentConfig& config,
                                         const std::string& cache_dir = ".cache");
    
    // Main method to authenticate and get access token
    std::string authenticate(bool try_auto_login = true);
    
    // Get the cached access token if valid
    std::string get_access_token() const;
    
    // Check if we have a valid token
    bool is_authenticated() const;
    
    // Force refresh of the session
    std::string refresh_session();
    
    // Get login URL for manual authentication if automated fails
    std::string get_login_url() const;
    
    // Get API key
    std::string getApiKey() const { return api_key_; }
    
    // Process request token after manual login
    bool process_request_token(const std::string& request_token);

private:
    // Credentials
    std::string api_key_;
    std::string api_secret_;
    std::string user_id_;
    std::string password_;
    std::string totp_key_;
    
    // Session data
    ZerodhaSession current_session_;
    
    // Logger
    Common::Logger* logger_;
    
    // Cache file path
    std::string token_file_;
    
    // TOTP generator instance
    TOTP totp_generator_;
    
    // Automated login to get request token
    std::string get_request_token();
    
    // Save session to cache file
    void save_session(const std::string& access_token, 
                      std::chrono::system_clock::time_point expiry =
                          std::chrono::system_clock::now() + std::chrono::hours(8));
    
    // Load session from cache file
    bool load_session();
    
    // HTTP request helper methods
    std::string http_post(const std::string& url, const std::string& data);
    std::string http_get(const std::string& url, bool follow_redirects = true);
    
    // Initialize CURL (called in constructor)
    void init_curl();
};

} // namespace Zerodha
} // namespace Adapter
