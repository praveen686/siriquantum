#include "instrument_token_manager.h"

#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <iomanip>
#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace Adapter {
namespace Zerodha {

namespace {
    // Helper to receive data from CURL
    size_t WriteCallback(void* contents, size_t size, size_t nmemb, std::string* userp) {
        userp->append((char*)contents, size * nmemb);
        return size * nmemb;
    }
    
    // Trim whitespace from string
    std::string trim(const std::string& str) {
        auto start = std::find_if_not(str.begin(), str.end(), [](unsigned char c) {
            return std::isspace(c);
        });
        auto end = std::find_if_not(str.rbegin(), str.rend(), [](unsigned char c) {
            return std::isspace(c);
        }).base();
        return (start < end) ? std::string(start, end) : std::string();
    }
    
    // Months for expiry date formatting
    const std::array<std::string, 12> MONTHS = {
        "JAN", "FEB", "MAR", "APR", "MAY", "JUN", 
        "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"
    };
}

InstrumentTokenManager::InstrumentTokenManager(ZerodhaAuthenticator* authenticator, 
                                         Common::Logger* logger,
                                         const std::string& cache_dir)
    : authenticator_(authenticator),
      logger_(logger),
      cache_dir_(cache_dir) {
    
    // Create cache directory if it doesn't exist
    std::filesystem::create_directories(cache_dir_);
    
    // Set cache file path
    cache_file_ = cache_dir_ + "/instruments.csv";
    
    logger_->log("%:% %() % Initialized InstrumentTokenManager with cache at: %\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                cache_file_.c_str());
}

InstrumentTokenManager::~InstrumentTokenManager() {
    logger_->log("%:% %() % InstrumentTokenManager destroyed\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
}

bool InstrumentTokenManager::initialize() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (initialized_) {
        return true;
    }
    
    logger_->log("%:% %() % Initializing InstrumentTokenManager\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    // Check if we need to update the instrument data
    bool update_required = shouldRefresh();
    std::string csv_data;
    
    if (update_required) {
        logger_->log("%:% %() % Cache needs refresh, downloading new instrument data\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
                    
        // Download new data
        csv_data = downloadInstrumentsCSV();
        
        if (!csv_data.empty()) {
            // Save to cache
            if (saveToCache(csv_data)) {
                logger_->log("%:% %() % Saved % bytes of instrument data to cache\n",
                            __FILE__, __LINE__, __FUNCTION__, 
                            Common::getCurrentTimeStr(&time_str_),
                            csv_data.size());
            } else {
                logger_->log("%:% %() % Failed to save instrument data to cache\n",
                            __FILE__, __LINE__, __FUNCTION__, 
                            Common::getCurrentTimeStr(&time_str_));
            }
        } else {
            // Download failed, try to load from cache
            logger_->log("%:% %() % Failed to download instrument data, attempting to load from cache\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_));
                        
            csv_data = loadFromCache();
        }
    } else {
        // Load from valid cache
        logger_->log("%:% %() % Using cached instrument data\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
                    
        csv_data = loadFromCache();
    }
    
    // Parse CSV data
    if (!csv_data.empty()) {
        if (parseCSV(csv_data)) {
            buildIndices();
            initialized_ = true;
            logger_->log("%:% %() % Successfully loaded % instruments\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        instruments_.size());
            return true;
        } else {
            logger_->log("%:% %() % Failed to parse CSV data\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_));
        }
    } else {
        logger_->log("%:% %() % No instrument data available\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
    }
    
    return false;
}

int32_t InstrumentTokenManager::getInstrumentToken(const std::string& symbol) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ && !initialize()) {
        logger_->log("%:% %() % Cannot get token, not initialized\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return 0;
    }
    
    // Check if this is an index symbol that needs future lookup
    // Symbols like "NSE:NIFTY 50" or "NSE:BANKNIFTY" would be treated as indices
    auto [exchange, symbol_name] = parseSymbol(symbol);
    
    if (exchange == Adapter::Zerodha::Exchange::NSE) {
        // Check if this is a well-known index by removing spaces
        std::string cleaned_name = symbol_name;
        cleaned_name.erase(std::remove(cleaned_name.begin(), cleaned_name.end(), ' '), cleaned_name.end());
        
        if (cleaned_name == "NIFTY50" || cleaned_name == "NIFTY" || 
            cleaned_name == "BANKNIFTY" || cleaned_name == "FINNIFTY") {
            // This is an index, check environment variable to see if we should get future
            const char* future_vars = std::getenv("ZKITE_USE_FUTURES_FOR_INDICES");
            if (future_vars && (std::string(future_vars) == "1" || 
                              std::string(future_vars) == "true" || 
                              std::string(future_vars) == "yes")) {
                
                // Clean up the index name for future lookup
                std::string index_name;
                if (cleaned_name == "NIFTY50" || cleaned_name == "NIFTY") {
                    index_name = "NIFTY";
                } else if (cleaned_name == "BANKNIFTY") {
                    index_name = "BANKNIFTY";  
                } else if (cleaned_name == "FINNIFTY") {
                    index_name = "FINNIFTY";
                }
                
                logger_->log("%:% %() % Index % detected, getting nearest future contract\n",
                            __FILE__, __LINE__, __FUNCTION__, 
                            Common::getCurrentTimeStr(&time_str_),
                            index_name.c_str());
                            
                return getNearestFutureToken(index_name);
            }
        }
    }
    
    // Regular symbol lookup
    auto it = symbol_index_.find(std::make_pair(exchange, symbol_name));
    if (it != symbol_index_.end()) {
        int32_t token = instruments_[it->second].instrument_token;
        logger_->log("%:% %() % Found token % for symbol %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    token, symbol.c_str());
        return token;
    }
    
    logger_->log("%:% %() % No token found for symbol %\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                symbol.c_str());
    return 0;
}

int32_t InstrumentTokenManager::getNearestFutureToken(const std::string& index_name) {
    std::lock_guard<std::mutex> lock(mutex_);
    
    if (!initialized_ && !initialize()) {
        logger_->log("%:% %() % Cannot get future token, not initialized\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return 0;
    }
    
    auto it = futures_by_index_.find(index_name);
    if (it == futures_by_index_.end() || it->second.empty()) {
        logger_->log("%:% %() % No futures found for index %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    index_name.c_str());
        return 0;
    }
    
    // Get current time
    auto now = std::chrono::system_clock::now();
    
    // Find the nearest expiry date that's in the future
    std::optional<size_t> nearest_idx;
    std::chrono::system_clock::time_point nearest_expiry;
    
    for (size_t instrument_idx : it->second) {
        const auto& instrument = instruments_[instrument_idx];
        
        // Skip if no expiry date
        if (!instrument.expiry.has_value()) {
            continue;
        }
        
        auto expiry = instrument.expiry.value();
        
        // Skip expired contracts
        if (expiry <= now) {
            continue;
        }
        
        // If this is the first valid expiry or it's closer than the current nearest
        if (!nearest_idx.has_value() || expiry < nearest_expiry) {
            nearest_idx = instrument_idx;
            nearest_expiry = expiry;
        }
    }
    
    if (nearest_idx.has_value()) {
        int32_t token = instruments_[*nearest_idx].instrument_token;
        
        logger_->log("%:% %() % Found nearest future contract for % with token % (expiry: %)\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    index_name.c_str(), token,
                    instruments_[*nearest_idx].trading_symbol.c_str());
                    
        return token;
    }
    
    // If no valid future contract found, try to construct one based on expected pattern
    // This is a fallback in case we don't have the latest instrument data
    auto next_expiry = calculateNextExpiryDate();
    std::string future_symbol = formatFutureSymbol(index_name, next_expiry);
    
    logger_->log("%:% %() % No valid future contract found, constructing symbol: %\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                future_symbol.c_str());
    
    // Try to find this constructed symbol
    auto symbol_it = symbol_index_.find(std::make_pair(Adapter::Zerodha::Exchange::NFO, future_symbol));
    if (symbol_it != symbol_index_.end()) {
        int32_t token = instruments_[symbol_it->second].instrument_token;
        logger_->log("%:% %() % Found constructed future symbol % with token %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    future_symbol.c_str(), token);
        return token;
    }
    
    logger_->log("%:% %() % Could not find any future contract for %\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                index_name.c_str());
    return 0;
}

bool InstrumentTokenManager::updateInstrumentData() {
    std::lock_guard<std::mutex> lock(mutex_);
    
    logger_->log("%:% %() % Updating instrument data\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    std::string csv_data = downloadInstrumentsCSV();
    if (csv_data.empty()) {
        logger_->log("%:% %() % Failed to download instrument data\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    // Save to cache
    if (!saveToCache(csv_data)) {
        logger_->log("%:% %() % Failed to save instrument data to cache\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    // Parse CSV data
    instruments_.clear();
    token_index_.clear();
    symbol_index_.clear();
    name_index_.clear();
    futures_by_index_.clear();
    
    if (!parseCSV(csv_data)) {
        logger_->log("%:% %() % Failed to parse CSV data\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    buildIndices();
    last_update_time_ = std::chrono::system_clock::now();
    initialized_ = true;
    
    logger_->log("%:% %() % Updated instrument data with % instruments\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                instruments_.size());
    
    return true;
}

bool InstrumentTokenManager::shouldRefresh() const {
    // Check if cache file exists
    if (!fileExists(cache_file_)) {
        return true;
    }
    
    // Check if cache is valid
    return !isCacheValid();
}

std::optional<InstrumentInfo> InstrumentTokenManager::getInstrumentInfo(int32_t token) const {
    std::lock_guard<std::mutex> lock(mutex_);
    
    auto it = token_index_.find(token);
    if (it != token_index_.end()) {
        return instruments_[it->second];
    }
    
    return std::nullopt;
}

std::pair<Exchange, std::string> InstrumentTokenManager::parseSymbol(const std::string& full_symbol) {
    // Parse symbol in format "EXCHANGE:SYMBOL"
    auto pos = full_symbol.find(':');
    if (pos != std::string::npos) {
        std::string exchange_str = full_symbol.substr(0, pos);
        std::string symbol = full_symbol.substr(pos + 1);
        return std::make_pair(stringToExchange(exchange_str), symbol);
    }
    
    // If no exchange specified, assume NSE
    return std::make_pair(Adapter::Zerodha::Exchange::NSE, full_symbol);
}

std::string InstrumentTokenManager::exchangeToString(Exchange exchange) {
    switch (exchange) {
        case Adapter::Zerodha::Exchange::NSE: return "NSE";
        case Adapter::Zerodha::Exchange::BSE: return "BSE";
        case Adapter::Zerodha::Exchange::NFO: return "NFO";
        case Adapter::Zerodha::Exchange::BFO: return "BFO";
        case Adapter::Zerodha::Exchange::CDS: return "CDS";
        case Adapter::Zerodha::Exchange::MCX: return "MCX";
        default: return "UNKNOWN";
    }
}

Exchange InstrumentTokenManager::stringToExchange(const std::string& exchange_str) {
    std::string upper_exchange = exchange_str;
    std::transform(upper_exchange.begin(), upper_exchange.end(), upper_exchange.begin(),
                  [](unsigned char c) { return std::toupper(c); });
    
    if (upper_exchange == "NSE") return Adapter::Zerodha::Exchange::NSE;
    if (upper_exchange == "BSE") return Adapter::Zerodha::Exchange::BSE;
    if (upper_exchange == "NFO") return Adapter::Zerodha::Exchange::NFO;
    if (upper_exchange == "BFO") return Adapter::Zerodha::Exchange::BFO;
    if (upper_exchange == "CDS") return Adapter::Zerodha::Exchange::CDS;
    if (upper_exchange == "MCX") return Adapter::Zerodha::Exchange::MCX;
    
    return Adapter::Zerodha::Exchange::UNKNOWN;
}

std::string InstrumentTokenManager::instrumentTypeToString(InstrumentType type) {
    switch (type) {
        case InstrumentType::EQ: return "EQ";
        case InstrumentType::FUT: return "FUT";
        case InstrumentType::OPT: return "OPT";
        case InstrumentType::INDEX: return "INDEX";
        default: return "UNKNOWN";
    }
}

InstrumentType InstrumentTokenManager::stringToInstrumentType(const std::string& type_str) {
    std::string upper_type = type_str;
    std::transform(upper_type.begin(), upper_type.end(), upper_type.begin(),
                  [](unsigned char c) { return std::toupper(c); });
    
    if (upper_type == "EQ") return InstrumentType::EQ;
    if (upper_type == "FUT") return InstrumentType::FUT;
    if (upper_type == "OPT") return InstrumentType::OPT;
    if (upper_type == "INDEX") return InstrumentType::INDEX;
    
    return InstrumentType::UNKNOWN;
}

bool InstrumentTokenManager::parseCSV(const std::string& csv_data) {
    std::istringstream stream(csv_data);
    std::string line;
    
    // Skip header line
    if (!std::getline(stream, line)) {
        logger_->log("%:% %() % CSV data is empty\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return false;
    }
    
    size_t line_count = 0;
    size_t valid_count = 0;
    
    // Process each line
    while (std::getline(stream, line)) {
        line_count++;
        
        auto instrument_opt = parseCSVLine(line);
        if (instrument_opt.has_value()) {
            instruments_.push_back(std::move(instrument_opt.value()));
            valid_count++;
        }
        
        // Log progress for large CSVs
        if (line_count % 10000 == 0) {
            logger_->log("%:% %() % Processed % CSV lines, % valid instruments\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        line_count, valid_count);
        }
    }
    
    logger_->log("%:% %() % Finished parsing CSV: % lines, % valid instruments\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                line_count, valid_count);
    
    return valid_count > 0;
}

std::optional<InstrumentInfo> InstrumentTokenManager::parseCSVLine(const std::string& line) {
    auto fields = splitCSVLine(line);
    
    // Zerodha CSV has these columns:
    // instrument_token, exchange_token, tradingsymbol, name, last_price, expiry, strike, 
    // tick_size, lot_size, instrument_type, segment, exchange
    
    if (fields.size() < 12) {
        return std::nullopt;
    }
    
    try {
        InstrumentInfo info;
        
        // Parse required fields
        info.instrument_token = std::stoi(fields[0]);
        info.exchange_token = std::stoi(fields[1]);
        info.trading_symbol = fields[2];
        info.name = fields[3];
        
        // Parse optional fields
        if (!fields[4].empty()) {
            info.last_price = std::stod(fields[4]);
        }
        
        if (!fields[5].empty()) {
            info.expiry = parseDate(fields[5]);
        }
        
        if (!fields[6].empty()) {
            info.strike = std::stod(fields[6]);
        }
        
        if (!fields[7].empty()) {
            info.tick_size = std::stod(fields[7]);
        }
        
        if (!fields[8].empty()) {
            info.lot_size = std::stoi(fields[8]);
        }
        
        info.instrument_type = stringToInstrumentType(fields[9]);
        info.segment = fields[10];
        info.exchange = stringToExchange(fields[11]);
        
        return info;
    } catch (const std::exception& e) {
        // Log parsing error but continue with other lines
        logger_->log("%:% %() % Error parsing CSV line: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    e.what());
        return std::nullopt;
    }
}

std::vector<std::string> InstrumentTokenManager::splitCSVLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string field;
    bool in_quotes = false;
    
    for (char c : line) {
        if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ',' && !in_quotes) {
            fields.push_back(trim(field));
            field.clear();
        } else {
            field += c;
        }
    }
    
    // Add the last field
    fields.push_back(trim(field));
    
    return fields;
}

std::optional<std::chrono::system_clock::time_point> InstrumentTokenManager::parseDate(const std::string& date_str) {
    // Parse date in format YYYY-MM-DD
    // Example: 2022-12-29
    std::tm tm = {};
    std::istringstream ss(date_str);
    ss >> std::get_time(&tm, "%Y-%m-%d");
    
    if (ss.fail()) {
        return std::nullopt;
    }
    
    // Convert tm to time_point
    auto time = std::mktime(&tm);
    return std::chrono::system_clock::from_time_t(time);
}

std::string InstrumentTokenManager::downloadInstrumentsCSV() {
    // Check if authenticator is available
    if (!authenticator_) {
        logger_->log("%:% %() % No authenticator available for API access\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return "";
    }
    
    logger_->log("%:% %() % Downloading instruments CSV from Zerodha API\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_));
    
    // Initialize CURL
    CURL* curl = curl_easy_init();
    if (!curl) {
        logger_->log("%:% %() % Failed to initialize CURL\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        return "";
    }
    
    // Ensure we have a valid session
    std::string access_token = authenticator_->get_access_token();
    if (access_token.empty()) {
        logger_->log("%:% %() % Authenticating with Zerodha\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_));
        access_token = authenticator_->authenticate();
        if (access_token.empty()) {
            logger_->log("%:% %() % Failed to authenticate with Zerodha\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_));
            curl_easy_cleanup(curl);
            return "";
        }
    }
    
    std::string url = "https://api.kite.trade/instruments";
    
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "X-Kite-Version: 3");
    
    std::string auth_header = "Authorization: token " + authenticator_->getApiKey() + ":" + access_token;
    headers = curl_slist_append(headers, auth_header.c_str());
    
    // Set up request
    std::string response;
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);  // 30 second timeout
    
    // Execute request
    CURLcode res = curl_easy_perform(curl);
    
    // Clean up
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        logger_->log("%:% %() % CURL failed: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    curl_easy_strerror(res));
        return "";
    }
    
    logger_->log("%:% %() % Downloaded % bytes of instrument data\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                response.size());
    
    return response;
}

bool InstrumentTokenManager::saveToCache(const std::string& csv_data) {
    try {
        std::ofstream file(cache_file_, std::ios::binary);
        if (!file.is_open()) {
            logger_->log("%:% %() % Failed to open cache file for writing: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        cache_file_.c_str());
            return false;
        }
        
        file.write(csv_data.data(), csv_data.size());
        file.close();
        
        // Update last update time
        last_update_time_ = std::chrono::system_clock::now();
        
        return true;
    } catch (const std::exception& e) {
        logger_->log("%:% %() % Exception while saving to cache: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    e.what());
        return false;
    }
}

std::string InstrumentTokenManager::loadFromCache() {
    try {
        std::ifstream file(cache_file_, std::ios::binary | std::ios::ate);
        if (!file.is_open()) {
            logger_->log("%:% %() % Failed to open cache file for reading: %\n",
                        __FILE__, __LINE__, __FUNCTION__, 
                        Common::getCurrentTimeStr(&time_str_),
                        cache_file_.c_str());
            return "";
        }
        
        // Get file size
        auto size = file.tellg();
        file.seekg(0, std::ios::beg);
        
        // Read file content
        std::string content(size, '\0');
        file.read(&content[0], size);
        
        logger_->log("%:% %() % Loaded % bytes from cache file\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    size);
        
        return content;
    } catch (const std::exception& e) {
        logger_->log("%:% %() % Exception while loading from cache: %\n",
                    __FILE__, __LINE__, __FUNCTION__, 
                    Common::getCurrentTimeStr(&time_str_),
                    e.what());
        return "";
    }
}

bool InstrumentTokenManager::isCacheValid() const {
    try {
        // Get file modification time
        auto file_time = std::filesystem::last_write_time(cache_file_);
        auto file_time_point = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
            std::chrono::file_clock::to_sys(file_time));
        
        // Check age
        auto now = std::chrono::system_clock::now();
        auto age = now - file_time_point;
        
        return age <= cache_max_age_;
    } catch (const std::exception& e) {
        // If any error occurs, consider cache invalid
        return false;
    }
}

void InstrumentTokenManager::buildIndices() {
    logger_->log("%:% %() % Building lookup indices for % instruments\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                instruments_.size());
    
    token_index_.clear();
    symbol_index_.clear();
    name_index_.clear();
    futures_by_index_.clear();
    
    for (size_t i = 0; i < instruments_.size(); ++i) {
        const auto& instrument = instruments_[i];
        
        // Index by token
        token_index_[instrument.instrument_token] = i;
        
        // Index by exchange and trading symbol
        symbol_index_[std::make_pair(instrument.exchange, instrument.trading_symbol)] = i;
        
        // Index by name
        if (!instrument.name.empty()) {
            name_index_.insert(std::make_pair(instrument.name, i));
        }
        
        // Index futures by underlying index
        if (instrument.instrument_type == InstrumentType::FUT && 
            instrument.exchange == Adapter::Zerodha::Exchange::NFO && 
            !instrument.name.empty()) {
            futures_by_index_[instrument.name].push_back(i);
        }
    }
    
    logger_->log("%:% %() % Built indices: % tokens, % symbols, % names, % future indices\n",
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                token_index_.size(), symbol_index_.size(), 
                name_index_.size(), futures_by_index_.size());
}

bool InstrumentTokenManager::fileExists(const std::string& file_path) const {
    return std::filesystem::exists(file_path);
}

std::chrono::system_clock::time_point InstrumentTokenManager::calculateNextExpiryDate() const {
    // Get current time
    auto now = std::chrono::system_clock::now();
    
    // Convert to tm
    std::time_t now_time = std::chrono::system_clock::to_time_t(now);
    std::tm* now_tm = std::localtime(&now_time);
    
    // In India, futures typically expire on the last Thursday of each month
    int month = now_tm->tm_mon;
    int year = now_tm->tm_year;
    
    // If we're close to the end of the month, look at next month
    if (now_tm->tm_mday >= 25) {
        month++;
        if (month > 11) {
            month = 0;
            year++;
        }
    }
    
    // Find the last Thursday of the month
    std::tm expiry_tm = {};
    expiry_tm.tm_year = year;
    expiry_tm.tm_mon = month;
    expiry_tm.tm_mday = 1;
    
    // Set to next month to find month end
    expiry_tm.tm_mon++;
    if (expiry_tm.tm_mon > 11) {
        expiry_tm.tm_mon = 0;
        expiry_tm.tm_year++;
    }
    
    // Go back one day to get last day of target month
    expiry_tm.tm_mday = 0;
    
    // Normalize
    std::time_t expiry_time = std::mktime(&expiry_tm);
    expiry_tm = *std::localtime(&expiry_time);
    
    // Find the last Thursday (day 4) of the month
    int last_day = expiry_tm.tm_mday;
    int weekday = expiry_tm.tm_wday;
    
    // Calculate days to go back to reach Thursday
    int days_to_last_thursday = (weekday + 7 - 4) % 7;
    expiry_tm.tm_mday = last_day - days_to_last_thursday;
    
    // Normalize again
    expiry_time = std::mktime(&expiry_tm);
    
    return std::chrono::system_clock::from_time_t(expiry_time);
}

std::string InstrumentTokenManager::getExpiryString(const std::chrono::system_clock::time_point& expiry) const {
    // Convert to tm
    std::time_t expiry_time = std::chrono::system_clock::to_time_t(expiry);
    std::tm* expiry_tm = std::localtime(&expiry_time);
    
    // Format as YYMMMDD, e.g. "22DEC"
    std::ostringstream oss;
    oss << std::setfill('0') << std::setw(2) << (expiry_tm->tm_year % 100)
        << MONTHS[expiry_tm->tm_mon];
    
    return oss.str();
}

std::string InstrumentTokenManager::formatFutureSymbol(const std::string& index_name, 
                                                    const std::chrono::system_clock::time_point& expiry) const {
    return index_name + getExpiryString(expiry) + "FUT";
}

} // namespace Zerodha
} // namespace Adapter