cmake_minimum_required(VERSION 3.14)
project(http-server-async CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find required packages
find_package(Boost 1.70 REQUIRED COMPONENTS thread)
find_package(Threads REQUIRED)

# Include directories
include_directories(incl)

# Source files
set(SOURCES
    src/main.cpp
    src/listener.cpp
    src/session.cpp
    src/utils.cpp
)

# Create executable
add_executable(http-server-async ${SOURCES})

# Link libraries
target_link_libraries(http-server-async
    Boost::headers
    Boost::thread
    Threads::Threads
)

# Set compile options for better warnings
target_compile_options(http-server-async PRIVATE
    -Wall -Wextra -Wpedantic
)
