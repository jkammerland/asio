# Windows Networking in ASIO - Complete Summary

## Overview
ASIO uses **I/O Completion Ports (IOCP)** on Windows, providing a true proactive asynchronous I/O model where the operating system performs I/O operations and notifies completion through kernel-managed queues.

## IOCP Architecture

### Core Concept
- **Pattern**: Proactor pattern - OS performs the I/O operation
- **Mechanism**: Kernel queues completion notifications
- **Threading**: Automatic thread pool management by kernel
- **Files**: `asio/detail/win_iocp_socket_service.hpp`, `asio/detail/win_iocp_io_context.hpp`

### Key Components
```cpp
class win_iocp_io_context {
  HANDLE iocp_;              // I/O Completion Port handle
  long outstanding_work_;    // Track pending operations
  thread_info* thread_call_stack_;  // Per-thread call stack
};

class win_iocp_socket_service {
  win_iocp_io_context& io_context_;
  reactor* reactor_;         // For select operations
  win_iocp_socket_service_base* next_;
  win_iocp_socket_service_base* prev_;
};
```

## Windows Socket (Winsock) Integration

### Extended Socket Functions
Windows provides extended functions for async operations:
```cpp
// Traditional vs Extended Functions
accept()     → AcceptEx()
connect()    → ConnectEx()
send()       → WSASend()
recv()       → WSARecv()
sendto()     → WSASendTo()
recvfrom()   → WSARecvFrom()

// Windows-specific extensions
TransmitFile()      // Zero-copy file transmission
TransmitPackets()   // Scatter-gather with files
DisconnectEx()      // Reusable socket disconnect
```

### OVERLAPPED Structure
Central to async operations:
```cpp
typedef struct _OVERLAPPED {
  ULONG_PTR Internal;      // Status code
  ULONG_PTR InternalHigh;  // Bytes transferred
  union {
    struct {
      DWORD Offset;
      DWORD OffsetHigh;
    } DUMMYSTRUCTNAME;
    PVOID Pointer;
  } DUMMYUNIONNAME;
  HANDLE hEvent;           // Event handle (not used with IOCP)
} OVERLAPPED;

// ASIO's extended overlapped structure
class win_iocp_operation : public OVERLAPPED {
  void (*func_)(win_iocp_io_context*, win_iocp_operation*, DWORD, DWORD);
  win_iocp_operation* next_;
  // ... operation-specific data
};
```

## Operation Flow

### 1. Socket Creation and Association
```cpp
// Create socket
SOCKET sock = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP,
                        NULL, 0, WSA_FLAG_OVERLAPPED);

// Associate with IOCP
HANDLE iocp = CreateIoCompletionPort(
    (HANDLE)sock,     // Socket handle
    existing_iocp,    // IOCP handle
    completion_key,   // User data
    0                 // Concurrent threads (0 = CPU count)
);
```

### 2. Initiating Async Operations
```cpp
// Async accept
BOOL AcceptEx(
    SOCKET listen_socket,
    SOCKET accept_socket,
    PVOID output_buffer,
    DWORD buffer_length,
    DWORD local_addr_length,
    DWORD remote_addr_length,
    LPDWORD bytes_received,
    LPOVERLAPPED overlapped
);

// Async send
int WSASend(
    SOCKET socket,
    LPWSABUF buffers,
    DWORD buffer_count,
    LPDWORD bytes_sent,
    DWORD flags,
    LPWSAOVERLAPPED overlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine
);

// Async receive
int WSARecv(
    SOCKET socket,
    LPWSABUF buffers,
    DWORD buffer_count,
    LPDWORD bytes_received,
    LPDWORD flags,
    LPWSAOVERLAPPED overlapped,
    LPWSAOVERLAPPED_COMPLETION_ROUTINE completion_routine
);
```

### 3. Completion Processing
```cpp
void run(long timeout_ms) {
  DWORD bytes_transferred;
  ULONG_PTR completion_key;
  LPOVERLAPPED overlapped;

  // Wait for completion
  BOOL ok = GetQueuedCompletionStatus(
      iocp_,                // IOCP handle
      &bytes_transferred,   // Bytes transferred
      &completion_key,      // User data
      &overlapped,          // OVERLAPPED structure
      timeout_ms            // Timeout
  );

  if (overlapped) {
    // Cast to ASIO operation
    win_iocp_operation* op =
        static_cast<win_iocp_operation*>(overlapped);

    // Get error code
    DWORD last_error = ok ? 0 : GetLastError();

    // Execute completion handler
    op->complete(last_error, bytes_transferred);
  }
}
```

## Advanced Features

### 1. Zero-Copy Operations
```cpp
// TransmitFile for zero-copy file sending
BOOL TransmitFile(
    SOCKET socket,
    HANDLE file_handle,
    DWORD bytes_to_write,
    DWORD bytes_per_send,
    LPOVERLAPPED overlapped,
    LPTRANSMIT_FILE_BUFFERS transmit_buffers,
    DWORD flags
);

// Registered I/O (RIO) for ultra-high performance
RIO_RQ rio_queue = RIOCreateRequestQueue(
    socket,
    max_recv_count,
    max_recv_data_buffers,
    max_send_count,
    max_send_data_buffers,
    recv_cq,
    send_cq,
    NULL
);
```

