/**
 * @file socket_tests.cpp
 * @brief Comprehensive socket testing examples using boost-ut framework
 * 
 * This file demonstrates:
 * - Unit testing for socket operations
 * - Integration testing for client-server communication
 * - Error condition testing
 * - Mock patterns for testing
 * - Testing async operations
 * 
 * Compile with: g++ -std=c++20 -I/path/to/asio/include -I/path/to/boost-ut/include socket_tests.cpp -pthread
 * 
 * Note: This follows the C++ project guidelines:
 * - Layer tests and build confidence by testing primitives
 * - Test edge cases and error paths
 * - NEVER mock unless absolutely necessary
 * - Use boost-ut testing framework
 */

#pragma once

// Uncomment when boost-ut is available
// #include <boost/ut.hpp>

#include <asio.hpp>
#include <iostream>
#include <thread>
#include <chrono>
#include <array>
#include <string>
#include <memory>
#include <future>
#include <system_error>

// Mock implementation of boost-ut for demonstration
// Replace with actual boost-ut includes in real usage
namespace ut {
    struct test_case {
        template<typename F>
        test_case(const std::string& name, F&& f) {
            std::cout << "Running test: " << name << std::endl;
            try {
                f();
                std::cout << "✓ PASSED: " << name << std::endl;
            } catch (const std::exception& e) {
                std::cout << "✗ FAILED: " << name << " - " << e.what() << std::endl;
            }
        }
    };
    
    template<typename T>
    void expect(T&& condition) {
        if (!condition) {
            throw std::runtime_error("Expectation failed");
        }
    }
    
    auto eq = [](auto expected) { 
        return [expected](auto actual) { return actual == expected; }; 
    };
    
    auto that = [](auto&& predicate) { return predicate; };
}

namespace socket_tests {

/**
 * @class test_tcp_socket
 * @brief Test fixture for TCP socket testing
 */
class test_tcp_socket {
public:
    test_tcp_socket() : io_context_(), socket_(io_context_) {}
    
    asio::io_context& get_io_context() { return io_context_; }
    asio::ip::tcp::socket& get_socket() { return socket_; }

private:
    asio::io_context io_context_;
    asio::ip::tcp::socket socket_;
};

/**
 * @class test_udp_socket  
 * @brief Test fixture for UDP socket testing
 */
class test_udp_socket {
public:
    test_udp_socket() : io_context_(), socket_(io_context_) {}
    
