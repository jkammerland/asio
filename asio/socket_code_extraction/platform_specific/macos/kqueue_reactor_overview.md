# macOS/BSD Kqueue Reactor Implementation

## Overview

ASIO uses the **kqueue** event notification interface on macOS and BSD systems (FreeBSD, NetBSD, OpenBSD) for high-performance asynchronous I/O operations. Kqueue is the BSD equivalent of Linux's epoll, providing efficient event notification for file descriptors.

## Platform Detection

ASIO automatically detects and enables kqueue support on the following platforms:

```cpp
// From config.hpp
#if (defined(__MACH__) && defined(__APPLE__)) \
  || defined(__FreeBSD__) \
  || defined(__NetBSD__) \
  || defined(__OpenBSD__)
# if !defined(ASIO_HAS_KQUEUE)
#  if !defined(ASIO_DISABLE_KQUEUE)
#   define ASIO_HAS_KQUEUE 1
#  endif
# endif
#endif
```

## Architecture

### Key Components

1. **kqueue_reactor** - Main reactor class implementing the proactor pattern
2. **descriptor_state** - Per-descriptor data structure tracking operations
3. **select_interrupter** - Mechanism to interrupt blocking kevent calls
4. **timer_queue_set** - Management of timer operations

### Core Data Structures

```cpp
class kqueue_reactor {
  // The kqueue file descriptor
  int kqueue_fd_;
  
  // Per-descriptor state management
  struct descriptor_state {
    descriptor_state* next_;
    descriptor_state* prev_;
    mutex mutex_;
    int descriptor_;
    int num_kevents_; // 1 = read only, 2 = read and write
    op_queue<reactor_op> op_queue_[max_ops];
    bool shutdown_;
  };
  
  // Object pool for descriptor states
  object_pool<descriptor_state> registered_descriptors_;
  
  // Timer management
  timer_queue_set timer_queues_;
  
  // Interrupt mechanism
  select_interrupter interrupter_;
};
```

## Kqueue Event Model

### Event Types

ASIO maps its operations to kqueue filters:

```cpp
enum op_types { 
  read_op = 0,    // Maps to EVFILT_READ
  write_op = 1,   // Maps to EVFILT_WRITE
  connect_op = 1, // Also maps to EVFILT_WRITE
  except_op = 2,  // Maps to EVFILT_READ with EV_OOBAND
  max_ops = 3 
};
```

### Event Registration

Events are registered using the EV_SET macro with platform-specific handling:

```cpp
// Platform-specific macro for NetBSD compatibility
#if defined(__NetBSD__) && __NetBSD_Version__ < 999001500
# define ASIO_KQUEUE_EV_SET(ev, ident, filt, flags, fflags, data, udata) \
    EV_SET(ev, ident, filt, flags, fflags, data, \
      reinterpret_cast<intptr_t>(static_cast<void*>(udata)))
#else
# define ASIO_KQUEUE_EV_SET(ev, ident, filt, flags, fflags, data, udata) \
    EV_SET(ev, ident, filt, flags, fflags, data, udata)
#endif
```

### Key Flags Used

- **EV_ADD** - Add event to kqueue
- **EV_DELETE** - Remove event from kqueue
- **EV_CLEAR** - Clear event state after retrieval
- **EV_OOBAND** - Out-of-band data (exceptional conditions)
- **EV_ERROR** - Error occurred

## Operation Flow

### 1. Descriptor Registration

```cpp
int register_descriptor(socket_type descriptor, per_descriptor_data& descriptor_data) {
  // Allocate descriptor state from object pool
  descriptor_data = allocate_descriptor_state();
  
  // Initialize descriptor state
  descriptor_data->descriptor_ = descriptor;
  descriptor_data->num_kevents_ = 0;
  descriptor_data->shutdown_ = false;
  
  return 0;
}
```

### 2. Starting Operations

```cpp
void start_op(int op_type, socket_type descriptor,
    per_descriptor_data& descriptor_data, reactor_op* op,
    bool is_continuation, bool allow_speculative) {
  
  // Try speculative execution first
  if (allow_speculative && op_type != read_op) {
    if (op->perform()) {
      // Operation completed immediately
      post_immediate_completion(op);
      return;
    }
  }
  
  // Register with kqueue if needed
  if (descriptor_data->op_queue_[op_type].empty()) {
    struct kevent events[2];
    ASIO_KQUEUE_EV_SET(&events[0], descriptor, EVFILT_READ,
        EV_ADD | EV_CLEAR, 0, 0, descriptor_data);
    ASIO_KQUEUE_EV_SET(&events[1], descriptor, EVFILT_WRITE,
        EV_ADD | EV_CLEAR, 0, 0, descriptor_data);
    kevent(kqueue_fd_, events, num_kevents[op_type], 0, 0, 0);
  }
  
  // Queue the operation
  descriptor_data->op_queue_[op_type].push(op);
}
```

