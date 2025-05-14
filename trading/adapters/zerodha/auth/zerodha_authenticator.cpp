#include "zerodha_authenticator.h"

#include <cstdlib>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <algorithm>
#include <regex>
#include <memory>
#include <array>
#include <cmath>

// For HTTP requests
#include <curl/curl.h>

// For SHA256
#include <openssl/evp.h>

// For error handling
#include <stdexcept>

namespace Adapter {
namespace Zerodha {

// Callback function for CURL to write response data
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* s) {
    size_t new_length = size * nmemb;
    try {
        s->append(static_cast<char*>(contents), new_length);
        return new_length;
    } catch(std::bad_alloc& e) {
        return 0;
    }
}

// Constructor with explicit credentials
ZerodhaAuthenticator::ZerodhaAuthenticator(
    const std::string& api_key,
    const std::string& api_secret,
    const std::string& user_id,
    const std::string& password,
    const std::string& totp_key,
    Common::Logger* logger,
    const std::string& cache_dir)
    : api_key_(api_key),
      api_secret_(api_secret),
      user_id_(user_id),
      password_(password),
      totp_key_(totp_key),
      logger_(logger),
      totp_generator_(totp_key)
{
    // Check if token path is specified in environment
    const char* token_path_env = std::getenv("ZKITE_ACCESS_TOKEN_PATH");
    if (token_path_env) {
        token_file_ = token_path_env;
        
        // Create parent directory if it doesn't exist
        std::filesystem::path parent_path = std::filesystem::path(token_file_).parent_path();
        std::filesystem::create_directories(parent_path);
    } else {
        // Use default cache directory if ZKITE_ACCESS_TOKEN_PATH not set
        std::filesystem::path cache_path = std::filesystem::absolute(cache_dir);
        std::filesystem::create_directories(cache_path);
        
        // Set token file path - unique per user
        token_file_ = (cache_path / ("token_" + user_id_ + ".json")).string();
    }
    
    std::string time_str;
    logger_->log("%:% %() % Pure C++ Authenticator: Token will be stored at: %\n", 
               __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), token_file_.c_str());
    
    // Initialize CURL globally (required for thread safety)
    init_curl();
    
    // Try to load existing session
    load_session();
}

// Initialize CURL
void ZerodhaAuthenticator::init_curl() {
    curl_global_init(CURL_GLOBAL_ALL);
}

