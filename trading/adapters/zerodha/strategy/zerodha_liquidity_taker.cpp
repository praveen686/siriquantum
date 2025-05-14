#include "zerodha_liquidity_taker.h"
#include "trading/strategy/trade_engine.h"
#include "common/time_utils.h"

#include <chrono>
#include <iomanip>
#include <sstream>
#include <cmath>

namespace Adapter {
namespace Zerodha {

ZerodhaLiquidityTaker::ZerodhaLiquidityTaker(
    Common::Logger *logger, 
    Trading::TradeEngine *trade_engine, 
    const Trading::FeatureEngine *feature_engine,
    Trading::OrderManager *order_manager,
    const Common::TradeEngineCfgHashMap &ticker_cfg,
    const Config &zerodha_config,
    const Adapter::Zerodha::ZerodhaMarketDataAdapter *market_data_adapter,
    Adapter::Zerodha::ZerodhaOrderGatewayAdapter *order_gateway_adapter,
    ::Exchange::ClientRequestLFQueue* client_requests,
    Common::ClientId client_id)
    : logger_(logger),
      order_manager_(order_manager),
      feature_engine_(feature_engine),
      ticker_cfg_(ticker_cfg),
      zerodha_config_(zerodha_config),
      market_data_adapter_(market_data_adapter),
      order_gateway_adapter_(order_gateway_adapter),
      client_id_(client_id),
      outgoing_requests_(client_requests) {
    
    // Create the encapsulated LiquidityTaker instance
    liquidity_taker_ = std::make_unique<Trading::LiquidityTaker>(
        logger, trade_engine, feature_engine, order_manager, ticker_cfg);
    
    // If client_requests is not provided, get it from the trade engine
    if (!outgoing_requests_) {
        // We'll need to access the client requests queue for direct order placement
        // This is typically provided by the trade engine, but for now we'll fall back to
        // using the order manager's methods
        logger_->log("%:% %() % No client requests queue provided, using order manager for routing\n", 
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_));
    }
    
    // Override the base class callbacks with our own
    trade_engine->algoOnOrderBookUpdate_ = [this](auto ticker_id, auto price, auto side, auto book) {
        onOrderBookUpdate(ticker_id, price, side, book);
    };
    trade_engine->algoOnTradeUpdate_ = [this](auto market_update, auto book) { 
        onTradeUpdate(market_update, book); 
    };
    trade_engine->algoOnOrderUpdate_ = [this](auto client_response) { 
        onOrderUpdate(client_response); 
    };

    // Parse trading hours strings to nanoseconds since midnight
    std::tm start_tm = {};
    std::tm end_tm = {};
    std::istringstream start_ss(zerodha_config_.trading_start_time);
    std::istringstream end_ss(zerodha_config_.trading_end_time);
    
    start_ss >> std::get_time(&start_tm, "%H:%M:%S");
    end_ss >> std::get_time(&end_tm, "%H:%M:%S");
    
    trading_start_time_ns_ = (start_tm.tm_hour * 3600 + start_tm.tm_min * 60 + start_tm.tm_sec) * 1'000'000'000ULL;
    trading_end_time_ns_ = (end_tm.tm_hour * 3600 + end_tm.tm_min * 60 + end_tm.tm_sec) * 1'000'000'000ULL;
    
    logger_->log("%:% %() % Zerodha Liquidity Taker initialized with trading hours %-%\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                zerodha_config_.trading_start_time.c_str(),
                zerodha_config_.trading_end_time.c_str());

