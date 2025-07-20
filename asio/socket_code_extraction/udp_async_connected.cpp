// UDP async sketch with connected sockets for testing
#include "udp_async_sketch.cpp"

#ifdef EXAMPLE_USAGE

// Override the example to use connected sockets
class connected_test_client {
  int fd_;
  std::atomic<bool> success_{false};
  std::atomic<bool> done_{false};
  
public:
  connected_test_client() : fd_(-1) {
    fd_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd_ < 0) {
      throw std::system_error(errno, std::generic_category(), "Failed to create client socket");
    }
    
    // Connect to server
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    server_addr.sin_port = htons(8081); // Different port for connected test
    
    if (connect(fd_, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
      throw std::system_error(errno, std::generic_category(), "Failed to connect");
    }
  }
  
  ~connected_test_client() {
    if (fd_ >= 0) {
#ifdef _WIN32
      closesocket(fd_);
#else
      close(fd_);
#endif
    }
  }
  
  bool run_test() {
    // Wait for server
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    const char* test_msg = "Hello, Connected UDP!";
    size_t msg_len = strlen(test_msg);
    
    // Send using connected socket
    ssize_t sent = send(fd_, test_msg, msg_len, 0);
    if (sent != static_cast<ssize_t>(msg_len)) {
      std::cerr << "Client: Failed to send\n";
      done_ = true;
      return false;
    }
    
    std::cout << "Client: Sent '" << test_msg << "'\n";
    
    // Set timeout
#ifdef _WIN32
    DWORD timeout = 2000;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, (char*)&timeout, sizeof(timeout));
#else
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(fd_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif
    
    // Receive echo
    char recv_buffer[1024];
    ssize_t received = recv(fd_, recv_buffer, sizeof(recv_buffer), 0);
    
    if (received == static_cast<ssize_t>(msg_len) && 
        memcmp(recv_buffer, test_msg, msg_len) == 0) {
      std::cout << "Client: Received correct echo!\n";
      success_ = true;
    } else {
      std::cerr << "Client: Echo mismatch or timeout\n";
    }
    
    done_ = true;
    return success_;
  }
  
  bool is_done() const { return done_; }
  bool is_success() const { return success_; }
};

int connected_example() {
#ifdef _WIN32
  iocp_event_loop loop;
#elif defined(__linux__)
  io_uring_event_loop loop;
#else
  kqueue_event_loop loop;
#endif

  auto server_socket = loop.create_udp_socket();
  endpoint bind_ep{0, 8081};
  server_socket->bind(bind_ep);
  
  // Get client endpoint for connection
  sockaddr_in client_addr{};
  socklen_t addr_len = sizeof(client_addr);
  
  // Server state
  std::atomic<bool> server_running{true};
  std::atomic<int> messages_processed{0};
  
  // Create connected client socket
  auto client_socket = loop.create_udp_socket();
  
  // Server receive loop
  std::byte recv_buffer[1024];
  std::function<void()> start_receive;
  
  start_receive = [&]() {
    server_socket->async_receive_from(
        recv_buffer,
        [&](std::error_code ec, size_t bytes_received, endpoint from) {
          if (!ec) {
            std::cout << "Server: Received " << bytes_received << " bytes\n";
            messages_processed++;
            
            // For connected test, accept first connection
            if (from.port != 0) {
              // Echo back using async_send_to
              server_socket->async_send_to({recv_buffer, bytes_received}, from,
                                    [&](std::error_code ec, size_t bytes_sent) {
                                      if (!ec) {
                                        std::cout << "Server: Sent " << bytes_sent << " bytes\n";
                                      }
                                    });
            }
            
            if (server_running) {
              start_receive();
            }
          }
        });
  };
  
  start_receive();
  
  // Run server in background
  std::thread server_thread([&]() {
    std::cout << "Server: Starting on port 8081...\n";
    loop.run();
    std::cout << "Server: Stopped\n";
  });
  
  // Run connected client test
  connected_test_client client;
  std::thread client_thread([&]() {
    client.run_test();
  });
  
  client_thread.join();
  server_running = false;
  loop.stop();
  
  std::this_thread::sleep_for(std::chrono::milliseconds(100));
  server_thread.join();
  
  return client.is_success() ? 0 : 1;
}

#undef main
int main() {
  // First run the simple blocking test
  std::cout << "=== Running simple blocking UDP test ===\n";
  system("./udp_simple_test");
  
  std::cout << "\n=== Running async UDP test ===\n";
  int result = example_usage();
  
  if (result == 0) {
    std::cout << "\nALL TESTS PASSED!\n";
  } else {
    std::cerr << "\nSOME TESTS FAILED!\n";
  }
  return result;
}

#endif