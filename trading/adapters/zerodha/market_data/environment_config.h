#pragma once

#include <string>
#include <vector>
#include <map>
#include <optional>
#include <set>
#include <chrono>

#include <nlohmann/json.hpp>
#include "common/logging.h"

namespace Adapter {
namespace Zerodha {

/**
 * Strategy type enumeration
 */
enum class StrategyType {
    LIQUIDITY_TAKER,
    MARKET_MAKER
};

/**
 * Trading mode enumeration
 */
enum class TradingMode {
    PAPER,
    LIVE
};

/**
 * Risk configuration structure
 */
struct RiskConfig {
    double max_daily_loss = 25000.0;
    double max_position_value = 1000000.0;
    bool enforce_circuit_limits = true;
    bool enforce_trading_hours = true;
};

/**
 * Strategy parameters structure
 */
struct StrategyConfig {
    StrategyType type = StrategyType::LIQUIDITY_TAKER;
    std::map<std::string, double> parameters;
    
    StrategyType getAlgoType() const { return type; }
};

/**
 * Paper trading simulation configuration
 */
struct PaperTradingConfig {
    double fill_probability = 0.9;
    double min_latency_ms = 0.5;
    double max_latency_ms = 5.0;
    std::string slippage_model = "NORMAL";
};

/**
 * Instrument configuration structure
 */
struct InstrumentConfig {
    std::string symbol;
    std::string exchange;
    int ticker_id = 0;
    bool is_futures = false;
    std::string expiry_date;
    
    // Strategy-specific parameters
    int clip = 1;
    double threshold = 0.5;
    int max_position = 100;
    double max_loss = 10000.0;
};

/**
 * Class to manage environment configuration for Zerodha adapter
 * 
 * This class loads and parses configuration from environment variables
 * and/or JSON configuration files, with support for structured data
 * like symbol maps, instruments, and trading mode selection.
 */
class EnvironmentConfig {
public:
    /**
     * Constructor
     * 
     * @param logger Logger for diagnostic messages
     * @param env_file Optional path to .env file to load
     * @param config_file Optional path to JSON config file to load
     */
    explicit EnvironmentConfig(Common::Logger* logger, 
                             const std::string& env_file = "/home/praveen/om/siriquantum/.env",
                             const std::string& config_file = "");
    
    /**
     * Load configuration from environment and/or config file
     * 
     * @return true if configuration was loaded successfully
     */
    bool load();
    
    /**
     * Load configuration from JSON file
     * 
     * @param config_file Path to JSON config file
     * @return true if configuration was loaded successfully
     */
    bool loadFromJson(const std::string& config_file);
    
    /**
     * Save configuration to JSON file
     * 
     * @param file_path Path to save configuration to
     * @return true if configuration was saved successfully
     */
    bool saveConfiguration(const std::string& file_path) const;
    
    /**
     * Get trading mode
     * 
     * @return Current trading mode
     */
    TradingMode getTradingMode() const;
    
    /**
     * Check if using live trading mode
     * 
     * @return true if in live trading mode
     */
    bool isLiveTrading() const;
    
    /**
     * Check if using paper trading mode
     * 
     * @return true if in paper trading mode
     */
    bool isPaperTrading() const;
    
    /**
     * Get strategy configuration
     * 
     * @return Strategy configuration
     */
    const StrategyConfig& getStrategyConfig() const;
    
    /**
     * Get paper trading configuration
     * 
     * @return Paper trading configuration for simulation
     */
    const PaperTradingConfig& getPaperTradingConfig() const;
    
    /**
     * Get risk configuration
     * 
     * @return Risk parameters
     */
    const RiskConfig& getRiskConfig() const;
    
    /**
     * Get API key
     * 
     * @return API key from configuration
     */
    std::string getApiKey() const;
    
    /**
     * Get API secret
     * 
     * @return API secret from configuration
     */
    std::string getApiSecret() const;
    
