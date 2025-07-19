# Windows Socket Service Architecture

## Overview

ASIO's Windows socket service architecture is built around `win_iocp_socket_service` and `win_iocp_socket_service_base`, providing IOCP-based asynchronous operations.

## Class Hierarchy

```
execution_context_service_base<win_iocp_socket_service<Protocol>>
    ↑
win_iocp_socket_service<Protocol>
    ↑
win_iocp_socket_service_base
```

## Core Components

### 1. win_iocp_socket_service_base

Base class providing IOCP integration for all socket types:

```cpp
class win_iocp_socket_service_base
{
  // Core members
  execution_context& context_;           // Execution context
  win_iocp_io_context& iocp_service_;   // IOCP service
  select_reactor* reactor_;              // Fallback reactor
  void* connect_ex_;                     // ConnectEx function pointer
  void* nt_set_info_;                   // NtSetInformationFile pointer
  
  // Implementation tracking
  base_implementation_type* impl_list_; // Linked list of sockets
  asio::detail::mutex mutex_;          // Protects impl_list_
};
```

### 2. base_implementation_type

Per-socket state and data:

```cpp
struct base_implementation_type
{
  socket_type socket_;                          // Native socket handle
  socket_ops::state_type state_;               // Socket state flags
  socket_ops::shared_cancel_token_type cancel_token_; // Cancellation token
  select_reactor::per_descriptor_data reactor_data_;  // Reactor data
  
#if defined(ASIO_ENABLE_CANCELIO)
  DWORD safe_cancellation_thread_id_;         // Thread ID for CancelIo
#endif
  
  // Linked list pointers
  base_implementation_type* next_;
  base_implementation_type* prev_;
};
```

### 3. win_iocp_socket_service<Protocol>

Protocol-specific service implementation:

```cpp
template <typename Protocol>
class win_iocp_socket_service : public win_iocp_socket_service_base
{
  // Protocol-specific implementation
  struct implementation_type : base_implementation_type
  {
    protocol_type protocol_;
    bool have_remote_endpoint_;
    endpoint_type remote_endpoint_;
  };
};
```

## Operation Flow

### 1. Socket Creation

```cpp
asio::error_code open(implementation_type& impl,
    const protocol_type& protocol, asio::error_code& ec)
{
  if (!do_open(impl, protocol.family(),
        protocol.type(), protocol.protocol(), ec))
  {
    impl.protocol_ = protocol;
    // Socket associated with IOCP in do_open
  }
}
```

### 2. Asynchronous Send Operation

```cpp
template <typename ConstBufferSequence, typename Handler>
void async_send(base_implementation_type& impl,
    const ConstBufferSequence& buffers, socket_base::message_flags flags,
    Handler& handler, const IoExecutor& io_ex)
{
  // 1. Allocate operation object
  typedef win_iocp_socket_send_op<...> op;
  operation* o = new op(impl.cancel_token_, buffers, handler, io_ex);
  
  // 2. Register cancellation if needed
  if (slot.is_connected())
    o = &slot.template emplace<iocp_op_cancellation>(impl.socket_, o);
  
  // 3. Start IOCP operation
  start_send_op(impl, bufs.buffers(), bufs.count(), flags, false, o);
}
```

### 3. Operation Completion

```cpp
void start_send_op(base_implementation_type& impl,
    WSABUF* buffers, std::size_t buffer_count,
    socket_base::message_flags flags, bool noop, operation* op)
{
  DWORD bytes_transferred = 0;
  int result = ::WSASend(impl.socket_, buffers, buffer_count,
      &bytes_transferred, flags, op, 0);
  
  DWORD last_error = ::WSAGetLastError();
  if (last_error == ERROR_IO_PENDING)
  {
    // Operation will complete asynchronously
    iocp_service_.on_pending(op);
  }
  else
  {
    // Operation completed immediately
    iocp_service_.on_completion(op, last_error, bytes_transferred);
  }
}
```

## Key Features

### 1. Hybrid Approach

ASIO uses both IOCP and select reactor:
- **IOCP**: Primary mechanism for data transfer operations
- **Reactor**: Used for connect operations and null buffer receives

### 2. Cancellation Support

Three cancellation mechanisms:
1. **CancelIoEx** (Vista+): Cancel from any thread
2. **CancelIo** (XP): Cancel from initiating thread only
3. **Socket closure**: Ultimate cancellation method

### 3. Per-Operation State

Each async operation has its own state object:
- Contains buffers, handler, and completion logic
- Passed as OVERLAPPED structure to WSA functions
- Retrieved on completion via IOCP

### 4. Thread Safety

- Socket list protected by mutex
- Cancel token prevents race conditions
- Safe cancellation thread ID tracking

## Operation Types

### 1. Data Transfer Operations
- `win_iocp_socket_send_op`: Async send
- `win_iocp_socket_recv_op`: Async receive
- `win_iocp_socket_recvfrom_op`: Async receive with address
- `win_iocp_socket_recvmsg_op`: Async receive with flags

### 2. Connection Operations
- `win_iocp_socket_accept_op`: Async accept
- `win_iocp_socket_connect_op`: Async connect

### 3. Special Operations
- `win_iocp_null_buffers_op`: Wait for readiness
- `win_iocp_wait_op`: Wait for socket events

## Memory Management

ASIO uses custom allocation for operations:
- Handler-associated allocation
- Operation recycling for performance
- Proper cleanup on cancellation

## Integration Points

1. **IOCP Service**: Central completion port management
2. **Reactor**: Fallback for non-IOCP operations
3. **Socket Ops**: Low-level socket operations
4. **Winsock Init**: Ensures proper initialization