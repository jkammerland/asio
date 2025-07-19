# ASIO Platform Architecture

This diagram shows how ASIO abstracts different platform-specific I/O mechanisms while providing a unified interface.

```mermaid
graph TB
    %% User layer
    subgraph "User Code Layer"
        UserCode[User Application]
        AsyncOps[Async Operations<br/>async_read/write/accept/connect]
        SyncOps[Sync Operations<br/>read/write/accept/connect]
    end
    
    %% ASIO abstraction layer
    subgraph "ASIO Abstraction Layer"
        IOContext[io_context]
        Executor[Executors<br/>system_executor<br/>thread_pool<br/>strand]
        BasicSocket[basic_socket<br/>basic_stream_socket<br/>basic_datagram_socket]
        Service[I/O Service<br/>Abstraction]
    end
    
    %% Platform service layer
    subgraph "Platform Service Layer"
        WinService[win_iocp_socket_service]
        ReactiveService[reactive_socket_service]
        IOUringService[io_uring_socket_service]
    end
    
    %% Platform implementation layer
    subgraph "Windows Platform"
        IOCP[I/O Completion Port<br/>CreateIoCompletionPort]
        Overlapped[Overlapped I/O<br/>WSASend/WSARecv<br/>AcceptEx/ConnectEx]
        WinSock[Windows Sockets<br/>WSA API]
        ThreadPool[IOCP Thread Pool]
    end
    
    subgraph "Linux Platform (epoll)"
        Epoll[epoll<br/>epoll_create/ctl/wait]
        EpollReactor[epoll_reactor]
        PosixDesc[POSIX Descriptors<br/>socket/accept/connect]
        EdgeTrigger[Edge-Triggered<br/>Events]
    end
    
    subgraph "Linux Platform (io_uring)"
        IOUring[io_uring<br/>io_uring_setup]
        SQE[Submission Queue<br/>io_uring_prep_*]
        CQE[Completion Queue<br/>io_uring_wait_cqe]
        KernelPoll[Kernel Polling<br/>IORING_SETUP_SQPOLL]
    end
    
    subgraph "macOS/BSD Platform"
        Kqueue[kqueue<br/>kqueue/kevent]
        KqueueReactor[kqueue_reactor]
        BSDSockets[BSD Sockets]
        KqueueFilters[Filters<br/>EVFILT_READ<br/>EVFILT_WRITE]
    end
    
    %% Connections - User to ASIO
    UserCode --> AsyncOps
    UserCode --> SyncOps
    AsyncOps --> IOContext
    SyncOps --> BasicSocket
    IOContext --> Executor
    Executor --> Service
    BasicSocket --> Service
    
    %% Connections - ASIO to Platform Services
    Service --> WinService
    Service --> ReactiveService
    Service --> IOUringService
    
    %% Connections - Platform Services to Implementation
    WinService --> IOCP
    WinService --> Overlapped
    WinService --> WinSock
    IOCP --> ThreadPool
    
    ReactiveService --> Epoll
    ReactiveService --> EpollReactor
    EpollReactor --> PosixDesc
    Epoll --> EdgeTrigger
    
    ReactiveService --> Kqueue
    ReactiveService --> KqueueReactor
    KqueueReactor --> BSDSockets
    Kqueue --> KqueueFilters
    
    IOUringService --> IOUring
    IOUring --> SQE
    IOUring --> CQE
    IOUring --> KernelPoll
    
    %% Platform detection
    subgraph "Compile-Time Platform Detection"
        ConfigH[config.hpp]
        HasIOCP{ASIO_HAS_IOCP}
        HasEpoll{ASIO_HAS_EPOLL}
        HasIOUring{ASIO_HAS_IO_URING}
        HasKqueue{ASIO_HAS_KQUEUE}
    end
    
    ConfigH --> HasIOCP
    ConfigH --> HasEpoll
    ConfigH --> HasIOUring
    ConfigH --> HasKqueue
    
    HasIOCP -->|defined| WinService
    HasEpoll -->|defined| ReactiveService
    HasIOUring -->|defined| IOUringService
    HasKqueue -->|defined| ReactiveService
```

## Platform-Specific Architecture Details

### Windows - I/O Completion Ports (IOCP)

**Architecture Pattern**: Proactor
- True asynchronous I/O where the OS performs the operation
- Completion notifications delivered to completion port
- Highly scalable with automatic thread pool management

