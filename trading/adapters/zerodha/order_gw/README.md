# Zerodha Order Gateway Adapter

This directory contains the implementation of the Zerodha order gateway adapter, which is responsible for sending orders to Zerodha and processing order responses.

## Responsibilities

The Zerodha order gateway adapter has the following responsibilities:

1. **Connection Management**:
   - Establish and maintain connection to Zerodha API
   - Handle authentication using API key and secret
   - Manage session tokens and renewals

2. **Order Processing**:
   - Convert internal order requests to Zerodha API format
   - Send orders to Zerodha
   - Process order responses from Zerodha
   - Convert Zerodha responses to internal format

3. **Order Tracking**:
   - Maintain mapping between internal order IDs and Zerodha order IDs
   - Track order state through its lifecycle
   - Handle partial fills, cancellations, and rejections

4. **Instrument Mapping**:
   - Map between internal ticker IDs and Zerodha instrument tokens/symbols
   - Register tradable instruments with appropriate exchange segments

## Components

- **zerodha_order_gateway_adapter.h** - Header file defining the adapter interface
- **zerodha_order_gateway_adapter.cpp** - Implementation of the adapter

## Zerodha Order Workflow

1. Internal `MEClientRequest` is received from the trading engine
2. The adapter converts this to Zerodha's order format
3. The order is sent to Zerodha via their REST API
4. Zerodha responds with order confirmation/rejection
5. The adapter converts the response to internal `MEClientResponse` format
6. The response is pushed to the client response queue for processing by the trading engine

## Order Types Supported

- Market orders
- Limit orders
- Stop loss orders
- Stop loss limit orders

## Usage

```cpp
// Create the adapter
Adapter::Zerodha::ZerodhaOrderGatewayAdapter order_gateway_adapter(
    logger,
    client_id,
    &client_requests_queue,
    &client_responses_queue,
    "api_key",
    "api_secret"
);

// Start the adapter
order_gateway_adapter.start();

// Register tradable instruments
order_gateway_adapter.registerInstrument("NIFTY-FUT", 0);
order_gateway_adapter.registerInstrument("RELIANCE-EQ", 1);

// Later, when done
order_gateway_adapter.stop();
```