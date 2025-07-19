# ASIO Basic Socket Implementation

## Overview
The `basic_socket` class template provides the core functionality common to both stream-oriented (TCP) and datagram-oriented (UDP) sockets. It serves as the base class for `basic_stream_socket` and `basic_datagram_socket`.

## File Location
- **Path**: `asio/include/asio/basic_socket.hpp`
- **Purpose**: Core socket implementation with common operations for all socket types

## Class Template Definition
```cpp
template <typename Protocol, typename Executor = any_io_executor>
class basic_socket : public socket_base
```

### Template Parameters
- **Protocol**: The protocol type (e.g., `tcp`, `udp`)
- **Executor**: The executor type for asynchronous operations (default: `any_io_executor`)

## Design Patterns

### 1. IO Object Pattern
- Uses `io_object_impl` for platform-specific implementation
- Delegates operations to a service layer
- Separates interface from implementation

### 2. Move Semantics
- Supports move construction and assignment
- Deleted copy constructor and assignment operator
- Enables efficient socket transfer between objects

### 3. Asynchronous Initiation
- Uses initiation objects for async operations
- Separates async operation setup from execution
- Ensures proper handler invocation

## Key Components

### Type Definitions
```cpp
typedef Executor executor_type;
typedef Protocol protocol_type;
typedef typename Protocol::endpoint endpoint_type;
typedef basic_socket<Protocol, Executor> lowest_layer_type;
```

### Native Handle Type
Platform-specific native socket handle:
- Windows Runtime: `null_socket_service`
- Windows IOCP: `win_iocp_socket_service`
- Linux io_uring: `io_uring_socket_service`
- Default (reactive): `reactive_socket_service`

## Constructor Variants

### 1. Basic Construction
```cpp
explicit basic_socket(const executor_type& ex)
explicit basic_socket(ExecutionContext& context)
```
- Creates socket without opening

### 2. Protocol-Based Construction
```cpp
basic_socket(const executor_type& ex, const protocol_type& protocol)
basic_socket(ExecutionContext& context, const protocol_type& protocol)
```
- Creates and opens socket with specified protocol

### 3. Endpoint-Based Construction
```cpp
basic_socket(const executor_type& ex, const endpoint_type& endpoint)
basic_socket(ExecutionContext& context, const endpoint_type& endpoint)
```
- Creates, opens, and binds socket to endpoint

### 4. Native Socket Construction
```cpp
basic_socket(const executor_type& ex, const protocol_type& protocol,
            const native_handle_type& native_socket)
```
- Wraps existing native socket

### 5. Move Construction
```cpp
basic_socket(basic_socket&& other) noexcept
template <typename Protocol1, typename Executor1>
basic_socket(basic_socket<Protocol1, Executor1>&& other)
```
- Supports cross-protocol/executor moves

## Core Operations

### Socket Lifecycle

#### open()
```cpp
void open(const protocol_type& protocol = protocol_type())
ASIO_SYNC_OP_VOID open(const protocol_type& protocol, error_code& ec)
```
- Opens socket with specified protocol
- Auto-opens in some operations (e.g., connect)

#### close()
```cpp
void close()
ASIO_SYNC_OP_VOID close(error_code& ec)
```
- Closes socket
- Cancels all pending async operations
- Operations complete with `operation_aborted`

#### release()
```cpp
native_handle_type release()
native_handle_type release(error_code& ec)
```
- Transfers ownership of native socket
- Cancels async operations
- Platform limitations on older Windows

### Connection Operations

#### bind()
```cpp
void bind(const endpoint_type& endpoint)
ASIO_SYNC_OP_VOID bind(const endpoint_type& endpoint, error_code& ec)
```
- Binds socket to local endpoint

#### connect()
```cpp
void connect(const endpoint_type& peer_endpoint)
ASIO_SYNC_OP_VOID connect(const endpoint_type& peer_endpoint, error_code& ec)
```
- Connects to remote endpoint
- Auto-opens socket if not open

#### async_connect()
```cpp
template <typename ConnectToken>
auto async_connect(const endpoint_type& peer_endpoint, ConnectToken&& token)
```
- Asynchronous connection
- Supports cancellation types
- Auto-opens socket if needed

### Socket Options

