# ASIO Socket Base Architecture

## Overview
The `socket_base` class serves as the foundational base class for all socket types in ASIO. It provides common definitions, enumerations, and socket options that are shared across different socket implementations (`basic_stream_socket` and `basic_datagram_socket`).

## File Location
- **Path**: `asio/include/asio/socket_base.hpp`
- **Purpose**: Defines the base class for socket operations with common types and options

## Class Design

### socket_base Class
```cpp
class socket_base
{
public:
    // Enumerations and typedefs
    // Socket options
protected:
    ~socket_base() {}  // Protected destructor prevents direct instantiation
};
```

### Design Pattern
- **Base Class Pattern**: Provides common interface and types for derived socket classes
- **Protected Destructor**: Prevents deletion through base class pointer (no virtual destructor needed)
- **Type-Safe Options**: Uses template-based socket options for compile-time safety

## Key Components

### 1. Shutdown Types
```cpp
enum shutdown_type
{
    shutdown_receive = SHUT_RD,  // Shutdown receive side
    shutdown_send = SHUT_WR,     // Shutdown send side
    shutdown_both = SHUT_RDWR    // Shutdown both sides
};
```
- Maps to platform-specific shutdown constants
- Used for graceful socket shutdown operations

### 2. Message Flags
```cpp
typedef int message_flags;

static const int message_peek = MSG_PEEK;           // Peek at data
static const int message_out_of_band = MSG_OOB;     // Out-of-band data
static const int message_do_not_route = MSG_DONTROUTE; // Bypass routing
static const int message_end_of_record = MSG_EOR;   // End of record marker
```
- Flags for send/receive operations
- Platform-independent interface to OS-specific flags

### 3. Wait Types
```cpp
enum wait_type
{
    wait_read,   // Wait for read readiness
    wait_write,  // Wait for write readiness
    wait_error   // Wait for error conditions
};
```
- Used with `wait()` and `async_wait()` operations
- Enables event-based socket programming

## Socket Options

### Boolean Options
1. **broadcast** - Enable/disable broadcast messages (UDP)
2. **debug** - Enable socket-level debugging
3. **do_not_route** - Bypass routing, use local interfaces only
4. **keep_alive** - Enable TCP keep-alive probes
5. **reuse_address** - Allow address reuse (SO_REUSEADDR)
6. **out_of_band_inline** - Place out-of-band data inline
7. **enable_connection_aborted** - Custom option for accept behavior

### Integer Options
1. **send_buffer_size** - Socket send buffer size
2. **send_low_watermark** - Send low watermark
3. **receive_buffer_size** - Socket receive buffer size
4. **receive_low_watermark** - Receive low watermark

### Special Options
1. **linger** - Control behavior on close with pending data
   ```cpp
   linger option(true, 30); // Enable linger with 30 second timeout
   ```

## IO Control Commands
```cpp
typedef asio::detail::io_control::bytes_readable bytes_readable;
```
- **bytes_readable**: Get available data without blocking (FIONREAD)

## Constants
```cpp
static const int max_listen_connections = SOMAXCONN;
```
- Maximum pending connections for listen queue

## Platform-Specific Considerations

### Macro Usage
- `ASIO_OS_DEF()`: Maps to platform-specific constants
- `ASIO_STATIC_CONSTANT()`: Portable static constant definition
- `GENERATING_DOCUMENTATION`: Documentation generation mode

### Platform Mapping Examples
```cpp
shutdown_receive = ASIO_OS_DEF(SHUT_RD)   // Maps to SHUT_RD on POSIX
message_peek = ASIO_OS_DEF(MSG_PEEK)      // Maps to MSG_PEEK
```

## Usage Pattern

### Setting Socket Options
```cpp
asio::ip::tcp::socket socket(io_context);
asio::socket_base::keep_alive option(true);
socket.set_option(option);
```

### Getting Socket Options
```cpp
asio::socket_base::receive_buffer_size option;
socket.get_option(option);
int size = option.value();
```

## Design Rationale

1. **Type Safety**: Template-based options provide compile-time type checking
2. **Platform Independence**: Abstracts OS-specific constants and behaviors
3. **Extensibility**: Easy to add new socket options following the same pattern
4. **Zero Overhead**: No virtual functions, minimal runtime cost
5. **Clear Ownership**: Protected destructor prevents misuse

## Integration Points
- Used as base class by:
  - `basic_stream_socket` (TCP-style sockets)
  - `basic_datagram_socket` (UDP-style sockets)
  - `basic_socket_acceptor` (Server acceptors)
- Works with:
  - `detail::socket_option` namespace for option implementations
  - `detail::io_control` for IO control commands
  - Platform-specific socket implementations

## Key Takeaways
- Provides unified interface for all socket types
- Encapsulates platform differences
- Type-safe socket option handling
- Foundation for ASIO's socket abstraction hierarchy