set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# Header-only adapter interfaces
add_library(libadapter INTERFACE)
target_include_directories(libadapter INTERFACE ${PROJECT_SOURCE_DIR})

# Add exchange-specific adapter subdirectories
add_subdirectory(zerodha)