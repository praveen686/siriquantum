cmake_minimum_required(VERSION 3.10)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Include directories
include_directories(${CMAKE_SOURCE_DIR})

# Add subdirectories
add_subdirectory(adapters)

# If there are other subdirectories in trading, uncomment and add them
# add_subdirectory(market_data)
# add_subdirectory(order_gw)
add_subdirectory(strategy)