    /**
     * Get user ID
     * 
     * @return User ID from configuration
     */
    std::string getUserId() const;
    
    /**
     * Get TOTP secret
     * 
     * @return TOTP secret from configuration
     */
    std::string getTotpSecret() const;
    
    /**
     * Get password
     * 
     * @return Password from configuration
     */
    std::string getPassword() const;
    
    /**
     * Get instruments cache directory
     * 
     * @return Path to cache directory
     */
    std::string getInstrumentsCacheDir() const;
    
    /**
     * Get instruments cache TTL
     * 
     * @return Cache time-to-live in hours
     */
    std::chrono::hours getInstrumentsCacheTTL() const;
    
    /**
     * Get access token path
     * 
     * @return Path to access token file
     */
    std::string getAccessTokenPath() const;
    
    /**
     * Check if futures should be used for indices
     * 
     * @return true if futures should be used for indices
     */
    bool useFuturesForIndices() const;
    
    /**
     * Get default exchange
     * 
     * @return Default exchange string
     */
    std::string getDefaultExchange() const;
    
    /**
     * Get symbol map
     * 
     * @return Map of symbol aliases to actual symbols
     */
    const std::map<std::string, std::string>& getSymbolMap() const;
    
    /**
     * Get list of indices to be treated as futures
     * 
     * @return Set of index names
     */
    const std::set<std::string>& getIndexFutures() const;
    
    /**
     * Get index futures rollover days
     * 
     * @return Days before expiry to roll over
     */
    int getIndexFuturesRolloverDays() const;
    
    /**
     * Get test symbols
     * 
     * @return List of symbols to use for testing
     */
    const std::vector<std::string>& getTestSymbols() const;
    
    /**
     * Get configured instruments
     * 
     * @return List of all configured instruments
     */
    const std::vector<InstrumentConfig>& getInstruments() const;
    
    /**
     * Get spot instruments for trading
     * 
     * @return List of spot instrument configurations
     */
    std::vector<InstrumentConfig> getSpotInstruments() const;
    
    /**
     * Get futures instruments for trading
     * 
     * @return List of futures instrument configurations
     */
    std::vector<InstrumentConfig> getFuturesInstruments() const;
    
    /**
     * Get nearest futures instruments for trading
     * 
     * @return List of nearest futures instrument configurations
     */
    std::vector<InstrumentConfig> getNearestFuturesInstruments() const;
    
    /**
     * Resolves a symbol alias to the actual symbol
     * 
     * @param symbol Symbol or alias
     * @return Resolved symbol
     */
    std::string resolveSymbol(const std::string& symbol) const;
    
    /**
     * Formats a symbol with exchange if not already present
     * 
     * @param symbol Symbol with or without exchange
     * @return Formatted symbol with exchange
     */
    std::string formatSymbol(const std::string& symbol) const;
    
    /**
     * Check if a symbol is an index that should be treated as a future
     * 
     * @param symbol Symbol to check
     * @return true if symbol is an index to be treated as a future
     */
    bool isIndexFuture(const std::string& symbol) const;
    
    /**
     * Get the nearest futures expiry date for a symbol
     * 
     * @param symbol Symbol to get expiry for
     * @return Nearest futures expiry date in YYMMDD format
     */
    std::string getNearestExpiryDate(const std::string& symbol) const;
    
    /**
     * Parse full symbol with exchange, instrument type, and expiry
     * 
     * @param full_symbol Full symbol string (e.g., "NFO:NIFTY23JULFUT")
     * @return InstrumentConfig structure with parsed components
     */
    InstrumentConfig parseFullSymbol(const std::string& full_symbol) const;

private:
    // Helper to get environment variable with fallback
    std::string getEnv(const std::string& name, const std::string& default_value = "") const;
    
    // Helper to get configuration value from JSON file or environment
    template<typename T>
    T getConfigValue(const std::string& json_path, const std::string& env_name, const T& default_value) const;
    
    // Helper to load configuration from .env file into environment variables
    bool loadEnvFile(const std::string& env_file_path);
    
