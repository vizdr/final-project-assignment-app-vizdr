cmake_minimum_required(VERSION 3.10)
project(can_server C)

set(CMAKE_C_STANDARD 11)

# Build executable
add_executable(can_send_detection
    src/can_send_detection.c
)

# Include directories
target_include_directories(can_send_detection PRIVATE include)

# Link required libraries
target_link_libraries(can_send_detection
    pthread
)
