# Environment Configuration

The EnvironmentConfig class provides a flexible configuration system for the Zerodha Market Data Adapter based on environment variables. It supports loading configuration from .env files and parsing complex data structures from environment variables.

## Purpose

Configuration management is essential for maintaining the Zerodha adapter across different environments (development, testing, production). The EnvironmentConfig class solves this by:

1. Loading configuration from environment variables or .env files
2. Parsing structured data (JSON) from environment variables
3. Providing typed access to configuration values
4. Implementing symbol resolution and mapping

## Features

- **Flexible Configuration Source**: Load from system environment or .env file
- **JSON Parsing**: Support for complex data structures in environment variables
- **Symbol Mapping**: Map between different symbol formats and aliases
- **Default Values**: Sensible defaults for optional configuration
- **Typed Accessors**: Type-safe getters for configuration values

## Configuration Options

The following environment variables are supported:

### API Credentials

```
ZKITE_API_KEY=your_api_key
ZKITE_API_SECRET=your_api_secret
ZKITE_USER_ID=your_user_id
```

### Cache Configuration

```
ZKITE_INSTRUMENTS_CACHE_DIR=/path/to/cache/dir
ZKITE_INSTRUMENTS_CACHE_TTL=24  # Hours
ZKITE_ACCESS_TOKEN_PATH=/path/to/token.json
```

### Symbol Configuration

```
ZKITE_USE_FUTURES_FOR_INDICES=1  # Set to 1 to use futures for indices
ZKITE_DEFAULT_EXCHANGE=NSE  # Default exchange if not specified in symbol
ZKITE_SYMBOL_MAP={"NIFTY":"NIFTY 50","BANKNIFTY":"NIFTY BANK","FINNIFTY":"NIFTY FIN SERVICE"}
ZKITE_INDEX_FUTURES=["NIFTY","BANKNIFTY","FINNIFTY","MIDCPNIFTY"]
ZKITE_INDEX_FUTURES_ROLLOVER_DAYS=2  # Days before expiry to roll over
```

### Test Configuration

```
ZKITE_TEST_SYMBOLS=["NSE:RELIANCE","NSE:INFY","NSE:HDFCBANK","NSE:TCS","NSE:NIFTY 50"]
```

### WebSocket Configuration

```
ZKITE_WEBSOCKET_RECONNECT_INTERVAL=5  # Seconds
ZKITE_WEBSOCKET_MAX_RECONNECT_ATTEMPTS=10
ZKITE_WEBSOCKET_PING_INTERVAL=30  # Seconds
```

### Debug Options

```
ZKITE_DEBUG_MODE=0  # Set to 1 for debug logging
ZKITE_LOG_MARKET_UPDATES=0  # Set to 1 to log all market updates
```

## JSON Format

The configuration supports JSON format for complex data structures:

- `ZKITE_SYMBOL_MAP`: JSON object mapping between symbol names
- `ZKITE_INDEX_FUTURES`: JSON array of index names to treat as futures
- `ZKITE_TEST_SYMBOLS`: JSON array of symbols for testing

## Usage

```cpp
// Create the config
Adapter::Zerodha::EnvironmentConfig config(
    logger,
    "/path/to/.env"  // Optional path to .env file
);

// Load configuration
if (config.load()) {
    // Access configuration values
    std::string api_key = config.getApiKey();
    std::string cache_dir = config.getInstrumentsCacheDir();
    bool use_futures = config.useFuturesForIndices();
    
    // Symbol resolution
    std::string nifty_symbol = config.resolveSymbol("NIFTY");  // Returns "NIFTY 50"
    std::string formatted = config.formatSymbol("RELIANCE");   // Returns "NSE:RELIANCE"
    
    // Get test symbols
    for (const auto& symbol : config.getTestSymbols()) {
        std::cout << "Test symbol: " << symbol << std::endl;
    }
}
```

## Symbol Resolution

The configuration provides two key symbol resolution functions:

1. `resolveSymbol()`: Resolves aliases to full symbols using the symbol map
   - Example: "NIFTY" → "NIFTY 50"

2. `formatSymbol()`: Ensures symbols include exchange prefix
   - Example: "RELIANCE" → "NSE:RELIANCE"

## Environment File Format

The .env file should use standard KEY=VALUE format:

```
# API Credentials
ZKITE_API_KEY=n8o9wnb25ov492cq
ZKITE_API_SECRET=7wnlmdi71axi00zrb1tdd49770cvx29f

# JSON-formatted values
ZKITE_SYMBOL_MAP={"NIFTY":"NIFTY 50","BANKNIFTY":"NIFTY BANK"}
ZKITE_INDEX_FUTURES=["NIFTY","BANKNIFTY","FINNIFTY"]
```