#!/bin/bash

# Script to build and test the pure C++ Zerodha authenticator

# Set up environment
echo "Setting up build environment..."
# Get the project root directory (two levels up from script directory)
SCRIPT_DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )"
CMAKE_DIR="$(dirname "$(dirname "$SCRIPT_DIR")")"
BUILD_DIR="${CMAKE_DIR}/build"

# Make build directory if it doesn't exist
mkdir -p ${BUILD_DIR}
cd ${BUILD_DIR}

# Build the project
echo "Building project..."
cmake ..
make -j$(nproc)

# Run the test
echo ""
echo "========================================="
echo "Running Zerodha Authenticator Test"
echo "========================================="
./tests/zerodha_auth_test

# Check result
if [ $? -eq 0 ]; then
    echo ""
    echo "SUCCESS: Zerodha authenticator works!"
    echo ""
    exit 0
else
    echo ""
    echo "ERROR: Test failed"
    echo ""
    exit 1
fi