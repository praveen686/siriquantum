# Binance Adapter

This directory contains adapter implementations for connecting the trading system to Binance, a major cryptocurrency exchange.

## Overview

The Binance adapter translates between the Binance API and our internal trading system interfaces. It handles all Binance-specific functionality, including:

- Authentication and connection management
- Market data subscription and translation
- Order submission, modification, and cancellation
- Position and account information retrieval

## Components

- **market_data/** - Binance market data adapter
  - Receives and processes market data from Binance WebSocket API
  - Converts Binance market data formats to our internal market update format
  - Handles market data subscription management
  
- **order_gw/** - Binance order gateway adapter
  - Translates internal order requests to Binance API calls
  - Processes order status updates from Binance
  - Maps between internal and Binance order IDs

## Binance API Integration

The adapter interacts with the following Binance APIs:

1. **Binance REST API** - For order placement and account management
2. **Binance WebSocket API** - For real-time market data

## Configuration

The Binance adapter requires the following configuration:

- API Key: Provided by Binance
- API Secret: Provided by Binance
- Trading instruments mapping: Maps Binance symbols to internal ticker IDs

## Implementation Notes

- Binance requires HMAC-SHA256 signatures for authenticated API calls
- WebSocket connections must be maintained and can reconnect automatically if disconnected
- Binance uses different price and quantity precision for different symbols
- The adapter handles rate limiting according to Binance API guidelines
- Market data from Binance is normalized to match our internal tick format