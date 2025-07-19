# ASIO Async Operation Flow

This diagram illustrates how asynchronous operations work in ASIO, from initiation through the reactor/proactor patterns to final completion handler invocation.

```mermaid
flowchart TB
    %% User initiates async operation
    User[User Code] -->|"async_read(socket, buffer, handler)"| AsyncOp[Async Operation Initiation]
    
    %% Operation initiation
    AsyncOp --> CheckImmediate{Can Complete<br/>Immediately?}
    CheckImmediate -->|Yes| PostHandler[Post Handler to Executor]
    CheckImmediate -->|No| InitiateOp[Create Operation Object]
    
    %% Operation object creation
    InitiateOp --> OpObject[Operation Object<br/>- Handler<br/>- Buffer<br/>- Socket<br/>- Completion Condition]
    
    %% Platform-specific registration
    OpObject --> PlatformCheck{Platform?}
    PlatformCheck -->|Windows| IOCP[Register with IOCP<br/>WSASend/WSARecv]
    PlatformCheck -->|Linux| Epoll[Register with epoll<br/>EPOLLIN/EPOLLOUT]
    PlatformCheck -->|Linux io_uring| IOUring[Submit to io_uring<br/>SQE preparation]
    PlatformCheck -->|macOS/BSD| Kqueue[Register with kqueue<br/>EVFILT_READ/WRITE]
    
    %% Return to caller
    IOCP --> ReturnCaller[Return to Caller<br/>Operation Pending]
    Epoll --> ReturnCaller
    IOUring --> ReturnCaller
    Kqueue --> ReturnCaller
    
    %% Event loop processing
    ReturnCaller --> EventLoop[io_context::run()<br/>Event Loop]
    
    %% Platform event detection
    EventLoop --> PlatformEvent{Platform Event}
    PlatformEvent -->|IOCP| GetQueued[GetQueuedCompletionStatus]
    PlatformEvent -->|epoll| EpollWait[epoll_wait]
    PlatformEvent -->|io_uring| CQPoll[io_uring_wait_cqe]
    PlatformEvent -->|kqueue| Kevent[kevent]
    
    %% Event ready
    GetQueued --> EventReady[I/O Event Ready]
    EpollWait --> EventReady
    CQPoll --> EventReady
    Kevent --> EventReady
    
    %% Reactor pattern processing
    EventReady --> ReactorOp{Reactor<br/>Operation?}
    ReactorOp -->|Yes| PerformIO[Perform I/O<br/>read()/write()]
    ReactorOp -->|No| ProactorComplete[I/O Already Complete<br/>(Proactor)]
    
    %% Check completion
    PerformIO --> CheckComplete{Operation<br/>Complete?}
    CheckComplete -->|No| ReRegister[Re-register<br/>with Reactor]
    CheckComplete -->|Yes| ExtractResult[Extract Result<br/>bytes_transferred/error_code]
    ProactorComplete --> ExtractResult
    
    ReRegister --> EventLoop
    
    %% Handler scheduling
    ExtractResult --> CheckStrand{Associated<br/>with Strand?}
    CheckStrand -->|Yes| StrandQueue[Queue to Strand]
    CheckStrand -->|No| DirectExecutor[Queue to Executor]
    
    %% Strand processing
    StrandQueue --> StrandCheck{Strand<br/>Running?}
    StrandCheck -->|No| RunStrand[Run on Strand]
    StrandCheck -->|Yes| QueueStrand[Queue for Later]
    
    QueueStrand --> WaitStrand[Wait for Strand]
    WaitStrand --> RunStrand
    
    %% Handler execution
    RunStrand --> InvokeHandler[Invoke Handler]
    DirectExecutor --> InvokeHandler
    PostHandler --> InvokeHandler
    
    %% Handler completion
    InvokeHandler --> HandlerCode[User Handler Code<br/>handler(error_code, bytes)]
    HandlerCode --> Complete[Operation Complete]
    
    %% Composed operations
    HandlerCode --> ComposedCheck{Composed<br/>Operation?}
    ComposedCheck -->|Yes| NextStep[Initiate Next Step]
    ComposedCheck -->|No| Complete
    NextStep --> AsyncOp
    
    %% Cancellation flow
    User -->|cancel()| CancelOp[Cancel Operation]
    CancelOp --> CancelPlatform{Platform<br/>Cancel}
    CancelPlatform -->|IOCP| CancelIOEx[CancelIoEx]
    CancelPlatform -->|epoll| RemoveEpoll[Remove from epoll]
    CancelPlatform -->|io_uring| CancelSQE[io_uring_prep_cancel]
    CancelPlatform -->|kqueue| RemoveKqueue[Remove from kqueue]
    
    CancelIOEx --> CancelComplete[Operation Cancelled]
    RemoveEpoll --> CancelComplete
    CancelSQE --> CancelComplete
    RemoveKqueue --> CancelComplete
    CancelComplete --> ExtractResult
```

