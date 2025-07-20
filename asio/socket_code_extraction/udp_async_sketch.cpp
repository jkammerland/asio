// Minimal Async UDP Implementation Sketch
// Platform-specific async I/O without coroutine integration

#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <span>
#include <system_error>
#include <variant>
#include <thread>
#include <atomic>
#include <chrono>

#ifndef _WIN32
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/time.h>
#else
#include <winsock2.h>
#endif

// Common types
struct endpoint {
  uint32_t address; // IPv4 for simplicity
  uint16_t port;

  // Platform-specific storage
  union {
    sockaddr_in sin;
    sockaddr_storage storage;
  };
};

using buffer = std::span<std::byte>;
using const_buffer = std::span<const std::byte>;
using completion_handler = std::function<void(std::error_code, size_t)>;
using receive_handler = std::function<void(std::error_code, size_t, endpoint)>;

// Base interface
class async_udp_socket {
public:
  virtual ~async_udp_socket() = default;
  virtual void async_send_to(const_buffer data, const endpoint &ep,
                             completion_handler handler) = 0;
  virtual void async_receive_from(buffer data, receive_handler handler) = 0;
  virtual void bind(const endpoint &ep) = 0;
  virtual void close() = 0;
};

#ifdef _WIN32
// =============================================================================
// Windows IOCP Implementation
// =============================================================================

#include <mswsock.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

class iocp_udp_socket : public async_udp_socket {
private:
  SOCKET socket_;
  HANDLE iocp_;

  // Operation types for IOCP
  enum op_type { op_send, op_recv };

  // Base operation structure
  struct operation : OVERLAPPED {
    op_type type;
    virtual void complete(DWORD bytes_transferred, DWORD error) = 0;
    virtual ~operation() = default;
  };

  // Send operation
  struct send_operation : operation {
    completion_handler handler;
    WSABUF wsabuf;

    void complete(DWORD bytes_transferred, DWORD error) override {
      std::error_code ec;
      if (error != 0) {
        ec = std::error_code(error, std::system_category());
      }
      handler(ec, bytes_transferred);
      delete this;
    }
  };

  // Receive operation
  struct receive_operation : operation {
    receive_handler handler;
    WSABUF wsabuf;
    sockaddr_storage addr;
    int addr_len = sizeof(addr);
    DWORD flags = 0;

    void complete(DWORD bytes_transferred, DWORD error) override {
      std::error_code ec;
      if (error != 0) {
        ec = std::error_code(error, std::system_category());
      }

      endpoint ep;
      if (!ec && addr.ss_family == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(&addr);
        ep.address = ntohl(sin->sin_addr.s_addr);
        ep.port = ntohs(sin->sin_port);
      }

      handler(ec, bytes_transferred, ep);
      delete this;
    }
  };

public:
  iocp_udp_socket(HANDLE iocp) : socket_(INVALID_SOCKET), iocp_(iocp) {
    // Create UDP socket
    socket_ = WSASocket(AF_INET, SOCK_DGRAM, IPPROTO_UDP, NULL, 0,
                        WSA_FLAG_OVERLAPPED);
    if (socket_ == INVALID_SOCKET) {
      throw std::system_error(WSAGetLastError(), std::system_category());
    }

    // Associate with IOCP
    if (CreateIoCompletionPort((HANDLE)socket_, iocp_, (ULONG_PTR)this, 0) ==
        NULL) {
      closesocket(socket_);
      throw std::system_error(GetLastError(), std::system_category());
    }
  }

  ~iocp_udp_socket() { close(); }

  void bind(const endpoint &ep) override {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ep.address);
    addr.sin_port = htons(ep.port);

    if (::bind(socket_, (sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR) {
      throw std::system_error(WSAGetLastError(), std::system_category());
    }
  }

  void async_send_to(const_buffer data, const endpoint &ep,
                     completion_handler handler) override {
    auto *op = new send_operation();
    op->type = op_send;
    op->handler = std::move(handler);
    op->wsabuf.buf = (char *)data.data();
    op->wsabuf.len = data.size();

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ep.address);
    addr.sin_port = htons(ep.port);

    DWORD bytes_sent;
    int result = WSASendTo(socket_, &op->wsabuf, 1, &bytes_sent, 0,
                           (sockaddr *)&addr, sizeof(addr), op, NULL);

    if (result == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (error != WSA_IO_PENDING) {
        op->complete(0, error);
      }
    }
  }

