# Zerodha Order Book

This module implements a limit order book (LOB) for Zerodha market data. It converts the Zerodha's market depth data into a full limit order book representation that can be used by trading strategies.

## Overview

The Zerodha market data feed provides up to 5 levels of depth on both bid and ask sides. This module constructs and maintains a full order book by:

1. Processing market updates from Zerodha WebSocket
2. Detecting changes in price levels
3. Generating appropriate internal market update events (ADD, MODIFY, CANCEL)
4. Maintaining the current state of the order book

## Classes

### ZerodhaOrderBook

This is the main class that:
- Maintains the current state of the order book for a specific instrument
- Processes market updates from Zerodha
- Detects changes in price levels and generates appropriate events
- Provides access to the current state of the order book

Key features:
- Maintains up to 5 levels of depth on both sides
- Generates synthetic order IDs for tracking price levels
- Provides access to the best bid/offer (BBO)
- Supports full order book clearing and recovery

### Integration with ZerodhaMarketDataAdapter

The `ZerodhaOrderBook` is integrated with the `ZerodhaMarketDataAdapter` to:
- Manage order books for all subscribed instruments
- Process market updates from Zerodha WebSocket
- Generate market update events for the trading system
- Handle connection recovery

## Usage

```cpp
// Get order book for a specific instrument
ZerodhaOrderBook* order_book = market_data_adapter.getOrderBook(ticker_id);

// Access order book state
double best_bid = order_book->getBestBidPrice();
double best_ask = order_book->getBestAskPrice();
const auto& bbo = order_book->getBBO();

// Get full depth
const auto& bids = order_book->getBids();
const auto& asks = order_book->getAsks();
```

## Limitations

- Zerodha provides a maximum of 5 levels of depth
- Individual orders are not visible, only aggregated quantities at each price level
- No sequence numbers for guaranteed order of updates
- No explicit add/modify/cancel events in the Zerodha feed

## Implementation Details

### Event Generation

The `ZerodhaOrderBook` generates the following events:

- **ADD**: When a new price level appears
- **MODIFY**: When the quantity at an existing price level changes
- **CANCEL**: When a price level disappears
- **TRADE**: When a trade occurs

### Order ID Generation

Since Zerodha doesn't provide order IDs, the `ZerodhaOrderBook` generates synthetic order IDs based on:
- Ticker ID
- Price level
- Side (BID/ASK)

This ensures consistent order IDs for the same price level across multiple updates.

### Reconnection Handling

When a reconnection occurs:
1. All order books are cleared
2. CANCEL events are generated for all price levels
3. New subscriptions are sent to Zerodha
4. Order books are rebuilt from scratch

## Configuration

The environment configuration has been extended to support both spot and futures instruments:

- `ZKITE_SPOT_SYMBOLS`: List of spot instruments to subscribe to
- `ZKITE_FUTURES_SYMBOLS`: List of futures instruments to subscribe to
- `ZKITE_USE_FUTURES_FOR_INDICES`: Whether to use futures for indices

## Example

```json
{
  "ZKITE_SPOT_SYMBOLS": ["NSE:RELIANCE", "NSE:HDFCBANK", "NSE:INFY", "NSE:TCS", "NSE:SBIN"],
  "ZKITE_FUTURES_SYMBOLS": ["NFO:NIFTY", "NFO:BANKNIFTY"],
  "ZKITE_USE_FUTURES_FOR_INDICES": true
}
```

This configuration would create order books for both spot equities and futures instruments.