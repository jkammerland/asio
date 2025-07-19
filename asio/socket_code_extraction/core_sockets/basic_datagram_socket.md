# ASIO Basic Datagram Socket

## Overview
The `basic_datagram_socket` class template provides asynchronous and blocking datagram-oriented socket functionality. It's primarily used for UDP communications but supports any datagram protocol.

## File Location
- **Path**: `asio/include/asio/basic_datagram_socket.hpp`
- **Purpose**: Datagram (connectionless) socket implementation

## Class Template Definition
```cpp
template <typename Protocol, typename Executor = any_io_executor>
class basic_datagram_socket : public basic_socket<Protocol, Executor>
```

### Template Parameters
- **Protocol**: The protocol type (e.g., `udp`)
- **Executor**: The executor type for asynchronous operations

## Design Patterns

### 1. Inheritance from basic_socket
- Inherits all base socket functionality
- Adds datagram-specific operations
- Reuses move semantics and lifecycle management

### 2. Connectionless vs Connected Modes
- Supports both connected and unconnected operations
- `send`/`receive`: For connected sockets
- `send_to`/`receive_from`: For unconnected sockets

### 3. Asynchronous Initiation
- Separate initiation classes for each async operation type
- Clean separation between setup and execution

## Key Components

### Type Definitions
```cpp
typedef Executor executor_type;
typedef Protocol protocol_type;
typedef typename Protocol::endpoint endpoint_type;
typedef typename basic_socket<Protocol, Executor>::native_handle_type native_handle_type;
```

## Constructor Variants

### 1. Basic Construction
```cpp
explicit basic_datagram_socket(const executor_type& ex)
explicit basic_datagram_socket(ExecutionContext& context)
```

### 2. Protocol-Based Construction
```cpp
basic_datagram_socket(const executor_type& ex, const protocol_type& protocol)
```

### 3. Endpoint-Based Construction
```cpp
basic_datagram_socket(const executor_type& ex, const endpoint_type& endpoint)
```
- Opens and binds to endpoint

### 4. Native Socket Construction
```cpp
basic_datagram_socket(const executor_type& ex, const protocol_type& protocol,
                     const native_handle_type& native_socket)
```

### 5. Move Construction
```cpp
basic_datagram_socket(basic_datagram_socket&& other) noexcept
```

## Core Operations

### Connected Socket Operations

#### send()
```cpp
// Basic send
template <typename ConstBufferSequence>
std::size_t send(const ConstBufferSequence& buffers)

// Send with flags
template <typename ConstBufferSequence>
std::size_t send(const ConstBufferSequence& buffers, 
                 socket_base::message_flags flags)
```
- Requires socket to be connected
- Returns bytes sent

#### async_send()
```cpp
template <typename ConstBufferSequence, typename WriteToken>
auto async_send(const ConstBufferSequence& buffers, WriteToken&& token)

template <typename ConstBufferSequence, typename WriteToken>
auto async_send(const ConstBufferSequence& buffers,
                socket_base::message_flags flags, WriteToken&& token)
```

#### receive()
```cpp
// Basic receive
template <typename MutableBufferSequence>
std::size_t receive(const MutableBufferSequence& buffers)

// Receive with flags
template <typename MutableBufferSequence>
std::size_t receive(const MutableBufferSequence& buffers,
                    socket_base::message_flags flags)
```

#### async_receive()
```cpp
template <typename MutableBufferSequence, typename ReadToken>
auto async_receive(const MutableBufferSequence& buffers, ReadToken&& token)

template <typename MutableBufferSequence, typename ReadToken>
auto async_receive(const MutableBufferSequence& buffers,
                   socket_base::message_flags flags, ReadToken&& token)
```

### Unconnected Socket Operations

#### send_to()
```cpp
// Basic send_to
template <typename ConstBufferSequence>
std::size_t send_to(const ConstBufferSequence& buffers,
                    const endpoint_type& destination)

// Send_to with flags
template <typename ConstBufferSequence>
std::size_t send_to(const ConstBufferSequence& buffers,
                    const endpoint_type& destination,
                    socket_base::message_flags flags)
```
- Sends to specific endpoint
- Does not require connection

#### async_send_to()
```cpp
template <typename ConstBufferSequence, typename WriteToken>
auto async_send_to(const ConstBufferSequence& buffers,
                   const endpoint_type& destination,
                   WriteToken&& token)

template <typename ConstBufferSequence, typename WriteToken>
auto async_send_to(const ConstBufferSequence& buffers,
                   const endpoint_type& destination,
                   socket_base::message_flags flags,
                   WriteToken&& token)
```

#### receive_from()
```cpp
// Basic receive_from
template <typename MutableBufferSequence>
std::size_t receive_from(const MutableBufferSequence& buffers,
                        endpoint_type& sender_endpoint)

// Receive_from with flags
template <typename MutableBufferSequence>
std::size_t receive_from(const MutableBufferSequence& buffers,
                        endpoint_type& sender_endpoint,
                        socket_base::message_flags flags)
```
- Receives data and sender's endpoint
- Works on unconnected sockets

