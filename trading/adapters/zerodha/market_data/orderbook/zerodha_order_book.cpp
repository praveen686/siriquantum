#include "trading/adapters/zerodha/market_data/orderbook/zerodha_order_book.h"

// Using the ExchangeNS alias defined in the header
using namespace ExchangeNS;

namespace Adapter {
namespace Zerodha {

ZerodhaOrderBook::ZerodhaOrderBook(Common::TickerId ticker_id, Common::Logger* logger) :
    ticker_id_(ticker_id),
    last_update_time_(0),
    logger_(logger),
    update_pool_(UPDATE_POOL_SIZE) {
    
    std::string time_str;
    logger_->log("%:% %() Creating ZerodhaOrderBook for ticker_id %\n", 
               __FILE__, __LINE__, __FUNCTION__, ticker_id);
}

ZerodhaOrderBook::~ZerodhaOrderBook() {
    std::string time_str;
    logger_->log("%:% %() Destroying ZerodhaOrderBook for ticker_id %\n", 
               __FILE__, __LINE__, __FUNCTION__, ticker_id_);
}

std::vector<ExchangeNS::MEMarketUpdate*> ZerodhaOrderBook::processMarketUpdate(
    const MarketUpdate& update) {
    
    // Store previous price levels for detecting disappeared levels
    prev_bid_prices_.clear();
    prev_ask_prices_.clear();
    
    // Store existing price levels
    for (const auto& [price, level] : bids_) {
        prev_bid_prices_.insert(price);
    }
    
    for (const auto& [price, level] : asks_) {
        prev_ask_prices_.insert(price);
    }
    
    // Generate market events from the update
    auto events = generateMarketEvents(update);
    
    // Update best bid/offer cache
    updateBBO();
    
    // Update last update time
    last_update_time_ = static_cast<uint64_t>(Common::getCurrentNanos());
    
    return events;
}

std::vector<ExchangeNS::MEMarketUpdate*> ZerodhaOrderBook::generateMarketEvents(
    const MarketUpdate& current_update) {
    
    std::vector<ExchangeNS::MEMarketUpdate*> events;
    
    // Only process if this is a FULL update with market depth
    if (current_update.type != MarketUpdateType::FULL) {
        return events;
    }
    
    // Process BID side (all 5 levels)
    for (size_t i = 0; i < MAX_DEPTH_LEVELS; i++) {
        auto& bid = current_update.bids[i];
        double price = bid.price / 100.0; // Convert paise to rupees
        
        // Skip empty levels
        if (bid.price <= 0 || bid.quantity <= 0) {
            continue;
        }
        
        // Check if this is a new price level or qty changed
        auto it = bids_.find(price);
        if (it == bids_.end()) {
            // New price level - generate ADD
            auto* event = update_pool_.allocate();
            event->type_ = ExchangeNS::MarketUpdateType::ADD;
            event->ticker_id_ = ticker_id_;
            event->side_ = Common::Side::BUY;
            event->price_ = price;
            event->qty_ = bid.quantity;
            event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::BUY);
            events.push_back(event);
            
            // Add to local book
            PriceLevel level{price, bid.quantity, bid.orders, static_cast<uint64_t>(Common::getCurrentNanos())};
            bids_[price] = level;
            
            std::string time_str;
            logger_->log("%:% %() ADD BID % x % [% orders]\n", 
                       __FILE__, __LINE__, __FUNCTION__, price, bid.quantity, bid.orders);
        } 
        else if (it->second.quantity != bid.quantity) {
            // Quantity changed - generate MODIFY
            auto* event = update_pool_.allocate();
            event->type_ = ExchangeNS::MarketUpdateType::MODIFY;
            event->ticker_id_ = ticker_id_;
            event->side_ = Common::Side::BUY;
            event->price_ = price;
            event->qty_ = bid.quantity;
            event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::BUY);
            events.push_back(event);
            
            // Update local book
            it->second.quantity = bid.quantity;
            it->second.orders = bid.orders;
            it->second.last_update_time = static_cast<uint64_t>(Common::getCurrentNanos());
            
            std::string time_str;
            logger_->log("%:% %() MODIFY BID % x % [% orders]\n", 
                       __FILE__, __LINE__, __FUNCTION__, price, bid.quantity, bid.orders);
        }
        
        // Mark as processed by removing from prev set
        prev_bid_prices_.erase(price);
    }
    
