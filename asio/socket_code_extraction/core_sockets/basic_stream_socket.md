# ASIO Basic Stream Socket

## Overview
The `basic_stream_socket` class template provides asynchronous and blocking stream-oriented socket functionality. It's primarily used for TCP communications and other reliable, connection-oriented protocols.

## File Location
- **Path**: `asio/include/asio/basic_stream_socket.hpp`
- **Purpose**: Stream-oriented (connection-based) socket implementation

## Class Template Definition
```cpp
template <typename Protocol, typename Executor = any_io_executor>
class basic_stream_socket : public basic_socket<Protocol, Executor>
```

### Template Parameters
- **Protocol**: The protocol type (e.g., `tcp`)
- **Executor**: The executor type for asynchronous operations

## Design Patterns

### 1. Stream Abstraction
- Implements AsyncReadStream and AsyncWriteStream concepts
- Provides both low-level and high-level I/O operations
- Supports partial reads/writes

### 2. Inheritance from basic_socket
- Inherits all socket lifecycle operations
- Adds stream-specific send/receive functionality
- Maintains consistent API with datagram sockets

### 3. Dual Interface Pattern
- `send`/`receive`: Low-level socket operations
- `write_some`/`read_some`: Stream-oriented operations
- Both map to same underlying implementation

## Key Components

### Type Definitions
```cpp
typedef Executor executor_type;
typedef Protocol protocol_type;
typedef typename Protocol::endpoint endpoint_type;
typedef typename basic_socket<Protocol, Executor>::native_handle_type native_handle_type;
```

### Concepts Implemented
- **AsyncReadStream**: Asynchronous read operations
- **AsyncWriteStream**: Asynchronous write operations
- **Stream**: General stream interface
- **SyncReadStream**: Synchronous read operations
- **SyncWriteStream**: Synchronous write operations

## Constructor Variants

### 1. Basic Construction
```cpp
explicit basic_stream_socket(const executor_type& ex)
explicit basic_stream_socket(ExecutionContext& context)
```

### 2. Protocol-Based Construction
```cpp
basic_stream_socket(const executor_type& ex, const protocol_type& protocol)
```

### 3. Endpoint-Based Construction
```cpp
basic_stream_socket(const executor_type& ex, const endpoint_type& endpoint)
```

### 4. Native Socket Construction
```cpp
basic_stream_socket(const executor_type& ex, const protocol_type& protocol,
                   const native_handle_type& native_socket)
```

### 5. Move Construction
```cpp
basic_stream_socket(basic_stream_socket&& other) noexcept
```

## Core Operations

### Send Operations

#### send()
```cpp
// Basic send
template <typename ConstBufferSequence>
std::size_t send(const ConstBufferSequence& buffers)

// Send with flags
template <typename ConstBufferSequence>
std::size_t send(const ConstBufferSequence& buffers,
                 socket_base::message_flags flags)

// Send with error code
template <typename ConstBufferSequence>
std::size_t send(const ConstBufferSequence& buffers,
                 socket_base::message_flags flags, error_code& ec)
```
- May not send all data (partial writes)
- Returns bytes actually sent
- Blocks until some data sent

#### async_send()
```cpp
template <typename ConstBufferSequence, typename WriteToken>
auto async_send(const ConstBufferSequence& buffers, WriteToken&& token)

template <typename ConstBufferSequence, typename WriteToken>
auto async_send(const ConstBufferSequence& buffers,
                socket_base::message_flags flags, WriteToken&& token)
```

### Receive Operations

#### receive()
```cpp
// Basic receive
template <typename MutableBufferSequence>
std::size_t receive(const MutableBufferSequence& buffers)

// Receive with flags
template <typename MutableBufferSequence>
std::size_t receive(const MutableBufferSequence& buffers,
                    socket_base::message_flags flags)

// Receive with error code
template <typename MutableBufferSequence>
std::size_t receive(const MutableBufferSequence& buffers,
                    socket_base::message_flags flags, error_code& ec)
```
- May not receive all available data
- Returns bytes actually received
- EOF indicated by `error::eof`

#### async_receive()
```cpp
template <typename MutableBufferSequence, typename ReadToken>
auto async_receive(const MutableBufferSequence& buffers, ReadToken&& token)

template <typename MutableBufferSequence, typename ReadToken>
auto async_receive(const MutableBufferSequence& buffers,
                   socket_base::message_flags flags, ReadToken&& token)
```

### Stream-Oriented Operations

#### write_some()
```cpp
template <typename ConstBufferSequence>
std::size_t write_some(const ConstBufferSequence& buffers)

template <typename ConstBufferSequence>
std::size_t write_some(const ConstBufferSequence& buffers, error_code& ec)
```
- Identical to `send()` but stream-oriented naming
- For compatibility with stream concepts

#### async_write_some()
```cpp
template <typename ConstBufferSequence, typename WriteToken>
auto async_write_some(const ConstBufferSequence& buffers, WriteToken&& token)
```

