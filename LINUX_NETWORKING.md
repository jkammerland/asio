# Linux Networking in ASIO - Complete Summary

## Overview
ASIO provides two distinct networking backends for Linux:
1. **epoll reactor** (default) - Traditional reactive model using edge-triggered epoll
2. **io_uring service** (optional) - Modern proactive model with kernel ring buffers (Linux 5.1+)

## epoll Reactor (Default Implementation)

### Architecture
- **Pattern**: Reactor pattern with edge-triggered notifications
- **Syscall**: `epoll_wait()` for event notification
- **Speculative Execution**: Attempts operations before registration to reduce syscalls
- **Files**: `asio/detail/epoll_reactor.hpp`, `asio/detail/impl/epoll_reactor.ipp`

### Core Components
```cpp
class epoll_reactor {
  int epoll_fd_;           // Core epoll file descriptor
  int timer_fd_;           // Optional timerfd for efficient timers
  select_interrupter interrupter_;  // Wake-up mechanism
  object_pool<descriptor_state> registered_descriptors_;
};
```

### Operation Flow
1. **Speculative Attempt**: Try operation immediately
2. **Registration**: If operation blocks, register with epoll
3. **Edge-Triggered Wait**: `epoll_wait()` with EPOLLET flag
4. **Batch Processing**: Handle multiple operations per event
5. **Completion**: Execute handlers in io_context

### Performance Characteristics
- **Strengths**:
  - Mature and stable
  - Low memory overhead
  - O(1) event delivery
  - Good for connection-heavy workloads
- **Weaknesses**:
  - Syscall overhead for each operation
  - Less efficient for high-throughput scenarios
  - Requires careful edge-triggered handling

### Configuration
```cpp
// Enable epoll (automatic on Linux)
#define ASIO_HAS_EPOLL 1

// Tuning parameters
reactor.registration_locking = true/false
reactor.io_locking = true/false
reactor.registration_locking_spin_count = 100
reactor.io_locking_spin_count = 100
reactor.preallocated_io_objects = 256
```

## io_uring Service (High-Performance Alternative)

### Architecture
- **Pattern**: Proactor pattern with true async I/O
- **Mechanism**: Shared ring buffers between kernel and userspace
- **Zero Syscalls**: Operations submitted/completed without syscalls in fast path
- **Files**: `asio/detail/io_uring_service.hpp`, `asio/detail/impl/io_uring_service.ipp`

### Core Components
```cpp
class io_uring_service {
  struct io_uring ring_;   // liburing instance
  unsigned sq_len_;        // Submission queue length
  object_pool<io_object> io_objects_;
  op_queue<io_uring_operation> pending_ops_;
};
```

### Ring Buffer Architecture
```
┌─────────────────┐
│   User Space    │
│  ┌───────────┐  │
│  │    SQ     │  │ ← Application writes
│  └───────────┘  │
│  ┌───────────┐  │
│  │    CQ     │  │ ← Application reads
│  └───────────┘  │
├─────────────────┤
│   Kernel Space  │ ← Processes SQ, writes CQ
└─────────────────┘
```

### Operation Flow
1. **Get SQE**: `io_uring_get_sqe()` from submission queue
2. **Prepare**: `io_uring_prep_*()` operation setup
3. **Submit**: `io_uring_submit()` batch submission
4. **Wait**: `io_uring_wait_cqe()` for completions
5. **Process**: Handle CQEs and execute handlers

### Advanced Features
- **Fixed Buffers**: Register buffers for zero-copy
- **Kernel Polling**: SQPOLL for syscall-free operation
- **Linked Operations**: Chain operations for sequential execution
- **Buffer Selection**: Kernel-managed buffer pools

### Performance Characteristics
- **Strengths**:
  - Minimal syscall overhead
  - True zero-copy capabilities
  - Batch submission/completion
  - Better CPU cache utilization
  - Excellent for high-throughput
- **Weaknesses**:
  - Requires newer kernels (5.1+, optimal 5.6+)
  - Higher memory usage for ring buffers
  - More complex implementation

### Configuration
```cpp
// Build with io_uring
#define ASIO_HAS_IO_URING 1
#define ASIO_HAS_IO_URING_AS_DEFAULT 1  // Use as default

// Link with liburing
-luring

// Ring size configuration
unsigned entries = 1024;  // Default: 128
io_uring_queue_init(entries, &ring_, 0);

// Kernel polling for ultra-low latency
params.flags |= IORING_SETUP_SQPOLL;
params.sq_thread_idle = 1000;  // ms
```

## Platform-Specific Socket Operations