**Key Components**:
- **IOCP**: Kernel object that queues completion notifications
- **Overlapped I/O**: Asynchronous I/O operations
- **Thread Pool**: Worker threads calling `GetQueuedCompletionStatus`
- **WSA Functions**: Windows-specific socket extensions

**Advantages**:
- Zero-copy networking possible
- Excellent scalability for high connection counts
- Kernel handles thread wake-up optimization

**Implementation Details**:
```cpp
// Simplified IOCP operation flow
CreateIoCompletionPort(socket, iocp, key, 0);
WSASend(socket, buffers, ..., &overlapped);
GetQueuedCompletionStatus(iocp, &bytes, &key, &overlapped, timeout);
```

### Linux - epoll

**Architecture Pattern**: Reactor
- Edge-triggered or level-triggered event notification
- Application must perform I/O when notified
- Scales better than select/poll

**Key Components**:
- **epoll instance**: Kernel event table
- **Interest List**: Registered file descriptors
- **Ready List**: Descriptors with events
- **Edge-Triggered Mode**: More efficient but requires careful handling

**Advantages**:
- O(1) event delivery (not O(n) like select)
- Supports large numbers of connections
- Fine-grained event control

**Implementation Details**:
```cpp
// Simplified epoll operation flow
int epfd = epoll_create1(EPOLL_CLOEXEC);
epoll_ctl(epfd, EPOLL_CTL_ADD, socket, &event);
int n = epoll_wait(epfd, events, max_events, timeout);
// Perform I/O on ready sockets
```

### Linux - io_uring

**Architecture Pattern**: Proactor
- True asynchronous I/O at kernel level
- Lock-free ring buffers for submission/completion
- Supports all I/O operations, not just sockets

**Key Components**:
- **Submission Queue (SQ)**: Application submits operations
- **Completion Queue (CQ)**: Kernel posts completions
- **SQEs/CQEs**: Submission/Completion queue entries
- **Kernel Polling**: Optional kernel-side polling thread

**Advantages**:
- Minimal syscall overhead
- Batch submission/completion
- True async file I/O support
- SQPOLL for syscall-free operation

**Implementation Details**:
```cpp
// Simplified io_uring operation flow
io_uring_setup(entries, &params);
io_uring_get_sqe(&ring); // Get submission entry
io_uring_prep_read(sqe, fd, buf, len, offset);
io_uring_submit(&ring);
io_uring_wait_cqe(&ring, &cqe);
```

### macOS/BSD - kqueue

**Architecture Pattern**: Reactor
- Similar to epoll but with different semantics
- Unified interface for various event types
- Supports file, signal, timer events

**Key Components**:
- **kqueue**: Kernel event queue
- **kevent structures**: Event registration/notification
- **Filters**: EVFILT_READ, EVFILT_WRITE, etc.
- **Flags**: EV_ADD, EV_DELETE, EV_ENABLE, etc.

**Advantages**:
- Very flexible event filtering
- Supports many event types beyond I/O
- Good performance characteristics

**Implementation Details**:
```cpp
// Simplified kqueue operation flow
int kq = kqueue();
EV_SET(&change, socket, EVFILT_READ, EV_ADD, 0, 0, NULL);
kevent(kq, &change, 1, events, max_events, &timeout);
// Perform I/O on ready sockets
```

## Service Selection Logic

ASIO selects the appropriate service at compile time:

1. **Windows**: Always uses IOCP when available
2. **Linux**: 
   - Prefers io_uring if available and enabled
   - Falls back to epoll
   - Can force selection via macros
3. **macOS/BSD**: Uses kqueue
4. **Fallback**: select() for maximum portability

## Key Abstractions

### io_context
- Manages event loop
- Dispatches completion handlers
- Platform-agnostic interface

### Socket Services
- Implement platform-specific operations
- Handle async operation lifecycle
- Manage platform resources

### Executors
- Control handler execution context
- Enable custom execution strategies
- Support strand-based serialization

### Operation Objects
- Encapsulate async operation state
- Store handlers with type erasure
- Link to platform-specific structures

## Performance Considerations

**IOCP (Windows)**:
- Best for high connection count servers
- Automatic thread scaling
- Higher memory usage per connection

**epoll (Linux)**:
- Excellent for typical server loads
- Lower memory overhead
- Requires careful edge-triggered handling

**io_uring (Linux)**:
- Best for high-performance I/O
- Lowest syscall overhead
- Requires newer kernels (5.1+)

**kqueue (macOS/BSD)**:
- Good general performance
- Rich event type support
- Well-integrated with OS

This architecture allows ASIO to leverage the best features of each platform while maintaining a consistent API for portable network programming.