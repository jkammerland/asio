# macOS/BSD Networking in ASIO - Complete Summary

## Overview
ASIO uses the **kqueue** event notification mechanism on macOS and BSD systems, providing efficient, scalable asynchronous I/O through a unified event interface.

## kqueue Reactor Implementation

### Architecture
- **Pattern**: Reactor pattern with filter-based event model
- **Syscall**: `kevent()` for registration and notification
- **Edge-Triggered**: Uses EV_CLEAR flag for edge-triggered behavior
- **Files**: `asio/detail/kqueue_reactor.hpp`, `asio/detail/impl/kqueue_reactor.ipp`

### Core Components
```cpp
class kqueue_reactor {
  int kqueue_fd_;          // kqueue descriptor
  select_interrupter interrupter_;  // Pipe-based interruption
  timer_queue_set timer_queues_;    // Integrated timers
  object_pool<descriptor_state> registered_descriptors_;
};

struct descriptor_state {
  mutex mutex_;
  op_queue<reactor_op> op_queue_[max_ops];
  bool shutdown_;
};
```

### Event Model
kqueue uses a **filter-based** approach rather than event masks:
```cpp
struct kevent {
  uintptr_t ident;  // Identifier (file descriptor)
  int16_t filter;   // Filter type (EVFILT_READ, EVFILT_WRITE)
  uint16_t flags;   // Action flags (EV_ADD, EV_DELETE, EV_CLEAR)
  uint32_t fflags;  // Filter-specific flags
  intptr_t data;    // Filter-specific data
  void* udata;      // User data pointer
};
```

## Key Differences from Linux epoll

| Feature | kqueue (macOS/BSD) | epoll (Linux) |
|---------|-------------------|---------------|
| Event Model | Filter-based | Event mask |
| Modification | DELETE + ADD | EPOLL_CTL_MOD |
| Edge-Triggered | EV_CLEAR flag | EPOLLET flag |
| Fork Behavior | Must recreate | Inherited |
| Error Reporting | In event.flags | Via errno |
| Timer Support | EVFILT_TIMER | timerfd |
| Signal Support | EVFILT_SIGNAL | signalfd |

## Operation Flow

### 1. Registration
```cpp
void register_descriptor(int descriptor, descriptor_state* state) {
  struct kevent events[2];

  // Register for read events
  EV_SET(&events[0], descriptor, EVFILT_READ,
         EV_ADD | EV_CLEAR, 0, 0, state);

  // Register for write events
  EV_SET(&events[1], descriptor, EVFILT_WRITE,
         EV_ADD | EV_CLEAR | EV_DISABLE, 0, 0, state);

  kevent(kqueue_fd_, events, 2, 0, 0, 0);
}
```

### 2. Event Processing Loop
```cpp
void run(long usec, op_queue<operation>& ops) {
  struct kevent events[128];
  timespec timeout = calculate_timeout(usec);

  // Wait for events
  int n = kevent(kqueue_fd_, 0, 0, events, 128, &timeout);

  // Process events
  for (int i = 0; i < n; ++i) {
    descriptor_state* state =
      static_cast<descriptor_state*>(events[i].udata);

    // Check for errors
    if (events[i].flags & EV_ERROR) {
      handle_error(events[i].data);
      continue;
    }

    // Process based on filter type
    switch (events[i].filter) {
      case EVFILT_READ:
        state->perform_read_operations();
        break;
      case EVFILT_WRITE:
        state->perform_write_operations();
        break;
    }
  }
}
```

### 3. Speculative Execution
```cpp
void start_op(int op_type, int descriptor,
              descriptor_state* state, reactor_op* op,
              bool allow_speculative) {
  // Try operation immediately
  if (allow_speculative && state->try_speculative_[op_type]) {
    if (op->perform() == reactor_op::done) {
      return;  // Completed without blocking
    }
  }

  // Queue for kqueue notification
  state->op_queue_[op_type].push(op);

  // Enable appropriate filter
  if (op_type == read_op) {
    enable_read_events(descriptor);
  } else if (op_type == write_op) {
    enable_write_events(descriptor);
  }
}
```

## Platform-Specific Features

