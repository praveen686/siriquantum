# Zerodha Adapter

This directory contains adapter implementations for connecting the trading system to Zerodha, an Indian broker and exchange platform.

## Overview

The Zerodha adapter translates between the Zerodha API and our internal trading system interfaces. It handles all Zerodha-specific functionality, including:

- Authentication and connection management
- Market data subscription and translation
- Order submission, modification, and cancellation
- Position and account information retrieval

## Components

- **auth/** - Zerodha authentication module
  - Provides TOTP-based authentication
  - Handles session management and token caching
  - Automates login process via API

- **market_data/** - Zerodha market data adapter
  - Receives and processes market data from Zerodha WebSocket API
  - Converts Zerodha market data formats to our internal market update format
  - Handles instrument token resolution and caching
  - Builds and maintains limit order books (LOB) for subscribed instruments
  - Supports advanced features like index-to-futures mapping
  
- **order_gw/** - Zerodha order gateway adapter
  - Translates internal order requests to Zerodha API calls
  - Processes order status updates from Zerodha
  - Maps between internal and Zerodha order IDs

## Features

- **Production-ready Authentication**: Secure TOTP-based authentication with session caching
- **Real-time Market Data**: Full support for Zerodha's WebSocket market data
- **Full Limit Order Book**: Maintains complete order books with synthetic events from depth data
- **Efficient Instrument Management**: Smart caching of instrument information
- **Environment-based Configuration**: Flexible configuration through environment variables
- **Advanced Symbol Handling**: Support for index symbols, futures contracts, and custom mappings

## Zerodha API Integration

The adapter interacts with the following Zerodha APIs:

1. **Kite Connect API** - RESTful API for authentication, order placement, and account management
2. **Kite WebSocket API** - Real-time market data delivery
3. **Kite Connect Instruments API** - For downloading and mapping instrument tokens

## Configuration

The Zerodha adapter is configured via environment variables, typically in a `.env` file. Key configuration options include:

```
# Authentication
ZKITE_API_KEY=your_api_key_here
ZKITE_API_SECRET=your_api_secret_here
ZKITE_USER_ID=your_user_id
ZKITE_PWD=your_password
ZKITE_TOTP_SECRET=your_totp_secret

# Cache Configuration
ZKITE_INSTRUMENTS_CACHE_DIR=/path/to/cache
ZKITE_ACCESS_TOKEN_PATH=/path/to/token/cache

# Symbol Configuration
ZKITE_SYMBOL_MAP={"NIFTY":"NIFTY 50","BANKNIFTY":"NIFTY BANK"}
ZKITE_USE_FUTURES_FOR_INDICES=1
```

See the individual component READMEs for more detailed configuration options.

## Usage

### Market Data Adapter

```cpp
// Create adapter with environment configuration
Adapter::Zerodha::ZerodhaMarketDataAdapter md_adapter(
    logger,
    &market_updates_queue,
    "/path/to/.env"
);

// Start the adapter
md_adapter.start();

// Subscribe to instruments
md_adapter.subscribe("NSE:RELIANCE", 1001);
md_adapter.subscribe("NSE:NIFTY 50", 1002);

// Access the order book for a specific instrument
Adapter::Zerodha::ZerodhaOrderBook* order_book = md_adapter.getOrderBook(1001);
if (order_book) {
    // Access order book state
    double best_bid = order_book->getBestBidPrice();
    double best_ask = order_book->getBestAskPrice();
    
    // Get depth information
    const auto& bids = order_book->getBids();
    const auto& asks = order_book->getAsks();
    
    // Get best bid/offer (BBO)
    const auto& bbo = order_book->getBBO();
    std::cout << "BBO: " << bbo.bid_quantity << " @ " << bbo.bid_price 
              << " / " << bbo.ask_price << " @ " << bbo.ask_quantity << std::endl;
}

// Process market updates
for (auto update = market_updates_queue.getNextToRead(); 
     market_updates_queue.size() > 0; 
     update = market_updates_queue.getNextToRead()) {
    // Process market update
    process_market_update(*update);
    market_updates_queue.updateReadIndex();
}

// Stop the adapter
md_adapter.stop();
```

### Authentication Module

```cpp
// Create authenticator from environment
auto authenticator = Adapter::Zerodha::ZerodhaAuthenticator::from_env(
    logger,
    ".cache/zerodha",
    "/path/to/.env"
);

// Authenticate and get access token
std::string access_token = authenticator.authenticate();

// Use access token for API requests
// ...

// Get access token for subsequent requests
access_token = authenticator.get_access_token();
```

## Implementation Notes

- Zerodha uses a proprietary instrument token system which we handle with our InstrumentTokenManager
- Market data from Zerodha needs to be normalized to match our internal MEMarketUpdate format
- The adapter implements reconnection logic and session management
- The market data adapter can automatically map index symbols to futures contracts based on configuration
- Zerodha provides only 5 levels of market depth without individual order information
- The order book component generates synthetic events (ADD/MODIFY/CANCEL) by comparing current and previous market depth
- Synthetic order IDs are generated based on ticker ID, price level, and side (BID/ASK)