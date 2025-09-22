# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository Overview

This is a fork of the ASIO C++ library (Asynchronous I/O for C++) from chriskohlhoff/asio. The fork contains two major efforts:

1. **Comprehensive documentation extraction** of ASIO's networking concepts for each platform in a digestible way (commit f33357ec)
2. **Experimental DTLS implementation** work on top of ASIO's UDP support

Current branch status (master):
- 5 commits ahead of upstream/master
- Key work: "AI generated extract networking layer asio - cross-platform" - comprehensive documentation of ASIO's networking implementation
- Recent additions: DTLS server implementation, UDP echo server examples

## Build Systems

ASIO supports multiple build systems:

### 1. Autotools Build (Main ASIO Library)
```bash
# Generate build files
cd asio
./autogen.sh

# Configure (standalone mode without Boost)
./configure --without-boost

# Configure with Boost
./configure --with-boost=/path/to/boost

# Build
make -j$(nproc)

# Run tests
make check
```

### 2. CMake Build (Socket Code Extraction)
```bash
cd asio/socket_code_extraction
mkdir build && cd build
cmake ..
make -j$(nproc)

# Available targets:
# udp_sketch, udp_simple_test, udp_echo_test, udp_demo, dtls_sketch
```

Platform dependencies:
- Linux: Requires liburing-dev for io_uring support
- Windows: Links against ws2_32
- All platforms: Requires OpenSSL for DTLS

## Running Tests

### Unit Tests (Autotools)
```bash
cd asio
# Run all tests
make check

# Run specific test
./src/tests/unit/test_name
```

### Socket Examples
```bash
cd asio/socket_code_extraction/build

# Run UDP tests
./udp_simple_test
./udp_echo_test
./udp_demo

# Run DTLS server
./dtls_sketch
```

## Architecture Overview

### Core ASIO Structure
- **include/asio/**: Header-only library core
  - Socket abstractions (basic_socket, basic_stream_socket, basic_datagram_socket)
  - I/O context and executor framework
  - Asynchronous operation support with completion handlers
  - Platform-specific backends (epoll, kqueue, IOCP)

- **src/**: Implementation files (when using separate compilation)
  - examples/cpp{11,14,17,20}/: Examples showcasing different C++ standard features
  - tests/: Comprehensive test suite including unit, performance, and latency tests

### Socket Code Extraction Project (Key Documentation Effort)
Located in `asio/socket_code_extraction/`, this comprehensive documentation effort extracts and explains ASIO's networking concepts in a digestible way:

#### Core Documentation Areas:
- **core_sockets/**: Detailed breakdown of ASIO's socket hierarchy
  - basic_socket: Foundation with lifecycle management
  - basic_stream_socket: TCP stream semantics
  - basic_datagram_socket: UDP datagram operations
  - basic_socket_acceptor: Server-side connection acceptance
  - socket_base: Common definitions and options

- **platform_specific/**: Platform-specific networking implementations explained
  - **Linux/**: epoll reactor vs io_uring (with ring buffer design, zero-copy, kernel polling)
  - **macOS/**: kqueue reactor, Grand Central Dispatch integration
  - **Windows/**: IOCP (I/O Completion Ports) for scalable async

- **ip_protocols/**: Protocol implementations dissected
  - TCP/UDP/ICMP protocol details
  - IPv4/IPv6 addressing and endpoints
  - Protocol comparisons and use cases

- **diagrams/**: Visual representations of complex concepts
  - Socket hierarchy diagrams
  - Async operation flow
  - Buffer management strategies
  - Platform architecture comparisons
  - TCP connection lifecycle
  - DNS resolver flow

- **ssl_tls/**: SSL/TLS integration (including experimental DTLS)

- **examples_and_tests/**: Practical code demonstrating concepts
  - Basic socket usage patterns
  - Async programming patterns
  - Performance benchmarks
  - Integration examples

### Key Design Patterns

1. **Proactor Pattern**: ASIO uses completion handlers for async operations
2. **RAII**: All resources (sockets, timers) are RAII-managed
3. **Template-based Protocol Selection**: Zero-overhead abstractions via templates
4. **Executor Framework**: Flexible execution context management

### DTLS Implementation (Experimental)
The recent additions focus on DTLS 1.2 support:
- Uses OpenSSL for DTLS protocol
- Implements cookie exchange for DoS prevention
- Self-signed certificate support via `self_signed_cert.hpp`
- Multi-threaded server design

## Development Workflow

### Working with the Documentation
The `socket_code_extraction/` directory contains extensive documentation that breaks down ASIO's complex networking implementation into understandable components. Use this as a reference when:
- Understanding platform-specific behavior (epoll vs io_uring vs kqueue vs IOCP)
- Implementing new socket-based features
- Debugging networking issues
- Learning ASIO's architectural patterns

### Adding New Features
1. For core ASIO changes: Modify headers in `include/asio/`
2. For examples: Add to appropriate `src/examples/cpp*/` directory
3. For DTLS work: Update files in `asio/socket_code_extraction/`
4. For documentation: Update relevant .md files in `socket_code_extraction/`

### Testing Changes
```bash
# Quick compile test
cd asio/socket_code_extraction/build
cmake .. && make dtls_sketch

# Run with verbose output
./dtls_sketch -v
```

### Common Issues
- **liburing not found**: Install liburing-dev (Linux) or build will fallback to epoll
- **OpenSSL linking**: Ensure OpenSSL dev packages are installed
- **Autotools issues**: Run `./autogen.sh` if configure script is missing