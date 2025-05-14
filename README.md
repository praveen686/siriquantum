# IDA Trading System

This is a low-latency trading system designed to connect to multiple exchanges including Zerodha and Binance. The system is based on the architecture described in "Building Low-Latency Applications with C++" by Sourav Ghosh.

## Project Structure

- **common/** - Common utilities and data structures used throughout the system
- **exchange/** - Core exchange functionality
  - **market_data/** - Market data handling components
  - **matcher/** - Order matching engine
  - **order_server/** - Order server for processing client requests
- **scripts/** - Build and utility scripts
- **trading/** - Trading components
  - **adapters/** - Exchange-specific adapters for connecting to external exchanges
    - **binance/** - Binance exchange adapter
    - **zerodha/** - Zerodha exchange adapter
  - **market_data/** - Market data consumers
  - **order_gw/** - Order gateway
  - **strategy/** - Trading strategy implementations

## Building the Project

```bash
cd /path/to/ida
./scripts/build.sh
```

## Design Philosophy

The system is designed with the following principles in mind:

1. **Low Latency** - Critical components are optimized for speed
2. **Modularity** - Clean separation of concerns with well-defined interfaces
3. **Adaptability** - Ability to connect to different exchanges through adapter pattern
4. **Resilience** - Robust error handling and recovery mechanisms

## Exchange Adapters

The system uses an adapter pattern to connect to different exchanges. Each exchange adapter implements a common interface, allowing the core trading components to remain exchange-agnostic.

- Zerodha adapter for Indian equity markets
- Binance adapter for cryptocurrency markets