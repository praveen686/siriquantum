# Instrument Token Manager

The InstrumentTokenManager is a key component of the Zerodha Market Data Adapter that handles the mapping between human-readable symbols and Zerodha's numerical instrument tokens.

## Purpose

Zerodha's WebSocket API requires numerical instrument tokens for market data subscription, but these tokens are not easily memorable or consistent across environments. The InstrumentTokenManager solves this problem by:

1. Automatically downloading Zerodha's instrument master data
2. Building efficient lookup indices for fast token resolution
3. Caching the data to minimize API calls
4. Providing special handling for index symbols and futures contracts

## Features

- **Automated Token Resolution**: Convert human-readable symbols like "NSE:RELIANCE" to Zerodha instrument tokens
- **Daily Refresh**: Automatically refresh instrument data daily or on demand
- **Efficient Caching**: Store instrument data locally with TTL-based invalidation
- **CSV Parsing**: Robust parsing of Zerodha's instrument CSV data with proper handling of quoted fields
- **Special Index Handling**: Ability to convert index symbols to their corresponding futures contracts
- **Thread Safety**: Concurrent access support with proper locking

## Usage

```cpp
// Create the manager
Adapter::Zerodha::InstrumentTokenManager token_manager(
    authenticator,
    logger,
    ".cache/zerodha"
);

// Initialize (downloads instrument data if needed)
token_manager.initialize();

// Get token for a symbol
int32_t reliance_token = token_manager.getInstrumentToken("NSE:RELIANCE");

// Get token for an index future
int32_t nifty_future_token = token_manager.getNearestFutureToken("NIFTY");

// Get detailed instrument information
auto instrument_info = token_manager.getInstrumentInfo(reliance_token);
if (instrument_info) {
    std::cout << "Trading symbol: " << instrument_info->trading_symbol << std::endl;
    std::cout << "Exchange: " << InstrumentTokenManager::exchangeToString(instrument_info->exchange) << std::endl;
    std::cout << "Last price: " << instrument_info->last_price << std::endl;
}

// Manually update instrument data if needed
token_manager.updateInstrumentData();
```

## CSV Data Format

The manager handles Zerodha's instrument CSV format with fields including:

- instrument_token: Numerical token for WebSocket subscription
- exchange_token: Exchange-specific token
- trading_symbol: Symbol used for trading (e.g., "RELIANCE")
- name: Full name of the instrument (e.g., "Reliance Industries Limited")
- last_price: Last traded price
- expiry: Expiry date for derivatives (futures/options)
- strike: Strike price for options
- tick_size: Minimum price movement
- lot_size: Contract size for derivatives
- instrument_type: Type (EQ, FUT, OPT, etc.)
- segment: Market segment
- exchange: Exchange code (NSE, BSE, NFO, etc.)

## Indices and Futures

The manager provides special handling for index symbols:

1. Indices (like "NIFTY 50") typically don't have order book data
2. The manager can automatically map these to their corresponding futures contracts
3. This is controlled via the `ZKITE_USE_FUTURES_FOR_INDICES` environment variable
4. The `getNearestFutureToken()` method finds the closest expiry future contract

## Caching Behavior

- The manager stores downloaded instrument data in a local cache file
- Cache is automatically refreshed when it expires (default: 24 hours)
- Cache location is configurable via constructor or environment
- Cache validity is checked on initialization and periodically

## Thread Safety

The manager uses a mutex to protect concurrent access to its data structures, making it safe to use in a multi-threaded environment.