  void async_receive_from(buffer data, receive_handler handler) override {
    auto *op = new receive_operation();
    op->type = op_recv;
    op->handler = std::move(handler);
    op->wsabuf.buf = (char *)data.data();
    op->wsabuf.len = data.size();

    DWORD bytes_received;
    int result =
        WSARecvFrom(socket_, &op->wsabuf, 1, &bytes_received, &op->flags,
                    (sockaddr *)&op->addr, &op->addr_len, op, NULL);

    if (result == SOCKET_ERROR) {
      int error = WSAGetLastError();
      if (error != WSA_IO_PENDING) {
        op->complete(0, error);
      }
    }
  }

  void close() override {
    if (socket_ != INVALID_SOCKET) {
      closesocket(socket_);
      socket_ = INVALID_SOCKET;
    }
  }

  // Called by IOCP thread
  static void handle_completion(OVERLAPPED *overlapped, DWORD bytes_transferred,
                                DWORD error) {
    auto *op = static_cast<operation *>(overlapped);
    op->complete(bytes_transferred, error);
  }
};

// IOCP Event Loop
class iocp_event_loop {
  HANDLE iocp_;

public:
  iocp_event_loop() {
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocp_ == NULL) {
      throw std::system_error(GetLastError(), std::system_category());
    }
  }

  ~iocp_event_loop() { CloseHandle(iocp_); }

  std::unique_ptr<async_udp_socket> create_udp_socket() {
    return std::make_unique<iocp_udp_socket>(iocp_);
  }

  void run() {
    DWORD bytes_transferred;
    ULONG_PTR completion_key;
    OVERLAPPED *overlapped;

    while (GetQueuedCompletionStatus(iocp_, &bytes_transferred, &completion_key,
                                     &overlapped, INFINITE)) {
      if (overlapped) {
        iocp_udp_socket::handle_completion(overlapped, bytes_transferred, 0);
      }
    }
  }

  void stop() { PostQueuedCompletionStatus(iocp_, 0, 0, NULL); }
};

#elif defined(__linux__)
// =============================================================================
// Linux io_uring Implementation
// =============================================================================

#include <cstring>
#include <liburing.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class io_uring_udp_socket : public async_udp_socket {
private:
  int fd_;
  io_uring *ring_;

  // Operation types
  enum op_type { op_send, op_recv };

public:
  // User data for operations
  struct operation {
    op_type type;
    virtual void complete(int32_t result) = 0;
    virtual ~operation() = default;
  };

  struct send_operation : operation {
    completion_handler handler;
    sockaddr_storage addr;
    socklen_t addr_len;

    void complete(int32_t result) override {
      std::error_code ec;
      size_t bytes = 0;

      if (result < 0) {
        ec = std::error_code(-result, std::generic_category());
      } else {
        bytes = result;
      }

      handler(ec, bytes);
      delete this;
    }
  };

  struct receive_operation : operation {
    receive_handler handler;
    sockaddr_storage addr;
    socklen_t addr_len;
    struct msghdr msg;
    struct iovec iov;
    
    receive_operation() : addr_len(sizeof(addr)) {}

    void complete(int32_t result) override {
      std::error_code ec;
      size_t bytes = 0;
      endpoint ep{};

      if (result < 0) {
        ec = std::error_code(-result, std::generic_category());
      } else {
        bytes = result;

        if (addr.ss_family == AF_INET) {
          auto *sin = reinterpret_cast<sockaddr_in *>(&addr);
          ep.address = ntohl(sin->sin_addr.s_addr);
          ep.port = ntohs(sin->sin_port);
          // Also copy the sockaddr_in structure for sendto
          ep.sin = *sin;
        }
      }

      handler(ec, bytes, ep);
      delete this;
    }
  };

