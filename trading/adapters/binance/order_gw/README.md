# Binance Order Gateway Adapter

This directory contains the implementation of the Binance order gateway adapter, which is responsible for sending orders to Binance and processing order responses.

## Responsibilities

The Binance order gateway adapter has the following responsibilities:

1. **Connection Management**:
   - Establish and maintain connection to Binance API
   - Handle authentication using API key and secret
   - Implement signature generation for API requests

2. **Order Processing**:
   - Convert internal order requests to Binance API format
   - Send orders to Binance
   - Process order responses from Binance
   - Convert Binance responses to internal format
   - Handle rate limiting on API requests

3. **Order Tracking**:
   - Maintain mapping between internal order IDs and Binance order IDs
   - Listen to user data stream for order updates
   - Track order state through its lifecycle
   - Handle partial fills, cancellations, and rejections

4. **Instrument Mapping**:
   - Map between internal ticker IDs and Binance symbols
   - Handle symbol-specific requirements (lot sizes, tick sizes)

## Components

- **binance_order_gateway_adapter.h** - Header file defining the adapter interface
- **binance_order_gateway_adapter.cpp** - Implementation of the adapter

## Binance Order Workflow

1. Internal `MEClientRequest` is received from the trading engine
2. The adapter converts this to Binance's order format
3. The adapter generates a signature for the request using HMAC-SHA256
4. The order is sent to Binance via their REST API
5. Binance responds with order confirmation/rejection
6. The adapter also listens to the user data stream for order updates
7. All responses are converted to internal `MEClientResponse` format
8. The responses are pushed to the client response queue for processing by the trading engine

## Security Considerations

- API keys are kept secure and never logged
- HMAC-SHA256 signatures are generated for all authenticated requests
- Timestamps are included in requests to prevent replay attacks

## Usage

```cpp
// Create the adapter
Adapter::Binance::BinanceOrderGatewayAdapter order_gateway_adapter(
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
order_gateway_adapter.registerInstrument("BTCUSDT", 0);
order_gateway_adapter.registerInstrument("ETHUSDT", 1);

// Later, when done
order_gateway_adapter.stop();
```