    // Helper to parse trading mode from string
    TradingMode parseTradingMode(const std::string& mode_str) const;
    
    // Helper to parse strategy type from string
    StrategyType parseStrategyType(const std::string& type_str) const;

private:
    Common::Logger* logger_ = nullptr;
    mutable std::string time_str_;  // Mutable to allow const methods to modify it
    std::string env_file_;
    std::string config_file_;
    bool loaded_ = false;
    
    // JSON configuration
    nlohmann::json json_config_;
    bool json_config_loaded_ = false;
    
    // Trading mode configuration
    TradingMode trading_mode_ = TradingMode::PAPER;
    
    // Strategy configuration
    StrategyConfig strategy_config_;
    
    // Paper trading configuration
    PaperTradingConfig paper_trading_config_;
    
    // Risk configuration
    RiskConfig risk_config_;
    
    // API credentials
    std::string api_key_;
    std::string api_secret_;
    std::string user_id_;
    std::string password_;
    std::string totp_secret_;
    
    // Cache configuration
    std::string instruments_cache_dir_;
    std::chrono::hours instruments_cache_ttl_{24};
    std::string access_token_path_;
    
    // Symbol configuration
    bool use_futures_for_indices_ = false;
    std::string default_exchange_ = "NSE";
    std::map<std::string, std::string> symbol_map_;
    std::set<std::string> index_futures_;
    int index_futures_rollover_days_ = 2;
    
    // Instrument configuration
    std::vector<InstrumentConfig> instruments_;
    
    // Spot and futures instruments
    std::vector<std::string> spot_symbols_;
    std::vector<std::string> futures_symbols_;
    
    // Test configuration
    std::vector<std::string> test_symbols_;
};

/**
 * Get configuration value from either:
 * 1. JSON config file if available
 * 2. Environment variable as JSON
 * 3. Plain environment variable (converted to appropriate type)
 * 
 * @param json_path JSON path in config file
 * @param env_name Environment variable name
 * @param default_value Default value if not found
 * @return Parsed configuration value or default
 */
template<typename T>
T EnvironmentConfig::getConfigValue(const std::string& json_path, 
                                  const std::string& env_name,
                                  const T& default_value) const {
    // Try to get from JSON config if available
    if (json_config_loaded_) {
        try {
            // Parse the JSON path (e.g., "zerodha.api_key")
            std::vector<std::string> path_parts;
            std::istringstream path_stream(json_path);
            std::string part;
            
            while (std::getline(path_stream, part, '.')) {
                path_parts.push_back(part);
            }
            
            // Navigate to the specified path in the JSON
            const nlohmann::json* current = &json_config_;
            for (const auto& path_part : path_parts) {
                if (!current->contains(path_part)) {
                    break;
                }
                current = &(*current)[path_part];
            }
            
            // If we found a value, return it
            if (current != &json_config_) {
                return current->get<T>();
            }
        } catch (const std::exception& e) {
            logger_->log("%:% %() % Failed to get % from JSON config: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        json_path.c_str(), e.what());
        }
    }
    
    // Try as JSON environment variable
    std::string env_value = getEnv(env_name);
    if (!env_value.empty()) {
        try {
            return nlohmann::json::parse(env_value).get<T>();
        } catch (const std::exception&) {
            // Not valid JSON, try as plain value if it's a basic type
            if constexpr (std::is_same_v<T, std::string>) {
                return env_value;
            } 
            else if constexpr (std::is_same_v<T, int>) {
                try { return std::stoi(env_value); } catch (...) {}
            }
            else if constexpr (std::is_same_v<T, double>) {
                try { return std::stod(env_value); } catch (...) {}
            }
            else if constexpr (std::is_same_v<T, bool>) {
                return (env_value == "1" || 
                        env_value == "true" || 
                        env_value == "yes" || 
                        env_value == "on");
            }
        }
    }
    
    return default_value;
}

} // namespace Zerodha
} // namespace Adapter