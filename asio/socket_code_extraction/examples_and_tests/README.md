# ASIO Socket Examples and Testing Framework

This directory contains comprehensive examples and testing strategies for ASIO socket programming, demonstrating practical usage patterns, testing methodologies, and performance characteristics.

## Directory Structure

```
examples_and_tests/
├── README.md                    # This file
├── basic_examples/             # Fundamental socket usage examples
│   ├── tcp_client_server.cpp   # Synchronous TCP client/server
│   └── udp_client_server.cpp   # UDP datagram examples
├── async_patterns/             # Asynchronous programming patterns
│   └── async_tcp_server.cpp    # Async server with multiple clients
├── testing_framework/          # Testing strategies and examples
│   └── socket_tests.cpp        # Comprehensive test suite
├── integration_examples/       # Real-world integration scenarios
│   └── http_client_server.cpp  # HTTP protocol implementation
└── performance_tests/          # Performance benchmarking
    └── socket_benchmarks.cpp   # Throughput and latency tests
```

## Testing Philosophy

Following the C++ project guidelines, our testing approach emphasizes:

### 1. Layered Testing Strategy
```
Layer 4: Integration Tests    ← HTTP client/server, real protocols
Layer 3: Component Tests      ← Socket + resolver + buffer interactions  
Layer 2: Unit Tests          ← Individual socket operations
Layer 1: Primitive Tests     ← Address creation, option setting
```

**Why this approach?**
- Build confidence by testing fundamentals first
- Each layer depends on the stability of layers below
- Failures are easier to diagnose and isolate
- Clear separation of concerns

### 2. Real Network Testing (No Mocking)
```cpp
// ✅ Good: Real network communication
asio::ip::tcp::socket client_socket(io_context);
client_socket.connect(server_endpoint);
asio::write(client_socket, asio::buffer("test"));

// ❌ Avoid: Mocking unless absolutely necessary
// MockSocket mock_socket;
// mock_socket.expect_write("test");
```

**Rationale:**
- Tests actual network behavior and edge cases
- Reveals platform-specific behaviors
- Catches real-world integration issues
- More confidence in production deployment

### 3. Error Condition Testing
```cpp
// Test connection failures
asio::ip::tcp::endpoint unreachable(address::from_string("127.0.0.1"), 1);
std::error_code ec;
socket.connect(unreachable, ec);
expect(ec != std::error_code{}); // Should fail

// Test buffer overflows, closed sockets, timeouts, etc.
```

## Testing Framework Usage

### boost-ut Integration
```cpp
#include <boost/ut.hpp>
using namespace boost::ut;

"TCP socket creation"_test = []() {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);
    
    expect(!socket.is_open());
    socket.open(asio::ip::tcp::v4());
    expect(socket.is_open());
};
```

### Test Organization Patterns
```cpp
// Group related tests
suite<"Socket Operations"> = []() {
    "creation"_test = []() { /* ... */ };
    "configuration"_test = []() { /* ... */ };
    "error_handling"_test = []() { /* ... */ };
};

// Use descriptive test names
"TCP client connects to server and exchanges messages"_test = []() {
    // Test implementation
};
```

## Performance Testing Strategy

### 1. Baseline Measurements
```cpp
// Measure fundamental operations
ankerl::nanobench::Bench().run("socket creation", []() {
    asio::io_context io_context;
    asio::ip::tcp::socket socket(io_context);
    socket.open(asio::ip::tcp::v4());
});
```

### 2. Throughput Testing
```cpp
// Test different message sizes
std::vector<size_t> sizes = {64, 256, 1024, 4096, 8192};
for (auto size : sizes) {
    bench.run("TCP throughput " + std::to_string(size) + "B", [=]() {
        // Send/receive test data of 'size' bytes
    });
}
```

### 3. Scalability Testing
```cpp
// Test with increasing connection counts
for (int connections = 1; connections <= 1000; connections *= 10) {
    bench.run(std::to_string(connections) + " connections", [=]() {
        // Create and manage 'connections' simultaneous connections
    });
}
```

## Example Categories

### 1. Basic Examples
**Purpose:** Learn fundamental socket operations
- Synchronous TCP client/server
- UDP datagram communication
- Error handling patterns
- Socket option configuration

**Key Learning Points:**
- RAII resource management
- Error code vs exception handling
- Socket lifecycle management
- Platform differences

### 2. Async Patterns
**Purpose:** Master asynchronous programming
- Async accept/connect operations
- Lifetime management with shared_ptr
- Callback chains and error propagation
- Thread safety considerations

**Key Learning Points:**
- `std::enable_shared_from_this` usage
- Capturing `self` in async lambdas
- Graceful shutdown procedures
- Memory management in async operations

### 3. Integration Examples
**Purpose:** Real-world protocol implementation
- HTTP client/server from scratch
- Request/response parsing
- Connection management
- Protocol layering concepts

**Key Learning Points:**
- Building protocols on top of sockets
- State machine design
- Buffer management strategies
- Component integration patterns

### 4. Performance Tests
**Purpose:** Understand performance characteristics
- Throughput benchmarking
- Latency measurements
- Memory allocation profiling
- Platform-specific optimizations

**Key Learning Points:**
- When to use sync vs async
- Buffer size impact on performance
- Connection pooling benefits
- Platform-specific tuning

## Testing Best Practices

### 1. Test Structure
```cpp
// Arrange
asio::io_context io_context;
test_server server(io_context, 0);
server.start();

// Act
test_client client(io_context);
auto result = client.send_message("test");

// Assert
expect(result == "test");

// Cleanup (RAII handles most)
server.stop();
```

