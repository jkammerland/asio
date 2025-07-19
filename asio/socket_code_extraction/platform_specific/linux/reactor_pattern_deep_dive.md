# Reactor Pattern Deep Dive - Linux epoll Implementation

## Conceptual Overview

The Reactor pattern is an event-driven architecture that efficiently handles multiple concurrent service requests. ASIO's epoll implementation uses edge-triggered notifications for optimal performance.

## Pattern Structure

```
┌─────────────────┐
│   Application   │
│  ┌───────────┐  │
│  │ Handlers  │  │ ← User-defined completion handlers
│  └───────────┘  │
├─────────────────┤
│   ASIO Layer    │
│  ┌───────────┐  │
│  │ Reactor   │  │ ← epoll_reactor orchestrates events
│  └───────────┘  │
│  ┌───────────┐  │
│  │Operations │  │ ← Queued async operations
│  └───────────┘  │
├─────────────────┤
│    OS Layer     │
│  ┌───────────┐  │
│  │  epoll    │  │ ← Kernel event notification
│  └───────────┘  │
└─────────────────┘
```

## Core Reactor Loop

### 1. Event Demultiplexing
```cpp
void epoll_reactor::run(long usec, op_queue<operation>& ops)
{
  // Phase 1: Wait for events
  epoll_event events[128];
  int timeout = calculate_timeout(usec);
  int num_events = epoll_wait(epoll_fd_, events, 128, timeout);
  
  // Phase 2: Demultiplex events to descriptor states
  for (int i = 0; i < num_events; ++i) {
    descriptor_state* state = static_cast<descriptor_state*>(events[i].data.ptr);
    state->set_ready_events(events[i].events);
    ops.push(state);  // Queue for processing
  }
  
  // Phase 3: Check timers
  timer_queues_.get_ready_timers(ops);
}
```

### 2. Event Dispatching
```cpp
// Scheduler processes the operation queue
while (operation* op = ops.front()) {
  ops.pop();
  op->complete(this, ec, bytes_transferred);
}
```

## Speculative Execution Strategy

### Concept
Try operations immediately before registering with epoll to avoid unnecessary syscalls.

### Implementation
```cpp
void start_op(int op_type, socket_type descriptor,
              descriptor_state* state, reactor_op* op,
              bool allow_speculative)
{
  if (state->op_queue_[op_type].empty() && allow_speculative) {
    // Try operation immediately
    if (state->try_speculative_[op_type]) {
      reactor_op::status status = op->perform();
      
      if (status == reactor_op::done) {
        // Completed without blocking - bypass epoll entirely
        scheduler_.post_immediate_completion(op, is_continuation);
        return;
      }
      
      if (status == reactor_op::done_and_exhausted) {
        // Disable speculation for this operation type
        state->try_speculative_[op_type] = false;
      }
    }
  }
  
  // Operation would block - register with epoll
  register_for_events(descriptor, state, op_type);
  state->op_queue_[op_type].push(op);
}
```

### Benefits
1. **Reduced latency**: No epoll round-trip for non-blocking ops
2. **Lower syscall overhead**: Many operations complete immediately
3. **Adaptive behavior**: Disables speculation when not beneficial

## Edge-Triggered epoll Semantics

### Registration
```cpp
epoll_event ev = { 0, { 0 } };
ev.events = EPOLLIN | EPOLLOUT | EPOLLERR | EPOLLHUP | EPOLLET;
                                                      // ^^^^^^^^ Edge-triggered
ev.data.ptr = descriptor_state;
epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
```

### Implications
1. **Must drain operations**: Process until EAGAIN
2. **No spurious wakeups**: Only notified on state changes
3. **Careful state management**: Track registered events

### Operation Draining
```cpp
operation* descriptor_state::perform_io(uint32_t events)
{
  // Process operations until none can make progress
  for (int j = max_ops - 1; j >= 0; --j) {
    if (events & event_mask[j]) {
      while (reactor_op* op = op_queue_[j].front()) {
        reactor_op::status status = op->perform();
        
        if (status == reactor_op::done) {
          op_queue_[j].pop();
          completed_ops.push(op);
          // Continue draining
        } else {
          break;  // Would block - stop draining
        }
      }
    }
  }
}
```

