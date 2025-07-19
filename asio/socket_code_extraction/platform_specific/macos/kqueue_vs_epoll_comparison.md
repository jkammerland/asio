# Kqueue vs Epoll: Detailed Comparison

## API Differences

### Event Registration

#### Kqueue
```cpp
struct kevent {
  uintptr_t ident;    // Identifier (file descriptor)
  int16_t   filter;   // Filter type (EVFILT_READ, EVFILT_WRITE)
  uint16_t  flags;    // Action flags (EV_ADD, EV_DELETE, etc.)
  uint32_t  fflags;   // Filter-specific flags
  intptr_t  data;     // Filter-specific data
  void*     udata;    // User data
};

// Add read event
EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, user_data);
kevent(kq, &kev, 1, NULL, 0, NULL);
```

#### Epoll
```cpp
struct epoll_event {
  uint32_t events;    // Event mask (EPOLLIN, EPOLLOUT, etc.)
  epoll_data_t data;  // User data union
};

// Add read event
struct epoll_event ev;
ev.events = EPOLLIN | EPOLLET;
ev.data.ptr = user_data;
epoll_ctl(epfd, EPOLL_CTL_ADD, fd, &ev);
```

### Event Types

| Operation | Kqueue | Epoll |
|-----------|--------|-------|
| Read ready | EVFILT_READ | EPOLLIN |
| Write ready | EVFILT_WRITE | EPOLLOUT |
| Error | EV_ERROR in flags | EPOLLERR in events |
| Hangup | EV_EOF in flags | EPOLLHUP in events |
| Priority/OOB | EV_OOBAND | EPOLLPRI |
| Edge-triggered | EV_CLEAR | EPOLLET |
| One-shot | EV_ONESHOT | EPOLLONESHOT |

## Implementation Differences in ASIO

### 1. Event Structure Management

#### Kqueue Implementation
```cpp
// Separate filters for read/write
static const int filter[max_ops] = 
  { EVFILT_READ, EVFILT_WRITE, EVFILT_READ };

// Must track number of registered filters
struct descriptor_state {
  int num_kevents_; // 1 = read only, 2 = read and write
};
```

#### Epoll Implementation
```cpp
// Combined event mask
uint32_t registered_events_;

// Single registration with combined events
epoll_event ev;
ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
```

### 2. Registration/Modification

#### Kqueue
```cpp
// Must DELETE then ADD to modify
void modify_events(int fd) {
  struct kevent events[2];
  // Delete old events
  EV_SET(&events[0], fd, EVFILT_READ, EV_DELETE, 0, 0, 0);
  // Add new events
  EV_SET(&events[1], fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, data);
  kevent(kq, events, 2, NULL, 0, NULL);
}
```

#### Epoll
```cpp
// Direct modification supported
void modify_events(int fd) {
  epoll_event ev;
  ev.events = EPOLLIN | EPOLLOUT | EPOLLET;
  ev.data.ptr = data;
  epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev);
}
```

### 3. Error Handling

#### Kqueue
```cpp
// Errors returned in event structure
if (events[i].flags & EV_ERROR) {
  error_code = static_cast<int>(events[i].data);
}
```

#### Epoll
```cpp
// Errors as event types
if (events[i].events & (EPOLLERR | EPOLLHUP)) {
  // Must use getsockopt to get actual error
  int error_value = 0;
  socklen_t error_len = sizeof(error_value);
  getsockopt(fd, SOL_SOCKET, SO_ERROR, &error_value, &error_len);
}
```

### 4. Fork Behavior

#### Kqueue
```cpp
void notify_fork(fork_event fork_ev) {
  if (fork_ev == fork_child) {
    // Kqueue fd is NOT inherited - must recreate
    kqueue_fd_ = do_kqueue_create();
    
    // Must re-register ALL events
    for (auto* state : registered_descriptors_) {
      re_register_descriptor(state);
    }
  }
}
```

#### Epoll
```cpp
void notify_fork(fork_event fork_ev) {
  if (fork_ev == fork_child) {
    // Epoll fd IS inherited - just recreate interrupter
    interrupter_.recreate();
    // No need to re-register descriptors
  }
}
```

## Performance Characteristics

### Memory Usage

| Aspect | Kqueue | Epoll |
|--------|--------|-------|
| Kernel memory per fd | Higher (separate entries per filter) | Lower (single entry) |
| User memory | descriptor_state tracks num_kevents_ | Single event mask |
| Event structure size | 64 bytes (kevent) | 12 bytes (epoll_event) |

### Syscall Overhead

#### Kqueue
- Single syscall can add/remove/wait
- Batch operations natural
- No separate "modify" operation

#### Epoll
- Separate syscalls for control and wait
- Modification is a single operation
- More syscalls for complex changes

### Edge-Triggered Behavior

#### Kqueue with EV_CLEAR
```cpp
// Automatic edge-triggered with EV_CLEAR
EV_SET(&kev, fd, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, data);
// Events are automatically cleared on retrieval
```

#### Epoll with EPOLLET
```cpp
// Must manually ensure all data is consumed
ev.events = EPOLLIN | EPOLLET;
// Application must read until EAGAIN
```

## Platform-Specific Workarounds

### Serial Port Issue (Kqueue)

```cpp
// Some devices don't support EV_CLEAR properly
if (events[i].filter == EVFILT_WRITE
    && descriptor_data->num_kevents_ == 2
    && descriptor_data->op_queue_[write_op].empty()) {
  // Remove EVFILT_WRITE to avoid busy loop
  struct kevent delete_events[1];
  EV_SET(&delete_events[0], descriptor, EVFILT_WRITE, EV_DELETE, 0, 0, 0);
  kevent(kqueue_fd_, delete_events, 1, 0, 0, 0);
  descriptor_data->num_kevents_ = 1;
}
```

### Out-of-Band Data

#### Kqueue
```cpp
// macOS may not define EV_OOBAND
#if !defined(EV_OOBAND)
# define EV_OOBAND EV_FLAG1
#endif

// Check for OOB data
if (events[i].flags & EV_OOBAND) {
  // Process exceptional condition
}
```

#### Epoll
```cpp
// Standard EPOLLPRI for urgent data
if (events[i].events & EPOLLPRI) {
  // Process urgent data
}
```

## Best Practices

### Kqueue Optimization
1. Use EV_CLEAR for edge-triggered behavior
2. Batch kevent operations when possible
3. Be aware of fork() implications
4. Handle platform-specific quirks (serial ports)

### Epoll Optimization
1. Use EPOLLET for edge-triggered
2. Combine events in single registration
3. Use EPOLL_CTL_MOD for changes
4. Leverage fork() inheritance

## Summary Comparison Table

| Feature | Kqueue | Epoll |
|---------|--------|-------|
| **API Style** | Filter-based | Event mask-based |
| **Modification** | Delete + Add | Direct modify |
| **Fork Behavior** | Must recreate | Inherited |
| **Edge Trigger** | EV_CLEAR flag | EPOLLET flag |
| **Error Reporting** | In event.flags/data | In event.events |
| **Batch Operations** | Natural (single syscall) | Separate ctl calls |
| **Platform** | macOS, *BSD | Linux only |
| **User Data** | Direct pointer | Union (ptr/fd/u32/u64) |
| **Filters** | Extensible (timer, signal, etc.) | FD only |