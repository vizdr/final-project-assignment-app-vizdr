cmake_minimum_required(VERSION 3.10)
project(Socket_CAN_Sender C)

set(CMAKE_C_STANDARD 11)

# Include directories
include_directories(include)

# Source files
set(SOURCES src/can_send_detection.c)

# Build executable
add_executable(can_send_detection ${SOURCES})

# Link required libraries
target_link_libraries(can_send_detection
    pthread
)