### Common Operations
Both backends implement these operations differently:
- `socket()`, `bind()`, `listen()`, `accept()`
- `connect()`, `send()`, `recv()`, `sendto()`, `recvfrom()`
- `setsockopt()`, `getsockopt()`, `ioctl()`

### epoll-Specific Behavior
```cpp
// Edge-triggered registration
epoll_event ev;
ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
ev.data.ptr = descriptor_state;
epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);

// Speculative execution
if (allow_speculative) {
  if (op->perform() == reactor_op::done) {
    return;  // Completed immediately
  }
}
// Register for notification
```

### io_uring-Specific Behavior
```cpp
// Direct submission
io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
io_uring_prep_send(sqe, socket, buf, len, flags);
io_uring_sqe_set_data(sqe, operation);
io_uring_submit(&ring_);

// Completion handling
io_uring_cqe* cqe;
io_uring_wait_cqe(&ring_, &cqe);
if (cqe->res < 0) {
  // Error: -cqe->res is errno
} else {
  // Success: cqe->res is bytes transferred
}
```

## Performance Tuning Guide

### For epoll
1. **Connection-Heavy Workloads**:
   - Enable locking for thread safety
   - Increase preallocated objects
   - Use edge-triggered mode (default)

2. **Low-Latency Requirements**:
   - Disable locking if single-threaded
   - Enable spin counts
   - Use timerfd for precise timers

3. **Mixed Workloads**:
   - Default settings with moderate preallocation
   - Balance between memory and performance

### For io_uring
1. **High-Throughput Applications**:
   - Large ring sizes (1024-4096)
   - Batch operations
   - Fixed buffers for zero-copy

2. **Ultra-Low Latency**:
   - Enable kernel polling (SQPOLL)
   - Pin SQ thread to CPU
   - Use fixed files

3. **Resource-Constrained**:
   - Smaller ring sizes
   - Disable kernel polling
   - Standard buffer management

## Build Requirements

### epoll
- Linux kernel 2.6.8+ (epoll support)
- Linux kernel 2.6.22+ (timerfd support)
- glibc 2.8+ (epoll_create1)

### io_uring
- Linux kernel 5.1+ (basic io_uring)
- Linux kernel 5.6+ (full features)
- liburing-dev package
- GCC 9+ or Clang 10+

## Comparison with Other Platforms

| Aspect | Linux epoll | Linux io_uring | Windows IOCP | macOS kqueue |
|--------|------------|----------------|--------------|--------------|
| Model | Reactive | Proactive | Proactive | Reactive |
| Syscall overhead | Moderate | Minimal | Minimal | Moderate |
| Zero-copy | Limited | Full | Full | Limited |
| Thread scaling | Good | Excellent | Excellent | Good |
| Maturity | Very mature | New (2019+) | Mature | Mature |
| Complexity | Moderate | High | High | Moderate |

## Migration Path

### From epoll to io_uring
1. **No code changes required** - ASIO abstracts the differences
2. **Build configuration**:
   ```bash
   # Add to build flags
   -DASIO_HAS_IO_URING -luring
   ```
3. **Runtime detection**:
   ```cpp
   #ifdef ASIO_HAS_IO_URING
     // io_uring is available
   #endif
   ```
4. **Performance testing**: Benchmark both implementations
5. **Gradual rollout**: Use feature flags for production

## Debugging Tips

### epoll Issues
- Use `strace -e epoll_wait,epoll_ctl` to trace epoll calls
- Check `/proc/sys/fs/epoll/max_user_watches` for limits
- Monitor with `ss -s` for socket statistics
- Use `lsof -p <pid>` to check file descriptors

### io_uring Issues
- Use `bpftrace` for io_uring tracing
- Check `dmesg` for kernel messages
- Monitor `/proc/<pid>/fdinfo/` for ring buffer state
- Use `io_uring-bench` for performance testing

## Best Practices

1. **Choose the right backend**:
   - epoll: Stable, production-tested, wide kernel support
   - io_uring: Maximum performance, newer kernels only

2. **Handle platform differences**:
   - Test on target kernel versions
   - Provide fallback for older systems
   - Monitor performance metrics

3. **Optimize for your workload**:
   - Connection-heavy: epoll may suffice
   - Data-heavy: io_uring excels
   - Mixed: Benchmark both

4. **Resource management**:
   - Set appropriate file descriptor limits
   - Monitor memory usage (especially io_uring)
   - Handle EMFILE/ENFILE errors gracefully

5. **Security considerations**:
   - io_uring requires CAP_SYS_ADMIN for some features
   - Consider seccomp filters
   - Validate all async operation results