### 3. Event Loop

```cpp
void run(long usec, op_queue<operation>& ops) {
  // Set timeout
  timespec timeout_buf = { 0, 0 };
  timespec* timeout = get_timeout(usec, timeout_buf);
  
  // Wait for events
  struct kevent events[128];
  int num_events = kevent(kqueue_fd_, 0, 0, events, 128, timeout);
  
  // Process events
  for (int i = 0; i < num_events; ++i) {
    if (events[i].udata == &interrupter_) {
      // Handle interruption
      interrupter_.reset();
    } else {
      // Process I/O events
      descriptor_state* state = static_cast<descriptor_state*>(events[i].udata);
      process_descriptor_events(state, events[i], ops);
    }
  }
  
  // Process timers
  timer_queues_.get_ready_timers(ops);
}
```

## Platform-Specific Features

### 1. Out-of-Band Data Handling

macOS may not define EV_OOBAND, so ASIO provides a fallback:

```cpp
#if !defined(EV_OOBAND)
# define EV_OOBAND EV_FLAG1
#endif
```

### 2. Serial Port Workaround

Some descriptor types (like serial ports) don't support EV_CLEAR with EVFILT_WRITE properly:

```cpp
if (events[i].filter == EVFILT_WRITE
    && descriptor_data->num_kevents_ == 2
    && descriptor_data->op_queue_[write_op].empty()) {
  // Remove EVFILT_WRITE registration to avoid tight spin
  struct kevent delete_events[1];
  ASIO_KQUEUE_EV_SET(&delete_events[0],
      descriptor_data->descriptor_, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
  ::kevent(kqueue_fd_, delete_events, 1, 0, 0, 0);
  descriptor_data->num_kevents_ = 1;
}
```

### 3. Fork Handling

Kqueue descriptors are not inherited across fork, requiring re-registration:

```cpp
void notify_fork(fork_event fork_ev) {
  if (fork_ev == fork_child) {
    // Recreate kqueue descriptor
    kqueue_fd_ = do_kqueue_create();
    
    // Re-register interrupter
    interrupter_.recreate();
    
    // Re-register all descriptors
    for (descriptor_state* state = registered_descriptors_.first();
         state != 0; state = state->next_) {
      // Re-add events to new kqueue
    }
  }
}
```

## Performance Characteristics

### Advantages

1. **O(1) Event Delivery** - Kqueue returns only active events
2. **Edge-Triggered with EV_CLEAR** - Reduces unnecessary wakeups
3. **Batched Operations** - Multiple events can be added/retrieved in one syscall
4. **User Data Association** - Direct pointer to descriptor state via udata

### Comparison with Other Reactors

| Feature | Kqueue (macOS/BSD) | Epoll (Linux) | Select (Fallback) |
|---------|-------------------|---------------|-------------------|
| Scalability | O(1) | O(1) | O(n) |
| Edge-triggered | Yes (EV_CLEAR) | Yes (EPOLLET) | No |
| Batch operations | Yes | Yes | No |
| Max descriptors | System limit | System limit | FD_SETSIZE |
| Fork handling | Manual re-registration | Inherited | Inherited |

### Optimization Strategies

1. **Speculative Execution** - Try operations immediately before queuing
2. **Batched Event Processing** - Process up to 128 events per kevent call
3. **Object Pooling** - Reuse descriptor_state objects
4. **Minimal Locking** - Per-descriptor mutexes with configurable spin counts

## Differences from Epoll

### Key Differences

1. **Event Structure**
   - Kqueue uses filters (EVFILT_READ, EVFILT_WRITE)
   - Epoll uses event masks (EPOLLIN, EPOLLOUT)

2. **Registration Model**
   - Kqueue requires explicit ADD/DELETE operations
   - Epoll supports MOD operation for changes

3. **Fork Behavior**
   - Kqueue descriptors must be recreated after fork
   - Epoll descriptors are inherited

4. **Out-of-Band Data**
   - Kqueue uses EV_OOBAND flag
   - Epoll uses EPOLLPRI event

5. **Error Handling**
   - Kqueue returns errors in event.flags
   - Epoll returns errors in event.events

## Limitations

1. **Fork Complexity** - Requires manual re-registration after fork
2. **Platform Variations** - Different BSD variants have subtle differences
3. **Serial Port Issues** - Some descriptors don't fully support all kqueue features
4. **No Modification Operation** - Must DELETE then ADD to change filters

## Best Practices

1. **Use EV_CLEAR** - Provides edge-triggered behavior
2. **Batch Operations** - Register multiple events in one kevent call
3. **Handle EV_ERROR** - Check event.flags for errors
4. **Proper Cleanup** - Remove events before closing descriptors
5. **Fork Safety** - Implement proper fork handlers