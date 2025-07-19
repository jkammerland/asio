# Key Differences: Windows vs POSIX Implementations

## Overview

ASIO provides a unified interface across platforms, but the underlying implementations differ significantly between Windows and POSIX systems.

## Fundamental Differences

### 1. I/O Model

| Aspect | Windows | POSIX |
|--------|---------|--------|
| Pattern | Proactor (IOCP) | Reactor (epoll/kqueue) |
| Operations | True async (kernel completes) | Ready notification |
| Blocking | Operations never block | Must check readiness |
| Buffers | Provided upfront | Provided when ready |

### 2. System Calls

**Windows:**
```cpp
// Async send
::WSASend(socket, buffers, buffer_count, 
          &bytes_transferred, flags, overlapped, 0);

// Async receive  
::WSARecv(socket, buffers, buffer_count,
          &bytes_transferred, &flags, overlapped, 0);
```

**POSIX:**
```cpp
// Check readiness first
epoll_wait(epfd, events, maxevents, timeout);

// Then perform I/O
::send(socket, buffer, length, flags);
::recv(socket, buffer, length, flags);
```

## Implementation Differences

### 1. Socket Creation

**Windows:**
```cpp
// Must initialize Winsock first
winsock_init<> init;

// Create socket and associate with IOCP
socket_type sock = ::WSASocket(family, type, protocol, 0, 0, WSA_FLAG_OVERLAPPED);
iocp_service.register_handle(reinterpret_cast<HANDLE>(sock), ec);
```

**POSIX:**
```cpp
// Direct socket creation
socket_type sock = ::socket(family, type, protocol);
// Register with reactor
reactor.register_descriptor(sock, reactor_data);
```

### 2. Async Operations

**Windows (IOCP):**
```cpp
void start_send_op(impl, buffers, count, flags, op)
{
  // Operation includes OVERLAPPED structure
  DWORD bytes = 0;
  int result = ::WSASend(impl.socket_, buffers, count,
                        &bytes, flags, op, 0);
  
  if (::WSAGetLastError() == ERROR_IO_PENDING)
    iocp_service_.on_pending(op);  // Will complete later
  else
    iocp_service_.on_completion(op, ec, bytes); // Immediate
}
```

**POSIX (Reactor):**
```cpp
void start_send_op(impl, buffers, count, flags, op)
{
  // Try immediate send
  size_t bytes = socket_ops::send(impl.socket_, buffers, count, flags, ec);
  
  if (ec == error::would_block)
    reactor_.start_op(impl.socket_, reactor::write_op, op); // Wait for ready
  else
    scheduler_.post_immediate_completion(op, ec, bytes);
}
```

### 3. Cancellation

**Windows:**
```cpp
// Vista+ - Cancel from any thread
::CancelIoEx(socket_handle, overlapped);

// XP - Must cancel from initiating thread
::CancelIo(socket_handle);

// Track safe cancellation thread ID
impl.safe_cancellation_thread_id_ = ::GetCurrentThreadId();
```

**POSIX:**
```cpp
// Remove from reactor
reactor_.cancel_ops(socket, reactor_data);

// Close socket to force cancellation
::close(socket);
```

### 4. Connect Operations

**Windows:**
```cpp
// Use ConnectEx for async connect (if available)
connect_ex_fn connect_ex = get_connect_ex(impl, type);
if (connect_ex)
{
  BOOL result = connect_ex(impl.socket_, addr, addrlen,
                          0, 0, 0, op);
  // Completes via IOCP
}
else
{
  // Fall back to reactor-based connect
  start_reactor_op(impl, select_reactor::connect_op, op);
}
```

**POSIX:**
```cpp
// Non-blocking connect
int result = ::connect(socket, addr, addrlen);
if (result == -1 && errno == EINPROGRESS)
{
  // Wait for writeable
  reactor_.start_op(socket, reactor::connect_op, op);
}
```

### 5. Accept Operations

**Windows:**
```cpp
// Pre-create accept socket
SOCKET new_socket = ::WSASocket(family, type, protocol, 0, 0, WSA_FLAG_OVERLAPPED);

// Use AcceptEx
DWORD bytes_read = 0;
BOOL result = ::AcceptEx(impl.socket_, new_socket,
                        output_buffer, 0,
                        address_length, address_length,
                        &bytes_read, op);
```

**POSIX:**
```cpp
// Accept creates new socket
socket_type new_socket = ::accept(socket, addr, addrlen);
```

## Error Handling Differences

### Windows-Specific Errors

```cpp
// From complete_iocp_recv
switch (ec.value())
{
case ERROR_NETNAME_DELETED:
  // Socket closed by peer or cancelled
  ec = cancel_token.expired() ? 
       error::operation_aborted : error::connection_reset;
  break;
  
case ERROR_PORT_UNREACHABLE:
  ec = error::connection_refused;
  break;
  
case WSAEMSGSIZE:
case ERROR_MORE_DATA:
  // Datagram truncated - not an error
  ec.clear();
  break;
}
```

### POSIX Errors

```cpp
// Standard errno mapping
switch (errno)
{
case ECONNRESET:
  ec = error::connection_reset;
  break;
  
case ECONNREFUSED:
  ec = error::connection_refused;
  break;
  
case EMSGSIZE:
  ec = error::message_size;
  break;
}
```

## Performance Implications

### Windows Advantages:
1. True async I/O - no polling required
2. Kernel manages thread wake-ups efficiently
3. Natural thread pool integration
4. Excellent for high connection counts

### POSIX Advantages:
1. Lower latency for ready operations
2. More predictable behavior
3. Better for low-latency scenarios
4. Simpler cancellation model

## Platform-Specific Features

### Windows Only:
- Zero-byte receives for stream readiness
- IOCP thread pool management
- Overlapped I/O with immediate completion
- Advanced socket options via WSAIoctl

### POSIX Only:
- Signal-driven I/O (SIGIO)
- File descriptor passing
- More granular reactor events
- Native fork() support

## Winsock Initialization

Windows requires explicit Winsock initialization:

```cpp
class winsock_init
{
  winsock_init()
  {
    WSADATA wsa_data;
    result_ = ::WSAStartup(MAKEWORD(2, 2), &wsa_data);
  }
  
  ~winsock_init()
  {
    ::WSACleanup();
  }
};

// Global instance ensures initialization
static const winsock_init<>& winsock_init_instance = winsock_init<>(false);
```

POSIX systems require no special initialization.