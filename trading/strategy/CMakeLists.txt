# Trading strategy components

# Include directories
include_directories(
    ${CMAKE_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/common
    ${CMAKE_SOURCE_DIR}/exchange
    ${CMAKE_SOURCE_DIR}/trading
)

# Source files for Trading strategy components
set(TRADING_STRATEGY_SOURCES
    liquidity_taker.cpp
    market_maker.cpp
    market_order_book.cpp
    market_order.cpp
    order_manager.cpp
    risk_manager.cpp
    trade_engine.cpp
)

# Header files for Trading strategy components
set(TRADING_STRATEGY_HEADERS
    feature_engine.h
    liquidity_taker.h
    market_maker.h
    market_order_book.h
    market_order.h
    om_order.h
    order_manager.h
    position_keeper.h
    risk_manager.h
    trade_engine.h
)

# Create a library for Trading strategy components
add_library(trading_strategy STATIC
    ${TRADING_STRATEGY_SOURCES}
    ${TRADING_STRATEGY_HEADERS}
)

# Find required dependencies
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# Link dependencies
target_link_libraries(trading_strategy
    PUBLIC libcommon
    PUBLIC ${Boost_LIBRARIES}
    PUBLIC ${OPENSSL_LIBRARIES}
)

# Add dependencies
add_dependencies(trading_strategy
    libcommon
)

# Set C++ standard
set_property(TARGET trading_strategy PROPERTY CXX_STANDARD 20)