### macOS-Specific
1. **Grand Central Dispatch Integration**:
   ```cpp
   // Can integrate with dispatch queues
   dispatch_queue_t queue = dispatch_get_main_queue();
   dispatch_source_t source = dispatch_source_create(
     DISPATCH_SOURCE_TYPE_READ, fd, 0, queue);
   ```

2. **SO_NOSIGPIPE Socket Option**:
   ```cpp
   // Prevent SIGPIPE on broken connections
   int set = 1;
   setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
   ```

3. **Network Framework Compatibility**:
   - Can coexist with Network.framework
   - Supports App Store sandboxing requirements

### BSD-Specific
1. **SO_REUSEPORT for Load Balancing**:
   ```cpp
   // Multiple processes can bind to same port
   int set = 1;
   setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &set, sizeof(set));
   ```

2. **accept_filter for Connection Filtering**:
   ```cpp
   // FreeBSD: Filter connections at kernel level
   struct accept_filter_arg afa;
   strcpy(afa.af_name, "dataready");
   setsockopt(fd, SOL_SOCKET, SO_ACCEPTFILTER, &afa, sizeof(afa));
   ```

## Fork Safety

### The Fork Problem
kqueue descriptors are **not inherited** across fork():
```cpp
void handle_fork() {
  if (fork() == 0) {
    // Child process
    // kqueue_fd_ is invalid here!

    // Must recreate kqueue
    kqueue_fd_ = kqueue();

    // Re-register all descriptors
    for (auto& desc : registered_descriptors_) {
      register_descriptor(desc.fd, &desc);
    }
  }
}
```

### ASIO's Fork Support
```cpp
io_context io_ctx;

// Parent prepares for fork
io_ctx.notify_fork(io_context::fork_prepare);

if (fork() == 0) {
  // Child reinitializes
  io_ctx.notify_fork(io_context::fork_child);
} else {
  // Parent continues
  io_ctx.notify_fork(io_context::fork_parent);
}
```

## Performance Characteristics

### Strengths
- **O(1) Scalability**: Returns only active events
- **Direct User Data**: Via kevent.udata pointer
- **Unified Interface**: Files, sockets, signals, timers
- **Fine Control**: Per-event enable/disable
- **Batched Operations**: Register/retrieve multiple events

### Weaknesses
- **Fork Overhead**: Must recreate after fork
- **Modification Cost**: DELETE + ADD for changes
- **Platform Quirks**: Some devices have special behavior
- **Lower FD Limits**: macOS defaults are restrictive

### Performance Tuning
```cpp
// Increase file descriptor limits
struct rlimit rlim;
rlim.rlim_cur = 10240;
rlim.rlim_max = 10240;
setrlimit(RLIMIT_NOFILE, &rlim);

// Batch event changes
struct kevent changes[32];
int nchanges = 0;
// ... accumulate changes ...
kevent(kq, changes, nchanges, NULL, 0, NULL);

// Use NOTE_LOWAT for receive threshold
EV_SET(&ev, fd, EVFILT_READ, EV_ADD, NOTE_LOWAT, 1024, udata);
```

## Common Issues and Solutions

### 1. Serial Port Issues
```cpp
// Some serial devices don't support EVFILT_WRITE properly
// Solution: Use alternative polling for serial ports
if (is_serial_port(fd)) {
  use_select_for_write(fd);
} else {
  use_kqueue_for_write(fd);
}
```

### 2. File Descriptor Limits
```bash
# Check current limits
ulimit -n

# Increase for current session
ulimit -n 10240

# Permanent increase (add to ~/.zshrc or ~/.bash_profile)
ulimit -n 10240
```

### 3. SIGPIPE Handling
```cpp
// Ignore SIGPIPE globally
signal(SIGPIPE, SIG_IGN);

// Or per-socket (macOS)
int set = 1;
setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(set));
```

### 4. Event Coalescing
```cpp
// Multiple events may be coalesced into one
// Always drain all available data
while (true) {
  ssize_t n = recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
  if (n <= 0) break;
  process_data(buffer, n);
}
```

## Integration with macOS Frameworks

