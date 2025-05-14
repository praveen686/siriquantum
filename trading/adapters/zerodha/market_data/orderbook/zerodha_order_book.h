#pragma once

#include <string>
#include <map>
#include <vector>
#include <unordered_map>
#include <array>
#include <memory>
#include <set>
#include <limits>
#include <cstdint>

#include "common/types.h"
#include "common/logging.h"
#include "common/time_utils.h"
#include "common/mem_pool.h"
#include "exchange/market_data/market_update.h"
#include "trading/adapters/zerodha/market_data/zerodha_websocket_client.h"

// Alias to avoid namespace confusion
namespace ExchangeNS = ::Exchange;

namespace Adapter {
namespace Zerodha {

/**
 * ZerodhaOrderBook - Maintains a full limit order book for a Zerodha instrument
 * 
 * This class:
 * - Processes market data updates from Zerodha WebSocket
 * - Builds and maintains a full order book representation
 * - Generates internal exchange market update events
 * - Provides access to current order book state
 */
class ZerodhaOrderBook {
public:
    /**
     * Price level structure representing aggregated orders at a price
     */
    struct PriceLevel {
        double price;        // Price in rupees
        int32_t quantity;    // Quantity at this price
        int16_t orders;      // Number of orders at this price
        uint64_t last_update_time;  // Timestamp of last update
    };

    /**
     * Best Bid/Offer structure for quick access to top of book
     */
    struct BBO {
        double bid_price;
        int32_t bid_quantity;
        double ask_price;
        int32_t ask_quantity;
        
        // Default constructor with invalid values
        BBO() : 
            bid_price(0.0), 
            bid_quantity(0), 
            ask_price(std::numeric_limits<double>::max()), 
            ask_quantity(0) {}
    };

public:
    /**
     * Constructor
     * 
     * @param ticker_id Internal ticker ID for this instrument
     * @param logger Logger for diagnostic messages
     */
    ZerodhaOrderBook(Common::TickerId ticker_id, Common::Logger* logger);
    
    /**
     * Destructor
     */
    ~ZerodhaOrderBook();
    
    /**
     * Process a Zerodha market update
     * 
     * @param update Zerodha market update to process
     * @return Vector of generated internal market updates
     */
    std::vector<ExchangeNS::MEMarketUpdate*> processMarketUpdate(
        const MarketUpdate& update);
        
    /**
     * Generate market events from price level changes
     * 
     * @param current_update Current market update from Zerodha
     * @return Vector of internal market updates
     */
    std::vector<ExchangeNS::MEMarketUpdate*> generateMarketEvents(
        const MarketUpdate& current_update);
        
    /**
     * Check for disappeared price levels
     * 
     * @param current_update Current market update from Zerodha
     * @param events Vector to add cancel events to
     */
    void checkForDisappearedLevels(
        const MarketUpdate& current_update,
        std::vector<ExchangeNS::MEMarketUpdate*>& events);
    
    /**
     * Clear the order book
     * 
     * @return Vector of cancel events for all orders
     */
    std::vector<ExchangeNS::MEMarketUpdate*> clear();
    
    /**
     * Update best bid/offer cache
     */
    void updateBBO();
    
    /**
     * Get best bid price
     * 
     * @return Best bid price
     */
    double getBestBidPrice() const;
    
    /**
     * Get best ask price
     * 
     * @return Best ask price
     */
    double getBestAskPrice() const;
    
    /**
     * Get best bid quantity
     * 
     * @return Best bid quantity
     */
    int32_t getBestBidQuantity() const;
    
    /**
     * Get best ask quantity
     * 
     * @return Best ask quantity
     */
    int32_t getBestAskQuantity() const;
    
    /**
     * Get best bid/offer
     * 
     * @return BBO structure with top of book
     */
    const BBO& getBBO() const;
    
    /**
     * Get order book depth
     * 
     * @return Number of price levels on each side
     */
    std::pair<size_t, size_t> getDepth() const;
    
    /**
     * Get all bid price levels
     * 
     * @return Const reference to bid price map
     */
    const std::map<double, PriceLevel, std::greater<double>>& getBids() const;
    
    /**
     * Get all ask price levels
     * 
     * @return Const reference to ask price map
     */
    const std::map<double, PriceLevel, std::less<double>>& getAsks() const;
    
    /**
     * Check if order book is empty
     * 
     * @return true if both sides are empty
     */
    bool isEmpty() const;
    
    /**
     * Get internal ticker ID
     * 
     * @return Internal ticker ID
     */
    Common::TickerId getTickerId() const;
    
    /**
     * Get timestamp of last update
     * 
     * @return Timestamp of last update
     */
    uint64_t getLastUpdateTime() const;
    
    /**
     * Generate consistent order ID for price level
     * 
     * @param ticker_id Internal ticker ID
     * @param price Price level
     * @param side Side (BUY or SELL)
     * @return Generated order ID
     */
    static Common::OrderId generateOrderId(
        Common::TickerId ticker_id, 
        double price, 
        Common::Side side);

private:
    // Order book data
    std::map<double, PriceLevel, std::greater<double>> bids_;  // Sorted high to low
    std::map<double, PriceLevel, std::less<double>> asks_;     // Sorted low to high
    
    // Previous state for tracking changes
    std::set<double> prev_bid_prices_;
    std::set<double> prev_ask_prices_;
    
    // BBO cache for quick access
    BBO bbo_;
    
    // Book metadata
    Common::TickerId ticker_id_;
    uint64_t last_update_time_;
    Common::Logger* logger_;
    
    // Memory pool for MEMarketUpdate objects
    Common::MemPool<ExchangeNS::MEMarketUpdate> update_pool_;
    
    // Constants
    static constexpr size_t MAX_DEPTH_LEVELS = 5;  // Maximum depth from Zerodha
    static constexpr size_t UPDATE_POOL_SIZE = 1024;  // Size of memory pool
};

} // namespace Zerodha
} // namespace Adapter