public:
  io_uring_udp_socket(io_uring *ring) : fd_(-1), ring_(ring) {
    fd_ = socket(AF_INET, SOCK_DGRAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd_ < 0) {
      throw std::system_error(errno, std::generic_category());
    }
  }

  ~io_uring_udp_socket() { close(); }

  void bind(const endpoint &ep) override {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ep.address);
    addr.sin_port = htons(ep.port);

    if (::bind(fd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
      throw std::system_error(errno, std::generic_category());
    }
  }

  void async_send_to(const_buffer data, const endpoint &ep,
                     completion_handler handler) override {
    auto *op = new send_operation();
    op->type = op_send;
    op->handler = std::move(handler);

    // Copy endpoint data to operation for persistence
    if (ep.sin.sin_family == AF_INET) {
      // Use the pre-filled sockaddr from recvmsg
      memcpy(&op->addr, &ep.sin, sizeof(sockaddr_in));
      op->addr_len = sizeof(sockaddr_in);

    } else {
      // Construct sockaddr from address/port
      sockaddr_in* addr = reinterpret_cast<sockaddr_in*>(&op->addr);
      memset(addr, 0, sizeof(sockaddr_in));
      addr->sin_family = AF_INET;
      addr->sin_addr.s_addr = htonl(ep.address);
      addr->sin_port = htons(ep.port);
      op->addr_len = sizeof(sockaddr_in);

    }

    io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
      op->complete(-EBUSY);
      return;
    }

    io_uring_prep_sendto(sqe, fd_, data.data(), data.size(), 0,
                         (sockaddr*)&op->addr, op->addr_len);
    io_uring_sqe_set_data(sqe, op);
  }

  void async_receive_from(buffer data, receive_handler handler) override {
    auto *op = new receive_operation();
    op->type = op_recv;
    op->handler = std::move(handler);

    io_uring_sqe *sqe = io_uring_get_sqe(ring_);
    if (!sqe) {
      op->complete(-EBUSY);
      return;
    }

    // For UDP, we need to use recvmsg to get sender address
    auto *recv_op = static_cast<receive_operation *>(op);
    
    // Setup iovec for the data buffer
    recv_op->iov.iov_base = data.data();
    recv_op->iov.iov_len = data.size();
    
    // Setup msghdr - must be done after iov is set up
    memset(&recv_op->msg, 0, sizeof(recv_op->msg));
    recv_op->msg.msg_name = &recv_op->addr;
    recv_op->msg.msg_namelen = recv_op->addr_len;
    recv_op->msg.msg_iov = &recv_op->iov;
    recv_op->msg.msg_iovlen = 1;

    // Use recvmsg to get sender address
    io_uring_prep_recvmsg(sqe, fd_, &recv_op->msg, 0);
    io_uring_sqe_set_data(sqe, op);
  }

  void close() override {
    if (fd_ >= 0) {
      ::close(fd_);
      fd_ = -1;
    }
  }
};

// io_uring Event Loop
class io_uring_event_loop {
  io_uring ring_;
  bool running_ = false;

public:
  io_uring_event_loop(unsigned entries = 256) {
    if (io_uring_queue_init(entries, &ring_, 0) < 0) {
      throw std::system_error(errno, std::generic_category());
    }
  }

  ~io_uring_event_loop() { io_uring_queue_exit(&ring_); }

  std::unique_ptr<async_udp_socket> create_udp_socket() {
    return std::make_unique<io_uring_udp_socket>(&ring_);
  }

  void run() {
    running_ = true;
    io_uring_cqe *cqe;

    while (running_) {
      // Submit any pending SQEs
      io_uring_submit(&ring_);

      // Wait for completion
      if (io_uring_wait_cqe(&ring_, &cqe) < 0) {
        continue;
      }

      // Process completion
      auto *op = static_cast<io_uring_udp_socket::operation *>(
          io_uring_cqe_get_data(cqe));
      if (op) {
        op->complete(cqe->res);
      }

      io_uring_cqe_seen(&ring_, cqe);
    }
  }

  void stop() {
    running_ = false;
    // Submit a no-op to wake up the event loop
    io_uring_sqe *sqe = io_uring_get_sqe(&ring_);
    if (sqe) {
      io_uring_prep_nop(sqe);
      io_uring_submit(&ring_);
    }
  }
};

#elif defined(__APPLE__) || defined(__FreeBSD__)
// =============================================================================
// macOS/BSD kqueue Implementation
// =============================================================================