## Concurrency Model

### Per-Descriptor Locking
```cpp
struct descriptor_state
{
  mutex mutex_;  // Protects operation queues
  op_queue<reactor_op> op_queue_[max_ops];
};

// Fine-grained locking during operation
operation* perform_io(uint32_t events)
{
  mutex::scoped_lock lock(mutex_);
  // Process operations...
}
```

### Lock Optimization Strategies

#### 1. Conditional Locking
```cpp
// Configure at runtime
bool io_locking = config.get("reactor.io_locking", true);

// Mutex is no-op when locking disabled
typedef conditionally_enabled_mutex mutex;
```

#### 2. Spin-Wait Optimization
```cpp
class spinlock_mutex
{
  void lock() {
    int spin_count = spin_count_;
    while (spin_count-- > 0) {
      if (try_lock())
        return;
      std::this_thread::yield();
    }
    // Fall back to OS mutex
    blocking_lock();
  }
};
```

## Timer Integration

### timerfd Approach (Preferred)
```cpp
// Create timer file descriptor
timer_fd_ = timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC);

// Register with epoll like any other fd
epoll_event ev;
ev.events = EPOLLIN | EPOLLERR;
ev.data.ptr = &timer_fd_;
epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, timer_fd_, &ev);

// Update timer
itimerspec timeout;
calculate_timeout(timeout);
timerfd_settime(timer_fd_, 0, &timeout, nullptr);
```

### Fallback Timer Handling
```cpp
// Without timerfd, calculate timeout for epoll_wait
int get_timeout(int requested_timeout)
{
  long timer_msec = timer_queues_.wait_duration_msec(max_timeout);
  return std::min(requested_timeout, timer_msec);
}
```

## Interrupt Mechanism

### Purpose
Wake reactor from epoll_wait for:
- New operations added from other threads
- Shutdown requests
- Timer updates (without timerfd)

### Implementation
```cpp
class select_interrupter
{
  int read_fd_;
  int write_fd_;
  
  void interrupt() {
    uint8_t byte = 1;
    ::write(write_fd_, &byte, 1);
  }
  
  void reset() {
    uint8_t data[1024];
    while (::read(read_fd_, data, sizeof(data)) > 0)
      continue;  // Drain pipe
  }
};
```

### Edge-Triggered Optimization
```cpp
// Register interrupter as edge-triggered
ev.events = EPOLLIN | EPOLLERR | EPOLLET;

// No need to reset after each wakeup
// Only re-armed when epoll registration changes
```

## Performance Patterns

### 1. Operation Batching
Group operations to reduce epoll modifications:
```cpp
if (descriptor_state->registered_events_ != required_events) {
  epoll_event ev;
  ev.events = required_events;
  ev.data.ptr = descriptor_state;
  epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
  descriptor_state->registered_events_ = required_events;
}
```

### 2. Lazy Event Registration
Only register for EPOLLOUT when needed:
```cpp
void start_write_op(descriptor_state* state, reactor_op* op)
{
  if (state->op_queue_[write_op].empty()) {
    // First write op - need EPOLLOUT
    state->registered_events_ |= EPOLLOUT;
    update_epoll_registration(state);
  }
  state->op_queue_[write_op].push(op);
}
```

### 3. Memory Pool Usage
```cpp
// Pre-allocate descriptor states
object_pool<descriptor_state> registered_descriptors_;

// Fast allocation/deallocation
descriptor_state* allocate_descriptor_state() {
  return registered_descriptors_.alloc();
}

void free_descriptor_state(descriptor_state* s) {
  registered_descriptors_.free(s);
}
```

## Common Pitfalls and Solutions

### 1. Starvation with Edge-Triggered epoll
**Problem**: Not draining all data can miss events
**Solution**: Always process until EAGAIN

### 2. Thundering Herd
**Problem**: Multiple threads woken for same event
**Solution**: EPOLLEXCLUSIVE flag (Linux 4.5+)

### 3. FD Reuse Issues  
**Problem**: File descriptor recycled while in epoll
**Solution**: Proper cleanup in deregister_descriptor

### 4. Priority Inversion
**Problem**: Exceptional events not processed first
**Solution**: Process in order: except → write → read