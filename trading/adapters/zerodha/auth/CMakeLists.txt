set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# List of source files for Zerodha authenticator
set(ZERODHA_AUTH_SOURCES
    zerodha_authenticator.cpp
)

# Build static library
add_library(zerodha_auth STATIC ${ZERODHA_AUTH_SOURCES})

# Link dependencies
target_link_libraries(zerodha_auth 
    PUBLIC 
    libcommon
    curl
    ssl
    crypto
)

# Test moved to tests directory