#include <fcntl.h>
#include <map>
#include <netinet/in.h>
#include <sys/event.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>

class kqueue_udp_socket : public async_udp_socket {
private:
  int fd_;
  int kq_;

  // Operations pending on this socket
  struct pending_ops {
    std::unique_ptr<completion_handler> send_handler;
    const_buffer send_buffer;
    endpoint send_endpoint;

    std::unique_ptr<receive_handler> recv_handler;
    buffer recv_buffer;
  };

  // Global map of fd -> operations (in real impl, would be in event loop)
  static std::map<int, pending_ops> pending_operations_;

public:
  kqueue_udp_socket(int kq) : fd_(-1), kq_(kq) {
    global_kq_ = kq; // Store for static method access
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
      throw std::system_error(errno, std::generic_category());
    }

    // Set non-blocking
    int flags = fcntl(fd_, F_GETFL, 0);
    if (fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0) {
      ::close(fd_);
      throw std::system_error(errno, std::generic_category());
    }
  }

  ~kqueue_udp_socket() { close(); }

  void bind(const endpoint &ep) override {
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ep.address);
    addr.sin_port = htons(ep.port);

    if (::bind(fd_, (sockaddr *)&addr, sizeof(addr)) < 0) {
      throw std::system_error(errno, std::generic_category());
    }
  }

  void async_send_to(const_buffer data, const endpoint &ep,
                     completion_handler handler) override {
    // Try immediate send
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(ep.address);
    addr.sin_port = htons(ep.port);

    ssize_t result = sendto(fd_, data.data(), data.size(), 0, (sockaddr *)&addr,
                            sizeof(addr));

    if (result >= 0) {
      // Immediate completion
      handler(std::error_code{}, result);
      return;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      handler(std::error_code(errno, std::generic_category()), 0);
      return;
    }

    // Register for write readiness
    auto &ops = pending_operations_[fd_];
    ops.send_handler = std::make_unique<completion_handler>(std::move(handler));
    ops.send_buffer = data;
    ops.send_endpoint = ep;

    struct kevent ev;
    EV_SET(&ev, fd_, EVFILT_WRITE, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
           (void *)(intptr_t)fd_);
    if (kevent(kq_, &ev, 1, NULL, 0, NULL) < 0) {
      ops.send_handler->operator()(
          std::error_code(errno, std::generic_category()), 0);
      ops.send_handler.reset();
    }
  }

  void async_receive_from(buffer data, receive_handler handler) override {
    // Try immediate receive
    sockaddr_storage addr{};
    socklen_t addr_len = sizeof(addr);

    ssize_t result = recvfrom(fd_, data.data(), data.size(), 0,
                              (sockaddr *)&addr, &addr_len);

    if (result >= 0) {
      // Immediate completion
      endpoint ep{};
      if (addr.ss_family == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(&addr);
        ep.address = ntohl(sin->sin_addr.s_addr);
        ep.port = ntohs(sin->sin_port);
      }
      handler(std::error_code{}, result, ep);
      return;
    }

    if (errno != EAGAIN && errno != EWOULDBLOCK) {
      handler(std::error_code(errno, std::generic_category()), 0, endpoint{});
      return;
    }

    // Register for read readiness
    auto &ops = pending_operations_[fd_];
    ops.recv_handler = std::make_unique<receive_handler>(std::move(handler));
    ops.recv_buffer = data;

    struct kevent ev;
    EV_SET(&ev, fd_, EVFILT_READ, EV_ADD | EV_ENABLE | EV_CLEAR, 0, 0,
           (void *)(intptr_t)fd_);
    if (kevent(kq_, &ev, 1, NULL, 0, NULL) < 0) {
      ops.recv_handler->operator()(
          std::error_code(errno, std::generic_category()), 0, endpoint{});
      ops.recv_handler.reset();
    }
  }

  void close() override {
    if (fd_ >= 0) {
      // Remove from kqueue
      struct kevent ev[2];
      EV_SET(&ev[0], fd_, EVFILT_READ, EV_DELETE, 0, 0, NULL);
      EV_SET(&ev[1], fd_, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
      kevent(kq_, ev, 2, NULL, 0, NULL);

      // Clean up pending operations
      pending_operations_.erase(fd_);

      ::close(fd_);
      fd_ = -1;
    }
  }

  // Need to store kqueue descriptor for handle_ready
  static int global_kq_;

  // Called by event loop when fd is ready
  static void handle_ready(int fd, int16_t filter) {
    auto it = pending_operations_.find(fd);
    if (it == pending_operations_.end()) {
      return;
    }

    auto &ops = it->second;

    if (filter == EVFILT_WRITE && ops.send_handler) {
      // Perform the send
      sockaddr_in addr{};
      addr.sin_family = AF_INET;
      addr.sin_addr.s_addr = htonl(ops.send_endpoint.address);
      addr.sin_port = htons(ops.send_endpoint.port);

      ssize_t result =
          sendto(fd, ops.send_buffer.data(), ops.send_buffer.size(), 0,
                 (sockaddr *)&addr, sizeof(addr));

      if (result >= 0) {
        (*ops.send_handler)(std::error_code{}, result);
      } else {
        (*ops.send_handler)(std::error_code(errno, std::generic_category()), 0);
      }

      ops.send_handler.reset();

      // Remove write interest
      struct kevent ev;
      EV_SET(&ev, fd, EVFILT_WRITE, EV_DELETE, 0, 0, NULL);
      kevent(global_kq_, &ev, 1, NULL, 0, NULL);
    }

    if (filter == EVFILT_READ && ops.recv_handler) {
      // Perform the receive
      sockaddr_storage addr{};
      socklen_t addr_len = sizeof(addr);

      ssize_t result =
          recvfrom(fd, ops.recv_buffer.data(), ops.recv_buffer.size(), 0,
                   (sockaddr *)&addr, &addr_len);

      endpoint ep{};
      if (result >= 0 && addr.ss_family == AF_INET) {
        auto *sin = reinterpret_cast<sockaddr_in *>(&addr);
        ep.address = ntohl(sin->sin_addr.s_addr);
        ep.port = ntohs(sin->sin_port);
      }

      if (result >= 0) {
        (*ops.recv_handler)(std::error_code{}, result, ep);
      } else {
        (*ops.recv_handler)(std::error_code(errno, std::generic_category()), 0,
                            ep);
      }

      ops.recv_handler.reset();

      // Remove read interest
      struct kevent ev;
      EV_SET(&ev, fd, EVFILT_READ, EV_DELETE, 0, 0, NULL);
      kevent(global_kq_, &ev, 1, NULL, 0, NULL);
    }
  }
};

