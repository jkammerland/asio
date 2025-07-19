# Kqueue Reactor Implementation Details

## Core Implementation

### 1. Kqueue Creation

```cpp
int kqueue_reactor::do_kqueue_create() {
  int fd = ::kqueue();
  if (fd == -1) {
    asio::error_code ec(errno,
        asio::error::get_system_category());
    asio::detail::throw_error(ec, "kqueue");
  }
  return fd;
}
```

The kqueue file descriptor is created during reactor construction and serves as the central event queue.

### 2. Descriptor State Management

#### Object Pool Allocation

```cpp
descriptor_state* kqueue_reactor::allocate_descriptor_state() {
  mutex::scoped_lock descriptors_lock(registered_descriptors_mutex_);
  return registered_descriptors_.alloc(io_locking_, io_locking_spin_count_);
}

void kqueue_reactor::free_descriptor_state(descriptor_state* s) {
  mutex::scoped_lock descriptors_lock(registered_descriptors_mutex_);
  registered_descriptors_.free(s);
}
```

Descriptor states are managed through an object pool for efficiency, avoiding frequent allocations.

#### Per-Descriptor Locking

```cpp
struct descriptor_state {
  descriptor_state(bool locking, int spin_count)
    : mutex_(locking, spin_count) {}
  
  mutex mutex_;  // Conditionally enabled mutex
  // ... other members
};
```

Each descriptor has its own mutex with configurable spin counts for fine-grained locking.

### 3. Event Registration Details

#### Initial Registration

```cpp
int register_descriptor(socket_type descriptor,
    per_descriptor_data& descriptor_data) {
  descriptor_data = allocate_descriptor_state();
  
  ASIO_HANDLER_REACTOR_REGISTRATION((
        context(), static_cast<uintmax_t>(descriptor),
        reinterpret_cast<uintmax_t>(descriptor_data)));
  
  mutex::scoped_lock lock(descriptor_data->mutex_);
  
  descriptor_data->descriptor_ = descriptor;
  descriptor_data->num_kevents_ = 0;  // No events registered yet
  descriptor_data->shutdown_ = false;
  
  return 0;
}
```

#### Adding Events to Kqueue

```cpp
// For read operations
if (descriptor_data->num_kevents_ < 1) {
  struct kevent events[1];
  ASIO_KQUEUE_EV_SET(&events[0], descriptor, EVFILT_READ,
      EV_ADD | EV_CLEAR, 0, 0, descriptor_data);
  if (::kevent(kqueue_fd_, events, 1, 0, 0, 0) != -1) {
    descriptor_data->num_kevents_ = 1;
  }
}

// For write operations (requires both read and write filters)
if (descriptor_data->num_kevents_ < 2) {
  struct kevent events[2];
  ASIO_KQUEUE_EV_SET(&events[0], descriptor, EVFILT_READ,
      EV_ADD | EV_CLEAR, 0, 0, descriptor_data);
  ASIO_KQUEUE_EV_SET(&events[1], descriptor, EVFILT_WRITE,
      EV_ADD | EV_CLEAR, 0, 0, descriptor_data);
  if (::kevent(kqueue_fd_, events, 2, 0, 0, 0) != -1) {
    descriptor_data->num_kevents_ = 2;
  }
}
```

### 4. Event Processing

#### Main Event Loop

```cpp
void kqueue_reactor::run(long usec, op_queue<operation>& ops) {
  mutex::scoped_lock lock(mutex_);
  
  // Determine timeout
  timespec timeout_buf = { 0, 0 };
  timespec* timeout = usec ? get_timeout(usec, timeout_buf) : &timeout_buf;
  
  lock.unlock();
  
  // Block on kqueue
  struct kevent events[128];
  int num_events = kevent(kqueue_fd_, 0, 0, events, 128, timeout);
  
  // Process events
  for (int i = 0; i < num_events; ++i) {
    void* ptr = reinterpret_cast<void*>(events[i].udata);
    
    if (ptr == &interrupter_) {
      // Handle interruption
      interrupter_.reset();
    } else {
      // Process I/O events
      descriptor_state* descriptor_data = static_cast<descriptor_state*>(ptr);
      mutex::scoped_lock descriptor_lock(descriptor_data->mutex_);
      
      // Handle special cases (e.g., serial ports)
      handle_descriptor_events(descriptor_data, events[i], ops);
    }
  }
  
  // Process expired timers
  lock.lock();
  timer_queues_.get_ready_timers(ops);
}
```

