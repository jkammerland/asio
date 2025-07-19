/**
 * @file socket_benchmarks.cpp
 * @brief Performance benchmarks for ASIO socket operations using nanobench
 * 
 * This file demonstrates:
 * - Throughput benchmarking for TCP and UDP
 * - Latency measurements for various operations
 * - Comparison between sync and async operations
 * - Platform-specific performance characteristics
 * - Scalability testing patterns
 * 
 * Compile with: g++ -std=c++20 -O3 -I/path/to/asio/include -I/path/to/nanobench/include socket_benchmarks.cpp -pthread
 */

#pragma once

// Uncomment when nanobench is available
// #include <nanobench.h>

#include <asio.hpp>
#include <iostream>
#include <chrono>
#include <thread>
#include <vector>
#include <array>
#include <string>
#include <atomic>
#include <memory>
#include <random>

// Mock implementation of nanobench for demonstration
// Replace with actual nanobench includes in real usage
namespace ankerl::nanobench {
    class Bench {
    public:
        Bench& title(const std::string& title) {
            title_ = title;
            std::cout << "\\n=== " << title_ << " ===" << std::endl;
            return *this;
        }
        
        template<typename Fn>
        Bench& run(const std::string& name, Fn&& fn) {
            std::cout << "Running: " << name << "... ";
            
            const int iterations = 1000;
            auto start = std::chrono::high_resolution_clock::now();
            
            for (int i = 0; i < iterations; ++i) {
                fn();
            }
            
            auto end = std::chrono::high_resolution_clock::now();
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end - start);
            double avg_time = static_cast<double>(duration.count()) / iterations;
            
            std::cout << "avg: " << avg_time << "μs" << std::endl;
            return *this;
        }
        
    private:
        std::string title_;
    };
}