std::map<int, kqueue_udp_socket::pending_ops>
    kqueue_udp_socket::pending_operations_;
int kqueue_udp_socket::global_kq_ = -1;

// kqueue Event Loop
class kqueue_event_loop {
  int kq_;
  bool running_ = false;

public:
  kqueue_event_loop() {
    kq_ = kqueue();
    if (kq_ < 0) {
      throw std::system_error(errno, std::generic_category());
    }
  }

  ~kqueue_event_loop() { ::close(kq_); }

  std::unique_ptr<async_udp_socket> create_udp_socket() {
    return std::make_unique<kqueue_udp_socket>(kq_);
  }

  void run() {
    running_ = true;
    struct kevent events[64];

    while (running_) {
      int nev = kevent(kq_, NULL, 0, events, 64, NULL);

      if (nev < 0) {
        if (errno == EINTR)
          continue;
        break;
      }

      for (int i = 0; i < nev; ++i) {
        int fd = (int)(intptr_t)events[i].udata;
        kqueue_udp_socket::handle_ready(fd, events[i].filter);
      }
    }
  }

  void stop() {
    running_ = false;
    // Could use a pipe or eventfd to wake up kevent
  }
};

#endif

// =============================================================================
// Usage Example
// =============================================================================

// Test client that runs in separate thread
class test_client {
  int fd_;
  std::atomic<bool> success_{false};
  std::atomic<bool> done_{false};
  
public:
  test_client() : fd_(-1) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
      throw std::system_error(errno, std::generic_category(), "Failed to create client socket");
    }
  }
  
  ~test_client() {
    if (fd_ >= 0) {
#ifdef _WIN32
      closesocket(fd_);
#else
      close(fd_);
#endif
    }
  }
  
  bool run_test() {
    // Wait a bit for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    // Server address
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1
    server_addr.sin_port = htons(8080);
    
    // Test message
    const char* test_msg = "Hello, UDP Echo Server!";
    size_t msg_len = strlen(test_msg);
    
    // Send test message
    ssize_t sent = sendto(fd_, test_msg, msg_len, 0,
                         (sockaddr*)&server_addr, sizeof(server_addr));
    if (sent != static_cast<ssize_t>(msg_len)) {
      std::cerr << "Client: Failed to send test message\n";
      done_ = true;
      return false;
    }
    
    std::cout << "Client: Sent '" << test_msg << "'\n";
    
    // Set receive timeout
#ifdef _WIN32
    DWORD timeout = 2000; // 2 seconds
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    // Receive echo
    char recv_buffer[1024];
    sockaddr_storage from_addr{};
    socklen_t from_len = sizeof(from_addr);
    
    ssize_t received = recvfrom(fd_, recv_buffer, sizeof(recv_buffer), 0,
                               (sockaddr*)&from_addr, &from_len);
    
    if (received < 0) {
      std::cerr << "Client: Timeout or error receiving echo\n";
      done_ = true;
      return false;
    }
    
    // Verify echo
    if (received == static_cast<ssize_t>(msg_len) && 
        memcmp(recv_buffer, test_msg, msg_len) == 0) {
      std::cout << "Client: Received correct echo: '" 
                << std::string(recv_buffer, received) << "'\n";
      success_ = true;
    } else {
      std::cerr << "Client: Echo mismatch!\n";
    }
    
    done_ = true;
    return success_;
  }
  
  bool is_done() const { return done_; }
  bool is_success() const { return success_; }
};

