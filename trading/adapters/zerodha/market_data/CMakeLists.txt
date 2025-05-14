set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

# Find Boost
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)

# Build zerodha market data library
add_library(zerodha_market_data
    zerodha_market_data_adapter.cpp
    zerodha_websocket_client.cpp
    instrument_token_manager.cpp
    environment_config.cpp
    orderbook/zerodha_order_book.cpp
)

target_include_directories(zerodha_market_data PUBLIC 
    ${PROJECT_SOURCE_DIR}
    ${Boost_INCLUDE_DIRS}
    ${OPENSSL_INCLUDE_DIR}
)

target_link_libraries(zerodha_market_data PUBLIC
    zerodha_auth
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    pthread
    stdc++fs
    curl
)

# Add WebSocket dependencies
target_compile_definitions(zerodha_market_data PRIVATE BOOST_NO_CXX17_HDR_STRING_VIEW)