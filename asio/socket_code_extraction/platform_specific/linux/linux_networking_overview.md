# Linux Networking Implementation in ASIO

## Overview

ASIO provides two distinct networking implementations for Linux:
1. **Traditional epoll-based reactive model** - The default implementation using edge-triggered epoll
2. **Modern io_uring-based proactive model** - High-performance kernel-bypass implementation (requires Linux 5.1+)

## Architecture Comparison

### Traditional Reactive Model (epoll)
- Based on the reactor pattern
- Uses edge-triggered epoll for scalable event notification
- Operations are initiated speculatively, then registered with epoll if they would block
- Supports timerfd for efficient timer handling when available

### Modern Proactive Model (io_uring)
- Based on kernel-bypass ring buffers
- Submission Queue (SQ) and Completion Queue (CQ) shared between kernel and userspace
- True asynchronous operations - no blocking syscalls in the fast path
- Significantly reduced syscall overhead

## Implementation Structure

### Common Components
- Socket services provide the high-level interface
- Operations are represented as command objects
- Per-socket state tracking
- Integration with ASIO's scheduler/executor framework

### Platform Selection
The implementation is selected at compile time based on:
- `ASIO_HAS_EPOLL` - Enables epoll reactor (default on Linux)
- `ASIO_HAS_IO_URING` - Enables io_uring support (requires liburing)
- `ASIO_HAS_IO_URING_AS_DEFAULT` - Makes io_uring the default implementation

## Key Differences from Windows IOCP

| Aspect | Linux epoll | Linux io_uring | Windows IOCP |
|--------|-------------|----------------|--------------|
| Model | Reactive | Proactive | Proactive |
| Syscall overhead | Moderate | Minimal | Minimal |
| Kernel integration | Traditional | Ring buffers | Kernel queues |
| Speculative operations | Yes | No | No |
| Zero-copy potential | Limited | High | High |
| Thread scaling | Good | Excellent | Excellent |

## Performance Characteristics

### epoll Reactor
- **Strengths**: 
  - Mature and stable
  - Low memory overhead
  - Good for connection-heavy workloads
- **Weaknesses**:
  - Syscall overhead for each operation
  - Less efficient for high-throughput scenarios

### io_uring Service  
- **Strengths**:
  - Minimal syscall overhead
  - True zero-copy capabilities
  - Better CPU cache utilization
  - Excellent for high-throughput scenarios
- **Weaknesses**:
  - Requires newer kernels (5.1+)
  - Higher memory usage for ring buffers
  - More complex implementation

## Configuration and Tuning

### epoll Configuration
- `reactor.registration_locking` - Enable/disable mutex for descriptor registration
- `reactor.io_locking` - Enable/disable per-descriptor locking
- `reactor.preallocated_io_objects` - Pre-allocate descriptor state objects

### io_uring Configuration
- Ring buffer sizes can be tuned based on workload
- Supports various operation flags for optimization
- Can use kernel-side polling for ultra-low latency

## Build Requirements

### For epoll (default)
- Linux kernel 2.6.8+ (for epoll)
- Linux kernel 2.6.22+ (for timerfd support)

### For io_uring
- Linux kernel 5.1+ (basic support)
- Linux kernel 5.6+ (full feature set)
- liburing development package
- Compile with `-DASIO_HAS_IO_URING`