### 2. Thread Pool Management
```cpp
// IOCP automatically manages thread pool
// Optimal thread count = CPU cores * 2

// Custom thread pool configuration
SYSTEM_INFO si;
GetSystemInfo(&si);
DWORD thread_count = si.dwNumberOfProcessors * 2;

for (DWORD i = 0; i < thread_count; ++i) {
  CreateThread(NULL, 0, WorkerThread, iocp, 0, NULL);
}

DWORD WINAPI WorkerThread(LPVOID iocp) {
  while (true) {
    GetQueuedCompletionStatus((HANDLE)iocp, ...);
    // Process completion
  }
}
```

### 3. Socket Reuse (SO_REUSEADDR)
```cpp
// Enable address reuse
BOOL reuse = TRUE;
setsockopt(socket, SOL_SOCKET, SO_REUSEADDR,
           (char*)&reuse, sizeof(reuse));

// DisconnectEx for connection reuse
BOOL DisconnectEx(
    SOCKET socket,
    LPOVERLAPPED overlapped,
    DWORD flags,  // TF_REUSE_SOCKET
    DWORD reserved
);
```

## Performance Characteristics

### Strengths
- **True Async I/O**: OS handles data transfer
- **Automatic Scaling**: Kernel manages thread wake-ups
- **Zero-Copy Capable**: Direct kernel buffer access
- **High Connection Count**: Efficient for 10K+ connections
- **Batch Completions**: GetQueuedCompletionStatusEx

### Weaknesses
- **Memory Overhead**: Per-socket memory requirements
- **Complexity**: More complex error handling
- **Windows-Only**: Not portable
- **Latency**: Slightly higher than select for few connections

### Performance Tuning
```cpp
// 1. Pre-allocate OVERLAPPED structures
class overlapped_pool {
  std::vector<win_iocp_operation> pool_;
  std::queue<win_iocp_operation*> free_list_;
};

// 2. Use scatter-gather I/O
WSABUF buffers[16];
WSASend(socket, buffers, 16, ...);

// 3. Batch completion processing
OVERLAPPED_ENTRY entries[32];
ULONG count;
GetQueuedCompletionStatusEx(
    iocp, entries, 32, &count, timeout, FALSE);

// 4. Set socket buffer sizes
int buffer_size = 64 * 1024;
setsockopt(socket, SOL_SOCKET, SO_SNDBUF,
           (char*)&buffer_size, sizeof(buffer_size));
setsockopt(socket, SOL_SOCKET, SO_RCVBUF,
           (char*)&buffer_size, sizeof(buffer_size));
```

## Error Handling

### Common Error Codes
```cpp
// Check operation result
if (!ok) {
  DWORD error = GetLastError();
  switch (error) {
    case ERROR_NETNAME_DELETED:     // Connection reset
    case ERROR_CONNECTION_ABORTED:   // Connection aborted
    case ERROR_OPERATION_ABORTED:    // Operation cancelled
    case ERROR_SEM_TIMEOUT:          // Timeout
    case WAIT_TIMEOUT:               // GetQueuedCompletionStatus timeout
      // Handle specific errors
      break;
  }
}

// WSA-specific errors
int wsa_error = WSAGetLastError();
switch (wsa_error) {
  case WSAECONNREFUSED:   // Connection refused
  case WSAECONNRESET:     // Connection reset by peer
  case WSAENETUNREACH:    // Network unreachable
  case WSAETIMEDOUT:      // Connection timed out
    // Handle network errors
    break;
}
```

### Cleanup and Cancellation
```cpp
// Cancel pending operations
CancelIoEx((HANDLE)socket, NULL);  // Cancel all
CancelIoEx((HANDLE)socket, overlapped);  // Cancel specific

// Proper cleanup sequence
shutdown(socket, SD_BOTH);
closesocket(socket);
CloseHandle(iocp);
```

## Windows-Specific Considerations

### 1. Winsock Initialization
```cpp
// Required before any socket operations
WSADATA wsa_data;
int result = WSAStartup(MAKEWORD(2, 2), &wsa_data);
if (result != 0) {
  // Handle error
}

// Cleanup on exit
WSACleanup();
```

### 2. Handle vs File Descriptor
```cpp
// Windows uses SOCKET (actually HANDLE)
SOCKET sock;  // Not int like POSIX

// Convert for C runtime functions
int fd = _open_osfhandle((intptr_t)sock, 0);

// Cannot use with select() directly
// Must use WSAPoll() or IOCP
```

### 3. Security and Permissions
```cpp
// Windows Firewall exceptions needed
// Administrator rights for raw sockets
// UAC considerations for binding to ports < 1024

// Set security attributes
SECURITY_ATTRIBUTES sa;
sa.nLength = sizeof(sa);
sa.lpSecurityDescriptor = NULL;
sa.bInheritHandle = FALSE;
```

