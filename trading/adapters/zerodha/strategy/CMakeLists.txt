cmake_minimum_required(VERSION 3.16)

# Set the project name
project(zerodha_strategy)

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/exchange
    ${CMAKE_SOURCE_DIR}/trading
)

# Source files for Zerodha strategy components
set(ZERODHA_STRATEGY_SOURCES
    zerodha_liquidity_taker.cpp
)

# Header files for Zerodha strategy components
set(ZERODHA_STRATEGY_HEADERS
    zerodha_liquidity_taker.h
)

# Create a library for Zerodha strategy components
add_library(zerodha_strategy STATIC
    ${ZERODHA_STRATEGY_SOURCES}
    ${ZERODHA_STRATEGY_HEADERS}
)

# Link dependencies
target_link_libraries(zerodha_strategy
    PUBLIC common
    PUBLIC trading_strategy
    PUBLIC zerodha_market_data
    PUBLIC zerodha_order_gateway
)

# Add dependencies
add_dependencies(zerodha_strategy
    common
    trading_strategy
    zerodha_market_data
    zerodha_order_gateway
)

# Set C++ standard
set_property(TARGET zerodha_strategy PROPERTY CXX_STANDARD 20)