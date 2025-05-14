#pragma once

#include <string>
#include <map>
#include <set>
#include <vector>
#include <mutex>
#include <memory>
#include <fstream>
#include <chrono>
#include <optional>
#include <filesystem>
#include <unordered_map>

#include "common/logging.h"
#include "common/time_utils.h"
#include "trading/adapters/zerodha/auth/zerodha_authenticator.h"

namespace Adapter {
namespace Zerodha {

/**
 * Enumeration of possible exchange types
 */
enum class Exchange {
    NSE,    // National Stock Exchange (Equities)
    BSE,    // Bombay Stock Exchange
    NFO,    // NSE Futures & Options
    BFO,    // BSE Futures & Options
    CDS,    // Currency Derivatives
    MCX,    // Multi Commodity Exchange
    UNKNOWN // Unknown exchange
};

/**
 * Enumeration of instrument types
 */
enum class InstrumentType {
    EQ,      // Equity
    FUT,     // Futures
    OPT,     // Options
    INDEX,   // Index
    UNKNOWN  // Unknown instrument type
};

/**
 * Structure to store instrument details
 */
struct InstrumentInfo {
    int32_t instrument_token = 0;
    int32_t exchange_token = 0;
    std::string trading_symbol;
    std::string name;
    double last_price = 0.0;
    std::optional<std::chrono::system_clock::time_point> expiry;
    std::optional<double> strike;
    double tick_size = 0.05;
    int32_t lot_size = 1;
    InstrumentType instrument_type = InstrumentType::UNKNOWN;
    std::string segment;
    Exchange exchange = Exchange::UNKNOWN;
    
    // Constructor with required fields
    InstrumentInfo(int32_t i_token, int32_t e_token, std::string t_symbol, 
                  std::string i_name, Exchange exch, InstrumentType i_type)
        : instrument_token(i_token), exchange_token(e_token), 
          trading_symbol(std::move(t_symbol)), name(std::move(i_name)),
          instrument_type(i_type), exchange(exch) {}
    
    // Default constructor
    InstrumentInfo() = default;
};

/**
 * Class to manage Zerodha instrument tokens
 * 
 * This class handles:
 * - Downloading and parsing the Zerodha instruments CSV
 * - Caching instrument tokens
 * - Looking up tokens for symbols, including special handling for indices
 * - Refreshing the instrument database daily
 */
class InstrumentTokenManager {
public:
    /**
     * Constructor
     * 
     * @param authenticator ZerodhaAuthenticator for API access
     * @param logger Logger for diagnostic messages
     * @param cache_dir Directory to cache instrument data
     */
    InstrumentTokenManager(ZerodhaAuthenticator* authenticator, 
                          Common::Logger* logger,
                          const std::string& cache_dir = ".cache/zerodha");
    
    /**
     * Destructor
     */
    ~InstrumentTokenManager();
    
    /**
     * Initialize the token manager
     * 
     * Downloads instrument data if needed and builds the lookup tables
     * 
     * @return true if initialization was successful
     */
    bool initialize();
    
    /**
     * Get instrument token for a symbol
     * 
     * @param symbol Symbol to look up (format: EXCHANGE:SYMBOL e.g. "NSE:RELIANCE")
     * @return Instrument token or 0 if not found
     */
    int32_t getInstrumentToken(const std::string& symbol);
    
    /**
     * Get instrument token for an index future
     * 
     * Finds the nearest expiry future contract for the specified index
     * 
     * @param index_name Index name (e.g. "NIFTY", "BANKNIFTY")
     * @return Instrument token for the nearest future or 0 if not found
     */
    int32_t getNearestFutureToken(const std::string& index_name);
    
    /**
     * Update the instrument data
     * 
     * Downloads the latest instrument data from Zerodha API
     * 
     * @return true if update was successful
     */
    bool updateInstrumentData();
    
    /**
     * Check if it's time to refresh the instrument data
     * 
     * @return true if refresh is needed
     */
    bool shouldRefresh() const;
    