### 1. Combine with CFRunLoop
```cpp
// Add kqueue to CFRunLoop for GUI apps
CFFileDescriptorRef fdref = CFFileDescriptorCreate(
  kCFAllocatorDefault, kqueue_fd_, true,
  kqueue_callback, &context);

CFRunLoopSourceRef source = CFFileDescriptorCreateRunLoopSource(
  kCFAllocatorDefault, fdref, 0);

CFRunLoopAddSource(CFRunLoopGetMain(), source, kCFRunLoopDefaultMode);
```

### 2. App Store Compliance
- Use ASIO for networking logic
- Ensure proper entitlements
- Handle sandboxing restrictions
- Respect privacy requirements

## Build Configuration

### Detection
```cpp
// Automatic on macOS/BSD
#if defined(__APPLE__) || defined(__FreeBSD__) || \
    defined(__NetBSD__) || defined(__OpenBSD__)
  #define ASIO_HAS_KQUEUE 1
#endif

// Disable if needed
#define ASIO_DISABLE_KQUEUE 1
```

### Compilation
```bash
# macOS
clang++ -std=c++17 -framework CoreFoundation app.cpp

# FreeBSD
c++ -std=c++17 app.cpp

# Link requirements
# No special libraries needed (kqueue is in libc)
```

## Debugging Tips

### 1. Trace kqueue Calls
```bash
# macOS
sudo dtruss -f -t kevent program

# FreeBSD
truss -f -e kevent program
```

### 2. Inspect kqueue State
```cpp
// Dump registered events
void dump_kqueue_state(int kq) {
  struct kevent events[256];
  struct timespec timeout = {0, 0};

  int n = kevent(kq, NULL, 0, events, 256, &timeout);
  for (int i = 0; i < n; ++i) {
    printf("FD: %lu, Filter: %d, Flags: %x\n",
           events[i].ident, events[i].filter, events[i].flags);
  }
}
```

### 3. Performance Monitoring
```bash
# macOS Instruments
instruments -t "System Trace" -D trace.trace program

# Sample stack traces
sample program 10 -file sample.txt

# File descriptor usage
lsof -p $(pgrep program)
```

## Best Practices

### 1. Event Management
- Use EV_CLEAR for edge-triggered behavior
- Batch kevent changes when possible
- Handle EV_ERROR in event flags
- Always check for EOF conditions

### 2. Resource Management
- Set appropriate file descriptor limits
- Handle EMFILE gracefully
- Clean up kqueue on fork
- Use SO_NOSIGPIPE on macOS

### 3. Performance Optimization
- Enable/disable filters as needed
- Use NOTE_LOWAT for read thresholds
- Batch operations when possible
- Profile with Instruments on macOS

### 4. Platform Compatibility
- Test on multiple BSD variants
- Handle platform-specific quirks
- Provide fallback for serial ports
- Consider SO_REUSEPORT on supported platforms

### 5. Error Handling
```cpp
// Check for errors in flags
if (event.flags & EV_ERROR) {
  errno = event.data;
  handle_error();
}

// Handle special conditions
if (event.flags & EV_EOF) {
  handle_disconnect();
}

// Filter-specific error checking
if (event.filter == EVFILT_READ && event.data == 0) {
  // Connection closed
}
```

## Migration from Linux epoll

### Key Differences to Handle
1. **Event Model**: Filters vs masks
2. **Modification**: No modify operation
3. **Fork Behavior**: Must recreate kqueue
4. **Error Reporting**: Check event.flags
5. **Timer Integration**: EVFILT_TIMER vs timerfd

### Code Adaptation
```cpp
// Linux epoll
epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &event);

// macOS kqueue (no direct modify)
struct kevent changes[2];
EV_SET(&changes[0], fd, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
EV_SET(&changes[1], fd, EVFILT_WRITE, EV_ADD|EV_CLEAR, 0, 0, udata);
kevent(kq, changes, 2, NULL, 0, NULL);
```

## Summary

kqueue provides a robust, efficient event notification system for ASIO on macOS and BSD platforms. While it has some quirks (fork behavior, no modify operation), it offers excellent performance and integrates well with platform-specific features. The filter-based model provides fine-grained control over event types, and the edge-triggered support via EV_CLEAR matches the efficiency of Linux's epoll EPOLLET mode.