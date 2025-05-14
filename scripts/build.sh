#!/bin/bash

# Exit on error
set -e

# Create build directory if it doesn't exist
mkdir -p build
cd build

# Run CMake
cmake ..

# Build
make -j$(nproc)

echo "Build completed successfully!"