# epoll Reactor Implementation Details

## Overview
The epoll reactor is ASIO's traditional Linux networking implementation, providing scalable asynchronous I/O using the epoll system call with edge-triggered notifications.

## Key Components

### 1. epoll_reactor Class
Located in: `asio/detail/epoll_reactor.hpp`

```cpp
class epoll_reactor : public execution_context_service_base<epoll_reactor>,
                      public scheduler_task
{
  // Core epoll file descriptor
  int epoll_fd_;
  
  // Optional timerfd for efficient timer handling
  int timer_fd_;
  
  // Interrupt mechanism for waking the reactor
  select_interrupter interrupter_;
  
  // Per-descriptor state management
  object_pool<descriptor_state> registered_descriptors_;
};
```

### 2. Descriptor State Management
Each socket/descriptor has associated state:

```cpp
struct descriptor_state : operation
{
  descriptor_state* next_;
  descriptor_state* prev_;
  
  mutex mutex_;
  epoll_reactor* reactor_;
  int descriptor_;
  uint32_t registered_events_;
  op_queue<reactor_op> op_queue_[max_ops];  // read, write, except
  bool try_speculative_[max_ops];
  bool shutdown_;
};
```

### 3. Operation Types
- **read_op**: Regular read operations (EPOLLIN)
- **write_op**: Write and connect operations (EPOLLOUT)
- **except_op**: Out-of-band data and errors (EPOLLPRI)

## Operation Flow

### 1. Starting an Operation
```cpp
void start_op(int op_type, socket_type descriptor,
              per_descriptor_data& descriptor_data, reactor_op* op,
              bool is_continuation, bool allow_speculative)
{
  // Try speculative execution first
  if (allow_speculative && descriptor_data->try_speculative_[op_type]) {
    if (op->perform() == reactor_op::done) {
      // Operation completed immediately
      return;
    }
  }
  
  // Queue operation and update epoll registration if needed
  descriptor_data->op_queue_[op_type].push(op);
  
  // Ensure epoll is monitoring for the appropriate events
  update_epoll_registration(descriptor, descriptor_data);
}
```

### 2. Event Processing Loop
```cpp
void run(long usec, op_queue<operation>& ops)
{
  // Calculate timeout
  int timeout = calculate_timeout(usec);
  
  // Wait for events
  epoll_event events[128];
  int num_events = epoll_wait(epoll_fd_, events, 128, timeout);
  
  // Process each event
  for (int i = 0; i < num_events; ++i) {
    descriptor_state* state = static_cast<descriptor_state*>(events[i].data.ptr);
    state->set_ready_events(events[i].events);
    ops.push(state);
  }
  
  // Check timers
  timer_queues_.get_ready_timers(ops);
}
```

### 3. Performing I/O Operations
```cpp
operation* descriptor_state::perform_io(uint32_t events)
{
  // Process operations in priority order: except, write, read
  for (int j = max_ops - 1; j >= 0; --j) {
    if (events & appropriate_flags[j]) {
      while (reactor_op* op = op_queue_[j].front()) {
        if (op->perform() == reactor_op::done) {
          op_queue_[j].pop();
          completed_ops.push(op);
        } else {
          break;  // Operation would block
        }
      }
    }
  }
}
```

## Edge-Triggered epoll Usage

### Registration
```cpp
int register_descriptor(socket_type descriptor, per_descriptor_data& data)
{
  epoll_event ev = { 0, { 0 } };
  ev.events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLPRI | EPOLLET;
  ev.data.ptr = descriptor_data;
  
  int result = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, descriptor, &ev);
}
```

### Key Features:
1. **Edge-triggered mode (EPOLLET)**: Only notified on state transitions
2. **Speculative operations**: Try operations immediately before registering
3. **Batched processing**: Multiple operations per event notification
4. **Efficient wakeup**: Interrupter uses edge-triggered notifications

## Timer Integration

### timerfd Support (Linux 2.6.22+)
When available, uses timerfd for efficient timer handling:
```cpp
int timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

// Update timer
itimerspec new_timeout;
timerfd_settime(timer_fd_, flags, &new_timeout, &old_timeout);
```

### Fallback Timer Implementation
Without timerfd, timers are checked after each epoll_wait with appropriate timeout calculation.

## Concurrency and Locking

### Registration Locking
- Protects the descriptor registration process
- Configurable via `reactor.registration_locking`

### I/O Locking  
- Per-descriptor mutexes for operation queues
- Configurable via `reactor.io_locking`
- Spin count optimization available

### Lock-Free Optimizations
- Speculative execution avoids locking when operations complete immediately
- Edge-triggered notifications reduce contention

## Performance Optimizations

### 1. Speculative Execution
Operations are tried immediately before epoll registration:
- Reduces syscall overhead for non-blocking operations
- Particularly effective for send operations with kernel buffer space

### 2. Operation Batching
Multiple operations processed per epoll event:
- Amortizes wake-up costs
- Improves cache locality

### 3. Memory Pooling
```cpp
object_pool<descriptor_state> registered_descriptors_;
```
- Pre-allocated descriptor states
- Reduces allocation overhead
- Configurable pool size

### 4. Selective Event Registration
Only registers for EPOLLOUT when needed:
- Reduces spurious wakeups
- Improves efficiency for read-heavy workloads

## Configuration Tuning

### Key Parameters:
1. **reactor.registration_locking**: Enable/disable registration mutex
2. **reactor.io_locking**: Enable/disable per-descriptor locking  
3. **reactor.registration_locking_spin_count**: Spin iterations before blocking
4. **reactor.io_locking_spin_count**: Spin iterations for I/O operations
5. **reactor.preallocated_io_objects**: Initial descriptor pool size

### Recommended Settings:
- High connection count: Enable locking, increase preallocated objects
- Low latency: Disable locking if single-threaded, enable spin counts
- Mixed workload: Default settings with moderate preallocation