## Detailed Flow Explanation

### 1. Operation Initiation

When a user calls an async operation like `async_read()`:
- The operation checks if it can complete immediately (e.g., data already available in buffer)
- If immediate completion is possible, the handler is posted to the executor
- Otherwise, an operation object is created to track the async operation

### 2. Operation Object

The operation object encapsulates:
- The completion handler (with proper type erasure if needed)
- Buffers for the I/O operation
- The socket/descriptor
- Any completion conditions
- Error handling information

### 3. Platform Registration

The operation is registered with the platform-specific I/O mechanism:

**Windows (IOCP - I/O Completion Ports)**:
- Uses `WSASend`/`WSARecv` with overlapped I/O
- Proactor pattern - OS performs the I/O

**Linux (epoll)**:
- Registers file descriptor with `epoll_ctl`
- Reactor pattern - notified when I/O is possible

**Linux (io_uring)**:
- Submits operation via submission queue entry (SQE)
- True asynchronous I/O at kernel level

**macOS/BSD (kqueue)**:
- Registers with `kevent`
- Similar to epoll but with different semantics

### 4. Event Loop Processing

The `io_context::run()` method:
- Waits for I/O events using platform-specific calls
- Processes ready events
- Executes completion handlers
- Continues until no more work

### 5. Event Detection

Platform-specific event detection:
- **IOCP**: `GetQueuedCompletionStatus` returns completed I/O
- **epoll**: `epoll_wait` returns ready file descriptors
- **io_uring**: `io_uring_wait_cqe` returns completion queue entries
- **kqueue**: `kevent` returns ready events

### 6. Reactor vs Proactor

**Reactor Pattern** (epoll, kqueue):
- Event indicates I/O is possible
- ASIO performs the actual I/O operation
- May need multiple attempts for completion

**Proactor Pattern** (IOCP, io_uring):
- Event indicates I/O is complete
- Result already available
- More efficient but platform-specific

### 7. Handler Scheduling

Completion handlers are scheduled according to their executor:
- Direct executor: Run immediately
- Strand: Serialized execution
- Thread pool: Queued for available thread

### 8. Strand Processing

Strands ensure handler serialization:
- Only one handler runs at a time per strand
- Handlers queue if strand is busy
- Prevents data races in handler code

### 9. Composed Operations

Complex operations like `async_read_until`:
- Implemented as state machines
- Each completion may trigger the next step
- Continue until overall operation completes

### 10. Cancellation

Cancellation flow varies by platform:
- Must remove pending operations
- Generate operation_aborted errors
- Ensure handlers still run (with error)

## Key Concepts

**Completion Handlers**: User-provided callbacks invoked when operations complete

**Executors**: Control where and how handlers run

**Strands**: Provide serialized handler execution

**Composed Operations**: Build complex operations from primitives

**Error Handling**: Consistent error_code propagation

This architecture allows ASIO to provide a uniform async programming model across different platforms while leveraging platform-specific optimizations.