#### read_some()
```cpp
template <typename MutableBufferSequence>
std::size_t read_some(const MutableBufferSequence& buffers)

template <typename MutableBufferSequence>
std::size_t read_some(const MutableBufferSequence& buffers, error_code& ec)
```
- Identical to `receive()` but stream-oriented naming

#### async_read_some()
```cpp
template <typename MutableBufferSequence, typename ReadToken>
auto async_read_some(const MutableBufferSequence& buffers, ReadToken&& token)
```

## Important Characteristics

### Partial Operations
- `send`/`write_some` may not send all data
- `receive`/`read_some` may not fill all buffers
- Use `write()`/`read()` free functions for complete transfers

### Thread Safety
- **Distinct objects**: Safe
- **Shared objects**: Unsafe
- **Exception**: Synchronous `send`, `receive`, `connect`, and `shutdown` are thread-safe with respect to each other

### Error Handling
- `error::eof`: Connection closed by peer
- `error::would_block`: Non-blocking mode, no data
- `error::broken_pipe`: Connection broken

## Usage Examples

### TCP Client
```cpp
asio::io_context io_context;
asio::ip::tcp::socket socket(io_context);

// Connect to server
asio::ip::tcp::endpoint endpoint(
    asio::ip::address::from_string("192.168.1.100"), 80);
socket.connect(endpoint);

// Send HTTP request
std::string request = "GET / HTTP/1.1\r\nHost: example.com\r\n\r\n";
socket.send(asio::buffer(request));

// Receive response
char reply[1024];
size_t reply_length = socket.receive(asio::buffer(reply));
```

### Async TCP Server Connection Handler
```cpp
class session : public std::enable_shared_from_this<session> {
    asio::ip::tcp::socket socket_;
    char data_[1024];

public:
    void start() {
        do_read();
    }

private:
    void do_read() {
        auto self(shared_from_this());
        socket_.async_read_some(asio::buffer(data_),
            [this, self](std::error_code ec, std::size_t length) {
                if (!ec) {
                    do_write(length);
                }
            });
    }

    void do_write(std::size_t length) {
        auto self(shared_from_this());
        socket_.async_write_some(asio::buffer(data_, length),
            [this, self](std::error_code ec, std::size_t) {
                if (!ec) {
                    do_read();
                }
            });
    }
};
```

### Using High-Level Functions
```cpp
// Ensure all data is sent
asio::write(socket, asio::buffer(data));

// Read until specific delimiter
std::string response;
asio::read_until(socket, asio::dynamic_buffer(response), "\r\n");

// Read exact amount
char buffer[256];
asio::read(socket, asio::buffer(buffer));
```

## Design Rationale

1. **Stream Semantics**: Partial operations reflect stream nature
2. **Dual Interface**: Support both socket and stream programming models
3. **Efficiency**: Zero-copy operations with scatter-gather I/O
4. **Flexibility**: Compatible with high-level composed operations
5. **Consistency**: Same patterns as datagram sockets where applicable

## Common Patterns

### 1. Echo Server
```cpp
void handle_client(tcp::socket socket) {
    try {
        for (;;) {
            char data[1024];
            error_code error;
            size_t length = socket.read_some(buffer(data), error);
            if (error == error::eof)
                break; // Connection closed
            else if (error)
                throw system_error(error);
            
            write(socket, buffer(data, length));
        }
    } catch (std::exception& e) {
        // Handle error
    }
}
```

### 2. HTTP Client
```cpp
void http_get(const std::string& server, const std::string& path) {
    tcp::resolver resolver(io_context);
    tcp::socket socket(io_context);
    
    // Connect
    connect(socket, resolver.resolve(server, "http"));
    
    // Send request
    std::string request = "GET " + path + " HTTP/1.0\r\n\r\n";
    write(socket, buffer(request));
    
    // Read response
    std::string response;
    read_until(socket, dynamic_buffer(response), "\r\n\r\n");
}
```

### 3. Async Chain
```cpp
class connection {
    void start() {
        // Read header
        async_read(socket_, buffer(header_),
            [this](error_code ec, size_t) {
                if (!ec) {
                    // Read body based on header
                    async_read(socket_, buffer(body_, header_.size),
                        [this](error_code ec, size_t) {
                            if (!ec) {
                                process_request();
                            }
                        });
                }
            });
    }
};
```

## Key Differences from Datagram Sockets

1. **Connection-Oriented**: Must connect before send/receive
2. **Stream Semantics**: No message boundaries
3. **Reliability**: Guaranteed delivery and ordering
4. **Partial Operations**: Common and expected
5. **EOF Detection**: Clean connection termination

## Integration with Composed Operations
Works seamlessly with:
- `async_read()` / `async_write()`: Complete transfers
- `async_read_until()`: Delimiter-based reading
- `async_read_at()` / `async_write_at()`: Random access (with `seekable`)

## Key Takeaways
- Foundation for TCP and stream protocols
- Supports partial read/write operations
- Thread-safe synchronous operations
- Implements standard stream concepts
- Designed for use with composed operations