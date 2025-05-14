# Order Server Components

This directory contains components related to client order processing.

## Components

### Client Request Messages

- **client_request.h** - Defines client request message structures
  - `ClientRequestType` - Enum defining types of client requests (NEW, CANCEL)
  - `MEClientRequest` - Internal client request structure used by the matching engine
  - `OMClientRequest` - Client request structure published over the network

### Client Response Messages

- **client_response.h** - Defines client response message structures
  - `ClientResponseType` - Enum defining types of client responses (ACCEPTED, REJECTED, CANCELED, FILLED, etc.)
  - `ClientResponseRejectReason` - Enum defining reasons for rejections
  - `MEClientResponse` - Internal client response structure used by the matching engine
  - `OSClientResponse` - Client response structure published over the network

### Order Server

- **order_server.h/.cpp** - Processes client order requests
  - Receives client requests from TCP connections
  - Validates requests
  - Forwards valid requests to the matching engine
  - Returns responses to clients

### FIFO Sequencer

- **fifo_sequencer.h** - Ensures FIFO processing of requests
  - Assigns sequence numbers to incoming requests
  - Ensures requests are processed in the order they were received

## Client Request Types

Client requests (`MEClientRequest`) can be of the following types:

- **NEW** - Create a new order
- **CANCEL** - Cancel an existing order

## Client Response Types

Client responses (`MEClientResponse`) can be of the following types:

- **ACCEPTED** - Order accepted by the exchange
- **REJECTED** - Order rejected by the exchange
- **CANCELED** - Order successfully canceled
- **FILLED** - Order filled (partially or fully)
- **CANCEL_REJECTED** - Cancel request rejected

## Rejection Reasons

When an order is rejected, the `reject_reason_` field specifies why:

- **INVALID_QUANTITY** - Order quantity is invalid
- **INVALID_PRICE** - Order price is invalid
- **INVALID_TICKER** - Invalid ticker symbol
- **INVALID_ORDER_ID** - Invalid order ID
- **DUPLICATE_ORDER_ID** - Order ID already exists
- **RISK_REJECT** - Rejected by risk management

## Data Flow

1. Trading clients connect to the order server via TCP
2. Clients send `OMClientRequest` messages to the order server
3. The order server validates and converts these to `MEClientRequest`
4. The FIFO sequencer assigns sequence numbers to the requests
5. The matching engine processes the requests and generates `MEClientResponse`
6. The order server converts these to `OSClientResponse` and sends them back to the clients

## Usage

Order server components are typically used as follows:

```cpp
// Create the client request and response queues
Exchange::ClientRequestLFQueue client_requests(ME_MAX_CLIENT_UPDATES);
Exchange::ClientResponseLFQueue client_responses(ME_MAX_CLIENT_UPDATES);

// Create the order server
OrderServer order_server(
    &client_requests,
    &client_responses,
    "0.0.0.0", 12345,  // Listen on all interfaces, port 12345
    "lo"  // Network interface
);

// Start the order server
order_server.start();
```