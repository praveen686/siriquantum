set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# Find required dependencies
find_package(Boost REQUIRED COMPONENTS system)
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json 3.10.0 REQUIRED)
find_package(CURL REQUIRED)

# Binance market data library
add_library(binance_market_data 
    market_data/binance_market_data_consumer.cpp
)

target_link_libraries(binance_market_data
    PUBLIC
    libcommon
    libexchange
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
    ${CURL_LIBRARIES}
    jsoncpp
    pthread
)

# Binance order gateway library
add_library(binance_order_gateway
    order_gw/binance_order_gateway_adapter.cpp
)

target_link_libraries(binance_order_gateway
    PUBLIC
    libcommon
    libexchange
    ${Boost_LIBRARIES}
    ${OPENSSL_LIBRARIES}
    nlohmann_json::nlohmann_json
    ${CURL_LIBRARIES}
    jsoncpp
    pthread
)

# Commented out order gateway until implementation is complete
# add_library(binance_order_gateway
#    order_gw/binance_order_gateway_adapter.cpp
# )
# 
# target_link_libraries(binance_order_gateway
#    PUBLIC
#    libcommon
#    libexchange
#    ${Boost_LIBRARIES}
#    ${OPENSSL_LIBRARIES}
#    nlohmann_json::nlohmann_json
#    ${CURL_LIBRARIES}
#    pthread
# )