# ASIO Core Socket Implementation Documentation

This directory contains comprehensive documentation for ASIO's core socket implementation files. These files form the foundation of ASIO's network programming capabilities.

## File Structure

### 1. [socket_base.md](socket_base.md)
- **Source**: `asio/include/asio/socket_base.hpp`
- **Purpose**: Base class providing common socket definitions and options
- **Key Features**:
  - Socket option definitions (reuse_address, keep_alive, etc.)
  - Shutdown types and message flags
  - Platform-independent constants
  - No virtual functions (zero overhead)

### 2. [basic_socket.md](basic_socket.md)
- **Source**: `asio/include/asio/basic_socket.hpp`
- **Purpose**: Core socket functionality common to all socket types
- **Key Features**:
  - Socket lifecycle management (open, close, bind, connect)
  - Option and IO control operations
  - Non-blocking mode support
  - Platform-specific service delegation

### 3. [basic_socket_acceptor.md](basic_socket_acceptor.md)
- **Source**: `asio/include/asio/basic_socket_acceptor.hpp`
- **Purpose**: Server-side socket for accepting connections
- **Key Features**:
  - Listen and accept operations
  - Thread-safe synchronous accepts
  - Multiple accept operation variants
  - Integration with socket creation

### 4. [basic_datagram_socket.md](basic_datagram_socket.md)
- **Source**: `asio/include/asio/basic_datagram_socket.hpp`
- **Purpose**: Datagram (UDP) socket implementation
- **Key Features**:
  - Connected and unconnected operations
  - send/receive vs send_to/receive_from
  - Message boundary preservation
  - Broadcast and multicast support

### 5. [basic_stream_socket.md](basic_stream_socket.md)
- **Source**: `asio/include/asio/basic_stream_socket.hpp`
- **Purpose**: Stream (TCP) socket implementation
- **Key Features**:
  - Stream-oriented operations
  - Partial read/write support
  - Integration with composed operations
  - AsyncReadStream/AsyncWriteStream concepts

## Class Hierarchy

```
socket_base
    │
    └── basic_socket<Protocol, Executor>
            │
            ├── basic_socket_acceptor<Protocol, Executor>
            │
            ├── basic_datagram_socket<Protocol, Executor>
            │
            └── basic_stream_socket<Protocol, Executor>
```

## Design Patterns

### 1. Template-Based Protocol Independence
- Protocol type determines socket behavior
- Compile-time binding for efficiency
- Examples: `tcp`, `udp`, `local::stream_protocol`

### 2. Service/Implementation Separation
- Platform-specific implementations:
  - Windows: IOCP (`win_iocp_socket_service`)
  - Linux: io_uring (`io_uring_socket_service`)
  - Default: Reactive (`reactive_socket_service`)

### 3. Move Semantics
- All socket types are move-only
- Clear ownership model
- Efficient socket transfer

### 4. Dual API Design
- Throwing versions for simplicity
- Error code versions for control
- Consistent across all operations

## Common Usage Patterns

### TCP Server
```cpp
asio::io_context io_context;

// Create acceptor
asio::ip::tcp::acceptor acceptor(io_context, 
    asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 8080));

// Accept connections
while (true) {
    asio::ip::tcp::socket socket(io_context);
    acceptor.accept(socket);
    // Handle connection...
}
```

### UDP Client/Server
```cpp
asio::io_context io_context;

// Create socket
asio::ip::udp::socket socket(io_context, 
    asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));

// Send datagram
asio::ip::udp::endpoint remote(
    asio::ip::address::from_string("192.168.1.1"), 12345);
socket.send_to(asio::buffer(data), remote);

// Receive datagram
asio::ip::udp::endpoint sender;
socket.receive_from(asio::buffer(buffer), sender);
```

### Async TCP Client
```cpp
class client {
    asio::ip::tcp::socket socket_;
    
    void connect(const asio::ip::tcp::endpoint& endpoint) {
        socket_.async_connect(endpoint,
            [this](std::error_code ec) {
                if (!ec) {
                    start_read();
                }
            });
    }
    
    void start_read() {
        socket_.async_read_some(asio::buffer(buffer_),
            [this](std::error_code ec, std::size_t length) {
                if (!ec) {
                    process_data(length);
                    start_read();
                }
            });
    }
};
```

## Platform Considerations

### Windows
- IOCP for high performance
- Some operations limited on older versions (XP/2003)
- Special handling for `release()` and `cancel()`

### Linux
- io_uring support for modern kernels
- epoll/select fallback
- Better cancel support

### macOS/BSD
- kqueue-based implementation
- Standard POSIX behavior

## Thread Safety

### General Rules
- **Distinct objects**: Always safe
- **Shared objects**: Generally unsafe

### Exceptions
- `basic_socket_acceptor`: Synchronous accept operations are thread-safe
- `basic_datagram_socket`: Synchronous send/receive operations are thread-safe
- `basic_stream_socket`: Synchronous send/receive operations are thread-safe

## Best Practices

1. **Use High-Level Operations**
   - Prefer `async_read`/`async_write` over `async_read_some`/`async_write_some`
   - Use composed operations for complex protocols

2. **Error Handling**
   - Always check for `error::eof` on stream sockets
   - Handle `error::would_block` for non-blocking operations
   - Use error_code versions in performance-critical code

3. **Resource Management**
   - Rely on RAII (destructor closes socket)
   - Use move semantics for transfer
   - Explicit `close()` when needed for error handling

4. **Async Patterns**
   - Use `std::enable_shared_from_this` for async handlers
   - Avoid capturing `this` directly in lambdas
   - Consider coroutines for linear async code

## Integration Points

### With ASIO Components
- **io_context**: Executor and event loop
- **buffers**: Zero-copy buffer management
- **composed operations**: Higher-level protocols
- **coroutines**: Simplified async code

### With Standard Library
- **std::error_code**: Error handling
- **std::move**: Transfer semantics
- **concepts**: Type constraints (C++20)

## Performance Tips

1. **Buffer Management**
   - Reuse buffers when possible
   - Use scatter-gather I/O for multiple buffers
   - Avoid unnecessary copies

2. **Async Operations**
   - Batch operations when possible
   - Use `post()` for handler dispatch control
   - Consider `defer()` for same-thread continuations

3. **Socket Options**
   - Set options before `bind()` when possible
   - Use `TCP_NODELAY` for low-latency
   - Tune buffer sizes for throughput

## Further Reading

- ASIO documentation: https://think-async.com/Asio/
- Network programming patterns
- Platform-specific socket documentation
- C++ networking TS proposals