    /**
     * Get instrument info for a token
     * 
     * @param token Instrument token to look up
     * @return InstrumentInfo or nullopt if not found
     */
    std::optional<InstrumentInfo> getInstrumentInfo(int32_t token) const;
    
    /**
     * Parse exchange and symbol from a formatted string
     * 
     * @param full_symbol Symbol in format "EXCHANGE:SYMBOL"
     * @return Pair of exchange and symbol
     */
    static std::pair<Exchange, std::string> parseSymbol(const std::string& full_symbol);
    
    /**
     * Convert Exchange enum to string
     * 
     * @param exchange Exchange enum
     * @return Exchange string (e.g. "NSE", "BSE")
     */
    static std::string exchangeToString(Exchange exchange);
    
    /**
     * Convert string to Exchange enum
     * 
     * @param exchange_str Exchange string
     * @return Exchange enum
     */
    static Exchange stringToExchange(const std::string& exchange_str);
    
    /**
     * Convert InstrumentType enum to string
     * 
     * @param type InstrumentType enum
     * @return InstrumentType string (e.g. "EQ", "FUT")
     */
    static std::string instrumentTypeToString(InstrumentType type);
    
    /**
     * Convert string to InstrumentType enum
     * 
     * @param type_str InstrumentType string
     * @return InstrumentType enum
     */
    static InstrumentType stringToInstrumentType(const std::string& type_str);

private:
    // Parse CSV data into instrument list
    bool parseCSV(const std::string& csv_data);
    
    // Parse a single CSV line
    std::optional<InstrumentInfo> parseCSVLine(const std::string& line);
    
    // Split a CSV line into fields, handling quoted fields correctly
    std::vector<std::string> splitCSVLine(const std::string& line);
    
    // Parse date string into time_point
    std::optional<std::chrono::system_clock::time_point> parseDate(const std::string& date_str);
    
    // Download instruments CSV from Zerodha API
    std::string downloadInstrumentsCSV();
    
    // Save CSV data to cache file
    bool saveToCache(const std::string& csv_data);
    
    // Load CSV data from cache file
    std::string loadFromCache();
    
    // Check if cache is valid (not expired)
    bool isCacheValid() const;
    
    // Build lookup indices
    void buildIndices();
    
    // Check if a file exists
    bool fileExists(const std::string& file_path) const;
    
    // Calculate next expiry date
    std::chrono::system_clock::time_point calculateNextExpiryDate() const;
    
    // Get expiry date string in YYMMM format (e.g. "22DEC")
    std::string getExpiryString(const std::chrono::system_clock::time_point& expiry) const;
    
    // Format a future symbol (e.g. "NIFTY22DECFUT")
    std::string formatFutureSymbol(const std::string& index_name, 
                                 const std::chrono::system_clock::time_point& expiry) const;

private:
    // Authentication for API requests
    ZerodhaAuthenticator* authenticator_ = nullptr;
    
    // Logger
    Common::Logger* logger_ = nullptr;
    mutable std::string time_str_;  // Mutable to allow const methods to modify it
    
    // Cache directory
    std::string cache_dir_;
    std::string cache_file_;
    
    // Last update time
    std::chrono::system_clock::time_point last_update_time_;
    
    // Instrument storage
    std::vector<InstrumentInfo> instruments_;
    
    // Lookup indices for fast access
    std::unordered_map<int32_t, size_t> token_index_;  // token -> index in instruments_
    std::map<std::pair<Exchange, std::string>, size_t> symbol_index_;  // (exchange, symbol) -> index
    std::multimap<std::string, size_t> name_index_;  // index name -> indices of related instruments
    
    // Future contract info
    std::map<std::string, std::vector<size_t>> futures_by_index_;  // index name -> indices of future contracts
    
    // Thread safety
    mutable std::mutex mutex_;
    
    // Cache max age (24 hours by default)
    const std::chrono::hours cache_max_age_{24};
    
    // Is initialized flag
    bool initialized_ = false;
};

} // namespace Zerodha
} // namespace Adapter