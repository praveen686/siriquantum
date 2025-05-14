# Exchange Core

This directory contains the core exchange functionality that forms the backbone of the trading system.

## Overview

The exchange core implements a simulated exchange with order matching, market data publication, and client order processing. It serves as a reference implementation and is used for testing and simulation purposes.

## Components

### Market Data

- **market_data/** - Market data related components
  - **market_update.h** - Defines market update messages
  - **market_data_publisher.h/.cpp** - Publishes market data updates
  - **snapshot_synthesizer.h/.cpp** - Creates market data snapshots

### Order Server

- **order_server/** - Order server components
  - **client_request.h** - Defines client order request messages
  - **client_response.h** - Defines client order response messages
  - **order_server.h/.cpp** - Processes client order requests
  - **fifo_sequencer.h** - Ensures FIFO processing of requests

### Matcher

- **matcher/** - Order matching engine
  - **matching_engine.h/.cpp** - Core matching logic
  - **me_order.h/.cpp** - Order representation in matching engine
  - **me_order_book.h/.cpp** - Order book implementation

## Architecture

The exchange components are designed to work together as follows:

1. The **Order Server** receives client order requests and sequences them
2. The **Matching Engine** processes the sequenced orders and updates order books
3. When order books change, the **Market Data Publisher** creates and publishes market updates
4. The **Snapshot Synthesizer** periodically creates complete order book snapshots

## Message Formats

### Market Updates

Market updates (`MEMarketUpdate`) represent changes to the market, such as:
- New orders added to the book (ADD)
- Existing orders modified (MODIFY)
- Orders canceled (CANCEL)
- Trades executed (TRADE)
- Book cleared (CLEAR)
- Snapshot markers (SNAPSHOT_START, SNAPSHOT_END)

### Client Requests

Client requests (`MEClientRequest`) represent actions from trading clients:
- New orders (NEW)
- Order cancellations (CANCEL)

### Client Responses

Client responses (`MEClientResponse`) represent exchange responses to client requests:
- Order accepted (ACCEPTED)
- Order rejected (REJECTED)
- Order canceled (CANCELED)
- Order filled (FILLED)
- Cancel rejected (CANCEL_REJECTED)

## Design Philosophy

The exchange core is designed with the following principles:

1. **Performance** - Efficient processing of orders and market data
2. **Determinism** - Predictable behavior for testing and simulation
3. **Simplicity** - Clear, easy-to-understand interfaces
4. **Realism** - Realistic exchange behavior for accurate testing