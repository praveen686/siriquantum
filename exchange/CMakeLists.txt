set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_COMPILER g++)
set(CMAKE_CXX_FLAGS "-std=c++2a -Wall -Wextra -Werror -Wpedantic")

include_directories(${PROJECT_SOURCE_DIR})

# Add the exchange library as header-only for now
add_library(libexchange INTERFACE)
target_include_directories(libexchange INTERFACE ${PROJECT_SOURCE_DIR})