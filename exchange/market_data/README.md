# Market Data Components

This directory contains components related to market data processing and distribution.

## Components

### Market Update Messages

- **market_update.h** - Defines market update message structures
  - `MarketUpdateType` - Enum defining types of market updates (ADD, MODIFY, CANCEL, TRADE, etc.)
  - `MEMarketUpdate` - Internal market update structure used by the matching engine
  - `MDPMarketUpdate` - Market update structure published over the network

### Market Data Publisher

- **market_data_publisher.h/.cpp** - Publishes market data updates
  - Receives updates from the matching engine
  - Sequences updates with sequential numbers
  - Publishes updates to subscribers via multicast

### Snapshot Synthesizer

- **snapshot_synthesizer.h/.cpp** - Creates market data snapshots
  - Periodically creates complete snapshots of the order book
  - Helps clients recover from missed incremental updates
  - Uses SNAPSHOT_START and SNAPSHOT_END markers to delimit snapshots

## Market Update Types

Market updates (`MEMarketUpdate`) can be of the following types:

- **ADD** - New order added to the book
- **MODIFY** - Existing order modified (e.g., quantity change)
- **CANCEL** - Order canceled or fully executed
- **TRADE** - Trade executed
- **CLEAR** - Order book cleared
- **SNAPSHOT_START** - Beginning of an order book snapshot
- **SNAPSHOT_END** - End of an order book snapshot

## Data Flow

1. The matching engine generates `MEMarketUpdate` messages when the order book changes
2. These updates are pushed to a lock-free queue for the market data publisher
3. The market data publisher adds sequence numbers and publishes them as `MDPMarketUpdate`
4. Periodically, the snapshot synthesizer creates a complete snapshot of the order book
5. The snapshot is published as a series of `MDPMarketUpdate` messages with SNAPSHOT_START and SNAPSHOT_END markers

## Usage

Market data components are typically used as follows:

```cpp
// Create the market data publisher
MarketDataPublisher publisher(
    &market_updates_queue,
    "233.252.14.1", 20000,  // Snapshot multicast
    "233.252.14.3", 20001,  // Incremental multicast
    "lo"  // Network interface
);

// Start the publisher
publisher.start();

// Create the snapshot synthesizer
SnapshotSynthesizer synthesizer(
    &matching_engine,
    &publisher,
    1000  // Generate snapshot every 1000ms
);

// Start the synthesizer
synthesizer.start();
```