#### set_option()
```cpp
template <typename SettableSocketOption>
void set_option(const SettableSocketOption& option)
```
- Sets socket options (e.g., keep_alive, no_delay)

#### get_option()
```cpp
template <typename GettableSocketOption>
void get_option(GettableSocketOption& option) const
```
- Retrieves socket option values

### IO Control

#### io_control()
```cpp
template <typename IoControlCommand>
void io_control(IoControlCommand& command)
```
- Executes IO control commands (e.g., bytes_readable)

### Non-Blocking Mode

#### non_blocking()
```cpp
bool non_blocking() const
void non_blocking(bool mode)
```
- Controls socket's non-blocking mode
- Affects synchronous operations only

#### native_non_blocking()
```cpp
bool native_non_blocking() const
void native_non_blocking(bool mode)
```
- Controls native socket's non-blocking mode
- For custom system call integration

### Socket State

#### is_open()
```cpp
bool is_open() const
```
- Checks if socket is open

#### at_mark()
```cpp
bool at_mark() const
```
- Checks for out-of-band data mark

#### available()
```cpp
std::size_t available() const
```
- Returns bytes available for reading

### Endpoint Information

#### local_endpoint()
```cpp
endpoint_type local_endpoint() const
endpoint_type local_endpoint(error_code& ec) const
```
- Returns local bound endpoint

#### remote_endpoint()
```cpp
endpoint_type remote_endpoint() const
endpoint_type remote_endpoint(error_code& ec) const
```
- Returns remote connected endpoint

### Socket Operations

#### shutdown()
```cpp
void shutdown(shutdown_type what)
ASIO_SYNC_OP_VOID shutdown(shutdown_type what, error_code& ec)
```
- Disables send/receive operations
- Types: shutdown_receive, shutdown_send, shutdown_both

#### cancel()
```cpp
void cancel()
ASIO_SYNC_OP_VOID cancel(error_code& ec)
```
- Cancels all async operations
- Platform limitations on older Windows

### Wait Operations

#### wait()
```cpp
void wait(wait_type w)
ASIO_SYNC_OP_VOID wait(wait_type w, error_code& ec)
```
- Blocks until socket is ready
- Types: wait_read, wait_write, wait_error

#### async_wait()
```cpp
template <typename WaitToken>
auto async_wait(wait_type w, WaitToken&& token)
```
- Asynchronous wait for socket readiness
- Supports cancellation

## Platform-Specific Implementation

### Service Selection
```cpp
#if defined(ASIO_WINDOWS_RUNTIME)
  detail::null_socket_service<Protocol>
#elif defined(ASIO_HAS_IOCP)
  detail::win_iocp_socket_service<Protocol>
#elif defined(ASIO_HAS_IO_URING_AS_DEFAULT)
  detail::io_uring_socket_service<Protocol>
#else
  detail::reactive_socket_service<Protocol>
#endif
```

### Platform Considerations
1. **Windows XP/2003**: Limited cancel() support
2. **Windows < 8.1**: release() not supported
3. **IOCP**: Uses Windows completion ports
4. **io_uring**: Uses Linux io_uring for async I/O
5. **Reactive**: Uses select/epoll/kqueue

## Thread Safety
- **Distinct objects**: Safe
- **Shared objects**: Unsafe
- Async operations use executor for handler dispatch

## Design Rationale

1. **Template-based**: Compile-time protocol binding
2. **Service delegation**: Platform abstraction
3. **Move-only**: Clear ownership semantics
4. **Auto-open**: Convenience for connect operations
5. **Error handling**: Dual APIs (exceptions/error_code)

## Usage Example

```cpp
// TCP socket
asio::ip::tcp::socket socket(io_context);

// Open and bind
socket.open(asio::ip::tcp::v4());
socket.bind(asio::ip::tcp::endpoint(asio::ip::tcp::v4(), 12345));

// Set options
asio::socket_base::keep_alive option(true);
socket.set_option(option);

// Connect
socket.connect(remote_endpoint);

// Check state
if (socket.is_open()) {
    size_t bytes = socket.available();
    // ... use socket
}

// Graceful shutdown
socket.shutdown(asio::socket_base::shutdown_both);
socket.close();
```

## Key Takeaways
- Foundation for all ASIO socket types
- Platform-independent interface
- Comprehensive async support
- Strong ownership model with move semantics
- Flexible error handling options