## Integration with ASIO

### Implementation Selection
```cpp
// Automatic on Windows
#ifdef _WIN32
  #define ASIO_HAS_IOCP 1
#endif

// ASIO service implementation
class win_iocp_socket_service_base {
public:
  typedef win_iocp_socket_service_base_ext::native_handle_type
    native_handle_type;
  typedef win_iocp_socket_service_base_ext::implementation_type
    base_implementation_type;
};
```

### ASIO Abstractions
```cpp
// ASIO hides platform differences
asio::io_context io_ctx;  // Creates IOCP internally

asio::ip::tcp::socket socket(io_ctx);
socket.async_read_some(buffer,
  [](error_code ec, size_t bytes) {
    // Completion handler
    // IOCP complexity hidden
  });

io_ctx.run();  // Calls GetQueuedCompletionStatus
```

## Comparison with Other Platforms

| Feature | Windows IOCP | Linux epoll | Linux io_uring | macOS kqueue |
|---------|--------------|-------------|----------------|--------------|
| Model | Proactor | Reactor | Proactor | Reactor |
| True Async | Yes | No | Yes | No |
| Zero-Copy | Yes | Limited | Yes | Limited |
| Thread Pool | Automatic | Manual | Manual | Manual |
| Scalability | Excellent | Good | Excellent | Good |
| Complexity | High | Moderate | High | Moderate |
| Memory Use | High | Low | Medium | Low |

## Best Practices

### 1. Socket Creation
```cpp
// Always use WSA_FLAG_OVERLAPPED
SOCKET sock = WSASocket(
    AF_INET, SOCK_STREAM, IPPROTO_TCP,
    NULL, 0, WSA_FLAG_OVERLAPPED);

// Immediately associate with IOCP
CreateIoCompletionPort((HANDLE)sock, iocp, key, 0);
```

### 2. Buffer Management
```cpp
// Use persistent buffers for operations
class socket_state {
  char recv_buffer[8192];
  char send_buffer[8192];
  WSABUF recv_wsabuf;
  WSABUF send_wsabuf;
};

// Avoid allocation in hot path
// Reuse OVERLAPPED structures
```

### 3. Thread Management
```cpp
// Let IOCP manage threads
// Don't block in completion handlers
// Use strand for serialization if needed

asio::io_context::strand strand(io_ctx);
socket.async_read_some(buffer,
  strand.wrap([](error_code ec, size_t bytes) {
    // Serialized execution
  }));
```

### 4. Error Recovery
```cpp
// Robust error handling
void handle_completion(DWORD error, DWORD bytes) {
  if (error == ERROR_OPERATION_ABORTED) {
    // Socket closing, cleanup
    return;
  }

  if (error == ERROR_NETNAME_DELETED ||
      error == ERROR_CONNECTION_ABORTED) {
    // Connection lost, reconnect logic
    return;
  }

  // Continue operation
}
```

## Debugging Tips

### 1. Performance Counters
```powershell
# Monitor IOCP performance
perfmon.exe
# Add counters:
# - Process\IO Data Operations/sec
# - Process\IO Other Operations/sec
# - System\Context Switches/sec
```

### 2. Network Tracing
```cpp
// Enable Winsock tracing
// HKEY_LOCAL_MACHINE\System\CurrentControlSet\Services\WinSock2\Parameters
// Add DWORD: DebugFlags = 1

// Use Event Tracing for Windows (ETW)
logman start mytrace -p Microsoft-Windows-Winsock-AFD
```

### 3. Handle Leaks
```cpp
// Check handle count
HANDLE process = GetCurrentProcess();
DWORD handle_count;
GetProcessHandleCount(process, &handle_count);

// Use Application Verifier
// appverif.exe /verify application.exe
```

### 4. Visual Studio Debugging
```cpp
// Break on specific completions
if (overlapped == watched_operation) {
  __debugbreak();
}

// Visualize IOCP state
// Natvis visualizers for ASIO types
```

## Migration from POSIX

### Key Differences
1. **Socket Type**: SOCKET vs int
2. **Error Codes**: GetLastError() vs errno
3. **Close Function**: closesocket() vs close()
4. **Initialization**: WSAStartup required
5. **Async Model**: Completion vs readiness

### Code Adaptation
```cpp
// POSIX
int sock = socket(AF_INET, SOCK_STREAM, 0);
if (sock < 0) {
  perror("socket");
}
close(sock);

// Windows
SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
if (sock == INVALID_SOCKET) {
  printf("socket failed: %d\n", WSAGetLastError());
}
closesocket(sock);
```

## Summary

Windows IOCP provides the most scalable and efficient async I/O model in ASIO, with true kernel-level asynchronous operations. While it's more complex than reactor patterns (epoll/kqueue), it offers superior performance for high-connection-count servers. The automatic thread pool management and zero-copy capabilities make it ideal for demanding network applications. ASIO successfully abstracts most of the complexity while preserving the performance benefits.