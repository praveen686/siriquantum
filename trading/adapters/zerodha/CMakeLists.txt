set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# Add subdirectories
add_subdirectory(auth)
add_subdirectory(market_data)
add_subdirectory(order_gw)

# Build zerodha adapter library
add_library(zerodha_adapter INTERFACE)
target_include_directories(zerodha_adapter INTERFACE ${PROJECT_SOURCE_DIR})
target_link_libraries(zerodha_adapter INTERFACE 
    zerodha_auth
    zerodha_market_data
    zerodha_order_gateway
)