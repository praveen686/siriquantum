#!/bin/bash

# Stop on any error
set -e

# Get the script directory
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"

# Set log file
LOG_FILE="$PROJECT_ROOT/logs/zerodha_liquidity_taker_test.log"

# Create logs directory if not exists
mkdir -p "$(dirname "$LOG_FILE")"

# Build the project
echo "Building project..."
cd "$PROJECT_ROOT"
mkdir -p build
cd build
cmake ..
make -j4

echo "Building Zerodha liquidity taker test..."
make zerodha_liquidity_taker_test

# Run the test
echo "Running Zerodha liquidity taker test..."
cd "$PROJECT_ROOT"
./build/tests/zerodha_liquidity_taker_test

echo "Test completed. Check logs at $LOG_FILE"