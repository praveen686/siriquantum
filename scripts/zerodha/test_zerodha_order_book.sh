#!/bin/bash

# Get script directory and project root
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
PROJECT_ROOT="$(dirname "$(dirname "$SCRIPT_DIR")")"

# Build path and name
BUILD_DIR="${PROJECT_ROOT}/build"
TEST_NAME="zerodha_order_book_test"

# Ensure build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo "Build directory not found. Creating it..."
    mkdir -p "$BUILD_DIR"
fi

# Change to build directory
cd "$BUILD_DIR"

# Compile
echo "Building $TEST_NAME..."
cmake "$PROJECT_ROOT" && make $TEST_NAME

# Check if compilation succeeded
if [ $? -ne 0 ]; then
    echo "Compilation failed."
    exit 1
fi

# Set environment variables
export ZKITE_LOG_LEVEL="DEBUG"

# Run the test
echo "Running $TEST_NAME..."
./tests/$TEST_NAME $@

# Exit with the exit code from the test
exit $?