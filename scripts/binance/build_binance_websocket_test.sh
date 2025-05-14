#!/bin/bash

# Script to build the Binance WebSocket test implementation
# This script builds the Binance WebSocket client and test program
# to verify connectivity with Binance exchange API

# Exit on error
set -e

# Create build directory if it doesn't exist
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="$ROOT_DIR/build"

echo "Building Binance WebSocket test implementation..."
echo "Root directory: $ROOT_DIR"
echo "Build directory: $BUILD_DIR"

# Create build directory if it doesn't exist
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure and build
cmake "$ROOT_DIR"
cmake --build . -- -j$(nproc)

echo "Build complete!"
echo "Binance WebSocket test is located at: $BUILD_DIR/bin/binance_websocket_test"