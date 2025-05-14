#pragma once

#include "trading/strategy/liquidity_taker.h"
#include "trading/adapters/zerodha/market_data/zerodha_market_data_adapter.h"
#include "trading/adapters/zerodha/order_gw/zerodha_order_gateway_adapter.h"

#include <map>
#include <unordered_map>
#include <vector>
#include <string>
#include <cstdint>
#include <mutex>
#include <atomic>
#include <memory>

namespace Adapter {
namespace Zerodha {

/**
 * @class ZerodhaLiquidityTaker
 * @brief Zerodha-specific adaptation of the Liquidity Taker strategy.
 * 
 * Enhances the base Liquidity Taker strategy with Zerodha-specific features:
 * - Volume profile analysis for Indian equities
 * - Circuit limit filter awareness
 * - VWAP-based entry filters
 * - Time-of-day trading restrictions
 * - Bracket orders with built-in stop-loss/target
 * - Order size adaptation for lot sizes in derivatives
 */
class ZerodhaLiquidityTaker {
public:
    /**
     * @brief Configuration for Zerodha Liquidity Taker strategy
     */
    struct Config {
        bool use_vwap_filter = true;          // Whether to use VWAP-based entry filter
        double vwap_threshold = 0.02;         // Minimum distance from VWAP to enter (2%)
        bool enforce_circuit_limits = true;   // Whether to enforce circuit limits
        bool use_bracket_orders = false;      // Whether to use bracket orders
        double stop_loss_percent = 0.5;       // Stop loss percentage
        double target_percent = 1.0;          // Target profit percentage
        int min_volume_percentile = 50;       // Minimum volume percentile to trade
        bool enforce_trading_hours = true;    // Whether to enforce trading hours
        std::string trading_start_time = "09:15:00"; // Trading start time
        std::string trading_end_time = "15:15:00";   // Trading end time
    };

    /**
     * @brief Circuit limits for a given instrument
     */
    struct CircuitLimits {
        double upper_circuit_price = 0.0;      // Upper circuit limit price
        double lower_circuit_price = 0.0;      // Lower circuit limit price
        uint64_t last_updated = 0;             // Timestamp of last update
    };

    /**
     * @brief Bracket order structure for tracking multi-leg orders
     */
    struct BracketOrder {
        Common::OrderId entry_order_id = 0;            // Entry order ID
        Common::OrderId stop_loss_order_id = 0;        // Stop loss order ID 
        Common::OrderId target_order_id = 0;           // Target order ID
        Common::TickerId ticker_id = 0;                // Instrument ticker ID
        Common::Side side = Common::Side::INVALID;     // Order side
        Common::Price entry_price = 0;                 // Entry price
        Common::Price stop_loss_price = 0;             // Stop loss price
        Common::Price target_price = 0;                // Target price
        Common::Qty quantity = 0;                      // Order quantity
        std::string zerodha_order_id;                  // Zerodha entry order ID
        std::string zerodha_sl_order_id;               // Zerodha stop loss order ID
        std::string zerodha_target_order_id;           // Zerodha target order ID
        bool entry_filled = false;                     // Whether entry order is filled
        bool stop_loss_triggered = false;              // Whether stop loss is triggered
        bool target_triggered = false;                 // Whether target is triggered
        uint64_t creation_time = 0;                    // Time when order was created
        uint64_t fill_time = 0;                        // Time when entry was filled
        uint64_t exit_time = 0;                        // Time when position was closed
    };

