# Windows ASIO Examples

This directory contains examples demonstrating Windows-specific features and optimizations in ASIO.

## Examples

### 1. iocp_server.cpp
A complete IOCP-based server demonstrating:
- Thread pool with IOCP work distribution
- Zero-byte receive optimization
- Windows-specific error handling
- SO_CONDITIONAL_ACCEPT usage
- Optimal thread pool sizing

### 2. windows_specific_features.cpp
Demonstrates Windows-specific ASIO features:
- Winsock initialization
- IOCP socket association
- ConnectEx availability and usage
- Zero-byte receive mechanics
- Immediate completion optimization
- Cancellation methods (CancelIoEx vs CancelIo)

## Building the Examples

### Requirements
- Windows SDK
- Visual Studio or MinGW compiler
- ASIO headers

### Visual Studio
```cmd
cl /EHsc iocp_server.cpp /I<path_to_asio> /link ws2_32.lib mswsock.lib
cl /EHsc windows_specific_features.cpp /I<path_to_asio> /link ws2_32.lib mswsock.lib
```

### MinGW
```cmd
g++ -std=c++11 iocp_server.cpp -I<path_to_asio> -lws2_32 -lmswsock -o iocp_server.exe
g++ -std=c++11 windows_specific_features.cpp -I<path_to_asio> -lws2_32 -lmswsock -o windows_features.exe
```

## Running the Examples

### IOCP Server
```cmd
iocp_server.exe 8080
```
Then connect with telnet or a TCP client:
```cmd
telnet localhost 8080
```

### Windows Features Demo
```cmd
windows_specific_features.exe
```

## Key Windows-Specific Concepts

### 1. I/O Completion Ports (IOCP)
- Kernel object for managing async I/O completions
- Automatically distributes work across threads
- No polling required - threads wake only when work is ready

### 2. Overlapped I/O
- All async operations use OVERLAPPED structures
- Operations can complete immediately or asynchronously
- Completion notification via IOCP

### 3. Thread Pool Best Practices
- Set thread count equal to CPU cores
- IOCP manages concurrency automatically
- Avoid creating too many threads

### 4. Performance Optimizations
- Zero-byte receives for stream readiness
- ConnectEx for async connects
- AcceptEx for efficient accepts
- Immediate completion detection

## Common Windows Error Codes

| Error | Value | Meaning | ASIO Mapping |
|-------|--------|---------|--------------|
| ERROR_IO_PENDING | 997 | Operation will complete later | Normal async |
| ERROR_NETNAME_DELETED | 64 | Connection closed | connection_reset/operation_aborted |
| ERROR_PORT_UNREACHABLE | 1234 | ICMP port unreachable | connection_refused |
| ERROR_MORE_DATA | 234 | Datagram truncated | Cleared (not an error) |

## Performance Tuning

### Socket Options
```cpp
// Low latency
socket.set_option(tcp::no_delay(true));

// High throughput
socket.set_option(socket_base::send_buffer_size(256 * 1024));
socket.set_option(socket_base::receive_buffer_size(256 * 1024));
```

### IOCP Tuning
- Keep operations pending to maintain throughput
- Use buffer pools to avoid allocation
- Batch operations when possible
- Monitor completion queue depth

## Debugging Tips

1. Use Event Viewer for system-level issues
2. Enable ASIO handler tracking
3. Monitor with Performance Monitor:
   - IOCP completion rate
   - Thread context switches
   - Network throughput
4. Use Visual Studio diagnostic tools for profiling