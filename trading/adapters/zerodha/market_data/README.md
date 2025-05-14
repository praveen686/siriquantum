# Zerodha Market Data Adapter

This directory contains the implementation of the Zerodha market data adapter, which is responsible for receiving market data from Zerodha and converting it to the internal format used by our trading system.

## Responsibilities

The Zerodha market data adapter has the following responsibilities:

1. **Connection Management**:
   - Establish and maintain connection to Zerodha WebSocket API
   - Handle authentication using API key and secret
   - Manage reconnections in case of disconnects

2. **Market Data Subscription**:
   - Subscribe to market data for specific instruments
   - Maintain mapping between Zerodha symbols and internal ticker IDs
   - Handle subscription updates, additions, and removals

3. **Data Processing**:
   - Parse incoming WebSocket messages from Zerodha
   - Convert Zerodha market data format to our internal market update format
   - Handle different types of market updates (quotes, trades, order book)
   - Filter and normalize data as needed

4. **Delivery to Trading Engine**:
   - Push processed market updates to the internal market update queue
   - Ensure sequential delivery of market data

## Components

- **zerodha_market_data_adapter.h/cpp** - Main adapter interface and implementation
- **instrument_token_manager.h/cpp** - Manages instrument token lookup and caching
- **environment_config.h/cpp** - Environment configuration for the adapter
- **zerodha_websocket_client.h/cpp** - WebSocket client for Zerodha market data
- **orderbook/zerodha_order_book.h/cpp** - Limit order book implementation for Zerodha market data

## Features

- **Dynamic Instrument Token Resolution**: Automatically downloads and parses Zerodha's instrument token data
- **Symbol Name Resolution**: Maps between different symbol formats and aliases
- **Limit Order Book**: Maintains a full limit order book for each subscribed instrument
- **Synthetic Event Generation**: Creates ADD/MODIFY/CANCEL events from market depth data
- **Caching**: Optimized caching of instrument data with TTL
- **Environment Configuration**: Flexible configuration through environment variables
- **Special Index Handling**: Support for index symbols and futures contracts
- **Fault Tolerance**: Automatic reconnection and session management

## Environment Configuration

The adapter can be configured using environment variables, typically in a `.env` file:

```
# Zerodha API credentials
ZKITE_API_KEY=your_api_key_here
ZKITE_API_SECRET=your_api_secret_here
ZKITE_USER_ID=your_user_id
ZKITE_PWD=your_password
ZKITE_TOTP_SECRET=your_totp_secret

# Cache configuration
ZKITE_INSTRUMENTS_CACHE_DIR=/path/to/cache/dir
ZKITE_INSTRUMENTS_CACHE_TTL=24  # Hours
ZKITE_ACCESS_TOKEN_PATH=/path/to/token/file

# Symbol configuration
ZKITE_USE_FUTURES_FOR_INDICES=1  # Set to 1 to automatically use futures for indices
ZKITE_DEFAULT_EXCHANGE=NSE  # Default exchange if not specified in symbol
ZKITE_SYMBOL_MAP={"NIFTY":"NIFTY 50","BANKNIFTY":"NIFTY BANK"}
ZKITE_INDEX_FUTURES=["NIFTY","BANKNIFTY","FINNIFTY"]
ZKITE_INDEX_FUTURES_ROLLOVER_DAYS=2  # Days before expiry to roll futures

# Test configuration
ZKITE_TEST_SYMBOLS=["NSE:RELIANCE","NSE:INFY","NSE:HDFCBANK","NSE:TCS","NSE:NIFTY 50"]
```

## Usage

### Basic Usage

```cpp
// Create the adapter with environment configuration
Adapter::Zerodha::ZerodhaMarketDataAdapter market_data_adapter(
    logger,
    &market_updates_queue,
    "/path/to/.env"  // Optional path to .env file
);

// Start the adapter
market_data_adapter.start();

// Subscribe to instruments
market_data_adapter.subscribe("NSE:RELIANCE", 1001);
market_data_adapter.subscribe("NSE:NIFTY 50", 1002);  // Will use index or futures based on config

// Access order book for a specific instrument
auto* order_book = market_data_adapter.getOrderBook(1001);
if (order_book) {
    // Access best bid/offer
    double best_bid = order_book->getBestBidPrice();
    double best_ask = order_book->getBestAskPrice();
    int32_t best_bid_qty = order_book->getBestBidQuantity();
    int32_t best_ask_qty = order_book->getBestAskQuantity();
    
    // Get full depth
    auto [bid_depth, ask_depth] = order_book->getDepth();
    
    // Iterate through price levels
    for (const auto& [price, level] : order_book->getBids()) {
        std::cout << "BID: " << level.quantity << " @ " << price 
                  << " (" << level.orders << " orders)" << std::endl;
    }
    
    for (const auto& [price, level] : order_book->getAsks()) {
        std::cout << "ASK: " << level.quantity << " @ " << price 
                  << " (" << level.orders << " orders)" << std::endl;
    }
}

// Process market updates from the queue
auto update = market_updates_queue.getNextToRead();
if (update) {
    // Process market update
    process_market_update(*update);
    market_updates_queue.updateReadIndex();
}

// Later, when done
market_data_adapter.stop();
```

### Test Symbols

The adapter supports automatic subscription to test symbols defined in the environment:

```cpp
// Subscribe to all test symbols defined in ZKITE_TEST_SYMBOLS
market_data_adapter.subscribeToTestSymbols();
```

## Market Data Format

Zerodha provides market data through their WebSocket API in the following formats:

1. **LTP (Last Traded Price)**: Simple mode with basic price information
2. **Quote**: Mid-level with best bid/ask
3. **Full**: Complete order book depth with all bids and offers

The adapter converts these formats to our internal `MEMarketUpdate` structure, setting appropriate types:
- `MarketUpdateType::ADD` for order book entries
- `MarketUpdateType::TRADE` for trade information
- `MarketUpdateType::MODIFY` for updates to existing orders
- `MarketUpdateType::CANCEL` for order book removals