namespace performance_tests {

/**
 * @class benchmark_server
 * @brief High-performance server for benchmarking
 */
class benchmark_server {
public:
    explicit benchmark_server(unsigned short port, bool use_async = true)
        : io_context_()
        , acceptor_(io_context_, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
        , running_(false)
        , use_async_(use_async)
        , bytes_transferred_(0)
        , connections_count_(0)
    {
        acceptor_.set_option(asio::socket_base::reuse_address(true));
    }

    void start() {
        running_ = true;
        
        if (use_async_) {
            start_accept();
            server_thread_ = std::thread([this]() { io_context_.run(); });
        } else {
            server_thread_ = std::thread([this]() { run_sync(); });
        }
        
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

    std::uint64_t get_bytes_transferred() const { return bytes_transferred_; }
    std::uint32_t get_connections_count() const { return connections_count_; }

private:
    asio::io_context io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> running_;
    bool use_async_;
    std::thread server_thread_;
    std::atomic<std::uint64_t> bytes_transferred_;
    std::atomic<std::uint32_t> connections_count_;

    void start_accept() {
        if (!running_) return;
        
        auto new_connection = std::make_shared<asio::ip::tcp::socket>(io_context_);
        
        acceptor_.async_accept(*new_connection,
            [this, new_connection](const std::error_code& error) {
                if (!error) {
                    ++connections_count_;
                    handle_connection_async(new_connection);
                }
                start_accept(); // Continue accepting
            });
    }

    void handle_connection_async(std::shared_ptr<asio::ip::tcp::socket> socket) {
        auto buffer = std::make_shared<std::array<char, 8192>>();
        
        socket->async_read_some(asio::buffer(*buffer),
            [this, socket, buffer](const std::error_code& error, std::size_t bytes_read) {
                if (!error && bytes_read > 0) {
                    bytes_transferred_ += bytes_read;
                    
                    // Echo back
                    asio::async_write(*socket, asio::buffer(*buffer, bytes_read),
                        [this, socket, buffer](const std::error_code& error, std::size_t bytes_written) {
                            if (!error) {
                                bytes_transferred_ += bytes_written;
                                handle_connection_async(socket); // Continue reading
                            }
                        });
                }
            });
    }

    void run_sync() {
        while (running_) {
            try {
                asio::ip::tcp::socket client_socket(io_context_);
                acceptor_.accept(client_socket);
                ++connections_count_;
                
                std::thread([this, socket = std::move(client_socket)]() mutable {
                    std::array<char, 8192> buffer;
                    try {
                        while (socket.is_open() && running_) {
                            std::error_code ec;
                            std::size_t bytes_read = socket.read_some(asio::buffer(buffer), ec);
                            if (ec || bytes_read == 0) break;
                            
                            bytes_transferred_ += bytes_read;
                            
                            asio::write(socket, asio::buffer(buffer, bytes_read), ec);
                            if (ec) break;
                            
                            bytes_transferred_ += bytes_read;
                        }
                    } catch (const std::exception&) {
                        // Connection closed
                    }
                }).detach();
                
            } catch (const std::exception&) {
                if (running_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
    }
};

/**
 * @class udp_benchmark_server
 * @brief UDP server for benchmarking datagram performance
 */
class udp_benchmark_server {
public:
    explicit udp_benchmark_server(unsigned short port)
        : io_context_()
        , socket_(io_context_, asio::ip::udp::endpoint(asio::ip::udp::v4(), port))
        , running_(false)
        , packets_received_(0)
        , bytes_transferred_(0)
    {
        socket_.set_option(asio::socket_base::reuse_address(true));
    }

    void start() {
        running_ = true;
        server_thread_ = std::thread([this]() { run(); });
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    void stop() {
        running_ = false;
        if (server_thread_.joinable()) {
            server_thread_.join();
        }
    }

    unsigned short get_port() const {
        return socket_.local_endpoint().port();
    }

    std::uint64_t get_packets_received() const { return packets_received_; }
    std::uint64_t get_bytes_transferred() const { return bytes_transferred_; }

private:
    asio::io_context io_context_;
    asio::ip::udp::socket socket_;
    std::atomic<bool> running_;
    std::thread server_thread_;
    std::atomic<std::uint64_t> packets_received_;
    std::atomic<std::uint64_t> bytes_transferred_;

    void run() {
        std::array<char, 8192> buffer;
        
        while (running_) {
            try {
                asio::ip::udp::endpoint sender_endpoint;
                std::size_t bytes_received = socket_.receive_from(
                    asio::buffer(buffer), sender_endpoint);
                
                ++packets_received_;
                bytes_transferred_ += bytes_received;
                
                // Echo back
                socket_.send_to(asio::buffer(buffer, bytes_received), sender_endpoint);
                bytes_transferred_ += bytes_received;
                
            } catch (const std::exception&) {
                if (running_) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
            }
        }
    }
};

} // namespace performance_tests

/**
 * @brief Benchmark basic socket operations
 */
void benchmark_socket_operations() {
    ankerl::nanobench::Bench().title("Socket Creation and Configuration");
    
    ankerl::nanobench::Bench().run("TCP socket creation", []() {
        asio::io_context io_context;
        asio::ip::tcp::socket socket(io_context);
        socket.open(asio::ip::tcp::v4());
        socket.close();
    });

    ankerl::nanobench::Bench().run("UDP socket creation", []() {
        asio::io_context io_context;
        asio::ip::udp::socket socket(io_context);
        socket.open(asio::ip::udp::v4());
        socket.close();
    });

    ankerl::nanobench::Bench().run("Socket option setting", []() {
        asio::io_context io_context;
        asio::ip::tcp::socket socket(io_context);
        socket.open(asio::ip::tcp::v4());
        
        asio::socket_base::reuse_address reuse_option(true);
        socket.set_option(reuse_option);
        
        asio::ip::tcp::no_delay no_delay_option(true);
        socket.set_option(no_delay_option);
        
        socket.close();
    });

    ankerl::nanobench::Bench().run("Endpoint creation", []() {
        asio::ip::tcp::endpoint endpoint(asio::ip::address::from_string("127.0.0.1"), 8080);
        (void)endpoint.address();
        (void)endpoint.port();
    });
}

/**
 * @brief Benchmark name resolution performance
 */
void benchmark_name_resolution() {
    ankerl::nanobench::Bench().title("Name Resolution Performance");
    
    ankerl::nanobench::Bench().run("Resolve localhost TCP", []() {
        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        auto results = resolver.resolve("localhost", "80");
        (void)results.begin();
    });

    ankerl::nanobench::Bench().run("Resolve localhost UDP", []() {
        asio::io_context io_context;
        asio::ip::udp::resolver resolver(io_context);
        auto results = resolver.resolve("localhost", "53");
        (void)results.begin();
    });

    ankerl::nanobench::Bench().run("Address from string IPv4", []() {
        auto addr = asio::ip::address_v4::from_string("192.168.1.1");
        (void)addr.to_string();
    });

    ankerl::nanobench::Bench().run("Address from string IPv6", []() {
        auto addr = asio::ip::address_v6::from_string("::1");
        (void)addr.to_string();
    });
}

/**
 * @brief Benchmark TCP throughput
 */
void benchmark_tcp_throughput() {
    ankerl::nanobench::Bench().title("TCP Throughput Benchmarks");
    
    // Test different message sizes
    std::vector<std::size_t> message_sizes = {64, 256, 1024, 4096, 8192};
    
    for (auto size : message_sizes) {
        std::string test_name = "TCP throughput " + std::to_string(size) + " bytes";
        
        ankerl::nanobench::Bench().run(test_name, [size]() {
            performance_tests::benchmark_server server(0, false); // Use sync for consistency
            server.start();
            
            try {
                asio::io_context io_context;
                asio::ip::tcp::socket client_socket(io_context);
                
                asio::ip::tcp::endpoint server_endpoint(
                    asio::ip::address::from_string("127.0.0.1"), server.get_port());
                client_socket.connect(server_endpoint);
                
                // Prepare test data
                std::string test_data(size, 'A');
                
                // Send and receive
                asio::write(client_socket, asio::buffer(test_data));
                
                std::vector<char> response(size);
                asio::read(client_socket, asio::buffer(response));
                
                client_socket.close();
            } catch (const std::exception&) {
                // Ignore errors for benchmarking
            }
            
            server.stop();
        });
    }
}

/**
 * @brief Benchmark UDP performance
 */
void benchmark_udp_performance() {
    ankerl::nanobench::Bench().title("UDP Performance Benchmarks");
    
    std::vector<std::size_t> packet_sizes = {64, 256, 512, 1024, 1472}; // 1472 = typical MTU limit
    
    for (auto size : packet_sizes) {
        std::string test_name = "UDP packet " + std::to_string(size) + " bytes";
        
        ankerl::nanobench::Bench().run(test_name, [size]() {
            performance_tests::udp_benchmark_server server(0);
            server.start();
            
            try {
                asio::io_context io_context;
                asio::ip::udp::socket client_socket(io_context);
                client_socket.open(asio::ip::udp::v4());
                
                asio::ip::udp::endpoint server_endpoint(
                    asio::ip::address::from_string("127.0.0.1"), server.get_port());
                
                // Prepare test data
                std::string test_data(size, 'B');
                
                // Send and receive
                client_socket.send_to(asio::buffer(test_data), server_endpoint);
                
                std::vector<char> response(size);
                asio::ip::udp::endpoint sender_endpoint;
                client_socket.receive_from(asio::buffer(response), sender_endpoint);
                
            } catch (const std::exception&) {
                // Ignore errors for benchmarking
            }
            
            server.stop();
        });
    }
}

/**
 * @brief Benchmark async vs sync operations
 */
void benchmark_async_vs_sync() {
    ankerl::nanobench::Bench().title("Async vs Sync Operation Comparison");
    
    // Sync TCP operation
    ankerl::nanobench::Bench().run("Sync TCP operation", []() {
        performance_tests::benchmark_server server(0, false);
        server.start();
        
        try {
            asio::io_context io_context;
            asio::ip::tcp::socket client_socket(io_context);
            
            asio::ip::tcp::endpoint server_endpoint(
                asio::ip::address::from_string("127.0.0.1"), server.get_port());
            client_socket.connect(server_endpoint);
            
            std::string test_data(1024, 'S');
            asio::write(client_socket, asio::buffer(test_data));
            
            std::vector<char> response(1024);
            asio::read(client_socket, asio::buffer(response));
            
        } catch (const std::exception&) {}
        
        server.stop();
    });

    // Async TCP operation
    ankerl::nanobench::Bench().run("Async TCP operation", []() {
        performance_tests::benchmark_server server(0, true);
        server.start();
        
        try {
            asio::io_context io_context;
            asio::ip::tcp::socket client_socket(io_context);
            
            asio::ip::tcp::endpoint server_endpoint(
                asio::ip::address::from_string("127.0.0.1"), server.get_port());
            
            std::promise<void> done_promise;
            auto done_future = done_promise.get_future();
            
            client_socket.async_connect(server_endpoint,
                [&](const std::error_code& ec) {
                    if (!ec) {
                        auto test_data = std::make_shared<std::string>(1024, 'A');
                        asio::async_write(client_socket, asio::buffer(*test_data),
                            [&, test_data](const std::error_code& ec, std::size_t) {
                                if (!ec) {
                                    auto response = std::make_shared<std::vector<char>>(1024);
                                    asio::async_read(client_socket, asio::buffer(*response),
                                        [&, response](const std::error_code& ec, std::size_t) {
                                            done_promise.set_value();
                                        });
                                } else {
                                    done_promise.set_value();
                                }
                            });
                    } else {
                        done_promise.set_value();
                    }
                });
            
            std::thread io_thread([&io_context]() { io_context.run(); });
            
            done_future.wait_for(std::chrono::seconds(1));
            
            io_context.stop();
            if (io_thread.joinable()) {
                io_thread.join();
            }
            
        } catch (const std::exception&) {}
        
        server.stop();
    });
}

/**
 * @brief Benchmark connection establishment overhead
 */
void benchmark_connection_overhead() {
    ankerl::nanobench::Bench().title("Connection Establishment Overhead");
    
    ankerl::nanobench::Bench().run("TCP connection establishment", []() {
        performance_tests::benchmark_server server(0, false);
        server.start();
        
        try {
            asio::io_context io_context;
            asio::ip::tcp::socket client_socket(io_context);
            
            asio::ip::tcp::endpoint server_endpoint(
                asio::ip::address::from_string("127.0.0.1"), server.get_port());
            
            client_socket.connect(server_endpoint);
            client_socket.close();
            
        } catch (const std::exception&) {}
        
        server.stop();
    });

    ankerl::nanobench::Bench().run("UDP \"connection\" (first packet)", []() {
        performance_tests::udp_benchmark_server server(0);
        server.start();
        
        try {
            asio::io_context io_context;
            asio::ip::udp::socket client_socket(io_context);
            client_socket.open(asio::ip::udp::v4());
            
            asio::ip::udp::endpoint server_endpoint(
                asio::ip::address::from_string("127.0.0.1"), server.get_port());
            
            std::string test_data(64, 'U');
            client_socket.send_to(asio::buffer(test_data), server_endpoint);
            
            std::vector<char> response(64);
            asio::ip::udp::endpoint sender_endpoint;
            client_socket.receive_from(asio::buffer(response), sender_endpoint);
            
        } catch (const std::exception&) {}
        
        server.stop();
    });
}

/**
 * @brief Memory allocation benchmarks
 */
void benchmark_memory_operations() {
    ankerl::nanobench::Bench().title("Memory and Buffer Operations");
    
    ankerl::nanobench::Bench().run("Buffer creation (std::array)", []() {
        std::array<char, 8192> buffer;
        asio::buffer(buffer);
    });

    ankerl::nanobench::Bench().run("Buffer creation (std::vector)", []() {
        std::vector<char> buffer(8192);
        asio::buffer(buffer);
    });

    ankerl::nanobench::Bench().run("Dynamic buffer allocation", []() {
        asio::dynamic_buffer(std::vector<char>{});
    });

    ankerl::nanobench::Bench().run("Mutable buffer sequence", []() {
        std::array<char, 4096> buffer1;
        std::array<char, 4096> buffer2;
        std::array<asio::mutable_buffer, 2> buffers = {{
            asio::buffer(buffer1),
            asio::buffer(buffer2)
        }};
        (void)buffers;
    });
}

int main() {
    std::cout << "=== ASIO Socket Performance Benchmarks ===" << std::endl;
    std::cout << "Measuring socket operation performance characteristics" << std::endl;
    std::cout << "=============================================" << std::endl;
    
    try {
        benchmark_socket_operations();
        benchmark_name_resolution();
        benchmark_tcp_throughput();
        benchmark_udp_performance();
        benchmark_async_vs_sync();
        benchmark_connection_overhead();
        benchmark_memory_operations();
        
        std::cout << "\\n=== Benchmark Results Summary ===" << std::endl;
        std::cout << "✓ Socket operations benchmarked" << std::endl;
        std::cout << "✓ Name resolution performance measured" << std::endl;
        std::cout << "✓ TCP/UDP throughput characterized" << std::endl;
        std::cout << "✓ Async vs sync performance compared" << std::endl;
        std::cout << "✓ Connection overhead quantified" << std::endl;
        std::cout << "✓ Memory operation costs measured" << std::endl;
        
    } catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}

/**
 * Performance Testing Guidelines:
 * 
 * 1. **Compiler Optimization**: 
 *    - Always use -O3 for release builds
 *    - Consider -march=native for target-specific optimizations
 *    - Profile with optimizations enabled
 * 
 * 2. **Measurement Considerations**:
 *    - Run multiple iterations for statistical significance
 *    - Warm up the code path before measuring
 *    - Consider system load and other processes
 *    - Test on target hardware and OS
 * 
 * 3. **Network Testing**:
 *    - Use localhost for consistent results
 *    - Test different message sizes
 *    - Consider network buffer sizes
 *    - Test both IPv4 and IPv6 if relevant
 * 
 * 4. **Platform Variations**:
 *    - Linux: io_uring vs epoll performance
 *    - Windows: IOCP behavior differences
 *    - macOS: kqueue characteristics
 * 
 * 5. **Memory Profiling**:
 *    - Monitor allocation patterns
 *    - Test buffer reuse strategies
 *    - Profile async operation memory usage
 * 
 * Expected Performance Characteristics:
 * - TCP: Higher latency, reliable delivery
 * - UDP: Lower latency, potential packet loss
 * - Async: Better scalability, higher complexity
 * - Sync: Lower latency for single operations
 * 
 * Optimization Opportunities:
 * - Buffer reuse and pooling
 * - Socket option tuning
 * - Batch operations where possible
 * - Platform-specific optimizations
 */