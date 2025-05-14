# Tests

This directory contains test programs for the IDA Trading System components.

## Test Files

- `zerodha_auth_test.cpp` - Tests the Zerodha authentication functionality
- `zerodha_market_data_test.cpp` - Tests the Zerodha market data adapter
- `zerodha_order_book_test.cpp` - Tests the Zerodha limit order book implementation
- `test_scaffold.cpp` - Tests the basic scaffolding of the system

## Running Tests

Tests can be run using the scripts in the `scripts` directory:

```bash
# Run all tests
./scripts/run_tests.sh

# Run a specific test
./scripts/test_zerodha_auth.sh
./scripts/test_zerodha_market_data.sh
./scripts/test_zerodha_order_book.sh
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
./scripts/test_zerodha_order_book.sh
```

The test runs continuously until terminated with Ctrl+C.

## Test Logs

All test logs are written to the `logs` directory. For the order book test, the log file is:

```
/home/praveen/om/siriquantum/ida/logs/zerodha_order_book_test.log
```