#### Event Processing Logic

```cpp
void handle_descriptor_events(descriptor_state* descriptor_data,
    const struct kevent& event, op_queue<operation>& ops) {
  // Map kqueue filters to operation types
  static const int filter[max_ops] = 
    { EVFILT_READ, EVFILT_WRITE, EVFILT_READ };
  
  // Process in priority order (except_op first)
  for (int j = max_ops - 1; j >= 0; --j) {
    if (event.filter == filter[j]) {
      // Special handling for exceptional conditions
      if (j != except_op || event.flags & EV_OOBAND) {
        while (reactor_op* op = descriptor_data->op_queue_[j].front()) {
          // Handle errors
          if (event.flags & EV_ERROR) {
            op->ec_ = asio::error_code(
                static_cast<int>(event.data),
                asio::error::get_system_category());
            descriptor_data->op_queue_[j].pop();
            ops.push(op);
          }
          // Try to perform the operation
          else if (op->perform()) {
            descriptor_data->op_queue_[j].pop();
            ops.push(op);
          } else {
            break;  // Operation not ready
          }
        }
      }
    }
  }
}
```

### 5. Speculative Execution

Operations are tried immediately before queuing when possible:

```cpp
void start_op(int op_type, socket_type descriptor,
    per_descriptor_data& descriptor_data, reactor_op* op,
    bool allow_speculative) {
  
  if (descriptor_data->op_queue_[op_type].empty()) {
    if (allow_speculative && 
        (op_type != read_op || descriptor_data->op_queue_[except_op].empty())) {
      // Try operation immediately
      if (op->perform()) {
        post_immediate_completion(op);
        return;
      }
      
      // Operation would block, register with kqueue
      register_events_if_needed(descriptor, descriptor_data, op_type);
    }
  }
  
  // Queue for later processing
  descriptor_data->op_queue_[op_type].push(op);
  scheduler_.work_started();
}
```

### 6. Cancellation

#### Cancel All Operations

```cpp
void cancel_ops(socket_type, per_descriptor_data& descriptor_data) {
  if (!descriptor_data) return;
  
  mutex::scoped_lock descriptor_lock(descriptor_data->mutex_);
  
  op_queue<operation> ops;
  for (int i = 0; i < max_ops; ++i) {
    while (reactor_op* op = descriptor_data->op_queue_[i].front()) {
      op->ec_ = asio::error::operation_aborted;
      descriptor_data->op_queue_[i].pop();
      ops.push(op);
    }
  }
  
  descriptor_lock.unlock();
  scheduler_.post_deferred_completions(ops);
}
```

#### Cancel by Key

```cpp
void cancel_ops_by_key(socket_type, per_descriptor_data& descriptor_data,
    int op_type, void* cancellation_key) {
  if (!descriptor_data) return;
  
  mutex::scoped_lock descriptor_lock(descriptor_data->mutex_);
  
  op_queue<operation> cancelled_ops;
  op_queue<reactor_op> remaining_ops;
  
  while (reactor_op* op = descriptor_data->op_queue_[op_type].front()) {
    descriptor_data->op_queue_[op_type].pop();
    if (op->cancellation_key_ == cancellation_key) {
      op->ec_ = asio::error::operation_aborted;
      cancelled_ops.push(op);
    } else {
      remaining_ops.push(op);
    }
  }
  
  descriptor_data->op_queue_[op_type].push(remaining_ops);
  
  descriptor_lock.unlock();
  scheduler_.post_deferred_completions(cancelled_ops);
}
```

### 7. Deregistration

