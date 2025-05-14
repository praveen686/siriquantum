set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# Find required dependencies
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.10.0 REQUIRED)
find_package(CURL REQUIRED)

# Zerodha auth test
add_executable(zerodha_auth_test zerodha_auth_test.cpp)
target_link_libraries(zerodha_auth_test 
    PUBLIC 
    zerodha_auth
    zerodha_market_data
    libcommon
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    pthread
)

# Zerodha market data test
add_executable(zerodha_market_data_test zerodha_market_data_test.cpp)
target_link_libraries(zerodha_market_data_test 
    PUBLIC 
    zerodha_auth
    zerodha_market_data
    libcommon
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    pthread
)

# Zerodha order book test
add_executable(zerodha_order_book_test zerodha_order_book_test.cpp)
target_link_libraries(zerodha_order_book_test 
    PUBLIC 
    zerodha_auth
    zerodha_market_data
    libcommon
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    pthread
)

# Zerodha order gateway test
add_executable(zerodha_order_gateway_test zerodha_order_gateway_test.cpp)
target_link_libraries(zerodha_order_gateway_test 
    PUBLIC 
    zerodha_auth
    zerodha_market_data
    zerodha_order_gateway
    libcommon
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    curl
    pthread
)

# Zerodha liquidity taker test
add_executable(zerodha_liquidity_taker_test zerodha_liquidity_taker_test.cpp)
target_link_libraries(zerodha_liquidity_taker_test 
    PUBLIC 
    zerodha_auth
    zerodha_market_data
    zerodha_order_gateway
    zerodha_strategy
    trading_strategy
    libcommon
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    curl
    pthread
)

# Binance WebSocket test
# Commented out due to missing source file
# add_executable(binance_websocket_test binance_websocket_test.cpp)
# target_link_libraries(binance_websocket_test

# Binance Trading System test
add_executable(test_binance_trading_system test_binance_trading_system.cpp)
target_link_libraries(test_binance_trading_system
    PUBLIC
    binance_market_data
    binance_order_gateway
    trading_strategy
    libcommon
    libexchange
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
    ${CURL_LIBRARIES}
    jsoncpp
    pthread
)