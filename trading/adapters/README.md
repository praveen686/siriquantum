# Exchange Adapters

This directory contains adapter implementations for connecting the trading system to different exchanges.

## Overview

The adapter pattern is used to integrate external exchanges with our internal trading system. Each exchange has its own API and protocol, but we standardize the interface to ensure the core trading components can remain exchange-agnostic.

## Structure

- **adapter_types.h** - Common types and enums for exchange adapters
- **market_data_adapter_interface.h** - Interface for all market data adapters
- **order_gateway_adapter_interface.h** - Interface for all order gateway adapters

### Exchange-Specific Adapters

- **zerodha/** - Adapter for Zerodha (Indian stock exchange)
  - **market_data/** - Zerodha market data adapter
  - **order_gw/** - Zerodha order gateway adapter

- **binance/** - Adapter for Binance (cryptocurrency exchange)
  - **market_data/** - Binance market data adapter
  - **order_gw/** - Binance order gateway adapter

## Integration Pattern

Each exchange adapter consists of two main components:

1. **Market Data Adapter**: Responsible for receiving market data from the exchange (order book updates, trades, etc.) and converting it to the internal format used by our trading engine.

2. **Order Gateway Adapter**: Responsible for sending orders to the exchange and receiving order confirmations/rejections.

## Implementing a New Exchange Adapter

To add a new exchange, you need to:

1. Create a new directory for the exchange under `adapters/`
2. Implement the market data adapter by inheriting from `MarketDataAdapterInterface`
3. Implement the order gateway adapter by inheriting from `OrderGatewayAdapterInterface`
4. Update `adapter_types.h` to include the new exchange type