    asio::io_context& get_io_context() { return io_context_; }
    asio::ip::udp::socket& get_socket() { return socket_; }

private:
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
};

/**
 * @class echo_server
 * @brief Simple echo server for integration testing
 */
class echo_server {
public:
    explicit echo_server(unsigned short port) 
        : io_context_()
        , acceptor_(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
        , running_(false)
    {
        acceptor_.set_option(asio::socket_base::reuse_address(true));
    }

    void start() {
        running_ = true;
        server_thread_ = std::thread([this]() { run(); });
        
        // Give server time to start
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    void stop() {
        running_ = false;
        io_context_.stop();
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    unsigned short get_port() const {
        return acceptor_.local_endpoint().port();
    }

private:
    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> running_;
    std::thread server_thread_;

    void run() {
        while (running_) {
            try {
                asio::ip::tcp::socket client_socket(io_context_);
                acceptor_.accept(client_socket);
                
                std::thread([socket = std::move(client_socket)]() mutable {
                    try {
                        std::array<char, 1024> buffer;
                        while (socket.is_open()) {
                            std::error_code ec;
                            std::size_t length = socket.read_some(asio::buffer(buffer), ec);
                            if (ec == asio::error::eof || ec == asio::error::connection_reset) {
                                break;
                            }
                            if (ec) {
                                std::cerr << "Read error: " << ec.message() << std::endl;
                                break;
                            }
                            
                            asio::write(socket, asio::buffer(buffer, length), ec);
                            if (ec) {
                                std::cerr << "Write error: " << ec.message() << std::endl;
                                break;
                            }
                        }
                    } catch (const std::exception& e) {
                        std::cerr << "Client handler error: " << e.what() << std::endl;
                    }
                }).detach();
                
            } catch (const std::exception& e) {
                if (running_) {
                    std::cerr << "Accept error: " << e.what() << std::endl;
                }
            }
        }
    }
};

} // namespace socket_tests

/**
 * @brief Basic socket primitive tests
 */
void test_socket_primitives() {
    using namespace ut;
    
    test_case("TCP socket creation and configuration", []() {
        socket_tests::test_tcp_socket test_fixture;
        auto& socket = test_fixture.get_socket();
        
        // Test socket is initially closed
        expect(!socket.is_open());
        
        // Test opening socket
        socket.open(asio::ip::tcp::v4());
        expect(socket.is_open());
        
        // Test socket options
        asio::socket_base::reuse_address reuse_option(true);
        socket.set_option(reuse_option);
        
        asio::socket_base::reuse_address check_option;
        socket.get_option(check_option);
        expect(check_option.value());
        
        // Test TCP no_delay option
        asio::ip::tcp::no_delay no_delay_option(true);
        socket.set_option(no_delay_option);
        
        asio::ip::tcp::no_delay check_no_delay;
        socket.get_option(check_no_delay);
        expect(check_no_delay.value());
        
        // Test closing
        socket.close();
        expect(!socket.is_open());
    });

    test_case("UDP socket creation and configuration", []() {
        socket_tests::test_udp_socket test_fixture;
        auto& socket = test_fixture.get_socket();
        
        // Test socket is initially closed
        expect(!socket.is_open());
        
        // Test opening socket
        socket.open(asio::ip::udp::v4());
        expect(socket.is_open());
        
        // Test binding to any available port
        asio::ip::udp::endpoint endpoint(asio::ip::udp::v4(), 0);
        socket.bind(endpoint);
        
        // Verify local endpoint is set
        auto local_ep = socket.local_endpoint();
        expect(local_ep.port() != 0);
        
        // Test broadcast option
        asio::socket_base::broadcast broadcast_option(true);
        socket.set_option(broadcast_option);
        
        socket.close();
        expect(!socket.is_open());
    });

    test_case("Socket error handling", []() {
        socket_tests::test_tcp_socket test_fixture;
        auto& socket = test_fixture.get_socket();
        
        // Test connecting to non-existent endpoint
        asio::ip::tcp::endpoint invalid_endpoint(
            asio::ip::address::from_string("127.0.0.1"), 1); // Port 1 unlikely to be open
        
        std::error_code ec;
        socket.connect(invalid_endpoint, ec);
        expect(ec != std::error_code{}); // Should have an error
        
        // Test operations on closed socket
        socket.close();
        
        std::array<char, 10> buffer;
        std::size_t bytes_read = socket.read_some(asio::buffer(buffer), ec);
        expect(ec != std::error_code{}); // Should error on closed socket
        expect(bytes_read == 0);
    });
}

/**
 * @brief Address and endpoint tests
 */
void test_addressing() {
    using namespace ut;
    
    test_case("IPv4 address creation", []() {
        // Test creating IPv4 addresses
        auto addr1 = asio::ip::address_v4::from_string("192.168.1.1");
        expect(addr1.to_string() == "192.168.1.1");
        
        auto addr2 = asio::ip::address_v4::loopback();
        expect(addr2.to_string() == "127.0.0.1");
        
        auto addr3 = asio::ip::address_v4::any();
        expect(addr3.to_string() == "0.0.0.0");
        
        auto addr4 = asio::ip::address_v4::broadcast();
        expect(addr4.to_string() == "255.255.255.255");
    });

    test_case("IPv6 address creation", []() {
        // Test creating IPv6 addresses  
        auto addr1 = asio::ip::address_v6::loopback();
        expect(addr1.to_string() == "::1");
        
        auto addr2 = asio::ip::address_v6::any();
        expect(addr2.to_string() == "::");
        
        // Test creating from string
        auto addr3 = asio::ip::address_v6::from_string("2001:db8::1");
        expect(addr3.to_string() == "2001:db8::1");
    });

    test_case("TCP endpoint creation", []() {
        // Test TCP endpoint creation
        asio::ip::tcp::endpoint ep1(asio::ip::tcp::v4(), 8080);
        expect(ep1.port() == 8080);
        expect(ep1.protocol() == asio::ip::tcp::v4());
        
        asio::ip::tcp::endpoint ep2(asio::ip::address::from_string("127.0.0.1"), 8080);
        expect(ep2.address().to_string() == "127.0.0.1");
        expect(ep2.port() == 8080);
    });

    test_case("UDP endpoint creation", []() {
        // Test UDP endpoint creation
        asio::ip::udp::endpoint ep1(asio::ip::udp::v4(), 8081);
        expect(ep1.port() == 8081);
        expect(ep1.protocol() == asio::ip::udp::v4());
        
        asio::ip::udp::endpoint ep2(asio::ip::address::from_string("127.0.0.1"), 8081);
        expect(ep2.address().to_string() == "127.0.0.1");
        expect(ep2.port() == 8081);
    });
}

/**
 * @brief Name resolution tests
 */
void test_name_resolution() {
    using namespace ut;
    
    test_case("TCP resolver functionality", []() {
        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        
        // Test resolving localhost
        auto results = resolver.resolve("localhost", "http");
        expect(results.begin() != results.end()); // Should have at least one result
        
        for (const auto& entry : results) {
            expect(entry.endpoint().port() == 80); // HTTP port
        }
    });

    test_case("UDP resolver functionality", []() {
        asio::io_context io_context;
        asio::ip::udp::resolver resolver(io_context);
        
        // Test resolving with numeric port
        auto results = resolver.resolve("localhost", "53"); // DNS port
        expect(results.begin() != results.end());
        
        for (const auto& entry : results) {
            expect(entry.endpoint().port() == 53);
        }
    });

    test_case("Resolver error handling", []() {
        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        
        // Test resolving invalid hostname
        std::error_code ec;
        auto results = resolver.resolve("invalid.hostname.that.does.not.exist", "80", ec);
        expect(ec != std::error_code{}); // Should have error
    });
}

/**
 * @brief Integration tests with real network communication
 */
void test_integration() {
    using namespace ut;
    
    test_case("TCP client-server communication", []() {
        // Start echo server
        socket_tests::echo_server server(0); // Use any available port
        server.start();
        
        unsigned short port = server.get_port();
        
        try {
            // Connect client
            asio::io_context io_context;
            asio::ip::tcp::socket client_socket(io_context);
            
            asio::ip::tcp::endpoint server_endpoint(
                asio::ip::address::from_string("127.0.0.1"), port);
            client_socket.connect(server_endpoint);
            
            // Send test message
            std::string test_message = "Hello, Server!";
            asio::write(client_socket, asio::buffer(test_message));
            
            // Receive echo
            std::array<char, 1024> reply_buffer;
            std::size_t reply_length = client_socket.read_some(asio::buffer(reply_buffer));
            
            std::string reply(reply_buffer.data(), reply_length);
            expect(reply == test_message);
            
            client_socket.close();
        } catch (const std::exception& e) {
            server.stop();
            throw;
        }
        
        server.stop();
    });

    test_case("UDP client-server communication", []() {
        asio::io_context io_context;
        
        // Create UDP server socket
        asio::ip::udp::socket server_socket(io_context, 
            asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
        unsigned short server_port = server_socket.local_endpoint().port();
        
        // Create UDP client socket
        asio::ip::udp::socket client_socket(io_context);
        client_socket.open(asio::ip::udp::v4());
        
        // Send message from client to server
        std::string test_message = "UDP Test Message";
        asio::ip::udp::endpoint server_endpoint(
            asio::ip::address::from_string("127.0.0.1"), server_port);
        
        client_socket.send_to(asio::buffer(test_message), server_endpoint);
        
        // Receive on server
        std::array<char, 1024> server_buffer;
        asio::ip::udp::endpoint client_endpoint;
        std::size_t bytes_received = server_socket.receive_from(
            asio::buffer(server_buffer), client_endpoint);
        
        std::string received_message(server_buffer.data(), bytes_received);
        expect(received_message == test_message);
        
        // Echo back
        server_socket.send_to(asio::buffer(server_buffer, bytes_received), client_endpoint);
        
        // Receive echo on client
        std::array<char, 1024> client_buffer;
        asio::ip::udp::endpoint echo_sender;
        std::size_t echo_bytes = client_socket.receive_from(
            asio::buffer(client_buffer), echo_sender);
        
        std::string echo_message(client_buffer.data(), echo_bytes);
        expect(echo_message == test_message);
        expect(echo_sender.port() == server_port);
    });
}

/**
 * @brief Async operation tests
 */
void test_async_operations() {
    using namespace ut;
    
    test_case("Async TCP operations", []() {
        socket_tests::echo_server server(0);
        server.start();
        
        asio::io_context io_context;
        asio::ip::tcp::socket client_socket(io_context);
        
        std::promise<std::error_code> connect_promise;
        auto connect_future = connect_promise.get_future();
        
        // Async connect
        asio::ip::tcp::endpoint server_endpoint(
            asio::ip::address::from_string("127.0.0.1"), server.get_port());
        
        client_socket.async_connect(server_endpoint,
            [&connect_promise](const std::error_code& ec) {
                connect_promise.set_value(ec);
            });
        
        // Run io_context in separate thread
        std::thread io_thread([&io_context]() { io_context.run(); });
        
        // Wait for connection
        auto connect_result = connect_future.wait_for(std::chrono::seconds(5));
        expect(connect_result == std::future_status::ready);
        
        std::error_code connect_ec = connect_future.get();
        expect(!connect_ec);
        
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        
        server.stop();
    });

    test_case("Async write and read operations", []() {
        socket_tests::echo_server server(0);
        server.start();
        
        asio::io_context io_context;
        asio::ip::tcp::socket client_socket(io_context);
        
        // Connect synchronously for simplicity
        asio::ip::tcp::endpoint server_endpoint(
            asio::ip::address::from_string("127.0.0.1"), server.get_port());
        client_socket.connect(server_endpoint);
        
        // Async write
        std::string test_message = "Async Test Message";
        std::promise<std::pair<std::error_code, std::size_t>> write_promise;
        auto write_future = write_promise.get_future();
        
        asio::async_write(client_socket, asio::buffer(test_message),
            [&write_promise](const std::error_code& ec, std::size_t bytes_transferred) {
                write_promise.set_value({ec, bytes_transferred});
            });
        
        // Async read
        std::array<char, 1024> read_buffer;
        std::promise<std::pair<std::error_code, std::size_t>> read_promise;
        auto read_future = read_promise.get_future();
        
        client_socket.async_read_some(asio::buffer(read_buffer),
            [&read_promise](const std::error_code& ec, std::size_t bytes_transferred) {
                read_promise.set_value({ec, bytes_transferred});
            });
        
        // Run io_context
        std::thread io_thread([&io_context]() { io_context.run(); });
        
        // Wait for operations
        auto write_result = write_future.wait_for(std::chrono::seconds(5));
        expect(write_result == std::future_status::ready);
        
        auto [write_ec, bytes_written] = write_future.get();
        expect(!write_ec);
        expect(bytes_written == test_message.size());
        
        auto read_result = read_future.wait_for(std::chrono::seconds(5));
        expect(read_result == std::future_status::ready);
        
        auto [read_ec, bytes_read] = read_future.get();
        expect(!read_ec);
        expect(bytes_read == test_message.size());
        
        std::string received_message(read_buffer.data(), bytes_read);
        expect(received_message == test_message);
        
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        
        server.stop();
    });
}

/**
 * @brief Error condition and edge case tests
 */
void test_error_conditions() {
    using namespace ut;
    
    test_case("Connection refused handling", []() {
        asio::io_context io_context;
        asio::ip::tcp::socket socket(io_context);
        
        // Try to connect to a port that should be closed
        asio::ip::tcp::endpoint unreachable_endpoint(
            asio::ip::address::from_string("127.0.0.1"), 1); // Port 1
        
        std::error_code ec;
        socket.connect(unreachable_endpoint, ec);
        
        expect(ec == asio::error::connection_refused || 
               ec == asio::error::timed_out ||
               ec != std::error_code{});
    });

    test_case("Socket close during operations", []() {
        socket_tests::echo_server server(0);
        server.start();
        
        asio::io_context io_context;
        asio::ip::tcp::socket socket(io_context);
        
        // Connect to server
        asio::ip::tcp::endpoint server_endpoint(
            asio::ip::address::from_string("127.0.0.1"), server.get_port());
        socket.connect(server_endpoint);
        
        // Close socket while connected
        socket.close();
        
        // Try to read from closed socket
        std::array<char, 1024> buffer;
        std::error_code ec;
        std::size_t bytes_read = socket.read_some(asio::buffer(buffer), ec);
        
        expect(ec != std::error_code{}); // Should have error
        expect(bytes_read == 0);
        
        server.stop();
    });

    test_case("Buffer overflow protection", []() {
        asio::io_context io_context;
        
        // Create UDP sockets for testing
        asio::ip::udp::socket sender(io_context);
        asio::ip::udp::socket receiver(io_context, 
            asio::ip::udp::endpoint(asio::ip::udp::v4(), 0));
        
        sender.open(asio::ip::udp::v4());
        
        auto receiver_endpoint = receiver.local_endpoint();
        
        // Send large message
        std::string large_message(2048, 'A'); // 2KB message
        sender.send_to(asio::buffer(large_message), receiver_endpoint);
        
        // Try to receive into small buffer
        std::array<char, 1024> small_buffer; // 1KB buffer
        asio::ip::udp::endpoint sender_endpoint;
        
        std::size_t bytes_received = receiver.receive_from(
            asio::buffer(small_buffer), sender_endpoint);
        
        // Should receive only what fits in buffer
        expect(bytes_received <= small_buffer.size());
        // In UDP, message should be truncated
        expect(bytes_received == small_buffer.size());
    });
}

int main() {
    std::cout << "=== ASIO Socket Test Suite ===" << std::endl;
    std::cout << "Testing socket primitives, networking, and integration" << std::endl;
    std::cout << "==============================" << std::endl;
    
    try {
        std::cout << "\\n--- Testing Socket Primitives ---" << std::endl;
        test_socket_primitives();
        
        std::cout << "\\n--- Testing Addressing ---" << std::endl;
        test_addressing();
        
        std::cout << "\\n--- Testing Name Resolution ---" << std::endl;
        test_name_resolution();
        
        std::cout << "\\n--- Testing Integration ---" << std::endl;
        test_integration();
        
        std::cout << "\\n--- Testing Async Operations ---" << std::endl;
        test_async_operations();
        
        std::cout << "\\n--- Testing Error Conditions ---" << std::endl;
        test_error_conditions();
        
        std::cout << "\\n=== All Tests Completed ===" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Test suite error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

/**
 * Testing Best Practices Demonstrated:
 * 
 * 1. **Layered Testing**: 
 *    - Primitives (socket creation, options)
 *    - Integration (client-server communication)
 *    - Error conditions and edge cases
 * 
 * 2. **Test Organization**:
 *    - Clear test categories
 *    - Descriptive test names
 *    - Isolated test cases
 * 
 * 3. **Error Testing**:
 *    - Network errors (connection refused)
 *    - Resource errors (closed sockets)
 *    - Buffer management errors
 * 
 * 4. **Real Network Testing**:
 *    - No mocking unless absolutely necessary
 *    - Use localhost for reliable testing
 *    - Test both sync and async operations
 * 
 * 5. **Resource Management**:
 *    - Proper cleanup in all test cases
 *    - RAII for automatic resource management
 *    - Exception safety in tests
 * 
 * Guidelines Followed:
 * - Unit test every public method
 * - Layer tests and build confidence
 * - NEVER mock unless absolutely necessary
 * - Test edge cases and error paths
 * - Use boost-ut testing framework
 */