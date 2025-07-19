# ASIO Basic Socket Acceptor

## Overview
The `basic_socket_acceptor` class template is used for accepting new socket connections. It implements the server-side socket functionality for listening and accepting incoming connections.

## File Location
- **Path**: `asio/include/asio/basic_socket_acceptor.hpp`
- **Purpose**: Server socket implementation for accepting connections

## Class Template Definition
```cpp
template <typename Protocol, typename Executor = any_io_executor>
class basic_socket_acceptor : public socket_base
```

### Template Parameters
- **Protocol**: The protocol type (e.g., `tcp`)
- **Executor**: The executor type for asynchronous operations (default: `any_io_executor`)

## Design Patterns

### 1. Acceptor Pattern
- Separates connection establishment from connection use
- Listens for incoming connections
- Creates new socket objects for accepted connections

### 2. Move Semantics
- Supports move construction and assignment
- Deleted copy operations
- Cross-protocol/executor moves supported

### 3. Asynchronous Initiation
- Separate initiation objects for async operations
- Clean separation of concerns

## Key Components

### Type Definitions
```cpp
typedef Executor executor_type;
typedef Protocol protocol_type;
typedef typename Protocol::endpoint endpoint_type;
typedef implementation_defined native_handle_type;
```

## Constructor Variants

### 1. Basic Construction
```cpp
explicit basic_socket_acceptor(const executor_type& ex)
explicit basic_socket_acceptor(ExecutionContext& context)
```
- Creates acceptor without opening

### 2. Protocol-Based Construction
```cpp
basic_socket_acceptor(const executor_type& ex, const protocol_type& protocol)
```
- Creates and opens acceptor with protocol

### 3. Endpoint-Based Construction
```cpp
basic_socket_acceptor(const executor_type& ex, const endpoint_type& endpoint, 
                     bool reuse_addr = true)
```
- Creates, opens, binds, and listens
- Optionally sets SO_REUSEADDR

### 4. Native Acceptor Construction
```cpp
basic_socket_acceptor(const executor_type& ex, const protocol_type& protocol,
                     const native_handle_type& native_acceptor)
```
- Wraps existing native acceptor

## Core Operations

### Acceptor Lifecycle

#### open()
```cpp
void open(const protocol_type& protocol = protocol_type())
```
- Opens acceptor for specified protocol

#### bind()
```cpp
void bind(const endpoint_type& endpoint)
```
- Binds acceptor to local endpoint

#### listen()
```cpp
void listen(int backlog = socket_base::max_listen_connections)
```
- Places acceptor in listening state
- Default backlog: SOMAXCONN

#### close()
```cpp
void close()
```
- Closes acceptor
- Cancels pending async operations

### Accept Operations

#### 1. Basic Accept (into existing socket)
```cpp
template <typename Protocol1, typename Executor1>
void accept(basic_socket<Protocol1, Executor1>& peer)
```
- Accepts connection into provided socket
- Blocks until connection received

#### 2. Accept with Endpoint
```cpp
void accept(basic_socket<protocol_type, Executor1>& peer, 
           endpoint_type& peer_endpoint)
```
- Accepts connection and retrieves peer endpoint

#### 3. Accept Returning New Socket
```cpp
typename Protocol::socket::template rebind_executor<executor_type>::other
accept()
```
- Creates and returns new socket for connection

#### 4. Async Accept Operations
```cpp
// Into existing socket
template <typename AcceptToken>
auto async_accept(basic_socket<Protocol1, Executor1>& peer, AcceptToken&& token)

// With endpoint
template <typename AcceptToken>
auto async_accept(basic_socket<protocol_type, Executor1>& peer,
                 endpoint_type& peer_endpoint, AcceptToken&& token)

// Returning new socket
template <typename MoveAcceptToken>
auto async_accept(MoveAcceptToken&& token)
```

### Socket Options

#### set_option() / get_option()
```cpp
template <typename SettableSocketOption>
void set_option(const SettableSocketOption& option)
```
Common options:
- `reuse_address`: Allow address reuse
- `enable_connection_aborted`: Report aborted connections

### IO Control

#### io_control()
```cpp
template <typename IoControlCommand>
void io_control(IoControlCommand& command)
```

### Non-Blocking Mode

#### non_blocking()
```cpp
bool non_blocking() const
void non_blocking(bool mode)
```
- Controls acceptor's non-blocking mode

#### native_non_blocking()
```cpp
bool native_non_blocking() const
void native_non_blocking(bool mode)
```
- Controls native acceptor's non-blocking mode

### State Information

#### is_open()
```cpp
bool is_open() const
```

#### local_endpoint()
```cpp
endpoint_type local_endpoint() const
```
- Returns acceptor's bound endpoint

### Wait Operations

#### wait() / async_wait()
```cpp
void wait(wait_type w)
template <typename WaitToken>
auto async_wait(wait_type w, WaitToken&& token)
```
- Wait for acceptor readiness

## Thread Safety
- **Distinct objects**: Safe
- **Shared objects**: Unsafe
- **Exception**: Synchronous accept operations are thread-safe

## Platform-Specific Implementation

Uses same service selection as basic_socket:
- Windows Runtime: `null_socket_service`
- Windows IOCP: `win_iocp_socket_service`
- Linux io_uring: `io_uring_socket_service`
- Default: `reactive_socket_service`

## Usage Example

### Basic TCP Server
```cpp
asio::io_context io_context;
asio::ip::tcp::acceptor acceptor(io_context);

// Setup acceptor
asio::ip::tcp::endpoint endpoint(asio::ip::tcp::v4(), 8080);
acceptor.open(endpoint.protocol());
acceptor.set_option(asio::ip::tcp::acceptor::reuse_address(true));
acceptor.bind(endpoint);
acceptor.listen();

// Accept connections
while (true) {
    asio::ip::tcp::socket socket(io_context);
    acceptor.accept(socket);
    // Handle connection...
}
```

### Async Accept Example
```cpp
void start_accept() {
    acceptor.async_accept(
        [this](asio::error_code ec, asio::ip::tcp::socket socket) {
            if (!ec) {
                // Handle new connection
                handle_connection(std::move(socket));
                
                // Accept next connection
                start_accept();
            }
        });
}
```

## Design Rationale

1. **Separation of Concerns**: Acceptor handles only connection establishment
2. **Flexibility**: Multiple accept variants for different use cases
3. **Efficiency**: Move semantics for socket creation
4. **Consistency**: Similar API to basic_socket
5. **Thread Safety**: Safe concurrent accepts

## Common Patterns

### 1. One Acceptor, Multiple Handlers
```cpp
// Single acceptor, multiple async accepts
for (int i = 0; i < num_threads; ++i) {
    start_accept();
}
```

### 2. Accept with Error Handling
```cpp
acceptor.async_accept(
    [](error_code ec, tcp::socket socket) {
        if (ec == asio::error::operation_aborted) {
            // Acceptor was closed
        } else if (ec) {
            // Other error
        } else {
            // Success
        }
    });
```

## Key Takeaways
- Central component for server applications
- Supports both sync and async accept operations
- Flexible socket creation options
- Thread-safe synchronous accepts
- Platform-independent server socket abstraction