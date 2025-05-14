# IDA Trading System

This is a low-latency trading system designed to connect to multiple exchanges including Zerodha and Binance. The system is based on the architecture described in "Building Low-Latency Applications with C++" by Sourav Ghosh.

## Project Structure

- **common/** - Common utilities and data structures used throughout the system
  - Lock-free queues, memory pools, logging, socket utilities, etc.
- **config/** - Configuration files for the trading system
  - JSON-based configuration for exchanges, instruments, and strategies
- **docs/** - Documentation including implementation plans and reference docs
  - Architecture documents, development plans, and design guidelines
- **exchange/** - Core exchange functionality
  - **market_data/** - Market data handling components
  - **matcher/** - Order matching engine
  - **order_server/** - Order server for processing client requests
- **logs/** - Venue-specific log directories
  - **binance/** - Logs for Binance adapter components
  - **zerodha/** - Logs for Zerodha adapter components
- **scripts/** - Build and utility scripts
  - **binance/** - Scripts for testing Binance adapter components
  - **system/** - Core build and system scripts
  - **zerodha/** - Scripts for testing Zerodha adapter components
- **tests/** - Test programs organized by venue
  - **binance/** - Binance-specific test programs
  - **zerodha/** - Zerodha-specific test programs
- **trading/** - Trading components
  - **adapters/** - Exchange-specific adapters for connecting to external exchanges
    - **binance/** - Binance exchange adapter
      - **market_data/** - Market data WebSocket client and processing
      - **order_gw/** - Order gateway for executing trades on Binance
    - **zerodha/** - Zerodha exchange adapter
      - **auth/** - Authentication module with TOTP support
      - **market_data/** - Market data adapter with WebSocket client
      - **order_gw/** - Order gateway adapter for Zerodha trading
      - **strategy/** - Zerodha-specific strategy implementations
  - **market_data/** - Market data consumers
  - **order_gw/** - Order gateway interfaces
  - **strategy/** - Trading strategy implementations
    - Liquidity taker, market maker, order book, order manager, etc.

## Building the Project

```bash
# Build the entire system
cd /path/to/ida
./scripts/system/build.sh

# Run the Zerodha tests
./scripts/zerodha/test_zerodha_auth.sh
./scripts/zerodha/test_zerodha_market_data.sh
./scripts/zerodha/test_zerodha_order_book.sh
./scripts/zerodha/test_zerodha_order_gateway.sh
./scripts/zerodha/test_zerodha_liquidity_taker.sh

# Run the Binance tests
./scripts/binance/build_and_test_binance_websocket.sh
```

## Design Philosophy

The system is designed with the following principles in mind:

1. **Low Latency** - Critical components are optimized for speed using lock-free algorithms, memory pools, and efficient data structures
2. **Modularity** - Clean separation of concerns with well-defined interfaces between components
3. **Adaptability** - Ability to connect to different exchanges through adapter pattern
4. **Resilience** - Robust error handling and recovery mechanisms with reconnection capabilities
5. **Testability** - Comprehensive test suite for all components

## Exchange Adapters

The system uses an adapter pattern to connect to different exchanges. Each exchange adapter implements a common interface, allowing the core trading components to remain exchange-agnostic.

### Zerodha Adapter (Complete)
- Full implementation for Indian equity markets
- Components include authenticator, market data adapter, order book, order gateway
- Supports both paper trading and live trading modes
- Features TOTP-based authentication, WebSocket market data, and REST API order execution

### Binance Adapter (Partial Implementation)
- Implementation for cryptocurrency markets
- Market data WebSocket client for real-time market updates
- Order book implementation for maintaining full depth
- Order gateway for executing trades through Binance API

## Trading Modes

The system supports two trading modes:

1. **Paper Trading Mode**
   - Uses real market data but simulates order execution
   - Configurable parameters for fill probability, latency, and slippage
   - For strategy development and testing without real money

2. **Live Trading Mode**
   - Real market data and real order execution
   - Full risk management and position controls
   - For production trading with real money

## Current Status

- Core components (lock-free queues, memory pools, logging) fully implemented
- Zerodha adapter mostly complete with functioning market data and order gateway
- Binance adapter partially implemented with working market data component
- Basic strategies implemented and ready for testing
- See [Implementation Plan](/docs/siriquantum_dev_impl_plan.md) for detailed status