#### async_receive_from()
```cpp
template <typename MutableBufferSequence, typename ReadToken>
auto async_receive_from(const MutableBufferSequence& buffers,
                       endpoint_type& sender_endpoint,
                       ReadToken&& token)

template <typename MutableBufferSequence, typename ReadToken>
auto async_receive_from(const MutableBufferSequence& buffers,
                       endpoint_type& sender_endpoint,
                       socket_base::message_flags flags,
                       ReadToken&& token)
```

## Message Flags
Common flags from `socket_base::message_flags`:
- `message_peek`: Peek at data without removing
- `message_out_of_band`: Out-of-band data
- `message_do_not_route`: Bypass routing

## Thread Safety
- **Distinct objects**: Safe
- **Shared objects**: Unsafe
- **Exception**: Synchronous `send`, `send_to`, `receive`, `receive_from`, `connect`, and `shutdown` operations are thread-safe with respect to each other

## Usage Examples

### UDP Client (Unconnected)
```cpp
asio::io_context io_context;
asio::ip::udp::socket socket(io_context, asio::ip::udp::v4());

// Send datagram
asio::ip::udp::endpoint destination(
    asio::ip::address::from_string("192.168.1.100"), 12345);
socket.send_to(asio::buffer("Hello"), destination);

// Receive response
char data[1024];
asio::ip::udp::endpoint sender_endpoint;
size_t len = socket.receive_from(asio::buffer(data), sender_endpoint);
```

### UDP Server
```cpp
asio::io_context io_context;
asio::ip::udp::socket socket(io_context, 
    asio::ip::udp::endpoint(asio::ip::udp::v4(), 12345));

// Async receive loop
void start_receive() {
    socket.async_receive_from(
        asio::buffer(recv_buffer), sender_endpoint,
        [this](std::error_code ec, std::size_t bytes_recvd) {
            if (!ec) {
                // Process received data
                handle_receive(bytes_recvd);
                
                // Continue receiving
                start_receive();
            }
        });
}
```

### Connected UDP Socket
```cpp
asio::io_context io_context;
asio::ip::udp::socket socket(io_context, asio::ip::udp::v4());

// Connect to remote endpoint
asio::ip::udp::endpoint remote(
    asio::ip::address::from_string("192.168.1.100"), 12345);
socket.connect(remote);

// Now can use send/receive instead of send_to/receive_from
socket.send(asio::buffer("Hello"));

char data[1024];
size_t len = socket.receive(asio::buffer(data));
```

## Design Rationale

1. **Dual Mode Support**: Both connected and unconnected operations
2. **Consistent API**: Similar to stream sockets where applicable
3. **Efficiency**: Zero-copy buffer management
4. **Flexibility**: Support for various datagram protocols
5. **Thread Safety**: Safe concurrent datagram operations

## Common Patterns

### 1. UDP Echo Server
```cpp
class udp_server {
    void start_receive() {
        socket_.async_receive_from(
            asio::buffer(recv_buffer_), remote_endpoint_,
            [this](error_code ec, size_t bytes_recvd) {
                if (!ec) {
                    // Echo back
                    socket_.async_send_to(
                        asio::buffer(recv_buffer_, bytes_recvd),
                        remote_endpoint_,
                        [this](error_code, size_t) {
                            start_receive();
                        });
                }
            });
    }
};
```

### 2. Multicast Receiver
```cpp
// Join multicast group
socket.set_option(asio::ip::multicast::join_group(
    asio::ip::address::from_string("239.255.0.1")));

// Receive multicast datagrams
socket.async_receive_from(buffer, sender_endpoint, handler);
```

### 3. Broadcasting
```cpp
// Enable broadcast
socket.set_option(asio::socket_base::broadcast(true));

// Send broadcast
asio::ip::udp::endpoint broadcast_endpoint(
    asio::ip::address_v4::broadcast(), 12345);
socket.send_to(buffer, broadcast_endpoint);
```

## Key Differences from Stream Sockets

1. **Message Boundaries**: Preserves message boundaries
2. **Unreliable**: No guaranteed delivery or ordering
3. **Connectionless**: Can communicate with multiple endpoints
4. **No Accept**: No listener/acceptor pattern
5. **Broadcast/Multicast**: Supports one-to-many communication

## Error Handling
Common errors:
- `would_block`: Non-blocking mode, no data available
- `message_size`: Datagram too large for buffer
- `connection_refused`: ICMP port unreachable (connected mode)

## Key Takeaways
- Provides both connected and unconnected datagram operations
- Thread-safe synchronous operations for concurrent use
- Efficient async operations with cancellation support
- Suitable for UDP and other datagram protocols
- Inherits all basic_socket functionality