```cpp
void deregister_descriptor(socket_type descriptor,
    per_descriptor_data& descriptor_data, bool closing) {
  if (!descriptor_data) return;
  
  mutex::scoped_lock descriptor_lock(descriptor_data->mutex_);
  
  if (!descriptor_data->shutdown_) {
    if (closing) {
      // Kqueue automatically removes events when descriptor is closed
    } else {
      // Explicitly remove events
      struct kevent events[2];
      ASIO_KQUEUE_EV_SET(&events[0], descriptor,
          EVFILT_READ, EV_DELETE, 0, 0, 0);
      ASIO_KQUEUE_EV_SET(&events[1], descriptor,
          EVFILT_WRITE, EV_DELETE, 0, 0, 0);
      ::kevent(kqueue_fd_, events, descriptor_data->num_kevents_, 0, 0, 0);
    }
    
    // Cancel all pending operations
    op_queue<operation> ops;
    for (int i = 0; i < max_ops; ++i) {
      while (reactor_op* op = descriptor_data->op_queue_[i].front()) {
        op->ec_ = asio::error::operation_aborted;
        descriptor_data->op_queue_[i].pop();
        ops.push(op);
      }
    }
    
    descriptor_data->descriptor_ = -1;
    descriptor_data->shutdown_ = true;
    
    descriptor_lock.unlock();
    
    ASIO_HANDLER_REACTOR_DEREGISTRATION((
          context(), static_cast<uintmax_t>(descriptor),
          reinterpret_cast<uintmax_t>(descriptor_data)));
    
    scheduler_.post_deferred_completions(ops);
  }
}
```

### 8. Timer Integration

```cpp
timespec* get_timeout(long usec, timespec& ts) {
  // Maximum wait of 5 minutes to detect system clock changes
  const long max_usec = 5 * 60 * 1000 * 1000;
  usec = timer_queues_.wait_duration_usec(
      (usec < 0 || max_usec < usec) ? max_usec : usec);
  
  ts.tv_sec = usec / 1000000;
  ts.tv_nsec = (usec % 1000000) * 1000;
  return &ts;
}
```

### 9. Interrupt Mechanism

The reactor uses a select_interrupter (typically a pipe) to wake up from blocking kevent calls:

```cpp
// In constructor
struct kevent events[1];
ASIO_KQUEUE_EV_SET(&events[0], interrupter_.read_descriptor(),
    EVFILT_READ, EV_ADD, 0, 0, &interrupter_);
if (::kevent(kqueue_fd_, events, 1, 0, 0, 0) == -1) {
  throw_error(errno);
}

// To interrupt
void interrupt() {
  interrupter_.interrupt();  // Writes to pipe
}

// In event loop
if (events[i].udata == &interrupter_) {
  interrupter_.reset();  // Drain pipe
}
```

## Thread Safety

### Locking Hierarchy

1. **reactor mutex_** - Protects reactor-wide state
2. **registered_descriptors_mutex_** - Protects descriptor list
3. **descriptor_state->mutex_** - Protects individual descriptor

### Lock-Free Optimizations

- Speculative execution avoids locking when possible
- Spin counts on mutexes reduce context switches
- Object pooling minimizes allocation contention

## Error Handling

### Kqueue-Specific Errors

```cpp
if (events[i].flags & EV_ERROR) {
  // Error code is in event.data
  op->ec_ = asio::error_code(
      static_cast<int>(events[i].data),
      asio::error::get_system_category());
}
```

### Common Error Scenarios

1. **EBADF** - Bad file descriptor
2. **EINVAL** - Invalid argument to kevent
3. **ENOENT** - Attempt to remove non-existent event
4. **ENOMEM** - Kernel memory allocation failure

## Performance Tuning

### Configuration Options

```cpp
kqueue_reactor(asio::execution_context& ctx)
  : mutex_(config(ctx).get("reactor", "registration_locking", true),
        config(ctx).get("reactor", "registration_locking_spin_count", 0)),
    io_locking_(config(ctx).get("reactor", "io_locking", true)),
    io_locking_spin_count_(
        config(ctx).get("reactor", "io_locking_spin_count", 0)),
    registered_descriptors_(execution_context::allocator<void>(ctx),
        config(ctx).get("reactor", "preallocated_io_objects", 0U),
        io_locking_, io_locking_spin_count_)
```

### Optimization Points

1. **Batch Size** - Process up to 128 events per kevent call
2. **Timeout Strategy** - Balance between responsiveness and CPU usage
3. **Object Pool Size** - Pre-allocate descriptor states
4. **Spin Counts** - Tune for specific workloads