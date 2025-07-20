cmake_minimum_required(VERSION 3.30.0)
project(udp_sketch)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)

# Main UDP async sketch
add_executable(udp_sketch udp_async_sketch.cpp)

# Simple UDP test
add_executable(udp_simple_test udp_simple_test.cpp)

# Echo test
add_executable(udp_echo_test udp_echo_test.cpp)

# Demo test
add_executable(udp_demo udp_async_demo.cpp)

# DTLS sketch
add_executable(dtls_sketch dtls_sketch.cpp)
target_link_libraries(dtls_sketch OpenSSL::SSL OpenSSL::Crypto pthread)

# Platform-specific libraries
if(UNIX AND NOT APPLE)
    # Linux - link against liburing
    find_library(URING_LIB uring)
    if(URING_LIB)
        target_link_libraries(udp_sketch ${URING_LIB})
        target_link_libraries(udp_demo ${URING_LIB})
    else()
        message(WARNING "liburing not found. Install liburing-dev/liburing-devel")
    endif()
elseif(WIN32)
    # Windows - link against Winsock2
    target_link_libraries(udp_sketch ws2_32)
    target_link_libraries(udp_simple_test ws2_32)
    target_link_libraries(udp_echo_test ws2_32)
    target_link_libraries(udp_demo ws2_32)
endif()