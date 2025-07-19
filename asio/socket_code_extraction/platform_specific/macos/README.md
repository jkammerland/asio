# macOS/BSD Networking Implementation in ASIO

This directory contains comprehensive documentation of ASIO's macOS and BSD-specific networking implementation, which uses the kqueue event notification mechanism.

## Documentation Structure

### 1. [kqueue_reactor_overview.md](kqueue_reactor_overview.md)
High-level overview of the kqueue reactor implementation:
- Platform detection and architecture
- Kqueue event model and operation flow
- Performance characteristics and optimization strategies
- Platform-specific features and limitations

### 2. [kqueue_implementation_details.md](kqueue_implementation_details.md)
Deep dive into the implementation:
- Core kqueue operations and descriptor management
- Event registration and processing logic
- Thread safety and locking mechanisms
- Error handling and edge cases
- Performance tuning options

### 3. [kqueue_vs_epoll_comparison.md](kqueue_vs_epoll_comparison.md)
Detailed comparison between kqueue (macOS/BSD) and epoll (Linux):
- API differences and event models
- Implementation variations in ASIO
- Performance characteristics
- Platform-specific workarounds
- Best practices for each platform

### 4. [macos_specific_features.md](macos_specific_features.md)
macOS-specific configurations and features:
- Platform detection mechanisms
- System-specific optimizations
- Integration with macOS frameworks
- Version-specific considerations
- Debugging tips and common issues

### 5. [kqueue_example_code.md](kqueue_example_code.md)
Practical code examples:
- Basic kqueue usage patterns
- Async operation implementations
- Error handling examples
- Fork safety implementations
- Complete working examples

## Key Takeaways

### Architecture
- **Kqueue** is the primary event notification mechanism on macOS/BSD
- Uses a **filter-based** model (EVFILT_READ, EVFILT_WRITE) vs Linux's event mask model
- Implements **edge-triggered** behavior using EV_CLEAR flag
- Supports batched operations for efficiency

### Performance
- **O(1) scalability** - only returns active events
- **Direct user data association** via kevent.udata pointer
- **Speculative execution** attempts operations before queuing
- **Object pooling** for descriptor state management

### Differences from Linux (epoll)
1. **Fork behavior**: Kqueue descriptors must be manually recreated
2. **Event registration**: Requires DELETE+ADD for modifications (no MOD operation)
3. **Error reporting**: Errors returned in event.flags and event.data
4. **Platform quirks**: Special handling for serial ports and some devices

### Best Practices
1. Use **EV_CLEAR** for edge-triggered behavior
2. **Batch kevent operations** when possible
3. Implement proper **fork handlers** for child processes
4. Handle **platform-specific quirks** (e.g., serial port EVFILT_WRITE issues)
5. Set appropriate **file descriptor limits** (macOS defaults are low)

## Implementation Files

The actual implementation is located in:
- Header: `/include/asio/detail/kqueue_reactor.hpp`
- Implementation: `/include/asio/detail/impl/kqueue_reactor.ipp`
- Platform config: `/include/asio/detail/config.hpp`

## Platform Support

This implementation is automatically enabled on:
- macOS (all versions, with specific optimizations for 10.5+)
- FreeBSD
- NetBSD
- OpenBSD

The implementation can be disabled by defining `ASIO_DISABLE_KQUEUE` before including ASIO headers.

## Related Components

- **select_interrupter**: Uses pipe-based interruption on macOS/BSD
- **timer_queue**: Integrated with kqueue timeout mechanism
- **socket_ops**: Platform-specific socket operations

## Performance Considerations

For high-performance applications on macOS:
1. Increase file descriptor limits (`ulimit -n`)
2. Use SO_NOSIGPIPE to prevent SIGPIPE
3. Consider SO_REUSEPORT for load balancing
4. Profile with Instruments for macOS-specific optimizations
5. Be aware of App Store sandboxing requirements

## Further Reading

- [kqueue(2) man page](https://developer.apple.com/library/archive/documentation/System/Conceptual/ManPages_iPhoneOS/man2/kqueue.2.html)
- [Apple's Network Programming Guide](https://developer.apple.com/library/archive/documentation/NetworkingInternet/Conceptual/NetworkingTopics/Introduction/Introduction.html)
- [ASIO documentation](https://think-async.com/Asio/)