// Helper method to load environment variables from .env file
// Static factory method to create from EnvironmentConfig
ZerodhaAuthenticator ZerodhaAuthenticator::from_config(Common::Logger* logger, 
                                                    const Adapter::Zerodha::EnvironmentConfig& config,
                                                    const std::string& cache_dir) {
    std::string time_str;
    logger->log("%:% %() % Creating ZerodhaAuthenticator from EnvironmentConfig\n", 
              __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    
    // Get API credentials from EnvironmentConfig
    std::string api_key = config.getApiKey();
    std::string api_secret = config.getApiSecret();
    std::string user_id = config.getUserId();
    
    // Get password from configuration
    std::string password = config.getPassword();
    
    // Get TOTP secret from config
    std::string totp_key = config.getTotpSecret();
    
    // Validate that all required credentials are available
    if (api_key.empty() || api_secret.empty() || user_id.empty() || password.empty() || totp_key.empty()) {
        logger->log("%:% %() % Missing Zerodha credentials in configuration\n", 
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        throw std::runtime_error("Missing Zerodha credentials");
    }
    
    // Create authenticator with the credentials
    return ZerodhaAuthenticator(
        api_key, api_secret, user_id, password, totp_key, logger, cache_dir
    );
}

// Main authentication method - pure C++ implementation
std::string ZerodhaAuthenticator::authenticate(bool try_auto_login) {
    std::string time_str;
    
    // Try to use cached session first
    if (is_authenticated()) {
        logger_->log("%:% %() % Using cached session\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        return current_session_.access_token;
    }
    
    // Skip auto-login attempt if requested
    if (!try_auto_login) {
        logger_->log("%:% %() % Auto-login disabled, skipping automated methods\n", 
                  __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
        return std::string();
    }
    
    // Need to authenticate with pure C++ implementation
    logger_->log("%:% %() % Authenticating with Zerodha (Pure C++ implementation)...\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
                  
    try {
        // Get request token through automated login
        std::string request_token = get_request_token();
        
        // Exchange request token for access token
        bool success = process_request_token(request_token);
        
        if (success) {
            logger_->log("%:% %() % Successfully authenticated via C++ implementation\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
            return current_session_.access_token;
        } else {
            logger_->log("%:% %() % C++ authentication failed\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
            return std::string();
        }
    }
    catch (const std::exception& e) {
        logger_->log("%:% %() % Authentication error: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), e.what());
        return std::string();
    }
}

// Automated login to get request token - using a single CURL session
std::string ZerodhaAuthenticator::get_request_token() {
    std::string time_str;
    logger_->log("%:% %() % Performing automated Zerodha login with persistent session...\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    
    try {
        // Create a temporary cookie file for this session
        char cookie_file[] = "/tmp/zerodha_cookies_XXXXXX";
        int fd = mkstemp(cookie_file);
        close(fd);
        
        logger_->log("%:% %() % Using cookie file: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), cookie_file);
        
        CURL* curl = curl_easy_init();
        if (!curl) {
            throw std::runtime_error("Failed to initialize CURL");
        }
        
        // Initialize headers in outer scope so it can be freed in catch block
        struct curl_slist* headers = NULL;
        
        try {
            // Step 1: Login with user_id and password
            logger_->log("%:% %() % Step 1: Logging in with user_id=%\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), user_id_.c_str());
            
            // Create headers for POST request
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
            
            std::string login_data = "user_id=" + user_id_ + "&password=" + password_;
            std::string login_resp;
            
            curl_easy_setopt(curl, CURLOPT_URL, "https://kite.zerodha.com/api/login");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, login_data.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &login_resp);
            curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file);
            
            CURLcode res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            headers = NULL;
            
            if (res != CURLE_OK) {
                throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
            }
            
            long http_code = 0;
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code >= 400) {
                throw std::runtime_error("Login HTTP error: " + std::to_string(http_code));
            }
            
            logger_->log("%:% %() % Login response: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), login_resp.c_str());
            
            // Parse JSON response
            auto json_resp = nlohmann::json::parse(login_resp);
            if (!json_resp.contains("data") || !json_resp["data"].contains("request_id")) {
                throw std::runtime_error("Request ID not found in login response");
            }
            
            std::string request_id = json_resp["data"]["request_id"];
            logger_->log("%:% %() % Got request_id: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), request_id.c_str());
            
            // Step 2: Submit TOTP
            std::string totp_code = totp_generator_.now();
            logger_->log("%:% %() % Step 2: Submitting TOTP code: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), totp_code.c_str());
            
            // Create headers for POST request again
            headers = NULL;
            headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
            
            std::string twofa_data = "user_id=" + user_id_ + 
                                    "&request_id=" + request_id +
                                    "&twofa_value=" + totp_code;
            std::string twofa_resp;
            
            curl_easy_setopt(curl, CURLOPT_URL, "https://kite.zerodha.com/api/twofa");
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, twofa_data.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &twofa_resp);
            curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file);
            curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file);
            
            res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            headers = NULL;
            
            if (res != CURLE_OK) {
                throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
            }
            
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code >= 400) {
                throw std::runtime_error("2FA HTTP error: " + std::to_string(http_code));
            }
            
            logger_->log("%:% %() % 2FA response: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), twofa_resp.c_str());
            
            // Step 3: Get request token by making a GET request to the redirect URL
            std::string redirect_url = "https://kite.zerodha.com/connect/login?v=3&api_key=" + api_key_;
            logger_->log("%:% %() % Step 3: Making GET request to: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), redirect_url.c_str());
            
            // We need to completely clear the session and start fresh for this step
            // Close the current curl handle
            curl_easy_cleanup(curl);
            
            // Create a completely new curl session
            curl = curl_easy_init();
            if (!curl) {
                throw std::runtime_error("Failed to initialize CURL for redirect step");
            }
            
            // Create headers for GET request
            headers = NULL;
            headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
            
            // Exact same pattern as the Python code
            curl_easy_setopt(curl, CURLOPT_URL, redirect_url.c_str());
            curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
            curl_easy_setopt(curl, CURLOPT_COOKIEFILE, cookie_file);
            curl_easy_setopt(curl, CURLOPT_COOKIEJAR, cookie_file);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);       // Critical: FOLLOW redirects
            curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);          // Allow up to 10 redirects
            curl_easy_setopt(curl, CURLOPT_AUTOREFERER, 1L);         // Set Referer on redirect
            curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);              // Get content, not just headers
            
            std::string full_response;
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &full_response);
            
            res = curl_easy_perform(curl);
            curl_slist_free_all(headers);
            headers = NULL;
            
            if (res != CURLE_OK && res != CURLE_WRITE_ERROR) {
                throw std::runtime_error(std::string("CURL error: ") + curl_easy_strerror(res));
            }
            
            // Get the effective URL (the final URL after all redirects)
            char* effective_url = nullptr;
            curl_easy_getinfo(curl, CURLINFO_EFFECTIVE_URL, &effective_url);
            std::string final_url = effective_url ? effective_url : "";
            
            // Get response code
            curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
            
            logger_->log("%:% %() % Response code: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), http_code);
            
            logger_->log("%:% %() % Final URL after redirects: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), final_url.c_str());
            
            // Clean up curl before we exit
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            
            // Directly mimic Python implementation - check if the URL contains request_token=
            if (final_url.find("request_token=") == std::string::npos) {
                logger_->log("%:% %() % Request token not found in URL: %\n", 
                          __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), final_url.c_str());
                std::remove(cookie_file);
                throw std::runtime_error("Request token not found in redirect URL");
            }
            
            // Extract the token exactly as Python does: split by request_token= and then by &
            std::string token = final_url.substr(final_url.find("request_token=") + 14);
            size_t ampersand_pos = token.find("&");
            if (ampersand_pos != std::string::npos) {
                token = token.substr(0, ampersand_pos);
            }
            
            logger_->log("%:% %() % Request token retrieved: %\n", 
                      __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), token.c_str());
            
            // Clean up
            std::remove(cookie_file);
            
            return token;
        }
        catch (const std::exception& e) {
            // Clean up on error
            if (headers) curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            std::remove(cookie_file);
            throw;
        }
    }
    catch (const std::exception& e) {
        logger_->log("%:% %() % Failed to fetch request_token: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), e.what());
        throw;
    }
}

// Process request token to get access token
bool ZerodhaAuthenticator::process_request_token(const std::string& request_token) {
    std::string time_str;
    logger_->log("%:% %() % Processing request token\n", 
                __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
    
    // Generate checksum (SHA256 hash of api_key + request_token + api_secret)
    std::string data_to_hash = api_key_ + request_token + api_secret_;
    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr);
    EVP_DigestUpdate(ctx, data_to_hash.c_str(), data_to_hash.length());
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);
    
    // Convert hash to hex string
    std::stringstream ss;
    for (unsigned int i = 0; i < hash_len; i++) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    std::string checksum = ss.str();
    
    // Prepare post data
    std::string post_data = "api_key=" + api_key_ + 
                           "&request_token=" + request_token +
                           "&checksum=" + checksum;
    
    // Make request to get access token
    try {
        std::string response = http_post("https://api.kite.trade/session/token", post_data);
        auto json_resp = nlohmann::json::parse(response);
        
        if (json_resp["status"] == "success" && json_resp.contains("data") && 
            json_resp["data"].contains("access_token")) {
            
            std::string access_token = json_resp["data"]["access_token"];
            
            // Save the token (valid for 8 hours by default)
            save_session(access_token);
            
            logger_->log("%:% %() % Successfully obtained access token\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
            return true;
        }
        else {
            std::string error = json_resp.contains("message") ? 
                               json_resp["message"].get<std::string>() : "Unknown error";
            
            logger_->log("%:% %() % Failed to obtain access token: %\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), error.c_str());
            return false;
        }
    }
    catch (const std::exception& e) {
        logger_->log("%:% %() % Error processing request token: %\n", 
                   __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), e.what());
        return false;
    }
}

// Save session to cache file
void ZerodhaAuthenticator::save_session(
    const std::string& access_token, 
    std::chrono::system_clock::time_point expiry) {
    
    current_session_.access_token = access_token;
    current_session_.expiry_time = expiry;
    
    // Serialize to JSON
    nlohmann::json j = current_session_.to_json();
    
    // Write to file
    std::ofstream file(token_file_);
    if (file.is_open()) {
        file << j.dump(4);  // Pretty print with 4-space indent
        file.close();
        
        std::string time_str;
        logger_->log("%:% %() % Session saved to cache: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), token_file_.c_str());
    }
}

// Load session from cache file
bool ZerodhaAuthenticator::load_session() {
    // Check if cache file exists
    if (!std::filesystem::exists(token_file_)) {
        return false;
    }
    
    try {
        // Read file content
        std::ifstream file(token_file_);
        if (!file.is_open()) {
            return false;
        }
        
        nlohmann::json j;
        file >> j;
        file.close();
        
        // Parse session
        auto session_opt = ZerodhaSession::from_json(j);
        if (!session_opt.has_value()) {
            return false;
        }
        
        // Check if session is still valid (with 5-minute buffer)
        auto session = session_opt.value();
        if (session.is_valid()) {
            current_session_ = session;
            
            std::string time_str;
            logger_->log("%:% %() % Loaded existing session from cache\n", 
                        __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str));
            return true;
        }
    }
    catch (const std::exception& e) {
        std::string time_str;
        logger_->log("%:% %() % Failed to load cached session: %\n", 
                    __FILE__, __LINE__, __FUNCTION__, Common::getCurrentTimeStr(&time_str), e.what());
    }
    
    return false;
}

// Get access token
std::string ZerodhaAuthenticator::get_access_token() const {
    return current_session_.access_token;
}

// Check if authenticated
bool ZerodhaAuthenticator::is_authenticated() const {
    return current_session_.is_valid();
}

// Force refresh session
std::string ZerodhaAuthenticator::refresh_session() {
    // Clear current session
    current_session_ = ZerodhaSession();
    
    // Re-authenticate
    return authenticate();
}

// Get login URL for manual authentication
std::string ZerodhaAuthenticator::get_login_url() const {
    return "https://kite.zerodha.com/connect/login?v=3&api_key=" + api_key_;
}


// HTTP POST request helper
std::string ZerodhaAuthenticator::http_post(const std::string& url, const std::string& data) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }
    
    std::string response;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, data.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "");
    
    // Set appropriate headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/x-www-form-urlencoded");
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up
    curl_slist_free_all(headers);
    
    if (res != CURLE_OK) {
        std::string error = curl_easy_strerror(res);
        curl_easy_cleanup(curl);
        throw std::runtime_error("CURL error: " + error);
    }
    
    // Get HTTP status code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    // Check for error status
    if (http_code >= 400) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("HTTP error: " + std::to_string(http_code));
    }
    
    curl_easy_cleanup(curl);
    return response;
}

// HTTP GET request helper
std::string ZerodhaAuthenticator::http_get(const std::string& url, bool follow_redirects) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize CURL");
    }
    
    std::string response;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Handle redirects
    if (follow_redirects) {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    } else {
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    }
    
    // Share cookies with other requests
    curl_easy_setopt(curl, CURLOPT_COOKIEFILE, "");
    curl_easy_setopt(curl, CURLOPT_COOKIEJAR, "");
    
    // Set appropriate headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/91.0.4472.124 Safari/537.36");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // For redirect tracking
    char* redirect_url = NULL;
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Check for CURL errors
    if (res != CURLE_OK) {
        std::string error = curl_easy_strerror(res);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error("CURL error: " + error);
    }
    
    // Get HTTP status code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    // Handle redirect if needed but not followed
    if ((http_code == 301 || http_code == 302 || http_code == 307) && !follow_redirects) {
        curl_easy_getinfo(curl, CURLINFO_REDIRECT_URL, &redirect_url);
        if (redirect_url) {
            response = redirect_url;
        }
    }
    
    // Check for error status
    if (http_code >= 400) {
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        throw std::runtime_error("HTTP error: " + std::to_string(http_code));
    }
    
    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return follow_redirects ? response : redirect_url ? std::string(redirect_url) : std::string();
}

} // namespace Zerodha
} // namespace Adapter