    // Process ASK side (all 5 levels)
    for (size_t i = 0; i < MAX_DEPTH_LEVELS; i++) {
        auto& ask = current_update.asks[i];
        double price = ask.price / 100.0; // Convert paise to rupees
        
        // Skip empty levels
        if (ask.price <= 0 || ask.quantity <= 0) {
            continue;
        }
        
        // Check if this is a new price level or qty changed
        auto it = asks_.find(price);
        if (it == asks_.end()) {
            // New price level - generate ADD
            auto* event = update_pool_.allocate();
            event->type_ = ExchangeNS::MarketUpdateType::ADD;
            event->ticker_id_ = ticker_id_;
            event->side_ = Common::Side::SELL;
            event->price_ = price;
            event->qty_ = ask.quantity;
            event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::SELL);
            events.push_back(event);
            
            // Add to local book
            PriceLevel level{price, ask.quantity, ask.orders, static_cast<uint64_t>(Common::getCurrentNanos())};
            asks_[price] = level;
            
            std::string time_str;
            logger_->log("%:% %() ADD ASK % x % [% orders]\n", 
                       __FILE__, __LINE__, __FUNCTION__, price, ask.quantity, ask.orders);
        } 
        else if (it->second.quantity != ask.quantity) {
            // Quantity changed - generate MODIFY
            auto* event = update_pool_.allocate();
            event->type_ = ExchangeNS::MarketUpdateType::MODIFY;
            event->ticker_id_ = ticker_id_;
            event->side_ = Common::Side::SELL;
            event->price_ = price;
            event->qty_ = ask.quantity;
            event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::SELL);
            events.push_back(event);
            
            // Update local book
            it->second.quantity = ask.quantity;
            it->second.orders = ask.orders;
            it->second.last_update_time = static_cast<uint64_t>(Common::getCurrentNanos());
            
            std::string time_str;
            logger_->log("%:% %() MODIFY ASK % x % [% orders]\n", 
                       __FILE__, __LINE__, __FUNCTION__, price, ask.quantity, ask.orders);
        }
        
        // Mark as processed by removing from prev set
        prev_ask_prices_.erase(price);
    }
    
    // Check for disappeared levels and generate CANCEL events
    checkForDisappearedLevels(current_update, events);
    
    // Process trade information if available
    if (current_update.last_quantity > 0) {
        auto* event = update_pool_.allocate();
        event->type_ = ExchangeNS::MarketUpdateType::TRADE;
        event->ticker_id_ = ticker_id_;
        event->price_ = current_update.last_price;
        event->qty_ = current_update.last_quantity;
        event->side_ = Common::Side::INVALID; // Side unknown for trades
        event->order_id_ = Common::OrderId_INVALID; // No order ID for trades
        events.push_back(event);
        
        std::string time_str;
        logger_->log("%:% %() TRADE % x %\n", 
                   __FILE__, __LINE__, __FUNCTION__, current_update.last_price, current_update.last_quantity);
    }
    
    return events;
}

void ZerodhaOrderBook::checkForDisappearedLevels(
    const MarketUpdate& /* current_update */,
    std::vector<ExchangeNS::MEMarketUpdate*>& events) {
    
    // Process disappeared bid levels
    for (double price : prev_bid_prices_) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            // Generate CANCEL event
            auto* event = update_pool_.allocate();
            event->type_ = ExchangeNS::MarketUpdateType::CANCEL;
            event->ticker_id_ = ticker_id_;
            event->side_ = Common::Side::BUY;
            event->price_ = price;
            event->qty_ = 0;
            event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::BUY);
            events.push_back(event);
            
            // Remove from local book
            std::string time_str;
            logger_->log("%:% %() CANCEL BID %\n", 
                       __FILE__, __LINE__, __FUNCTION__, price);
            bids_.erase(it);
        }
    }
    
    // Process disappeared ask levels
    for (double price : prev_ask_prices_) {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            // Generate CANCEL event
            auto* event = update_pool_.allocate();
            event->type_ = ExchangeNS::MarketUpdateType::CANCEL;
            event->ticker_id_ = ticker_id_;
            event->side_ = Common::Side::SELL;
            event->price_ = price;
            event->qty_ = 0;
            event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::SELL);
            events.push_back(event);
            
            // Remove from local book
            std::string time_str;
            logger_->log("%:% %() CANCEL ASK %\n", 
                       __FILE__, __LINE__, __FUNCTION__, price);
            asks_.erase(it);
        }
    }
}

