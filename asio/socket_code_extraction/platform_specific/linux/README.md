# Linux Networking Implementation Documentation

This directory contains comprehensive documentation of ASIO's Linux-specific networking implementations.

## Overview

ASIO provides two networking backends for Linux:
- **epoll reactor**: Traditional reactive model using edge-triggered epoll
- **io_uring service**: Modern proactive model using kernel ring buffers

## Documentation Structure

### Core Documentation Files

1. **[linux_networking_overview.md](./linux_networking_overview.md)**
   - High-level comparison of epoll vs io_uring
   - Architecture overview
   - Build configuration options
   - Platform requirements

2. **[epoll_reactor_implementation.md](./epoll_reactor_implementation.md)**
   - Detailed epoll reactor design
   - Speculative execution strategy
   - Edge-triggered event handling
   - Configuration and tuning options

3. **[io_uring_implementation.md](./io_uring_implementation.md)**
   - io_uring architecture and ring buffer design
   - Operation submission and completion flow
   - Zero-copy capabilities
   - Kernel polling modes

5. **[reactor_pattern_deep_dive.md](./reactor_pattern_deep_dive.md)**
   - In-depth analysis of the reactor pattern
   - Concurrency model and locking strategies
   - Timer integration approaches
   - Common pitfalls and solutions

6. **[io_uring_advanced_features.md](./io_uring_advanced_features.md)**
   - Advanced io_uring capabilities
   - Multishot operations
   - Buffer selection and fixed files
   - Future kernel features

## Quick Start Guide

### Using epoll (Default)
```cpp
#include <asio.hpp>

// epoll is used by default on Linux
asio::io_context io_context;
asio::ip::tcp::socket socket(io_context);
```

### Enabling io_uring
```bash
# Build with io_uring support
g++ -std=c++17 myapp.cpp -DASIO_HAS_IO_URING -luring

# Make io_uring the default
g++ -std=c++17 myapp.cpp -DASIO_HAS_IO_URING_AS_DEFAULT -luring
```

## Key Implementation Files

### epoll Implementation
- Header: `asio/detail/epoll_reactor.hpp`
- Implementation: `asio/detail/impl/epoll_reactor.ipp`
- Socket Service: `asio/detail/reactive_socket_service.hpp`
- Base Service: `asio/detail/reactive_socket_service_base.hpp`

### io_uring Implementation  
- Header: `asio/detail/io_uring_service.hpp`
- Implementation: `asio/detail/impl/io_uring_service.ipp`
- Socket Service: `asio/detail/io_uring_socket_service.hpp`
- Base Service: `asio/detail/io_uring_socket_service_base.hpp`

## Performance Guidelines

### When to Use epoll
- Older kernels (< Linux 5.6)
- High connection count (> 100K concurrent)
- Mostly idle connections
- Memory-constrained environments

### When to Use io_uring
- Modern kernels (Linux 5.10+)
- High throughput requirements
- Low latency requirements
- CPU efficiency is critical

## Configuration Examples

### epoll Tuning
```cpp
// Access reactor configuration
auto& reactor = asio::use_service<asio::detail::epoll_reactor>(io_context);

// Configure via environment or config file
// reactor.registration_locking = false;  // Single-threaded mode
// reactor.preallocated_io_objects = 10000;  // Pre-allocate for 10K connections
```

### io_uring Setup
```cpp
// io_uring parameters are set during ring initialization
// See io_uring_implementation.md for detailed configuration options
```

## Building and Testing

### Requirements
- **epoll**: Linux 2.6.8+ (2.6.22+ for timerfd)
- **io_uring**: Linux 5.1+ (5.10+ recommended), liburing-dev

### Test Programs
```bash
# Test epoll implementation
./test_epoll_echo_server

# Test io_uring implementation  
./test_io_uring_echo_server

# Benchmark comparison
./benchmark_epoll_vs_io_uring
```

## Troubleshooting

### Common Issues

1. **io_uring not available**
   - Check kernel version: `uname -r`
   - Install liburing: `sudo apt install liburing-dev`
   - Verify with: `pkg-config --modversion liburing`

2. **Performance regression with io_uring**
   - Check ring size configuration
   - Enable kernel polling for low latency
   - Consider workload characteristics

3. **High CPU usage with epoll**
   - Enable speculative execution
   - Tune spin counts for mutexes
   - Consider io_uring for high-throughput workloads

## Further Reading

- [Linux epoll man page](https://man7.org/linux/man-pages/man7/epoll.7.html)
- [io_uring documentation](https://kernel.dk/io_uring.pdf)
- [ASIO documentation](https://think-async.com/Asio/)
- [Kernel io_uring guide](https://kernel.org/doc/html/latest/filesystems/io_uring.html)