# io_uring Implementation Details

## Overview
The io_uring implementation provides a high-performance, kernel-bypass networking backend for ASIO on modern Linux systems. It uses ring buffers shared between kernel and userspace to minimize syscall overhead.

## Architecture

### Core Components

#### 1. io_uring_service Class
Located in: `asio/detail/io_uring_service.hpp`

```cpp
class io_uring_service : public execution_context_service_base<io_uring_service>,
                         public scheduler_task
{
  // liburing instance
  struct io_uring ring_;
  
  // Submission queue management
  unsigned sq_len_;
  
  // I/O object tracking
  object_pool<io_object> io_objects_;
  
  // Operation management
  op_queue<io_uring_operation> pending_ops_;
};
```

#### 2. I/O Object State
Each socket has associated io_uring-specific state:

```cpp
struct io_object
{
  io_object* next_;
  io_object* prev_;
  
  mutex mutex_;
  io_uring_service* service_;
  io_queue queues_[max_ops];  // Serialized operation queues
  bool shutdown_;
};
```

#### 3. I/O Queue
Operations that must run serially are queued:

```cpp
class io_queue : operation
{
  io_object* io_object_;
  op_queue<io_uring_operation> op_queue_;
  bool cancel_requested_;
  
  operation* perform_io(int result);
};
```

## Ring Buffer Architecture

### Submission Queue (SQ)
- User → Kernel communication
- Contains submission queue entries (SQEs)
- Lock-free single producer design

### Completion Queue (CQ)
- Kernel → User communication  
- Contains completion queue entries (CQEs)
- Lock-free single consumer design

### Memory Layout
```
┌─────────────────┐
│   User Space    │
│  ┌───────────┐  │
│  │    SQ     │  │ ← Application writes
│  └───────────┘  │
│  ┌───────────┐  │
│  │    CQ     │  │ ← Application reads
│  └───────────┘  │
├─────────────────┤
│   Kernel Space  │
│                 │ ← Kernel processes SQ
│                 │ ← Kernel writes to CQ
└─────────────────┘
```

## Operation Flow

### 1. Submitting Operations
```cpp
void start_op(base_implementation_type& impl, int op_type,
              io_uring_operation* op, bool is_continuation, bool noop)
{
  // Queue operation for serialization
  impl.io_object_data_->queues_[op_type].op_queue_.push(op);
  
  // Try to submit immediately if queue was empty
  if (first_op_in_queue) {
    io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
    if (sqe) {
      op->prepare(sqe);  // Set up SQE
      io_uring_sqe_set_data(sqe, op);
      io_uring_submit(&ring_);
    }
  }
}
```

### 2. Operation Types

#### Socket Operations
```cpp
// Send operation
class io_uring_socket_send_op : public io_uring_operation
{
  void prepare(io_uring_sqe* sqe) {
    io_uring_prep_send(sqe, socket_, buffers_.data(), 
                       buffers_.size(), flags_);
  }
};

// Receive operation  
class io_uring_socket_recv_op : public io_uring_operation
{
  void prepare(io_uring_sqe* sqe) {
    io_uring_prep_recv(sqe, socket_, buffers_.data(),
                       buffers_.size(), flags_);
  }
};

// Accept operation
class io_uring_socket_accept_op : public io_uring_operation
{
  void prepare(io_uring_sqe* sqe) {
    io_uring_prep_accept(sqe, listen_socket_, addr_, &addrlen_, 0);
  }
};
```

### 3. Completion Processing
```cpp
void run(long usec, op_queue<operation>& ops)
{
  // Set timeout
  __kernel_timespec ts;
  set_timeout(&ts, usec);
  
  // Wait for completions
  io_uring_cqe* cqe;
  int ret = io_uring_wait_cqe_timeout(&ring_, &cqe, &ts);
  
  // Process all available completions
  unsigned head;
  io_uring_for_each_cqe(&ring_, head, cqe) {
    io_uring_operation* op = 
      static_cast<io_uring_operation*>(io_uring_cqe_get_data(cqe));
    
    op->complete(cqe->res);  // res contains result/error
    ops.push(op);
  }
  
  // Advance CQ
  io_uring_cq_advance(&ring_, count);
}
```

## Advanced Features

### 1. Linked Operations
Chain multiple operations to execute sequentially:
```cpp
io_uring_sqe* sqe1 = io_uring_get_sqe(&ring_);
prepare_read(sqe1);
sqe1->flags |= IOSQE_IO_LINK;

io_uring_sqe* sqe2 = io_uring_get_sqe(&ring_);
prepare_write(sqe2);
```

### 2. Fixed Buffers
Register buffers with kernel for zero-copy:
```cpp
struct iovec buffers[N];
io_uring_register_buffers(&ring_, buffers, N);

// Use fixed buffer in operation
io_uring_prep_read_fixed(sqe, fd, buf, len, offset, buf_index);
```

### 3. Kernel Polling
For ultra-low latency:
```cpp
struct io_uring_params params = {0};
params.flags |= IORING_SETUP_SQPOLL;
params.sq_thread_idle = 1000;  // milliseconds

io_uring_queue_init_params(entries, &ring_, &params);
```

## Comparison with epoll

### Advantages
1. **Truly asynchronous**: Operations never block in userspace
2. **Reduced syscalls**: Batch submission and completion
3. **Zero-copy capable**: Fixed buffers and buffer selection
4. **Lower latency**: Kernel polling mode available
5. **Better scaling**: Less contention, better CPU cache usage

### Considerations
1. **Memory usage**: Ring buffers require upfront allocation
2. **Kernel version**: Requires Linux 5.1+, optimal on 5.6+
3. **Complexity**: More complex error handling and state management

## Performance Tuning

### Ring Size Configuration
```cpp
// Default: 128 entries
// High-throughput: 1024-4096 entries
unsigned entries = 1024;
io_uring_queue_init(entries, &ring_, 0);
```

### Submission Batching
```cpp
// Batch multiple operations before submitting
for (auto& op : pending_ops) {
  io_uring_sqe* sqe = io_uring_get_sqe(&ring_);
  op->prepare(sqe);
}
io_uring_submit(&ring_);  // Single syscall
```

### CPU Affinity
```cpp
// Pin SQ poll thread to specific CPU
struct io_uring_params params = {0};
params.flags |= IORING_SETUP_SQ_AFF;
params.sq_thread_cpu = 2;  // CPU core 2
```

## Error Handling

### Completion Errors
```cpp
void handle_completion(io_uring_cqe* cqe)
{
  if (cqe->res < 0) {
    // Error occurred
    asio::error_code ec(-cqe->res, asio::error::get_system_category());
    operation->complete(ec);
  } else {
    // Success, res contains bytes transferred
    operation->complete(asio::error_code(), cqe->res);
  }
}
```

### Queue Overflow
- SQ overflow: Operations queued in userspace
- CQ overflow: Lost completions (must handle)

## Build Configuration

### Requirements
```cmake
# Check for io_uring support
find_package(PkgConfig)
pkg_check_modules(LIBURING liburing>=2.0)

if(LIBURING_FOUND)
  target_compile_definitions(asio PRIVATE ASIO_HAS_IO_URING)
  target_link_libraries(asio ${LIBURING_LIBRARIES})
endif()
```

### Enabling io_uring
```cpp
// Compile flags
-DASIO_HAS_IO_URING              // Enable io_uring support
-DASIO_HAS_IO_URING_AS_DEFAULT   // Use as default (not epoll)

// Runtime check
#if defined(ASIO_HAS_IO_URING)
  // io_uring code path
#endif
```