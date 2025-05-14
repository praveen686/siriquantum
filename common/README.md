# Common Utilities

This directory contains common utilities and data structures used throughout the trading system.

## Components

### Core Utilities

- **types.h** - Common type definitions and constants
- **macros.h** - Utility macros for assertions, likely/unlikely hints, etc.
- **logging.h** - Logging infrastructure
- **time_utils.h** - Time-related utilities (timestamps, conversions, etc.)

### Threading and Synchronization

- **thread_utils.h** - Thread creation and management utilities
- **lf_queue.h** - Lock-free queue implementation for high-performance inter-thread communication
- **mem_pool.h** - Memory pool for efficient memory allocation/deallocation

### Networking

- **tcp_socket.h/.cpp** - TCP socket wrapper
- **tcp_server.h/.cpp** - TCP server implementation
- **mcast_socket.h/.cpp** - Multicast socket implementation
- **socket_utils.h** - Socket utility functions

## Key Features

### Lock-Free Queue

The lock-free queue (`lf_queue.h`) is a critical component for high-performance inter-thread communication. It allows for efficient passing of market data updates and order messages between different components of the system without the overhead of locks.

```cpp
// Example usage
LFQueue<int> queue(1024);  // Create a queue with space for 1024 elements
queue.push(42);            // Add an element
int value;
if (queue.pop(value)) {    // Try to get an element
    // Process value
}
```

### Memory Pool

The memory pool (`mem_pool.h`) provides efficient memory management for frequently allocated objects. It pre-allocates a pool of objects and reuses them, avoiding the overhead of frequent memory allocation/deallocation.

```cpp
// Example usage
MemPool<MyObject> pool(1024);  // Create a pool with space for 1024 objects
auto* obj = pool.allocate();   // Get an object from the pool
// Use obj...
pool.deallocate(obj);          // Return the object to the pool
```

### Logging

The logging infrastructure (`logging.h`) provides thread-safe logging capabilities with different log levels and automatic timestamps.

```cpp
// Example usage
Logger logger("app.log");
logger.log("%:% %() % Starting application\n", __FILE__, __LINE__, __FUNCTION__, 
           getCurrentTimeStr(&time_str));
```

## Design Philosophy

These utilities are designed with the following principles in mind:

1. **Performance** - Minimal overhead, especially in critical paths
2. **Thread Safety** - Safe to use from multiple threads where appropriate
3. **Simplicity** - Clear, easy-to-understand interfaces
4. **Reusability** - Generic enough to be used throughout the system