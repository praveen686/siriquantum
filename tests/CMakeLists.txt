set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# Find required dependencies
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

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