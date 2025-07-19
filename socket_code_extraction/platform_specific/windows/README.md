# ASIO Windows Networking Implementation

This documentation provides a comprehensive overview of ASIO's Windows-specific networking implementation, focusing on the use of I/O Completion Ports (IOCP) and key architectural differences from POSIX systems.

## Table of Contents

1. [Windows IOCP Overview](./iocp_overview.md)
2. [Socket Service Architecture](./socket_service_architecture.md)
3. [Key Differences from POSIX](./posix_differences.md)
4. [Performance Characteristics](./performance_characteristics.md)
5. [Code Examples](./examples/)

## Quick Summary

ASIO's Windows implementation leverages I/O Completion Ports (IOCP) for high-performance asynchronous I/O operations. The architecture differs significantly from POSIX systems:

- **IOCP-based**: Uses Windows I/O Completion Ports for async operations
- **Proactor Pattern**: True asynchronous I/O (operations complete in kernel)
- **Thread Pool Integration**: IOCP manages thread pool efficiently
- **WSA Functions**: Uses WSASend, WSARecv, etc. for network operations
- **Hybrid Approach**: Falls back to select reactor for some operations

## Key Components

### 1. Win IOCP Socket Service
- `win_iocp_socket_service`: Template class for protocol-specific services
- `win_iocp_socket_service_base`: Base implementation with IOCP integration

### 2. Socket Operations
- `socket_ops`: Low-level socket operations with Windows-specific implementations
- Separate code paths for IOCP vs non-IOCP operations

### 3. Winsock Initialization
- `winsock_init`: Ensures proper Winsock 2.2 initialization
- Reference counting for multi-use scenarios

## Architecture Highlights

1. **True Asynchronous I/O**: Operations complete in kernel space
2. **Efficient Thread Usage**: IOCP manages thread wakeup
3. **Cancellation Support**: Uses CancelIoEx on Vista+
4. **Error Mapping**: Windows-specific errors mapped to portable ones
5. **Reactor Fallback**: Uses select reactor for certain operations