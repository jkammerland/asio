# Windows I/O Completion Ports (IOCP) in ASIO

## Overview

I/O Completion Ports (IOCP) is Windows' high-performance asynchronous I/O mechanism. ASIO leverages IOCP to provide efficient, scalable networking on Windows platforms.

## Key Concepts

### What is IOCP?

IOCP is a Windows kernel object that:
- Manages completion notifications for asynchronous I/O operations
- Efficiently distributes work across a thread pool
- Minimizes context switches and thread wake-ups
- Supports true asynchronous I/O (operations complete in kernel)

### Proactor vs Reactor Pattern

Unlike POSIX systems that typically use the Reactor pattern (epoll/kqueue), Windows IOCP implements the Proactor pattern:

- **Reactor (POSIX)**: "Tell me when I can perform I/O"
- **Proactor (Windows)**: "Tell me when I/O has completed"

## IOCP Integration in ASIO

### 1. Socket Creation and Association

```cpp
// From win_iocp_socket_service_base::do_open
socket_type sock = socket_ops::socket(family, type, protocol, ec);
if (sock != invalid_socket)
{
  // Associate socket with IOCP
  iocp_service_.register_handle(reinterpret_cast<HANDLE>(sock), ec);
}
```

### 2. Asynchronous Operations

ASIO initiates async operations using WSA functions with OVERLAPPED structures:

```cpp
// From start_send_op
DWORD bytes_transferred = 0;
int result = ::WSASend(impl.socket_, buffers, buffer_count,
    &bytes_transferred, flags, op, 0);
```

### 3. Completion Handling

When operations complete, IOCP notifies the waiting threads:

```cpp
// Completion notification flow:
// 1. Kernel completes I/O operation
// 2. Completion packet posted to IOCP
// 3. Worker thread woken by GetQueuedCompletionStatus
// 4. ASIO operation handler invoked
```

## IOCP-Specific Features

### 1. Zero-byte Reads

For stream sockets, ASIO uses zero-byte reads to detect when data is available:

```cpp
// From start_null_buffers_receive_op
if ((impl.state_ & socket_ops::stream_oriented) != 0)
{
  ::WSABUF buf = { 0, 0 };
  start_receive_op(impl, &buf, 1, flags, false, iocp_op);
}
```

### 2. ConnectEx Support

ASIO uses ConnectEx for asynchronous connects when available:

```cpp
// From get_connect_ex
GUID connect_ex_guid = WSAID_CONNECTEX;
connect_ex_fn connect_ex = 0;
DWORD bytes = 0;
if (::WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
    &connect_ex_guid, sizeof(connect_ex_guid),
    &connect_ex, sizeof(connect_ex), &bytes, 0, 0) != 0)
{
  // ConnectEx not available, fall back to reactor
}
```

### 3. AcceptEx Optimization

Uses AcceptEx for efficient accept operations with pre-allocated sockets.

## Thread Management

IOCP naturally manages thread concurrency:
- Threads block on GetQueuedCompletionStatus
- Kernel wakes threads as completions arrive
- Automatic load balancing across threads
- Configurable concurrency level

## Error Handling

Windows-specific errors are mapped to portable ASIO errors:

```cpp
// From complete_iocp_recv
if (ec.value() == ERROR_NETNAME_DELETED)
{
  if (cancel_token.expired())
    ec = asio::error::operation_aborted;
  else
    ec = asio::error::connection_reset;
}
else if (ec.value() == ERROR_PORT_UNREACHABLE)
{
  ec = asio::error::connection_refused;
}
```

## Performance Benefits

1. **Kernel-level completion**: No need to poll for readiness
2. **Efficient thread usage**: Threads only wake when work is ready
3. **Reduced system calls**: Operations complete without additional calls
4. **Natural scalability**: IOCP handles high connection counts efficiently