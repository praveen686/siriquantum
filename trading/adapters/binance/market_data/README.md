# Binance Market Data Adapter

This directory contains the implementation of the Binance market data adapter, which is responsible for receiving market data from Binance and converting it to the internal format used by our trading system.

## Responsibilities

The Binance market data adapter has the following responsibilities:

1. **Connection Management**:
   - Establish and maintain WebSocket connections to Binance
   - Handle connection errors and reconnection logic
   - Manage multiple WebSocket streams efficiently

2. **Market Data Subscription**:
   - Subscribe to market data streams for specific trading pairs
   - Support different types of market data (depth, trades, ticker)
   - Maintain mapping between Binance symbols and internal ticker IDs

3. **Data Processing**:
   - Parse incoming WebSocket messages from Binance
   - Handle rate limiting and buffering if needed
   - Convert Binance market data format to our internal market update format
   - Process order book snapshots and incremental updates

4. **Delivery to Trading Engine**:
   - Push processed market updates to the internal market update queue
   - Ensure sequential delivery of market data

## Components

- **binance_market_data_adapter.h** - Header file defining the adapter interface
- **binance_market_data_adapter.cpp** - Implementation of the adapter

## Binance Market Data Streams

Binance offers several WebSocket streams:

1. **Depth Stream**: Order book updates
   - Partial book depth streams (`<symbol>@depth<levels>`)
   - Diff. depth stream (`<symbol>@depth`)

2. **Trade Streams**:
   - Individual trade information (`<symbol>@trade`)
   - Aggregated trades (`<symbol>@aggTrade`)

3. **Ticker Streams**:
   - 24hr statistics (`<symbol>@ticker`)
   - Book ticker with best bid/ask (`<symbol>@bookTicker`)

Each of these needs to be converted to our internal `MEMarketUpdate` format with appropriate `MarketUpdateType` (ADD, MODIFY, CANCEL, TRADE).

## Usage

```cpp
// Create the adapter
Adapter::Binance::BinanceMarketDataAdapter market_data_adapter(
    logger,
    &market_updates_queue,
    "api_key",
    "api_secret"
);

// Start the adapter
market_data_adapter.start();

// Subscribe to instruments
market_data_adapter.subscribe("BTCUSDT", 0);
market_data_adapter.subscribe("ETHUSDT", 1);

// Later, when done
market_data_adapter.stop();
```