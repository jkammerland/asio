cmake_minimum_required(VERSION 3.30.0)
project(udp_sketch)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

add_executable(udp_sketch udp_async_sketch.cpp)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    # Linux - link against liburing
    find_library(URING_LIB uring)
    if(URING_LIB)
        target_link_libraries(udp_sketch ${URING_LIB})
    else()
        message(WARNING "liburing not found. Install liburing-dev/liburing-devel")
    endif()
elseif(WIN32)
    # Windows - link against Winsock2
    target_link_libraries(udp_sketch ws2_32)
endif()