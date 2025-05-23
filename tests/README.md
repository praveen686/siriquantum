# Tests

This directory contains test programs for the IDA Trading System components, organized by venue.

## Directory Structure

- `zerodha/` - Tests for Zerodha venue adapter components
  - `zerodha_auth_test.cpp` - Tests the Zerodha authentication functionality
  - `zerodha_market_data_test.cpp` - Tests the Zerodha market data adapter
  - `zerodha_order_book_test.cpp` - Tests the Zerodha limit order book implementation
  - `zerodha_order_gateway_test.cpp` - Tests the Zerodha order gateway functionality
  - `zerodha_liquidity_taker_test.cpp` - Tests the Zerodha liquidity taker strategy

- `binance/` - Tests for Binance venue adapter components
  - `test_binance_trading_system.cpp` - Tests the complete Binance trading system integration

## Running Tests

Tests can be run using the scripts in the `scripts` directory:

```bash
# Build the system
./scripts/system/build.sh

# Run specific Zerodha tests
./scripts/zerodha/test_zerodha_auth.sh
./scripts/zerodha/test_zerodha_market_data.sh
./scripts/zerodha/test_zerodha_order_book.sh
./scripts/zerodha/test_zerodha_order_gateway.sh
./scripts/zerodha/test_zerodha_liquidity_taker.sh

# Run Binance tests
./scripts/binance/build_and_test_binance_websocket.sh
```

## Order Book Test

The Zerodha Order Book test (`zerodha_order_book_test.cpp`) demonstrates the functionality of the limit order book implementation for Zerodha market data. It:

1. Connects to Zerodha's WebSocket API
2. Subscribes to the symbols configured in the environment
3. Processes market data updates
4. Builds and maintains limit order books for each subscribed instrument
5. Displays the current state of the order books periodically

The test displays:
- The best bid and ask prices and quantities (BBO)
- The top 5 price levels on both bid and ask sides
- The number of orders at each price level

To run the order book test:

```bash
./scripts/zerodha/test_zerodha_order_book.sh
```

The test runs continuously until terminated with Ctrl+C.

## Test Logs

All test logs are written to the venue-specific subdirectories in the `logs` directory:

```
# Zerodha test logs
/home/praveen/om/siriquantum/ida/logs/zerodha/

# Binance test logs
/home/praveen/om/siriquantum/ida/logs/binance/
```