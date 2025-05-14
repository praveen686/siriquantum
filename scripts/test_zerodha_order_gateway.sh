#!/bin/bash

# Exit on error
set -e

# Build the test
echo "Building Zerodha Order Gateway test..."
cd "$(dirname "$0")/.."
./scripts/build.sh

# Parse command line arguments
CONFIG_FILE=""
TRADING_MODE="paper"  # Default to paper trading for safety

# Parse options
while [ "$#" -gt 0 ]; do
  case "$1" in
    --config=*)
      CONFIG_FILE="${1#*=}"
      shift
      ;;
    --mode=*)
      TRADING_MODE="${1#*=}"
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--config=<config_file>] [--mode=paper|live]"
      exit 1
      ;;
  esac
done

# Show warning for live trading mode
if [ "$TRADING_MODE" = "live" ]; then
    echo "WARNING: Running in LIVE trading mode. Real orders will be placed."
    read -p "Are you sure you want to continue? (y/n) " -n 1 -r
    echo    # Move to a new line
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        echo "Aborting test."
        exit 1
    fi
fi

# Run the test
if [ -n "$CONFIG_FILE" ]; then
    echo "Running Zerodha Order Gateway test with config file: $CONFIG_FILE and mode: $TRADING_MODE"
    ./build/tests/zerodha_order_gateway_test "$CONFIG_FILE" "$TRADING_MODE"
else
    echo "Running Zerodha Order Gateway test with default config and mode: $TRADING_MODE"
    ./build/tests/zerodha_order_gateway_test "" "$TRADING_MODE"
fi

echo "Test completed."