    // Initialize circuit limits if we have market data adapter
    if (market_data_adapter_) {
        updateCircuitLimits();
    }
}

auto ZerodhaLiquidityTaker::onOrderBookUpdate(
    Common::TickerId ticker_id, 
    Common::Price price, 
    Common::Side side, 
    Trading::MarketOrderBook *book) noexcept -> void {
    
    // First, delegate to the encapsulated LiquidityTaker
    liquidity_taker_->onOrderBookUpdate(ticker_id, price, side, book);
    
    logger_->log("%:% %() % ticker:% price:% side:%\n", 
                __FILE__, __LINE__, __FUNCTION__,
                Common::getCurrentTimeStr(&time_str_), ticker_id, 
                Common::priceToString(price).c_str(),
                Common::sideToString(side).c_str());
                
    // Update VWAP calculation on order book update
    updateVWAP(ticker_id, book);
    
    // Update volume percentile on order book update
    updateVolumePercentile(ticker_id, book);
}

auto ZerodhaLiquidityTaker::onTradeUpdate(
    const ::Exchange::MEMarketUpdate *market_update, 
    Trading::MarketOrderBook *book) noexcept -> void {
    
    logger_->log("%:% %() % %\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                market_update->toString().c_str());

    // Check trading hours restriction if enabled
    if (zerodha_config_.enforce_trading_hours && !isWithinTradingHours()) {
        logger_->log("%:% %() % Outside trading hours - not taking action\n", 
                    __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_));
        return;
    }

    // Get BBO and aggressive trade ratio from base strategy
    const auto bbo = book->getBBO();
    const auto agg_qty_ratio = feature_engine_->getAggTradeQtyRatio();

    if (LIKELY(bbo->bid_price_ != Common::Price_INVALID && 
               bbo->ask_price_ != Common::Price_INVALID && 
               agg_qty_ratio != Trading::Feature_INVALID)) {
        
        logger_->log("%:% %() % % agg-qty-ratio:%\n", 
                    __FILE__, __LINE__, __FUNCTION__,
                    Common::getCurrentTimeStr(&time_str_),
                    bbo->toString().c_str(), agg_qty_ratio);

        const auto clip = ticker_cfg_.at(market_update->ticker_id_).clip_;
        const auto threshold = ticker_cfg_.at(market_update->ticker_id_).threshold_;

        // Check if aggressive ratio exceeds threshold
        if (agg_qty_ratio >= threshold) {
            // Check additional Zerodha-specific conditions
            bool proceed = true;

            // Check circuit limits if enabled
            Common::Price entry_price = Common::Price_INVALID;
            Common::Side entry_side = Common::Side::INVALID;
            
            if (market_update->side_ == Common::Side::BUY) {
                entry_price = bbo->ask_price_;
                entry_side = Common::Side::BUY;
            } else {
                entry_price = bbo->bid_price_;
                entry_side = Common::Side::SELL;
            }

            // Enforce circuit limits if configured
            if (zerodha_config_.enforce_circuit_limits && 
                !isWithinCircuitLimits(market_update->ticker_id_, entry_price, entry_side)) {
                logger_->log("%:% %() % Circuit limit violation - not taking action\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                proceed = false;
            }

            // Check VWAP condition if configured
            if (proceed && zerodha_config_.use_vwap_filter && 
                !checkVwapCondition(market_update->ticker_id_, entry_price, entry_side)) {
                logger_->log("%:% %() % VWAP condition not met - not taking action\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                proceed = false;
            }

            // Check volume condition
            if (proceed && !checkVolumeCondition(market_update->ticker_id_)) {
                logger_->log("%:% %() % Volume condition not met - not taking action\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                proceed = false;
            }

            // Adjust order quantity for lot size
            size_t adjusted_clip = adjustQuantityToLotSize(market_update->ticker_id_, clip);
            
            // Send order if all conditions met
            if (proceed) {
                if (market_update->side_ == Common::Side::BUY) {
                    // Calculate stop loss and target prices
                    Common::Price stop_loss_price = static_cast<Common::Price>(
                        bbo->ask_price_ * (1.0 - zerodha_config_.stop_loss_percent / 100.0));
                    Common::Price target_price = static_cast<Common::Price>(
                        bbo->ask_price_ * (1.0 + zerodha_config_.target_percent / 100.0));
                    
                    // Place the order (bracket or regular)
                    if (zerodha_config_.use_bracket_orders) {
                        // Place a bracket order
                        Common::OrderId order_id = sendBracketOrder(
                            market_update->ticker_id_, 
                            Common::Side::BUY, 
                            bbo->ask_price_, 
                            adjusted_clip,
                            stop_loss_price, 
                            target_price);
                        
                        logger_->log("%:% %() % Placed bracket order %: BUY at % with SL % and target %\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_),
                                   order_id,
                                   Common::priceToString(bbo->ask_price_).c_str(),
                                   Common::priceToString(stop_loss_price).c_str(),
                                   Common::priceToString(target_price).c_str());
                    } else {
                        // Place a regular order
                        Common::OrderId order_id = sendDirectOrder(
                            market_update->ticker_id_,
                            Common::Side::BUY,
                            bbo->ask_price_,
                            adjusted_clip);
                        
                        logger_->log("%:% %() % Placed direct order %: BUY at %\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_),
                                   order_id,
                                   Common::priceToString(bbo->ask_price_).c_str());
                    }
                } else {
                    // Calculate stop loss and target prices
                    Common::Price stop_loss_price = static_cast<Common::Price>(
                        bbo->bid_price_ * (1.0 + zerodha_config_.stop_loss_percent / 100.0));
                    Common::Price target_price = static_cast<Common::Price>(
                        bbo->bid_price_ * (1.0 - zerodha_config_.target_percent / 100.0));
                    
                    // Place the order (bracket or regular)
                    if (zerodha_config_.use_bracket_orders) {
                        // Place a bracket order
                        Common::OrderId order_id = sendBracketOrder(
                            market_update->ticker_id_, 
                            Common::Side::SELL, 
                            bbo->bid_price_, 
                            adjusted_clip,
                            stop_loss_price, 
                            target_price);
                        
                        logger_->log("%:% %() % Placed bracket order %: SELL at % with SL % and target %\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_),
                                   order_id,
                                   Common::priceToString(bbo->bid_price_).c_str(),
                                   Common::priceToString(stop_loss_price).c_str(),
                                   Common::priceToString(target_price).c_str());
                    } else {
                        // Place a regular order
                        Common::OrderId order_id = sendDirectOrder(
                            market_update->ticker_id_,
                            Common::Side::SELL,
                            bbo->bid_price_,
                            adjusted_clip);
                        
                        logger_->log("%:% %() % Placed direct order %: SELL at %\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_),
                                   order_id,
                                   Common::priceToString(bbo->bid_price_).c_str());
                    }
                }
            }
        }
    }
}

auto ZerodhaLiquidityTaker::onOrderUpdate(
    const ::Exchange::MEClientResponse *client_response) noexcept -> void {
    
    // First, delegate to the encapsulated LiquidityTaker
    liquidity_taker_->onOrderUpdate(client_response);
    
    logger_->log("%:% %() % %\n", 
                __FILE__, __LINE__, __FUNCTION__, 
                Common::getCurrentTimeStr(&time_str_),
                client_response->toString().c_str());
    
    // Check if this is a fill for a bracket order
    if (client_response->type_ == ::Exchange::ClientResponseType::FILLED ||
        client_response->type_ == ::Exchange::ClientResponseType::PARTIALLY_FILLED) {
        handleBracketOrderFill(client_response);
    }
    
    // Process Zerodha-specific order responses
    // Handle specific rejection reasons from Zerodha
    if (client_response->type_ == ::Exchange::ClientResponseType::REJECTED) {
        logger_->log("%:% %() % Order rejected, reason: %\n", 
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   ::Exchange::clientResponseRejectReasonToString(client_response->reject_reason_));
        
        // Based on rejection reason, we could take different actions
        switch (client_response->reject_reason_) {
            case ::Exchange::ClientResponseRejectReason::INVALID_PRICE:
                // Update our circuit limit cache
                updateCircuitLimits();
                
                logger_->log("%:% %() % Invalid price rejection detected, updated circuit limits\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                break;
                
            case ::Exchange::ClientResponseRejectReason::RISK_REJECT:
                // Handle insufficient funds rejection
                logger_->log("%:% %() % Risk rejection detected, possibly insufficient funds\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                break;
                
            default:
                logger_->log("%:% %() % Unknown rejection reason\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_));
                break;
        }
    }
}

bool ZerodhaLiquidityTaker::isWithinTradingHours() const {
    const auto current_time = Common::getCurrentNanos();
    const auto current_time_of_day = current_time % (24ULL * 3600ULL * 1'000'000'000ULL);
    
    return (current_time_of_day >= trading_start_time_ns_ && 
            current_time_of_day <= trading_end_time_ns_);
}

bool ZerodhaLiquidityTaker::isWithinCircuitLimits(
    Common::TickerId ticker_id, 
    Common::Price price, 
    Common::Side side) const {
    
    // Check our circuit limits cache
    auto it = circuit_limits_cache_.find(ticker_id);
    if (it != circuit_limits_cache_.end()) {
        const auto& limits = it->second;
        
        if (side == Common::Side::BUY) {
            // When buying, check if our price exceeds the upper circuit
            return price <= limits.upper_circuit_price;
        } else {
            // When selling, check if our price is below the lower circuit
            return price >= limits.lower_circuit_price;
        }
    }
    
    // If we don't have circuit limits cached, default to true
    // In a production system, we should be more conservative
    return true;
}

size_t ZerodhaLiquidityTaker::adjustQuantityToLotSize(
    Common::TickerId ticker_id, 
    size_t qty) const {
    
    // Check for cached lot size
    auto it = lot_sizes_.find(ticker_id);
    if (it != lot_sizes_.end()) {
        size_t lot_size = it->second;
        
        // Round down to the nearest multiple of lot size
        size_t adjusted_qty = (qty / lot_size) * lot_size;
        
        // Ensure at least one lot
        return std::max(lot_size, adjusted_qty);
    }
    
    // If we don't know the lot size, return the original quantity
    // In a production system, we would query this from the exchange
    return qty;
}

bool ZerodhaLiquidityTaker::checkVwapCondition(
    Common::TickerId ticker_id, 
    Common::Price price, 
    Common::Side side) const {
    
    // Check our VWAP cache
    auto it = vwap_cache_.find(ticker_id);
    if (it != vwap_cache_.end()) {
        double vwap = it->second;
        
        // VWAP filter logic:
        // For BUY orders: Only buy if price is not too far above VWAP (prevents buying tops)
        // For SELL orders: Only sell if price is not too far below VWAP (prevents selling bottoms)
        
        double price_vwap_ratio = static_cast<double>(price) / vwap;
        
        if (side == Common::Side::BUY) {
            // Don't buy if price is more than threshold% above VWAP
            return price_vwap_ratio <= (1.0 + zerodha_config_.vwap_threshold);
        } else {
            // Don't sell if price is more than threshold% below VWAP
            return price_vwap_ratio >= (1.0 - zerodha_config_.vwap_threshold);
        }
    }
    
    // If we don't have VWAP data, default to true to allow the trade
    // In a production system, we should be more conservative
    return true;
}

bool ZerodhaLiquidityTaker::checkVolumeCondition(
    Common::TickerId ticker_id) const {
    
    // Check our volume percentile cache
    auto it = volume_percentile_cache_.find(ticker_id);
    if (it != volume_percentile_cache_.end()) {
        int volume_percentile = it->second;
        
        // Only trade instruments with sufficient volume
        return volume_percentile >= zerodha_config_.min_volume_percentile;
    }
    
    // If we don't have volume data, default to true to allow the trade
    // In a production system, we should be more conservative
    return true;
}

void ZerodhaLiquidityTaker::updateVWAP(
    Common::TickerId ticker_id, 
    Trading::MarketOrderBook *book) {
    
    if (!book) {
        return;
    }
    
    // VWAP calculation requires trade data, but we can approximate using mid prices
    // This is a simplified calculation; a real implementation would track actual trades
    
    double total_value = 0.0;
    double total_volume = 0.0;
    
    // Get BBO
    const auto bbo = book->getBBO();
    
    if (bbo->bid_price_ != Common::Price_INVALID && bbo->ask_price_ != Common::Price_INVALID) {
        // Calculate mid price
        double mid_price = (static_cast<double>(bbo->bid_price_) + static_cast<double>(bbo->ask_price_)) / 2.0;
        
        // Use current visible volume as weight
        double volume = static_cast<double>(bbo->bid_qty_ + bbo->ask_qty_);
        
        // Update running totals
        total_value += mid_price * volume;
        total_volume += volume;
        
        if (total_volume > 0) {
            // Calculate VWAP
            double vwap = total_value / total_volume;
            
            // Update cache
            vwap_cache_[ticker_id] = vwap;
            
            logger_->log("%:% %() % Updated VWAP for ticker %: %\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, vwap);
        }
    }
}

void ZerodhaLiquidityTaker::updateVolumePercentile(
    Common::TickerId ticker_id, 
    Trading::MarketOrderBook *book) {
    
    if (!book) {
        return;
    }
    
    // This is a simplified calculation; a real implementation would compare
    // current volume with historical volume distribution
    
    // For now, use a simple heuristic based on visible liquidity
    const auto bbo = book->getBBO();
    
    if (bbo->bid_price_ != Common::Price_INVALID && bbo->ask_price_ != Common::Price_INVALID) {
        // Total visible volume
        int total_visible_volume = bbo->bid_qty_ + bbo->ask_qty_;
        
        // Use a simple scaling to estimate percentile
        // In a real system, we would compare with historical distribution
        int estimated_percentile = 0;
        
        if (total_visible_volume > 10000) {
            estimated_percentile = 90;  // Very high volume
        } else if (total_visible_volume > 5000) {
            estimated_percentile = 75;  // High volume
        } else if (total_visible_volume > 1000) {
            estimated_percentile = 50;  // Medium volume
        } else if (total_visible_volume > 500) {
            estimated_percentile = 25;  // Low volume
        } else {
            estimated_percentile = 10;  // Very low volume
        }
        
        // Update cache
        volume_percentile_cache_[ticker_id] = estimated_percentile;
        
        logger_->log("%:% %() % Updated volume percentile for ticker %: %\n", 
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   ticker_id, estimated_percentile);
    }
}

void ZerodhaLiquidityTaker::updateCircuitLimits() {
    // For each instrument, get its circuit limits
    // In a real implementation, we would query this from the exchange API
    
    for (size_t i = 0; i < Common::ME_MAX_TICKERS; ++i) {
        Common::TickerId ticker_id = i;
        
        // Get current price
        double current_price = 0.0;
        
        // Get order book from market data adapter
        const ZerodhaOrderBook* order_book = nullptr;
        if (market_data_adapter_) {
            order_book = market_data_adapter_->getOrderBook(ticker_id);
        }
        
        if (order_book) {
            // Get BBO
            const auto& bbo = order_book->getBBO();
            
            // Use mid price as current price
            if (bbo.bid_price > 0 && bbo.ask_price < std::numeric_limits<double>::max()) {
                current_price = (bbo.bid_price + bbo.ask_price) / 2.0;
            }
        }
        
        if (current_price > 0) {
            // For Indian equities, circuit limits are typically ±10% for most stocks
            // For F&O, they could be ±20% or have no limit
            // Here we use a simple approximation
            
            double circuit_percentage = 0.10;  // 10% default
            
            // For index futures, use wider limits based on ticker_id
            // Assuming ticker_id 1001=NIFTY, 1002=BANKNIFTY based on config
            if (ticker_id == 1001 || ticker_id == 1002) {
                circuit_percentage = 0.20;  // 20% for indices
            }
            
            // Calculate and cache circuit limits
            CircuitLimits limits;
            limits.upper_circuit_price = current_price * (1.0 + circuit_percentage);
            limits.lower_circuit_price = current_price * (1.0 - circuit_percentage);
            limits.last_updated = Common::getCurrentNanos();
            
            circuit_limits_cache_[ticker_id] = limits;
            
            logger_->log("%:% %() % Updated circuit limits for ticker %: [%, %]\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, 
                       limits.lower_circuit_price, 
                       limits.upper_circuit_price);
            
            // Also initialize lot size for this instrument
            // In a real implementation, we would query this from the exchange API
            
            // Default lot size is 1 for equities
            size_t lot_size = 1;
            
            // Set lot sizes based on ticker_id
            if (ticker_id == 1001) {
                lot_size = 50;  // NIFTY typically has lot size of 50
            } else if (ticker_id == 1002) {
                lot_size = 25;  // BANKNIFTY typically has lot size of 25
            } else if (ticker_id >= 2000) {
                lot_size = 25;  // Other futures usually have lot sizes
            }
            
            lot_sizes_[ticker_id] = lot_size;
            
            logger_->log("%:% %() % Set lot size for ticker %: %\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, lot_size);
        } else {
            logger_->log("%:% %() % Could not update circuit limits for ticker %: no price available\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id);
        }
    }
}

Common::OrderId ZerodhaLiquidityTaker::sendBracketOrder(
    Common::TickerId ticker_id,
    Common::Side side,
    Common::Price entry_price,
    size_t quantity,
    Common::Price stop_loss_price,
    Common::Price target_price) {
    
    // Generate a unique order ID for the entry order
    Common::OrderId entry_order_id = generateOrderId();
    
    // Create the bracket order structure
    BracketOrder bracket_order = createBracketOrder(
        ticker_id,
        side,
        entry_price,
        quantity,
        stop_loss_price,
        target_price,
        entry_order_id
    );
    
    // Lock the bracket orders map
    std::lock_guard<std::mutex> lock(bracket_orders_mutex_);
    
    // Store the bracket order
    active_bracket_orders_[entry_order_id] = bracket_order;
    
    // Now place the entry order
    if (outgoing_requests_) {
        // We have direct queue access, use it
        ::Exchange::MEClientRequest request;
        request.client_id_ = client_id_;
        request.order_id_ = entry_order_id;
        request.ticker_id_ = ticker_id;
        request.type_ = ::Exchange::ClientRequestType::NEW;
        request.side_ = side;
        request.price_ = entry_price;
        request.qty_ = quantity;
        
        // Get the next request to write to
        auto next_write = outgoing_requests_->getNextToWriteTo();
        if (next_write) {
            *next_write = request;
            outgoing_requests_->updateWriteIndex();
            
            logger_->log("%:% %() % Sent bracket order entry for ticker %: % % @ %\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, 
                       side == Common::Side::BUY ? "BUY" : "SELL", 
                       quantity, 
                       Common::priceToString(entry_price).c_str());
        } else {
            logger_->log("%:% %() % Failed to send bracket order entry: queue full\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_));
        }
    } else {
        // Use the order manager instead
        if (side == Common::Side::BUY) {
            order_manager_->moveOrders(ticker_id, entry_price, Common::Price_INVALID, quantity);
            logger_->log("%:% %() % Sent bracket order entry via order manager for ticker %: BUY % @ %\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, quantity, Common::priceToString(entry_price).c_str());
        } else {
            order_manager_->moveOrders(ticker_id, Common::Price_INVALID, entry_price, quantity);
            logger_->log("%:% %() % Sent bracket order entry via order manager for ticker %: SELL % @ %\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, quantity, Common::priceToString(entry_price).c_str());
        }
    }
    
    return entry_order_id;
}

Common::OrderId ZerodhaLiquidityTaker::sendDirectOrder(
    Common::TickerId ticker_id,
    Common::Side side,
    Common::Price price,
    size_t quantity) {
    
    // Generate a unique order ID
    Common::OrderId order_id = generateOrderId();
    
    // Place a direct order via the order manager
    if (side == Common::Side::BUY) {
        order_manager_->moveOrders(ticker_id, price, Common::Price_INVALID, quantity);
    } else {
        order_manager_->moveOrders(ticker_id, Common::Price_INVALID, price, quantity);
    }
    
    return order_id;
}

Common::OrderId ZerodhaLiquidityTaker::sendMarketOrder(
    Common::TickerId ticker_id,
    Common::Side side,
    size_t quantity) {
    
    // Generate a unique order ID
    Common::OrderId order_id = generateOrderId();
    
    if (outgoing_requests_) {
        // Create a market order request
        ::Exchange::MEClientRequest request;
        request.client_id_ = client_id_;
        request.order_id_ = order_id;
        request.ticker_id_ = ticker_id;
        request.type_ = ::Exchange::ClientRequestType::NEW;
        request.side_ = side;
        request.price_ = 0;  // Market order
        request.qty_ = quantity;
        
        // Get the next request to write to
        auto next_write = outgoing_requests_->getNextToWriteTo();
        if (next_write) {
            *next_write = request;
            outgoing_requests_->updateWriteIndex();
            
            logger_->log("%:% %() % Sent market order for ticker %: % % @ Market\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       ticker_id, side == Common::Side::BUY ? "BUY" : "SELL", quantity);
        } else {
            logger_->log("%:% %() % Failed to send market order: queue full\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_));
        }
    } else {
        // Use the order manager
        if (side == Common::Side::BUY) {
            order_manager_->moveOrders(ticker_id, 0, Common::Price_INVALID, quantity); // 0 for market order
        } else {
            order_manager_->moveOrders(ticker_id, Common::Price_INVALID, 0, quantity); // 0 for market order
        }
        
        logger_->log("%:% %() % Sent market order via order manager for ticker %: % % @ Market\n", 
                   __FILE__, __LINE__, __FUNCTION__,
                   Common::getCurrentTimeStr(&time_str_),
                   ticker_id, side == Common::Side::BUY ? "BUY" : "SELL", quantity);
    }
    
    return order_id;
}

void ZerodhaLiquidityTaker::handleBracketOrderFill(
    const ::Exchange::MEClientResponse *client_response) {
    
    // Check if this is a fill for an entry order of a bracket order
    std::lock_guard<std::mutex> lock(bracket_orders_mutex_);
    
    auto it = active_bracket_orders_.find(client_response->order_id_);
    if (it != active_bracket_orders_.end()) {
        // This is a fill for an entry order of a bracket order
        BracketOrder& bracket_order = it->second;
        
        // Check if this is the first fill for this bracket order
        if (!bracket_order.entry_filled) {
            // Mark the entry order as filled
            bracket_order.entry_filled = true;
            bracket_order.fill_time = Common::getCurrentNanos();
            
            logger_->log("%:% %() % Bracket order entry filled: %\n", 
                       __FILE__, __LINE__, __FUNCTION__,
                       Common::getCurrentTimeStr(&time_str_),
                       client_response->order_id_);
            
            // Now place the contingent stop loss and target orders
            // Generate unique order IDs for stop loss and target orders
            bracket_order.stop_loss_order_id = generateOrderId();
            bracket_order.target_order_id = generateOrderId();
            
            // Direction for the exit orders (opposite of entry)
            Common::Side exit_side = bracket_order.side == Common::Side::BUY ? Common::Side::SELL : Common::Side::BUY;
            
            if (outgoing_requests_) {
                // Place stop loss order
                ::Exchange::MEClientRequest sl_request;
                sl_request.client_id_ = client_id_;
                sl_request.order_id_ = bracket_order.stop_loss_order_id;
                sl_request.ticker_id_ = bracket_order.ticker_id;
                sl_request.type_ = ::Exchange::ClientRequestType::NEW;
                sl_request.side_ = exit_side;
                sl_request.price_ = bracket_order.stop_loss_price;
                sl_request.qty_ = bracket_order.quantity;
                
                // Place target order
                ::Exchange::MEClientRequest target_request;
                target_request.client_id_ = client_id_;
                target_request.order_id_ = bracket_order.target_order_id;
                target_request.ticker_id_ = bracket_order.ticker_id;
                target_request.type_ = ::Exchange::ClientRequestType::NEW;
                target_request.side_ = exit_side;
                target_request.price_ = bracket_order.target_price;
                target_request.qty_ = bracket_order.quantity;
                
                // Get the next request to write to for stop loss
                auto sl_next_write = outgoing_requests_->getNextToWriteTo();
                if (sl_next_write) {
                    *sl_next_write = sl_request;
                    outgoing_requests_->updateWriteIndex();
                    
                    logger_->log("%:% %() % Placed stop loss order % for bracket order %: % % @ %\n", 
                               __FILE__, __LINE__, __FUNCTION__,
                               Common::getCurrentTimeStr(&time_str_),
                               bracket_order.stop_loss_order_id,
                               bracket_order.entry_order_id,
                               exit_side == Common::Side::BUY ? "BUY" : "SELL",
                               bracket_order.quantity,
                               Common::priceToString(bracket_order.stop_loss_price).c_str());
                } else {
                    logger_->log("%:% %() % Failed to place stop loss order for bracket order %: queue full\n", 
                               __FILE__, __LINE__, __FUNCTION__,
                               Common::getCurrentTimeStr(&time_str_),
                               bracket_order.entry_order_id);
                }
                
                // Get the next request to write to for target
                auto target_next_write = outgoing_requests_->getNextToWriteTo();
                if (target_next_write) {
                    *target_next_write = target_request;
                    outgoing_requests_->updateWriteIndex();
                    
                    logger_->log("%:% %() % Placed target order % for bracket order %: % % @ %\n", 
                               __FILE__, __LINE__, __FUNCTION__,
                               Common::getCurrentTimeStr(&time_str_),
                               bracket_order.target_order_id,
                               bracket_order.entry_order_id,
                               exit_side == Common::Side::BUY ? "BUY" : "SELL",
                               bracket_order.quantity,
                               Common::priceToString(bracket_order.target_price).c_str());
                } else {
                    logger_->log("%:% %() % Failed to place target order for bracket order %: queue full\n", 
                               __FILE__, __LINE__, __FUNCTION__,
                               Common::getCurrentTimeStr(&time_str_),
                               bracket_order.entry_order_id);
                }
            } else {
                // Use order manager for both orders
                
                // Place stop loss order with order manager
                if (exit_side == Common::Side::BUY) {
                    order_manager_->moveOrders(bracket_order.ticker_id, bracket_order.stop_loss_price, Common::Price_INVALID, bracket_order.quantity);
                } else {
                    order_manager_->moveOrders(bracket_order.ticker_id, Common::Price_INVALID, bracket_order.stop_loss_price, bracket_order.quantity);
                }
                
                logger_->log("%:% %() % Placed stop loss order via order manager for bracket order %: % % @ %\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           bracket_order.entry_order_id,
                           exit_side == Common::Side::BUY ? "BUY" : "SELL",
                           bracket_order.quantity,
                           Common::priceToString(bracket_order.stop_loss_price).c_str());
                
                // Place target order with order manager
                if (exit_side == Common::Side::BUY) {
                    order_manager_->moveOrders(bracket_order.ticker_id, bracket_order.target_price, Common::Price_INVALID, bracket_order.quantity);
                } else {
                    order_manager_->moveOrders(bracket_order.ticker_id, Common::Price_INVALID, bracket_order.target_price, bracket_order.quantity);
                }
                
                logger_->log("%:% %() % Placed target order via order manager for bracket order %: % % @ %\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           bracket_order.entry_order_id,
                           exit_side == Common::Side::BUY ? "BUY" : "SELL",
                           bracket_order.quantity,
                           Common::priceToString(bracket_order.target_price).c_str());
            }
        }
    } else {
        // Check if this is a fill for a stop loss or target order
        for (auto& bracket_entry : active_bracket_orders_) {
            BracketOrder& bracket_order = bracket_entry.second;
            
            if (client_response->order_id_ == bracket_order.stop_loss_order_id) {
                // This is a fill for a stop loss order
                bracket_order.stop_loss_triggered = true;
                bracket_order.exit_time = Common::getCurrentNanos();
                
                logger_->log("%:% %() % Bracket order stop loss triggered: %, exiting position\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           bracket_order.entry_order_id);
                
                // Cancel the target order
                if (bracket_order.target_order_id != 0) {
                    if (outgoing_requests_) {
                        ::Exchange::MEClientRequest cancel_request;
                        cancel_request.client_id_ = client_id_;
                        cancel_request.order_id_ = bracket_order.target_order_id;
                        cancel_request.ticker_id_ = bracket_order.ticker_id;
                        cancel_request.type_ = ::Exchange::ClientRequestType::CANCEL;
                        
                        // Get the next request to write to
                        auto next_write = outgoing_requests_->getNextToWriteTo();
                        if (next_write) {
                            *next_write = cancel_request;
                            outgoing_requests_->updateWriteIndex();
                            
                            logger_->log("%:% %() % Cancelling target order % for bracket order %\n", 
                                       __FILE__, __LINE__, __FUNCTION__,
                                       Common::getCurrentTimeStr(&time_str_),
                                       bracket_order.target_order_id,
                                       bracket_order.entry_order_id);
                        } else {
                            logger_->log("%:% %() % Failed to cancel target order for bracket order %: queue full\n", 
                                       __FILE__, __LINE__, __FUNCTION__,
                                       Common::getCurrentTimeStr(&time_str_),
                                       bracket_order.entry_order_id);
                        }
                    } else {
                        // Since we don't have direct queue access, we can't cancel directly
                        // Best approach is to place a new order in the opposite direction to neutralize
                        logger_->log("%:% %() % Cannot directly cancel target order for bracket order % without queue access\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_),
                                   bracket_order.entry_order_id);
                    }
                }
                
                break;
            } else if (client_response->order_id_ == bracket_order.target_order_id) {
                // This is a fill for a target order
                bracket_order.target_triggered = true;
                bracket_order.exit_time = Common::getCurrentNanos();
                
                logger_->log("%:% %() % Bracket order target reached: %, exiting position\n", 
                           __FILE__, __LINE__, __FUNCTION__,
                           Common::getCurrentTimeStr(&time_str_),
                           bracket_order.entry_order_id);
                
                // Cancel the stop loss order
                if (bracket_order.stop_loss_order_id != 0) {
                    if (outgoing_requests_) {
                        ::Exchange::MEClientRequest cancel_request;
                        cancel_request.client_id_ = client_id_;
                        cancel_request.order_id_ = bracket_order.stop_loss_order_id;
                        cancel_request.ticker_id_ = bracket_order.ticker_id;
                        cancel_request.type_ = ::Exchange::ClientRequestType::CANCEL;
                        
                        // Get the next request to write to
                        auto next_write = outgoing_requests_->getNextToWriteTo();
                        if (next_write) {
                            *next_write = cancel_request;
                            outgoing_requests_->updateWriteIndex();
                            
                            logger_->log("%:% %() % Cancelling stop loss order % for bracket order %\n", 
                                       __FILE__, __LINE__, __FUNCTION__,
                                       Common::getCurrentTimeStr(&time_str_),
                                       bracket_order.stop_loss_order_id,
                                       bracket_order.entry_order_id);
                        } else {
                            logger_->log("%:% %() % Failed to cancel stop loss order for bracket order %: queue full\n", 
                                       __FILE__, __LINE__, __FUNCTION__,
                                       Common::getCurrentTimeStr(&time_str_),
                                       bracket_order.entry_order_id);
                        }
                    } else {
                        // Since we don't have direct queue access, we can't cancel directly
                        // Best approach is to place a new order in the opposite direction to neutralize
                        logger_->log("%:% %() % Cannot directly cancel stop loss order for bracket order % without queue access\n", 
                                   __FILE__, __LINE__, __FUNCTION__,
                                   Common::getCurrentTimeStr(&time_str_),
                                   bracket_order.entry_order_id);
                    }
                }
                
                break;
            }
        }
    }
}

ZerodhaLiquidityTaker::BracketOrder ZerodhaLiquidityTaker::createBracketOrder(
    Common::TickerId ticker_id,
    Common::Side side,
    Common::Price entry_price,
    size_t quantity,
    Common::Price stop_loss_price,
    Common::Price target_price,
    Common::OrderId entry_order_id) {
    
    BracketOrder bracket_order;
    bracket_order.entry_order_id = entry_order_id;
    bracket_order.stop_loss_order_id = 0;  // Will be set when entry is filled
    bracket_order.target_order_id = 0;     // Will be set when entry is filled
    bracket_order.ticker_id = ticker_id;
    bracket_order.side = side;
    bracket_order.entry_price = entry_price;
    bracket_order.stop_loss_price = stop_loss_price;
    bracket_order.target_price = target_price;
    bracket_order.quantity = quantity;
    bracket_order.entry_filled = false;
    bracket_order.stop_loss_triggered = false;
    bracket_order.target_triggered = false;
    bracket_order.creation_time = Common::getCurrentNanos();
    bracket_order.fill_time = 0;
    bracket_order.exit_time = 0;
    
    return bracket_order;
}

Common::OrderId ZerodhaLiquidityTaker::generateOrderId() {
    // Generate a unique order ID from the atomic counter
    return next_order_id_++;
}

} // namespace Zerodha
} // namespace Adapter