    /**
     * @brief Constructor for Zerodha Liquidity Taker
     * 
     * @param logger Pointer to logger
     * @param trade_engine Pointer to trade engine
     * @param feature_engine Pointer to feature engine
     * @param order_manager Pointer to order manager
     * @param ticker_cfg Trading configuration for tickers
     * @param zerodha_config Zerodha-specific configuration
     * @param market_data_adapter Pointer to Zerodha market data adapter
     * @param order_gateway_adapter Pointer to Zerodha order gateway adapter
     */
    ZerodhaLiquidityTaker(Common::Logger *logger, 
                         Trading::TradeEngine *trade_engine, 
                         const Trading::FeatureEngine *feature_engine,
                         Trading::OrderManager *order_manager,
                         const Common::TradeEngineCfgHashMap &ticker_cfg,
                         const Config &zerodha_config,
                         const Adapter::Zerodha::ZerodhaMarketDataAdapter *market_data_adapter,
                         Adapter::Zerodha::ZerodhaOrderGatewayAdapter *order_gateway_adapter,
                         ::Exchange::ClientRequestLFQueue* client_requests = nullptr,
                         Common::ClientId client_id = 1);

    /**
     * @brief Process order book updates
     * 
     * @param ticker_id Ticker ID
     * @param price Price
     * @param side Side
     * @param book Pointer to market order book
     */
    auto onOrderBookUpdate(Common::TickerId ticker_id, Common::Price price, 
                          Common::Side side, Trading::MarketOrderBook *book) noexcept -> void;

    /**
     * @brief Process trade updates
     * 
     * @param market_update Pointer to market update
     * @param book Pointer to market order book
     */
    auto onTradeUpdate(const ::Exchange::MEMarketUpdate *market_update, 
                      Trading::MarketOrderBook *book) noexcept -> void;

    /**
     * @brief Process order updates
     * 
     * @param client_response Pointer to client response
     */
    auto onOrderUpdate(const ::Exchange::MEClientResponse *client_response) noexcept -> void;

private:
    /**
     * @brief Check if current time is within trading hours
     * 
     * @return true if within trading hours, false otherwise
     */
    bool isWithinTradingHours() const;

    /**
     * @brief Check if price is within circuit limits
     * 
     * @param ticker_id Ticker ID
     * @param price Price to check
     * @param side Side (BUY or SELL)
     * @return true if within circuit limits, false otherwise
     */
    bool isWithinCircuitLimits(Common::TickerId ticker_id, Common::Price price, Common::Side side) const;

    /**
     * @brief Adjust order quantity based on lot size
     * 
     * @param ticker_id Ticker ID
     * @param qty Quantity
     * @return Adjusted quantity
     */
    size_t adjustQuantityToLotSize(Common::TickerId ticker_id, size_t qty) const;

    /**
     * @brief Check VWAP-based entry conditions
     * 
     * @param ticker_id Ticker ID
     * @param price Entry price
     * @param side Side (BUY or SELL)
     * @return true if VWAP condition is met, false otherwise
     */
    bool checkVwapCondition(Common::TickerId ticker_id, Common::Price price, Common::Side side) const;

    /**
     * @brief Check volume profile conditions
     * 
     * @param ticker_id Ticker ID
     * @return true if volume is sufficient, false otherwise
     */
    bool checkVolumeCondition(Common::TickerId ticker_id) const;

    /**
     * @brief Update VWAP calculation for a ticker
     * 
     * @param ticker_id Ticker ID
     * @param book Market order book
     */
    void updateVWAP(Common::TickerId ticker_id, Trading::MarketOrderBook *book);
    
    /**
     * @brief Update volume percentile calculation for a ticker
     * 
     * @param ticker_id Ticker ID
     * @param book Market order book
     */
    void updateVolumePercentile(Common::TickerId ticker_id, Trading::MarketOrderBook *book);
    
    /**
     * @brief Update circuit limits for all tickers
     */
    void updateCircuitLimits();
    
    /**
     * @brief Send a bracket order
     * 
     * Places an entry order with automatic stop loss and target price orders.
     * The stop loss and target orders are activated only when the entry order is filled.
     * 
     * @param ticker_id Ticker ID
     * @param side Order side (BUY or SELL)
     * @param entry_price Entry price
     * @param quantity Order quantity
     * @param stop_loss_price Stop loss price
     * @param target_price Target price
     * @return Order ID of the entry order
     */
    Common::OrderId sendBracketOrder(
        Common::TickerId ticker_id,
        Common::Side side,
        Common::Price entry_price,
        size_t quantity,
        Common::Price stop_loss_price,
        Common::Price target_price);
    
