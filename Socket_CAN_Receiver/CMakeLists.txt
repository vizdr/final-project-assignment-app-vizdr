cmake_minimum_required(VERSION 3.10)
project(can_server_receiver C)

set(CMAKE_C_STANDARD 11)

# Build executable
add_executable(can_server_receiver
    src/can_server.c
)

# Include directories
target_include_directories(can_server_receiver PRIVATE include)

# Link required libraries
target_link_libraries(can_server_receiver
    pthread
)