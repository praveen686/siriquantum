#!/bin/bash

# Exit on error
set -e

# Build the test
echo "Building Zerodha Market Data test..."
cd "$(dirname "$0")/../.."
./scripts/system/build.sh

# Run the test
if [ "$1" != "" ]; then
    echo "Running Zerodha Market Data test with config file: $1"
    ./build/tests/zerodha_market_data_test "$1"
else
    echo "Running Zerodha Market Data test with environment configuration"
    ./build/tests/zerodha_market_data_test
fi

echo "Test completed."