    /**
     * @brief Send a direct order via the order manager
     * 
     * @param ticker_id Ticker ID
     * @param side Order side (BUY or SELL)
     * @param price Order price (0 for market orders)
     * @param quantity Order quantity
     * @return Order ID
     */
    Common::OrderId sendDirectOrder(
        Common::TickerId ticker_id,
        Common::Side side,
        Common::Price price,
        size_t quantity);
    
    /**
     * @brief Send a market order
     * 
     * @param ticker_id Ticker ID
     * @param side Order side (BUY or SELL)
     * @param quantity Order quantity
     * @return Order ID
     */
    Common::OrderId sendMarketOrder(
        Common::TickerId ticker_id,
        Common::Side side,
        size_t quantity);
    
    /**
     * @brief Handle order fill for bracket orders
     * 
     * @param client_response Client response with fill details
     */
    void handleBracketOrderFill(const ::Exchange::MEClientResponse *client_response);
    
    /**
     * @brief Create a new bracket order
     * 
     * @param ticker_id Ticker ID
     * @param side Order side (BUY or SELL)
     * @param entry_price Entry price
     * @param quantity Order quantity
     * @param stop_loss_price Stop loss price
     * @param target_price Target price
     * @param entry_order_id Entry order ID
     * @return BracketOrder structure
     */
    BracketOrder createBracketOrder(
        Common::TickerId ticker_id,
        Common::Side side,
        Common::Price entry_price,
        size_t quantity,
        Common::Price stop_loss_price,
        Common::Price target_price,
        Common::OrderId entry_order_id);
    
    /**
     * @brief Generate a unique order ID
     * 
     * @return Unique order ID
     */
    Common::OrderId generateOrderId();

    // The encapsulated LiquidityTaker object for delegation
    std::unique_ptr<Trading::LiquidityTaker> liquidity_taker_;
    
    // Underlying components
    Common::Logger *logger_ = nullptr;
    Trading::OrderManager *order_manager_ = nullptr;
    const Trading::FeatureEngine *feature_engine_ = nullptr;
    const Common::TradeEngineCfgHashMap &ticker_cfg_;

    // Zerodha-specific configuration
    const Config zerodha_config_;

    // Zerodha-specific adapters
    const Adapter::Zerodha::ZerodhaMarketDataAdapter *market_data_adapter_ = nullptr;
    Adapter::Zerodha::ZerodhaOrderGatewayAdapter *order_gateway_adapter_ = nullptr;

    // Time-related variables
    std::string time_str_;
    uint64_t trading_start_time_ns_;
    uint64_t trading_end_time_ns_;
    
    // VWAP cache for each ticker
    std::unordered_map<Common::TickerId, double> vwap_cache_;
    
    // Volume percentile cache for each ticker
    std::unordered_map<Common::TickerId, int> volume_percentile_cache_;
    
    // Circuit limits cache for each ticker
    std::unordered_map<Common::TickerId, CircuitLimits> circuit_limits_cache_;
    
    // Lot size cache for each ticker
    std::unordered_map<Common::TickerId, size_t> lot_sizes_;
    
    // Track bracket orders
    std::unordered_map<Common::OrderId, BracketOrder> active_bracket_orders_;
    std::mutex bracket_orders_mutex_;
    
    // Order ID generation
    std::atomic<Common::OrderId> next_order_id_{1000000};
    
    // Client ID for order identification
    Common::ClientId client_id_ = 1;
    
    // Outgoing request queue pointer for direct order access
    ::Exchange::ClientRequestLFQueue* outgoing_requests_ = nullptr;
};

} // namespace Zerodha
} // namespace Adapter