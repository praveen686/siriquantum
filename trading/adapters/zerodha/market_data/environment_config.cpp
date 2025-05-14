#include "environment_config.h"
#include "trading/adapters/zerodha/auth/zerodha_authenticator.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <regex>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <fstream>

namespace Adapter {
namespace Zerodha {

// Helpers for JSON serialization and deserialization
void to_json(nlohmann::json& j, const InstrumentConfig& c) {
    j = nlohmann::json{
        {"symbol", c.symbol},
        {"exchange", c.exchange},
        {"ticker_id", c.ticker_id},
        {"is_futures", c.is_futures},
        {"expiry_date", c.expiry_date},
        {"clip", c.clip},
        {"threshold", c.threshold},
        {"max_position", c.max_position},
        {"max_loss", c.max_loss}
    };
}

void from_json(const nlohmann::json& j, InstrumentConfig& c) {
    c.symbol = j.at("symbol").get<std::string>();
    c.exchange = j.at("exchange").get<std::string>();
    c.ticker_id = j.value("ticker_id", 0);
    c.is_futures = j.value("is_futures", false);
    c.expiry_date = j.value("expiry_date", "");
    c.clip = j.value("clip", 1);
    c.threshold = j.value("threshold", 0.5);
    c.max_position = j.value("max_position", 100);
    c.max_loss = j.value("max_loss", 10000.0);
}

void to_json(nlohmann::json& j, const PaperTradingConfig& c) {
    j = nlohmann::json{
        {"fill_probability", c.fill_probability},
        {"min_latency_ms", c.min_latency_ms},
        {"max_latency_ms", c.max_latency_ms},
        {"slippage_model", c.slippage_model}
    };
}

void from_json(const nlohmann::json& j, PaperTradingConfig& c) {
    c.fill_probability = j.value("fill_probability", 0.9);
    c.min_latency_ms = j.value("min_latency_ms", 0.5);
    c.max_latency_ms = j.value("max_latency_ms", 5.0);
    c.slippage_model = j.value("slippage_model", "NORMAL");
}

void to_json(nlohmann::json& j, const RiskConfig& c) {
    j = nlohmann::json{
        {"max_daily_loss", c.max_daily_loss},
        {"max_position_value", c.max_position_value},
        {"enforce_circuit_limits", c.enforce_circuit_limits},
        {"enforce_trading_hours", c.enforce_trading_hours}
    };
}

void from_json(const nlohmann::json& j, RiskConfig& c) {
    c.max_daily_loss = j.value("max_daily_loss", 25000.0);
    c.max_position_value = j.value("max_position_value", 1000000.0);
    c.enforce_circuit_limits = j.value("enforce_circuit_limits", true);
    c.enforce_trading_hours = j.value("enforce_trading_hours", true);
}

void to_json(nlohmann::json& j, const StrategyConfig& c) {
    j = nlohmann::json{
        {"type", c.type == StrategyType::LIQUIDITY_TAKER ? "LIQUIDITY_TAKER" : "MARKET_MAKER"},
        {"parameters", c.parameters}
    };
}

void from_json(const nlohmann::json& j, StrategyConfig& c) {
    std::string type_str = j.value("type", "LIQUIDITY_TAKER");
    c.type = (type_str == "MARKET_MAKER") ? StrategyType::MARKET_MAKER : StrategyType::LIQUIDITY_TAKER;
    
    if (j.contains("parameters") && j["parameters"].is_object()) {
        c.parameters = j["parameters"].get<std::map<std::string, double>>();
    }
}

EnvironmentConfig::EnvironmentConfig(Common::Logger* logger, 
                                   const std::string& env_file,
                                   const std::string& config_file)
    : logger_(logger), env_file_(env_file), config_file_(config_file) {
    
    logger_->log("%:% %() % Initializing EnvironmentConfig with env file: %, config file: %\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                env_file.c_str(), config_file.empty() ? "none" : config_file.c_str());
}

bool EnvironmentConfig::loadEnvFile(const std::string& env_file_path) {
    std::ifstream file(env_file_path);
    if (!file.is_open()) {
        return false;
    }
    
    std::regex env_regex("^\\s*([^\\s=#]+)\\s*=\\s*(.*)\\s*$");
    std::string line;
    
    logger_->log("%:% %() % Loading environment from file: %\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                env_file_path.c_str());
    
    while (std::getline(file, line)) {
        std::smatch match;
        
        // Skip comments and empty lines
        if (line.empty() || line[0] == '#') {
            continue;
        }
        
        // Parse environment variable
        if (std::regex_match(line, match, env_regex) && match.size() > 2) {
            std::string key = match[1].str();
            std::string value = match[2].str();
            
            // Remove quotes if present
            if (value.size() >= 2 && 
                ((value.front() == '"' && value.back() == '"') || 
                 (value.front() == '\'' && value.back() == '\''))) {
                value = value.substr(1, value.size() - 2);
            }
            
            // Set the environment variable
            setenv(key.c_str(), value.c_str(), 1);
        }
    }
    
    return true;
}

bool EnvironmentConfig::load() {
    // First try to load from JSON config file if specified
    if (!config_file_.empty()) {
        if (loadFromJson(config_file_)) {
            logger_->log("%:% %() % Successfully loaded configuration from JSON file: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        config_file_.c_str());
            json_config_loaded_ = true;
            
            // We might still load environment variables as fallback
            if (!env_file_.empty()) {
                // Load environment variables as fallback
                loadEnvFile(env_file_);
            }
            
            // Continue with loading to allow fallback to environment variables
        } else {
            logger_->log("%:% %() % Failed to load configuration from JSON file: %, falling back to environment\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        config_file_.c_str());
        }
    }
    
    // Load environment variables from .env file if specified
    if (!env_file_.empty() && !json_config_loaded_) {
        if (!loadEnvFile(env_file_)) {
            logger_->log("%:% %() % Failed to load environment from file: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        env_file_.c_str());
            // Continue anyway, variables might be set in the environment
        } else {
            logger_->log("%:% %() % Loaded environment from file: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        env_file_.c_str());
        }
    }
    
    // Load core configuration values using unified approach
    
    // Trading mode
    if (!json_config_loaded_ || !json_config_.contains("trading_mode")) {
        std::string trading_mode_str = getConfigValue<std::string>("trading_mode", "ZKITE_TRADING_MODE", "PAPER");
        trading_mode_ = parseTradingMode(trading_mode_str);
    }
    
    // Strategy configuration
    if (!json_config_loaded_ || !json_config_.contains("strategy")) {
        std::string strategy_type_str = getConfigValue<std::string>("strategy.type", "ZKITE_STRATEGY_TYPE", "LIQUIDITY_TAKER");
        strategy_config_.type = parseStrategyType(strategy_type_str);
        
        auto strategy_params = getConfigValue<std::map<std::string, double>>(
            "strategy.parameters", "ZKITE_STRATEGY_PARAMS", std::map<std::string, double>());
        if (!strategy_params.empty()) {
            strategy_config_.parameters = std::move(strategy_params);
        }
    }
    
    // API credentials - Try from JSON config first, then environment variables
    if (api_key_.empty()) {
        api_key_ = getConfigValue<std::string>("zerodha.api_key", "ZKITE_API_KEY", "");
    }
    
    if (api_secret_.empty()) {
        api_secret_ = getConfigValue<std::string>("zerodha.api_secret", "ZKITE_API_SECRET", "");
    }
    
    if (user_id_.empty()) {
        user_id_ = getConfigValue<std::string>("zerodha.user_id", "ZKITE_USER_ID", "");
    }
    
    if (totp_secret_.empty()) {
        totp_secret_ = getConfigValue<std::string>("zerodha.totp_secret", "ZKITE_TOTP_SECRET", "");
    }
    
    if (password_.empty()) {
        password_ = getConfigValue<std::string>("zerodha.password", "ZKITE_PWD", "");
    }
    
    // Verify we have required credentials
    if (api_key_.empty() || api_secret_.empty()) {
        logger_->log("%:% %() % Required API credentials not found in config or environment\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    // Cache configuration
    instruments_cache_dir_ = getConfigValue<std::string>("zerodha.cache.instruments_dir", 
                                                      "ZKITE_INSTRUMENTS_CACHE_DIR", ".cache/zerodha");
    
    int cache_ttl_hours = getConfigValue<int>("zerodha.cache.ttl_hours", "ZKITE_INSTRUMENTS_CACHE_TTL", 24);
    instruments_cache_ttl_ = std::chrono::hours(cache_ttl_hours);
    
    access_token_path_ = getConfigValue<std::string>("zerodha.cache.access_token_path", 
                                                  "ZKITE_ACCESS_TOKEN_PATH", "");
    
    // Create cache directory if it doesn't exist
    std::filesystem::create_directories(instruments_cache_dir_);
    
    // Symbol configuration
    use_futures_for_indices_ = getConfigValue<bool>("zerodha.use_futures_for_indices", 
                                                 "ZKITE_USE_FUTURES_FOR_INDICES", false);
    
    default_exchange_ = getConfigValue<std::string>("zerodha.default_exchange", 
                                                 "ZKITE_DEFAULT_EXCHANGE", "NSE");
    
    // Symbol map
    auto symbol_map = getConfigValue<std::map<std::string, std::string>>(
        "zerodha.symbol_map", "ZKITE_SYMBOL_MAP", std::map<std::string, std::string>());
    if (!symbol_map.empty()) {
        symbol_map_ = std::move(symbol_map);
    }
    
    // Index futures
    auto index_futures_vec = getConfigValue<std::vector<std::string>>(
        "zerodha.index_futures", "ZKITE_INDEX_FUTURES", std::vector<std::string>{"NIFTY", "BANKNIFTY", "FINNIFTY"});
    
    index_futures_ = std::set<std::string>(index_futures_vec.begin(), index_futures_vec.end());
    
    // Index futures rollover days
    index_futures_rollover_days_ = getConfigValue<int>("zerodha.index_futures_rollover_days", 
                                                   "ZKITE_INDEX_FUTURES_ROLLOVER_DAYS", 2);
    
    // Spot and futures symbols
    spot_symbols_ = getConfigValue<std::vector<std::string>>("zerodha.spot_symbols", 
                                                         "ZKITE_SPOT_SYMBOLS", std::vector<std::string>{});
    
    futures_symbols_ = getConfigValue<std::vector<std::string>>("zerodha.futures_symbols", 
                                                            "ZKITE_FUTURES_SYMBOLS", std::vector<std::string>{});
    
    // Test symbols
    test_symbols_ = getConfigValue<std::vector<std::string>>("zerodha.test_symbols", "ZKITE_TEST_SYMBOLS", 
                                                          std::vector<std::string>{"NSE:RELIANCE", "NSE:SBIN", 
                                                                                 "NSE:INFY", "NSE:TCS", "NSE:HDFCBANK"});
    
    // Instruments collection - initialize if not already done by JSON config
    if (instruments_.empty() && (!json_config_loaded_ || !json_config_.contains("instruments"))) {
        // Initialize instruments from spot and futures
        auto spot_instruments = getSpotInstruments();
        auto futures_instruments = getFuturesInstruments();
        
        instruments_.clear();
        instruments_.reserve(spot_instruments.size() + futures_instruments.size());
        
        for (const auto& instrument : spot_instruments) {
            instruments_.push_back(instrument);
        }
        
        for (const auto& instrument : futures_instruments) {
            instruments_.push_back(instrument);
        }
    }
    
    logger_->log("%:% %() % Loaded configuration: % instruments, % symbol mappings, % index futures\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                instruments_.size(), symbol_map_.size(), index_futures_.size());
    
    logger_->log("%:% %() % Trading mode: %\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                trading_mode_ == TradingMode::PAPER ? "PAPER" : "LIVE");
    
    loaded_ = true;
    return true;
}

bool EnvironmentConfig::loadFromJson(const std::string& config_file) {
    try {
        // Open and read the JSON config file
        std::ifstream file(config_file);
        if (!file.is_open()) {
            logger_->log("%:% %() % Failed to open config file: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        config_file.c_str());
            return false;
        }
        
        // Parse the JSON
        file >> json_config_;
        
        // Display summary of loaded configuration
        logger_->log("%:% %() % Loaded JSON configuration with % sections\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    json_config_.size());
        
        for (auto it = json_config_.begin(); it != json_config_.end(); ++it) {
            logger_->log("%:% %() % - Found config section: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        it.key().c_str());
        }
        
        // Process core configuration sections
        
        // Parse trading system configuration
        if (json_config_.contains("trading_system")) {
            auto& trading_system = json_config_["trading_system"];
            
            // Parse trading mode
            if (trading_system.contains("trading_mode")) {
                trading_mode_ = parseTradingMode(trading_system["trading_mode"].get<std::string>());
            }
            
            // Parse strategy configuration
            if (trading_system.contains("strategy")) {
                from_json(trading_system["strategy"], strategy_config_);
            }
            
            // Parse paper trading configuration
            if (trading_system.contains("paper_trading")) {
                from_json(trading_system["paper_trading"], paper_trading_config_);
            }
        }
        
        // Parse risk configuration
        if (json_config_.contains("risk")) {
            from_json(json_config_["risk"], risk_config_);
        }
        
        // Parse exchange-specific configuration
        if (json_config_.contains("exchanges") && json_config_["exchanges"].contains("ZERODHA")) {
            auto& zerodha = json_config_["exchanges"]["ZERODHA"];
            
            // API credentials
            if (zerodha.contains("api_credentials")) {
                auto& credentials = zerodha["api_credentials"];
                
                if (credentials.contains("api_key")) {
                    api_key_ = credentials["api_key"].get<std::string>();
                }
                
                if (credentials.contains("api_secret")) {
                    api_secret_ = credentials["api_secret"].get<std::string>();
                }
                
                if (credentials.contains("user_id")) {
                    user_id_ = credentials["user_id"].get<std::string>();
                }
                
                if (credentials.contains("totp_secret")) {
                    totp_secret_ = credentials["totp_secret"].get<std::string>();
                }
                
                if (credentials.contains("password")) {
                    password_ = credentials["password"].get<std::string>();
                }
            }
            
            // Paper trading simulation config
            if (zerodha.contains("paper_trading")) {
                from_json(zerodha["paper_trading"], paper_trading_config_);
            }
        }
        
        // Parse instruments
        if (json_config_.contains("instruments")) {
            instruments_.clear();
            
            for (const auto& instrument_json : json_config_["instruments"]) {
                InstrumentConfig instrument;
                from_json(instrument_json, instrument);
                instruments_.push_back(instrument);
            }
            
            // Assign ticker IDs if not already assigned
            int next_id = 1001; // Starting ID for auto-assignment
            for (auto& instrument : instruments_) {
                if (instrument.ticker_id == 0) {
                    instrument.ticker_id = next_id++;
                }
            }
        }
        
        return true;
    } catch (const std::exception& e) {
        logger_->log("%:% %() % Failed to parse JSON config file: %: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    config_file.c_str(), e.what());
        return false;
    }
}

bool EnvironmentConfig::saveConfiguration(const std::string& file_path) const {
    try {
        // Create a new JSON object with the current configuration
        nlohmann::json config;
        
        // If we have a loaded JSON config, start with that as a base
        if (json_config_loaded_) {
            config = json_config_;
        }
        
        // Add/update trading mode
        config["trading_mode"] = (trading_mode_ == TradingMode::PAPER) ? "PAPER" : "LIVE";
        
        // Add/update strategy configuration
        nlohmann::json strategy;
        to_json(strategy, strategy_config_);
        config["strategy"] = strategy;
        
        // Add/update paper trading configuration
        nlohmann::json paper_trading;
        to_json(paper_trading, paper_trading_config_);
        config["paper_trading"] = paper_trading;
        
        // Add/update risk configuration
        nlohmann::json risk;
        to_json(risk, risk_config_);
        config["risk"] = risk;
        
        // Add/update Zerodha-specific configuration
        nlohmann::json zerodha;
        
        // Don't include API credentials in saved config for security reasons
        // We'll reload these from environment variables
        
        // Add paper trading simulation config
        nlohmann::json paper_trading_zerodha;
        to_json(paper_trading_zerodha, paper_trading_config_);
        zerodha["paper_trading"] = paper_trading_zerodha;
        
        config["zerodha"] = zerodha;
        
        // Add/update instruments
        nlohmann::json instruments_json = nlohmann::json::array();
        for (const auto& instrument : instruments_) {
            nlohmann::json instrument_json;
            to_json(instrument_json, instrument);
            instruments_json.push_back(instrument_json);
        }
        config["instruments"] = instruments_json;
        
        // Write to file with pretty formatting
        std::ofstream file(file_path);
        if (!file.is_open()) {
            logger_->log("%:% %() % Failed to open file for writing: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        file_path.c_str());
            return false;
        }
        
        file << config.dump(4); // 4-space indentation
        
        logger_->log("%:% %() % Configuration saved to: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    file_path.c_str());
        
        return true;
    } catch (const std::exception& e) {
        logger_->log("%:% %() % Failed to save configuration to file: %: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    file_path.c_str(), e.what());
        return false;
    }
}

TradingMode EnvironmentConfig::getTradingMode() const {
    return trading_mode_;
}

bool EnvironmentConfig::isLiveTrading() const {
    return trading_mode_ == TradingMode::LIVE;
}

bool EnvironmentConfig::isPaperTrading() const {
    return trading_mode_ == TradingMode::PAPER;
}

const StrategyConfig& EnvironmentConfig::getStrategyConfig() const {
    return strategy_config_;
}

const PaperTradingConfig& EnvironmentConfig::getPaperTradingConfig() const {
    return paper_trading_config_;
}

const RiskConfig& EnvironmentConfig::getRiskConfig() const {
    return risk_config_;
}

std::string EnvironmentConfig::getApiKey() const {
    return api_key_;
}

std::string EnvironmentConfig::getApiSecret() const {
    return api_secret_;
}

std::string EnvironmentConfig::getUserId() const {
    return user_id_;
}

std::string EnvironmentConfig::getTotpSecret() const {
    return totp_secret_;
}

std::string EnvironmentConfig::getPassword() const {
    return password_;
}

std::string EnvironmentConfig::getInstrumentsCacheDir() const {
    return instruments_cache_dir_;
}

std::chrono::hours EnvironmentConfig::getInstrumentsCacheTTL() const {
    return instruments_cache_ttl_;
}

std::string EnvironmentConfig::getAccessTokenPath() const {
    return access_token_path_;
}

bool EnvironmentConfig::useFuturesForIndices() const {
    return use_futures_for_indices_;
}

std::string EnvironmentConfig::getDefaultExchange() const {
    return default_exchange_;
}

const std::map<std::string, std::string>& EnvironmentConfig::getSymbolMap() const {
    return symbol_map_;
}

const std::set<std::string>& EnvironmentConfig::getIndexFutures() const {
    return index_futures_;
}

int EnvironmentConfig::getIndexFuturesRolloverDays() const {
    return index_futures_rollover_days_;
}

const std::vector<std::string>& EnvironmentConfig::getTestSymbols() const {
    return test_symbols_;
}

const std::vector<InstrumentConfig>& EnvironmentConfig::getInstruments() const {
    return instruments_;
}

TradingMode EnvironmentConfig::parseTradingMode(const std::string& mode_str) const {
    std::string upper_mode = mode_str;
    std::transform(upper_mode.begin(), upper_mode.end(), upper_mode.begin(),
                  [](unsigned char c) { return std::toupper(c); });
                  
    if (upper_mode == "LIVE") {
        return TradingMode::LIVE;
    } else {
        return TradingMode::PAPER; // Default to paper trading for safety
    }
}

StrategyType EnvironmentConfig::parseStrategyType(const std::string& type_str) const {
    std::string upper_type = type_str;
    std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(),
                  [](unsigned char c) { return std::toupper(c); });
                  
    if (upper_type == "MARKET_MAKER") {
        return StrategyType::MARKET_MAKER;
    } else {
        return StrategyType::LIQUIDITY_TAKER; // Default to liquidity taker
    }
}

std::vector<InstrumentConfig> EnvironmentConfig::getSpotInstruments() const {
    std::vector<InstrumentConfig> spot_instruments;
    
    // Filter instruments that are not futures
    for (const auto& instrument : instruments_) {
        if (!instrument.is_futures) {
            spot_instruments.push_back(instrument);
        }
    }
    
    // If no instruments are configured, fall back to the old implementation
    if (spot_instruments.empty()) {
        // Use spot symbols from config, or fall back to test symbols if not specified
        const auto& symbols = spot_symbols_.empty() ? test_symbols_ : spot_symbols_;
        
        for (const auto& symbol : symbols) {
            // Format and resolve symbol
            std::string formatted_symbol = formatSymbol(symbol);
            std::string resolved_symbol = resolveSymbol(formatted_symbol);
            
            // Parse exchange and symbol name
            std::string exchange = "NSE";
            std::string symbol_name = resolved_symbol;
            
            auto pos = resolved_symbol.find(':');
            if (pos != std::string::npos) {
                exchange = resolved_symbol.substr(0, pos);
                symbol_name = resolved_symbol.substr(pos + 1);
            }
            
            // Skip if this is configured as an index future
            if (isIndexFuture(resolved_symbol)) {
                continue;
            }
            
            // Create instrument config
            InstrumentConfig config;
            config.symbol = symbol_name;
            config.exchange = exchange;
            config.is_futures = false;
            config.expiry_date = "";
            
            spot_instruments.push_back(config);
        }
    }
    
    return spot_instruments;
}

// The duplicate methods have been completely removed as they were identical
// to the first implementations already present in the file above.

std::vector<InstrumentConfig> EnvironmentConfig::getFuturesInstruments() const {
    std::vector<InstrumentConfig> futures_instruments;
    
    // Filter instruments that are futures
    for (const auto& instrument : instruments_) {
        if (instrument.is_futures) {
            futures_instruments.push_back(instrument);
        }
    }
    
    // If no instruments are configured, fall back to the old implementation
    if (futures_instruments.empty()) {
        // Use futures symbols from config, or fall back to index futures if not specified
        std::vector<std::string> symbols = futures_symbols_;
        
        // If no explicit futures symbols, use index futures
        if (symbols.empty()) {
            for (const auto& index : index_futures_) {
                symbols.push_back("NFO:" + index);
            }
        }
        
        for (const auto& symbol : symbols) {
            // Format and resolve symbol
            std::string formatted_symbol = formatSymbol(symbol);
            std::string resolved_symbol = resolveSymbol(formatted_symbol);
            
            // Check if this is a full futures symbol with expiry
            auto config = parseFullSymbol(resolved_symbol);
            if (config.is_futures) {
                futures_instruments.push_back(config);
                continue;
            }
            
            // Parse exchange and symbol name
            std::string exchange = "NFO";  // Default to NFO for futures
            std::string symbol_name = resolved_symbol;
            
            auto pos = resolved_symbol.find(':');
            if (pos != std::string::npos) {
                exchange = resolved_symbol.substr(0, pos);
                symbol_name = resolved_symbol.substr(pos + 1);
            }
            
            // Create config for a basic futures instrument (current month)
            InstrumentConfig basic_config;
            basic_config.symbol = symbol_name;
            basic_config.exchange = exchange;
            basic_config.is_futures = true;
            
            // Get nearest expiry date
            basic_config.expiry_date = getNearestExpiryDate(symbol_name);
            
            futures_instruments.push_back(basic_config);
        }
    }
    
    return futures_instruments;
}

std::vector<InstrumentConfig> EnvironmentConfig::getNearestFuturesInstruments() const {
    auto futures_instruments = getFuturesInstruments();
    
    // Filter for nearest expiry only
    std::map<std::string, InstrumentConfig> nearest_futures;
    
    for (const auto& instrument : futures_instruments) {
        std::string key = instrument.symbol;
        
        if (nearest_futures.find(key) == nearest_futures.end() ||
            instrument.expiry_date < nearest_futures[key].expiry_date) {
            nearest_futures[key] = instrument;
        }
    }
    
    // Convert map back to vector
    std::vector<InstrumentConfig> result;
    result.reserve(nearest_futures.size());
    
    for (const auto& [key, config] : nearest_futures) {
        result.push_back(config);
    }
    
    return result;
}

std::string EnvironmentConfig::resolveSymbol(const std::string& symbol) const {
    // Check if symbol is in the map
    auto it = symbol_map_.find(symbol);
    if (it != symbol_map_.end()) {
        return it->second;
    }
    
    // Check if symbol without exchange is in the map
    auto pos = symbol.find(':');
    if (pos != std::string::npos) {
        std::string symbol_without_exchange = symbol.substr(pos + 1);
        it = symbol_map_.find(symbol_without_exchange);
        if (it != symbol_map_.end()) {
            // Check if the mapped symbol already has an exchange
            if (it->second.find(':') == std::string::npos) {
                // Prepend the original exchange
                return symbol.substr(0, pos + 1) + it->second;
            } else {
                // Use the mapped symbol as is
                return it->second;
            }
        }
    }
    
    // No mapping found, return original symbol
    return symbol;
}

std::string EnvironmentConfig::formatSymbol(const std::string& symbol) const {
    // Check if symbol already has an exchange
    if (symbol.find(':') != std::string::npos) {
        return symbol;
    }
    
    // Add default exchange
    return default_exchange_ + ":" + symbol;
}

bool EnvironmentConfig::isIndexFuture(const std::string& symbol) const {
    if (!use_futures_for_indices_) {
        return false;
    }
    
    // Parse symbol to get name without exchange
    std::string symbol_name = symbol;
    auto pos = symbol.find(':');
    if (pos != std::string::npos) {
        symbol_name = symbol.substr(pos + 1);
    }
    
    // Remove spaces from symbol name
    std::string cleaned_name = symbol_name;
    cleaned_name.erase(std::remove(cleaned_name.begin(), cleaned_name.end(), ' '), cleaned_name.end());
    
    // Check if the cleaned name is in the index futures set
    return index_futures_.find(cleaned_name) != index_futures_.end();
}

std::string EnvironmentConfig::getNearestExpiryDate(const std::string& /* symbol */) const {
    // Get current date
    auto now = std::chrono::system_clock::now();
    auto now_time_t = std::chrono::system_clock::to_time_t(now);
    std::tm now_tm = *std::localtime(&now_time_t);
    
    // Find the last Thursday of the current month
    int year = now_tm.tm_year + 1900;
    int month = now_tm.tm_mon + 1;
    
    // Check if we're close to month end - if so, use next month
    int day = now_tm.tm_mday;
    int days_in_month = 31; // Rough approximation
    
    if (day > days_in_month - index_futures_rollover_days_) {
        // We're close to rollover, use next month
        month++;
        if (month > 12) {
            month = 1;
            year++;
        }
    }
    
    // Find last Thursday of month
    std::tm expiry_tm = {};
    expiry_tm.tm_year = year - 1900;
    expiry_tm.tm_mon = month - 1;
    expiry_tm.tm_mday = 1;
    
    // Start with next month's 1st day
    if (month == 12) {
        expiry_tm.tm_year++;
        expiry_tm.tm_mon = 0;
    } else {
        expiry_tm.tm_mon++;
    }
    
    // Go back until we find a Thursday (3 is Thursday in tm_wday)
    do {
        expiry_tm.tm_mday--;
        std::mktime(&expiry_tm);
    } while (expiry_tm.tm_wday != 4); // 4 is Thursday (0 = Sunday)
    
    // Format as YYMMDD
    std::stringstream ss;
    ss << std::setfill('0') << std::setw(2) << (expiry_tm.tm_year % 100)
       << std::setfill('0') << std::setw(2) << (expiry_tm.tm_mon + 1)
       << std::setfill('0') << std::setw(2) << expiry_tm.tm_mday;
    
    return ss.str();
}

InstrumentConfig EnvironmentConfig::parseFullSymbol(const std::string& full_symbol) const {
    InstrumentConfig config;
    
    // Parse exchange
    auto pos = full_symbol.find(':');
    if (pos != std::string::npos) {
        config.exchange = full_symbol.substr(0, pos);
        config.symbol = full_symbol.substr(pos + 1);
    } else {
        config.exchange = "NSE"; // Default exchange
        config.symbol = full_symbol;
    }
    
    // Check if it's a futures symbol
    std::regex futures_regex("([A-Za-z0-9]+)(\\d{6})(?:FUT)?");
    std::smatch matches;
    
    if (std::regex_search(config.symbol, matches, futures_regex) && matches.size() > 2) {
        // Extract base symbol and expiry date
        config.symbol = matches[1].str();
        config.expiry_date = matches[2].str();
        config.is_futures = true;
        
        // If exchange isn't specified or is NSE, use NFO
        if (config.exchange == "NSE") {
            config.exchange = "NFO";
        }
    } else {
        config.is_futures = false;
        config.expiry_date = "";
    }
    
    return config;
}

std::string EnvironmentConfig::getEnv(const std::string& name, const std::string& default_value) const {
    const char* value = std::getenv(name.c_str());
    if (value && value[0] != '\0') {
        return value;
    }
    return default_value;
}

// The getConfigValue template now handles all variable parsing including booleans and integers

} // namespace Zerodha
} // namespace Adapter