std::vector<ExchangeNS::MEMarketUpdate*> ZerodhaOrderBook::clear() {
    std::vector<ExchangeNS::MEMarketUpdate*> events;
    
    // Generate CANCEL events for all bid levels
    for (const auto& [price, level] : bids_) {
        auto* event = update_pool_.allocate();
        event->type_ = ExchangeNS::MarketUpdateType::CANCEL;
        event->ticker_id_ = ticker_id_;
        event->side_ = Common::Side::BUY;
        event->price_ = price;
        event->qty_ = 0;
        event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::BUY);
        events.push_back(event);
    }
    
    // Generate CANCEL events for all ask levels
    for (const auto& [price, level] : asks_) {
        auto* event = update_pool_.allocate();
        event->type_ = ExchangeNS::MarketUpdateType::CANCEL;
        event->ticker_id_ = ticker_id_;
        event->side_ = Common::Side::SELL;
        event->price_ = price;
        event->qty_ = 0;
        event->order_id_ = generateOrderId(ticker_id_, price, Common::Side::SELL);
        events.push_back(event);
    }
    
    // Generate a CLEAR event for the entire book
    auto* clear_event = update_pool_.allocate();
    clear_event->type_ = ExchangeNS::MarketUpdateType::CLEAR;
    clear_event->ticker_id_ = ticker_id_;
    clear_event->side_ = Common::Side::INVALID;
    clear_event->price_ = Common::Price_INVALID;
    clear_event->qty_ = Common::Qty_INVALID;
    clear_event->order_id_ = Common::OrderId_INVALID;
    events.push_back(clear_event);
    
    // Clear local book data
    bids_.clear();
    asks_.clear();
    prev_bid_prices_.clear();
    prev_ask_prices_.clear();
    
    // Reset BBO
    bbo_ = BBO();
    
    std::string time_str;
    logger_->log("%:% %() ORDER BOOK CLEARED for ticker_id %\n", 
               __FILE__, __LINE__, __FUNCTION__, ticker_id_);
    
    return events;
}

void ZerodhaOrderBook::updateBBO() {
    // Update best bid if any bids exist
    if (!bids_.empty()) {
        const auto& best_bid = bids_.begin()->second;
        bbo_.bid_price = best_bid.price;
        bbo_.bid_quantity = best_bid.quantity;
    } else {
        bbo_.bid_price = 0.0;
        bbo_.bid_quantity = 0;
    }
    
    // Update best ask if any asks exist
    if (!asks_.empty()) {
        const auto& best_ask = asks_.begin()->second;
        bbo_.ask_price = best_ask.price;
        bbo_.ask_quantity = best_ask.quantity;
    } else {
        bbo_.ask_price = std::numeric_limits<double>::max();
        bbo_.ask_quantity = 0;
    }
}

double ZerodhaOrderBook::getBestBidPrice() const {
    return bbo_.bid_price;
}

double ZerodhaOrderBook::getBestAskPrice() const {
    return bbo_.ask_price;
}

int32_t ZerodhaOrderBook::getBestBidQuantity() const {
    return bbo_.bid_quantity;
}

int32_t ZerodhaOrderBook::getBestAskQuantity() const {
    return bbo_.ask_quantity;
}

const ZerodhaOrderBook::BBO& ZerodhaOrderBook::getBBO() const {
    return bbo_;
}

std::pair<size_t, size_t> ZerodhaOrderBook::getDepth() const {
    return {bids_.size(), asks_.size()};
}

const std::map<double, ZerodhaOrderBook::PriceLevel, std::greater<double>>& 
ZerodhaOrderBook::getBids() const {
    return bids_;
}

const std::map<double, ZerodhaOrderBook::PriceLevel, std::less<double>>& 
ZerodhaOrderBook::getAsks() const {
    return asks_;
}

bool ZerodhaOrderBook::isEmpty() const {
    return bids_.empty() && asks_.empty();
}

Common::TickerId ZerodhaOrderBook::getTickerId() const {
    return ticker_id_;
}

uint64_t ZerodhaOrderBook::getLastUpdateTime() const {
    return last_update_time_;
}

Common::OrderId ZerodhaOrderBook::generateOrderId(
    Common::TickerId ticker_id, 
    double price, 
    Common::Side side) {
    
    // Convert price to a sortable integer (paise)
    uint64_t price_bits = static_cast<uint64_t>(price * 100.0);
    uint64_t side_bit = (side == Common::Side::BUY) ? 0 : 1;
    
    // Combine ticker_id (high bits), price, and side into a unique 64-bit ID
    return (static_cast<uint64_t>(ticker_id) << 48) | 
           (price_bits << 1) | 
           side_bit;
}

} // namespace Zerodha
} // namespace Adapter