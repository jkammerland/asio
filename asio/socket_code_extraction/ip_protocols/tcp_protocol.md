# TCP Protocol Implementation in ASIO

## Overview

The TCP (Transmission Control Protocol) implementation in ASIO provides a complete abstraction for TCP/IP networking. TCP is a connection-oriented, reliable, stream-based protocol that ensures ordered delivery of data.

## Key Components

### TCP Class Structure

```cpp
class tcp {
public:
    // Type aliases for TCP components
    typedef basic_endpoint<tcp> endpoint;
    typedef basic_stream_socket<tcp> socket;
    typedef basic_socket_acceptor<tcp> acceptor;
    typedef basic_resolver<tcp> resolver;
    typedef basic_socket_iostream<tcp> iostream;
    
    // Protocol family constructors
    static tcp v4() noexcept;  // IPv4 TCP
    static tcp v6() noexcept;  // IPv6 TCP
    
    // Protocol identifiers
    int type() const noexcept;     // Returns SOCK_STREAM
    int protocol() const noexcept; // Returns IPPROTO_TCP
    int family() const noexcept;   // Returns AF_INET or AF_INET6
};
```

## Protocol Characteristics

### 1. Connection-Oriented
- TCP requires establishing a connection before data transfer
- Uses three-way handshake for connection establishment
- Provides full-duplex communication

### 2. Stream-Based
- Data is treated as a continuous stream of bytes
- No message boundaries preserved (unlike UDP)
- Uses `basic_stream_socket` for implementation

### 3. Reliable Delivery
- Guarantees data delivery or error notification
- Automatic retransmission of lost packets
- In-order delivery of data

## Integration with Socket Classes

### Socket Type
TCP uses `basic_stream_socket<tcp>` which provides:
- `send()` and `receive()` operations for data transfer
- `async_send()` and `async_receive()` for asynchronous operations
- `connect()` for establishing connections
- Stream-oriented read/write operations

### Acceptor Type
TCP uses `basic_socket_acceptor<tcp>` for server-side operations:
- `bind()` to associate with a local endpoint
- `listen()` to put socket in listening state
- `accept()` to accept incoming connections
- `async_accept()` for asynchronous connection acceptance

### Resolver Type
TCP uses `basic_resolver<tcp>` for name resolution:
- Converts hostnames to IP addresses
- Supports both synchronous and asynchronous resolution
- Works with both IPv4 and IPv6

## IPv4 vs IPv6 Support

### Protocol Selection
```cpp
// IPv4 TCP
tcp::socket socket_v4(io_context, tcp::v4());

// IPv6 TCP
tcp::socket socket_v6(io_context, tcp::v6());

// Version-independent (determined by endpoint)
tcp::socket socket(io_context);
```

### Key Differences
- **IPv4**: Uses AF_INET family, 32-bit addresses
- **IPv6**: Uses AF_INET6 family, 128-bit addresses
- Both share the same TCP protocol semantics
- Socket API remains consistent across versions

## Socket Options

### TCP_NODELAY (Nagle Algorithm)
```cpp
typedef asio::detail::socket_option::boolean<
    IPPROTO_TCP, TCP_NODELAY> no_delay;
```

- Disables Nagle's algorithm for low-latency applications
- When enabled, small packets are sent immediately
- Useful for interactive applications

Example usage:
```cpp
tcp::socket socket(io_context);
tcp::no_delay option(true);
socket.set_option(option);
```

## Usage Examples

### Client Connection
```cpp
tcp::socket socket(io_context);
tcp::resolver resolver(io_context);
auto endpoints = resolver.resolve("example.com", "80");
asio::connect(socket, endpoints);
```

### Server Acceptance
```cpp
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8080));
tcp::socket socket(io_context);
acceptor.accept(socket);
```

### Stream Operations
```cpp
// TCP supports iostream interface
tcp::iostream stream("example.com", "80");
stream << "GET / HTTP/1.0\r\n\r\n";
string line;
getline(stream, line);
```

## Thread Safety

- **Distinct objects**: Safe to use from multiple threads
- **Shared objects**: Safe for const operations only
- Follows standard ASIO thread safety guarantees

## Design Patterns

1. **Protocol Class Pattern**: TCP class acts as a traits class
2. **Template Parameterization**: Used by socket classes for protocol-specific behavior
3. **Factory Methods**: `v4()` and `v6()` provide protocol instances
4. **Type Safety**: Compile-time protocol checking through templates

## Integration Points

1. **Endpoint**: Uses `basic_endpoint<tcp>` for address/port pairs
2. **Socket**: Integrates with `basic_stream_socket` template
3. **Acceptor**: Works with `basic_socket_acceptor` template
4. **Resolver**: Compatible with `basic_resolver` template
5. **IOStream**: Provides `basic_socket_iostream` support

## Performance Considerations

1. **Buffering**: TCP uses kernel buffers for reliable delivery
2. **Nagle Algorithm**: Can be disabled for latency-sensitive apps
3. **Keep-Alive**: Supported through socket options
4. **Window Scaling**: Handled by OS TCP stack

## Error Handling

TCP operations can produce various errors:
- Connection refused
- Connection reset
- Connection timed out
- Network unreachable
All handled through ASIO's error_code system