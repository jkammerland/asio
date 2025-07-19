# macOS-Specific Networking Features in ASIO

## Platform Detection

ASIO detects macOS using the following macros:
```cpp
#if defined(__MACH__) && defined(__APPLE__)
```

## macOS-Specific Configurations

### 1. Kqueue as Default Reactor

On macOS, kqueue is automatically selected as the reactor implementation:

```cpp
#if (defined(__MACH__) && defined(__APPLE__))
# if !defined(ASIO_HAS_KQUEUE)
#  if !defined(ASIO_DISABLE_KQUEUE)
#   define ASIO_HAS_KQUEUE 1
#  endif
# endif
#endif
```

### 2. Select Interrupter

macOS uses pipe-based select interrupter since eventfd is Linux-specific:

```cpp
// On macOS, this resolves to pipe_select_interrupter
typedef pipe_select_interrupter select_interrupter;
```

The pipe interrupter creates a pipe pair for waking the reactor:
- Write a byte to interrupt
- Read to acknowledge interruption
- Automatically recreated after fork()

### 3. getaddrinfo Support

macOS version-specific support for getaddrinfo:

```cpp
#elif defined(__MACH__) && defined(__APPLE__)
# if defined(__MAC_OS_X_VERSION_MIN_REQUIRED)
#  if (__MAC_OS_X_VERSION_MIN_REQUIRED >= 1050)
#   define ASIO_HAS_GETADDRINFO 1
#  endif
# else
#  define ASIO_HAS_GETADDRINFO 1
# endif
#endif
```

### 4. POSIX Features

macOS supports standard POSIX features:

```cpp
// ssize_t support
#if defined(__linux__) || (defined(__MACH__) && defined(__APPLE__))
# define ASIO_HAS_SSIZE_T 1
#endif

// unistd.h availability
#if (defined(__MACH__) && defined(__APPLE__))
# define ASIO_HAS_UNISTD_H 1
#endif
```

## Kqueue-Specific Optimizations

### 1. Out-of-Band Data Handling

```cpp
// Compatibility for older macOS versions
#if !defined(EV_OOBAND)
# define EV_OOBAND EV_FLAG1
#endif
```

### 2. NetBSD Compatibility in macOS Code

The kqueue implementation includes NetBSD compatibility that also benefits macOS:

```cpp
#if defined(__NetBSD__) && __NetBSD_Version__ < 999001500
# define ASIO_KQUEUE_EV_SET(ev, ident, filt, flags, fflags, data, udata) \
    EV_SET(ev, ident, filt, flags, fflags, data, \
      reinterpret_cast<intptr_t>(static_cast<void*>(udata)))
#else
# define ASIO_KQUEUE_EV_SET(ev, ident, filt, flags, fflags, data, udata) \
    EV_SET(ev, ident, filt, flags, fflags, data, udata)
#endif
```

## Performance Considerations

### 1. Kqueue Advantages on macOS

- **Native Integration**: Kqueue is deeply integrated with the Darwin kernel
- **Unified Event Model**: Can monitor files, sockets, signals, and more
- **Low Overhead**: Minimal memory usage per monitored descriptor
- **Efficient Wakeups**: Edge-triggered behavior with EV_CLEAR

### 2. Limitations

- **Fork Handling**: Kqueue descriptors not inherited across fork()
- **Serial Devices**: Some device types have quirks with EVFILT_WRITE
- **No Modification**: Must delete and re-add to change event filters

### 3. Optimization Tips

```cpp
// Batch operations for efficiency
struct kevent changes[MAX_CHANGES];
struct kevent events[MAX_EVENTS];
int nchanges = 0;

// Add multiple changes
EV_SET(&changes[nchanges++], fd1, EVFILT_READ, EV_ADD | EV_CLEAR, 0, 0, data1);
EV_SET(&changes[nchanges++], fd2, EVFILT_WRITE, EV_ADD | EV_CLEAR, 0, 0, data2);

// Apply all changes and wait for events in one syscall
int nevents = kevent(kq, changes, nchanges, events, MAX_EVENTS, timeout);
```