int example_usage() {
#ifdef _WIN32
  iocp_event_loop loop;
#elif defined(__linux__)
  io_uring_event_loop loop;
#else
  kqueue_event_loop loop;
#endif

  auto socket = loop.create_udp_socket();

  // Bind to port
  endpoint bind_ep{0, 8080}; // 0.0.0.0:8080
  socket->bind(bind_ep);

  // Server state
  std::atomic<bool> server_running{true};
  std::atomic<int> messages_processed{0};
  
  // Async receive
  std::byte recv_buffer[1024];
  std::function<void()> start_receive;
  
  start_receive = [&]() {
    socket->async_receive_from(
        recv_buffer,
        [&](std::error_code ec, size_t bytes_received, endpoint from) {
          if (!ec) {
            std::cout << "Server: Received " << bytes_received << " bytes from "
                      << from.address << ":" << from.port << "\n";
            
            messages_processed++;
            
            // Echo back

            socket->async_send_to({recv_buffer, bytes_received}, from,
                                  [&](std::error_code ec, size_t bytes_sent) {
                                    if (!ec) {
                                      std::cout << "Server: Sent " << bytes_sent
                                                << " bytes\n";
                                    } else {
                                      std::cerr << "Server: Send error: " << ec.message() << "\n";
                                    }
                                  });
            
            // Continue receiving if server is still running
            if (server_running) {
              start_receive();
            }
          } else if (server_running) {
            std::cerr << "Server: Receive error: " << ec.message() << "\n";
          }
        });
  };
  
  start_receive();

  // Run server in background thread
  std::thread server_thread([&]() {
    std::cout << "Server: Starting on port 8080...\n";
    loop.run();
    std::cout << "Server: Stopped. Processed " << messages_processed << " messages.\n";
  });
  
  // Run client test
  test_client client;
  std::thread client_thread([&]() {
    client.run_test();
  });
  
  // Wait for client to complete
  client_thread.join();
  
  // Stop server
  server_running = false;
  loop.stop();
  
  // Give server time to process stop
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  
  // Join server thread
  server_thread.join();
  
  // Return based on test result
  return client.is_success() ? 0 : 1;
}

#ifndef SKIP_MAIN
int main() {
  int result = example_usage();
  if (result == 0) {
    std::cout << "\nTEST PASSED: Client-server handshake successful!\n";
  } else {
    std::cerr << "\nTEST FAILED: Client-server handshake failed!\n";
  }
  return result;
}
#endif // SKIP_MAIN