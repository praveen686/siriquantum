#!/bin/bash

# Comprehensive script to build and test the Binance WebSocket implementation
# This script:
# 1. Builds the Binance WebSocket client and its test program
# 2. Runs the test to verify connectivity with Binance exchange API
# 3. Reports results and log locations

# Exit on error
set -e

# Get script directory and set paths
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="$ROOT_DIR/build"
LOG_DIR="$ROOT_DIR/logs/binance"

# Ensure logs directory exists
mkdir -p "$LOG_DIR"

echo "=== Building Binance WebSocket Implementation ==="
echo "Root directory: $ROOT_DIR"
echo "Build directory: $BUILD_DIR"

# Create build directory if it doesn't exist
mkdir -p "$BUILD_DIR"
cd "$BUILD_DIR"

# Configure and build
echo "Running CMake..."
cmake "$ROOT_DIR"
echo "Building project..."
cmake --build . -- -j$(nproc)

# Check if the test executable exists
TEST_EXECUTABLE="$BUILD_DIR/bin/binance_websocket_test"
if [ ! -f "$TEST_EXECUTABLE" ]; then
    echo "Error: Test executable not found after build: $TEST_EXECUTABLE"
    exit 1
fi

echo ""
echo "=== Running Binance WebSocket Test ==="
echo "Logs will be written to: $LOG_DIR/websocket_test.log"

# Run the test
"$TEST_EXECUTABLE"

# Check the exit code
EXIT_CODE=$?
if [ $EXIT_CODE -eq 0 ]; then
    echo ""
    echo "=== Test completed successfully! ==="
    echo "Check logs for detailed information: $LOG_DIR/websocket_test.log"
else
    echo ""
    echo "=== Test failed with exit code: $EXIT_CODE ==="
    echo "Check logs for details: $LOG_DIR/websocket_test.log"
    exit $EXIT_CODE
fi