## Socket Options

### macOS-Specific Socket Behaviors

1. **SO_NOSIGPIPE**: macOS-specific option to prevent SIGPIPE
```cpp
int flag = 1;
setsockopt(socket, SOL_SOCKET, SO_NOSIGPIPE, &flag, sizeof(flag));
```

2. **TCP_NODELAY**: Standard but important for macOS performance
```cpp
int flag = 1;
setsockopt(socket, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
```

3. **SO_REUSEPORT**: Available on macOS for load balancing
```cpp
int flag = 1;
setsockopt(socket, SOL_SOCKET, SO_REUSEPORT, &flag, sizeof(flag));
```

## System Limits

### File Descriptor Limits

macOS has different default limits than Linux:

```bash
# Default soft limit (often 256)
ulimit -n

# Maximum hard limit (varies by macOS version)
ulimit -Hn

# Increase limit for process
ulimit -n 10240
```

### Kqueue Limits

```cpp
// Maximum events to process per kevent call
static const int MAX_EVENTS = 128;  // ASIO default

// Maximum timeout to detect clock changes
const long max_usec = 5 * 60 * 1000 * 1000;  // 5 minutes
```

## Debugging Tips

### 1. Enable Handler Tracking

```cpp
#define ASIO_ENABLE_HANDLER_TRACKING
```

### 2. Kqueue Event Monitoring

```cpp
// In debug builds, log kqueue events
#if defined(ASIO_ENABLE_HANDLER_TRACKING)
for (int i = 0; i < num_events; ++i) {
  unsigned event_mask = 0;
  switch (events[i].filter) {
  case EVFILT_READ:
    event_mask |= ASIO_HANDLER_REACTOR_READ_EVENT;
    break;
  case EVFILT_WRITE:
    event_mask |= ASIO_HANDLER_REACTOR_WRITE_EVENT;
    break;
  }
  if ((events[i].flags & (EV_ERROR | EV_OOBAND)) != 0)
    event_mask |= ASIO_HANDLER_REACTOR_ERROR_EVENT;
  ASIO_HANDLER_REACTOR_EVENTS((context(),
        reinterpret_cast<uintmax_t>(ptr), event_mask));
}
#endif
```

### 3. Common Issues

1. **"Too many open files"**: Increase ulimit
2. **Fork issues**: Ensure proper fork handlers
3. **Serial port spinning**: Check for EVFILT_WRITE removal logic
4. **Performance degradation**: Monitor number of registered events

## Integration with macOS Features

### 1. Grand Central Dispatch (GCD)

While ASIO uses kqueue directly, it can coexist with GCD:
- Use separate threads for ASIO and GCD
- Or use ASIO's io_context as the main event loop

### 2. Network Framework

For newer macOS features (e.g., QUIC), consider:
- Using ASIO for traditional TCP/UDP
- Network.framework for modern protocols
- Bridging between the two when needed

### 3. App Sandboxing

When using ASIO in sandboxed macOS apps:
- Ensure network client/server entitlements
- Handle sandbox file access restrictions
- Consider Mach port limitations

## Version-Specific Considerations

### macOS 10.5+ (Leopard)
- Full getaddrinfo support
- Modern kqueue implementation

### macOS 10.15+ (Catalina)
- Hardened runtime requirements
- Notarization may affect network apps

### macOS 11+ (Big Sur)
- Apple Silicon considerations
- Universal binary support

## Best Practices for macOS

1. **Always handle fork() properly** - Kqueue descriptors don't survive fork
2. **Set appropriate ulimits** - macOS defaults are low
3. **Use SO_NOSIGPIPE** - Prevent unexpected SIGPIPE
4. **Test with various devices** - Serial/USB devices may have quirks
5. **Profile with Instruments** - Use macOS native tools for performance analysis
6. **Consider App Store requirements** - Network permissions, sandboxing