### 2. Error Handling in Tests
```cpp
"connection error handling"_test = []() {
    try {
        // Test code that might throw
        socket.connect(invalid_endpoint);
        expect(false) << "Should have thrown";
    } catch (const std::system_error& e) {
        expect(e.code() == asio::error::connection_refused);
    }
};
```

### 3. Timeout Handling
```cpp
"async operation timeout"_test = []() {
    auto start = std::chrono::steady_clock::now();
    
    // Start async operation
    socket.async_connect(endpoint, [&](auto ec) {
        auto duration = std::chrono::steady_clock::now() - start;
        expect(duration < std::chrono::seconds(5));
    });
    
    // Run with timeout
    io_context.run_for(std::chrono::seconds(10));
};
```

### 4. Resource Cleanup
```cpp
class test_fixture {
    asio::io_context io_context_;
    std::unique_ptr<test_server> server_;
    
public:
    void setup() {
        server_ = std::make_unique<test_server>(io_context_, 0);
        server_->start();
    }
    
    void teardown() {
        if (server_) {
            server_->stop();
        }
        io_context_.stop();
    }
    
    ~test_fixture() { teardown(); } // RAII cleanup
};
```

## Common Testing Patterns

### 1. Echo Server Pattern
```cpp
// Simple echo server for testing client functionality
class echo_server {
    // Accept connections
    // Read data from client
    // Echo data back unchanged
    // Handle client disconnection
};
```

### 2. Mock Client Pattern
```cpp
// Simulate client behavior for server testing
class mock_client {
    // Connect to server
    // Send predefined test messages
    // Verify expected responses
    // Disconnect cleanly
};
```

### 3. State Machine Testing
```cpp
// Test protocol state transitions
enum class state { disconnected, connecting, connected, error };

"state transitions"_test = []() {
    protocol_handler handler;
    expect(handler.get_state() == state::disconnected);
    
    handler.connect();
    expect(handler.get_state() == state::connecting);
    
    handler.on_connected();
    expect(handler.get_state() == state::connected);
};
```

### 4. Stress Testing
```cpp
"high connection load"_test = []() {
    const int max_connections = 1000;
    std::vector<std::unique_ptr<test_client>> clients;
    
    for (int i = 0; i < max_connections; ++i) {
        auto client = std::make_unique<test_client>();
        expect(client->connect());
        clients.push_back(std::move(client));
    }
    
    // Verify all connections are stable
    for (auto& client : clients) {
        expect(client->is_connected());
    }
};
```

## Debugging and Troubleshooting

### 1. Logging Strategy
```cpp
// Use structured logging for debugging
#define LOG_DEBUG(msg) std::cout << "[DEBUG] " << msg << std::endl
#define LOG_ERROR(msg) std::cerr << "[ERROR] " << msg << std::endl

void handle_error(const std::error_code& ec) {
    LOG_ERROR("Socket error: " << ec.message() 
              << " (category: " << ec.category().name() << ")");
}
```

### 2. Network Debugging
```cpp
// Capture network state for debugging
void log_socket_state(const asio::ip::tcp::socket& socket) {
    if (socket.is_open()) {
        LOG_DEBUG("Local: " << socket.local_endpoint());
        LOG_DEBUG("Remote: " << socket.remote_endpoint());
        LOG_DEBUG("Available: " << socket.available());
    } else {
        LOG_DEBUG("Socket is closed");
    }
}
```

### 3. Platform-Specific Issues
```cpp
// Handle platform differences in tests
#ifdef _WIN32
    // Windows-specific test behavior
    expect(ec == asio::error::connection_aborted);
#else
    // Unix-like system behavior
    expect(ec == asio::error::connection_reset);
#endif
```

## Performance Optimization Testing

### 1. Buffer Size Impact
```cpp
"buffer size performance"_test = []() {
    std::vector<size_t> buffer_sizes = {1024, 4096, 8192, 16384, 65536};
    
    for (auto size : buffer_sizes) {
        auto throughput = measure_throughput_with_buffer_size(size);
        LOG_DEBUG("Buffer size " << size << ": " << throughput << " MB/s");
    }
};
```

### 2. Socket Option Tuning
```cpp
"socket option impact"_test = []() {
    // Test TCP_NODELAY impact
    auto latency_with_nagle = measure_latency(false);
    auto latency_without_nagle = measure_latency(true);
    
    expect(latency_without_nagle < latency_with_nagle);
};
```

### 3. Async vs Sync Performance
```cpp
"async vs sync comparison"_test = []() {
    auto sync_throughput = measure_sync_throughput();
    auto async_throughput = measure_async_throughput();
    
    // Async should scale better with multiple connections
    expect(async_throughput > sync_throughput);
};
```

## Continuous Integration Considerations

### 1. Test Reliability
- Use localhost for network tests to avoid external dependencies
- Implement retry logic for flaky network conditions
- Set appropriate timeouts for CI environments
- Mock only when external services are unavailable

### 2. Platform Testing
- Test on target deployment platforms (Linux, Windows, macOS)
- Verify behavior with different kernel versions
- Test both IPv4 and IPv6 where applicable
- Consider container environments (Docker)

### 3. Performance Regression Detection
- Establish performance baselines
- Set acceptable performance thresholds
- Track performance trends over time
- Alert on significant regressions

## Conclusion

This testing framework demonstrates industry best practices for socket programming:

1. **Comprehensive Coverage:** From primitives to integration
2. **Real-World Testing:** Using actual network operations
3. **Performance Awareness:** Understanding system characteristics
4. **Maintainable Code:** Clear structure and documentation
5. **Platform Agnostic:** Works across different systems

By following these patterns, you can build robust, well-tested network applications with confidence in their behavior under various conditions.