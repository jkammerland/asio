/**
 * @file async_tcp_server.cpp
 * @brief Comprehensive async TCP server demonstrating modern ASIO patterns
 * 
 * This example demonstrates:
 * - Asynchronous TCP server with multiple client support
 * - Proper lifetime management with shared_ptr
 * - Error handling in async operations
 * - Graceful shutdown procedures
 * - Modern C++ features (lambdas, auto, smart pointers)
 * 
 * Compile with: g++ -std=c++20 -I/path/to/asio/include async_tcp_server.cpp -pthread
 */

#pragma once

#include <asio.hpp>
#include <iostream>
#include <memory>
#include <array>
#include <string>
#include <set>
#include <mutex>
#include <atomic>

namespace async_examples {

/**
 * @class connection
 * @brief Represents a single client connection
 * 
 * Demonstrates:
 * - Async read/write operations
 * - Proper lifetime management with shared_from_this
 * - Error handling in async callbacks
 * - Clean disconnection handling
 */
class connection : public std::enable_shared_from_this<connection> {
public:
    using pointer = std::shared_ptr<connection>;

    /**
     * @brief Create a new connection instance
     */
    static pointer create(asio::io_context& io_context) {
        return pointer(new connection(io_context));
    }

    /**
     * @brief Get the socket for this connection
     */
    asio::ip::tcp::socket& socket() {
        return socket_;
    }

    /**
     * @brief Start handling this connection
     */
    void start() {
        auto remote_endpoint = socket_.remote_endpoint();
        std::cout << "New connection from: " << remote_endpoint << std::endl;
        
        // Start the read chain
        start_read();
    }

    /**
     * @brief Send data to client asynchronously
     * @param data Data to send
     */
    void send_data(const std::string& data) {
        auto self = shared_from_this();
        
        asio::async_write(socket_, asio::buffer(data),
            [this, self](const std::error_code& error, std::size_t bytes_transferred) {
                if (error) {
                    std::cerr << "Write error: " << error.message() << std::endl;
                    handle_disconnect();
                } else {
                    std::cout << "Sent " << bytes_transferred << " bytes" << std::endl;
                }
            });
    }

    /**
     * @brief Close connection gracefully
     */
    void close() {
        std::error_code ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    asio::ip::tcp::socket socket_;
    std::array<char, 1024> buffer_;

    /**
     * @brief Private constructor (use create() instead)
     */
    explicit connection(asio::io_context& io_context) : socket_(io_context) {}

    /**
     * @brief Start async read operation
     */
    void start_read() {
        auto self = shared_from_this();
        
        socket_.async_read_some(asio::buffer(buffer_),
            [this, self](const std::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    handle_read(bytes_transferred);
                } else if (error == asio::error::eof) {
                    std::cout << "Client disconnected gracefully" << std::endl;
                    handle_disconnect();
                } else {
                    std::cerr << "Read error: " << error.message() << std::endl;
                    handle_disconnect();
                }
            });
    }

    /**
     * @brief Handle received data
     * @param bytes_transferred Number of bytes received
     */
    void handle_read(std::size_t bytes_transferred) {
        // Echo the data back
        std::string_view message(buffer_.data(), bytes_transferred);
        std::cout << "Received: " << message;
        
        // Echo back asynchronously
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(buffer_, bytes_transferred),
            [this, self](const std::error_code& error, std::size_t bytes_sent) {
                if (!error) {
                    // Continue reading
                    start_read();
                } else {
                    std::cerr << "Echo write error: " << error.message() << std::endl;
                    handle_disconnect();
                }
            });
    }

    /**
     * @brief Handle disconnection
     */
    void handle_disconnect() {
        std::cout << "Connection closed" << std::endl;
        close();
    }
};

/**
 * @class async_tcp_server
 * @brief Asynchronous TCP server supporting multiple concurrent connections
 * 
 * Demonstrates:
 * - Async accept operations
 * - Connection management
 * - Graceful shutdown
 * - Thread-safe operations
 */
class async_tcp_server {
public:
    /**
     * @brief Construct server on specified port
     */
    explicit async_tcp_server(asio::io_context& io_context, unsigned short port)
        : io_context_(io_context)
        , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
        , running_(true)
    {
        configure_acceptor();
        std::cout << "Async TCP Server listening on port " << port << std::endl;
        
        // Start accepting connections
        start_accept();
    }

    /**
     * @brief Stop the server gracefully
     */
    void stop() {
        running_ = false;
        
        std::error_code ec;
        acceptor_.close(ec);
        
        // Close all active connections
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_) {
            conn->close();
        }
        connections_.clear();
        
        std::cout << "Server stopped" << std::endl;
    }

    /**
     * @brief Get number of active connections
     */
    std::size_t get_connection_count() const {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        return connections_.size();
    }

    /**
     * @brief Broadcast message to all connected clients
     * @param message Message to broadcast
     */
    void broadcast(const std::string& message) {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        for (auto& conn : connections_) {
            conn->send_data(message);
        }
    }

private:
    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    std::atomic<bool> running_;
    
    // Thread-safe connection management
    mutable std::mutex connections_mutex_;
    std::set<connection::pointer> connections_;

    /**
     * @brief Configure acceptor options
     */
    void configure_acceptor() {
        acceptor_.set_option(asio::socket_base::reuse_address(true));
    }

    /**
     * @brief Start accepting new connections
     */
    void start_accept() {
        if (!running_) return;
        
        connection::pointer new_connection = connection::create(io_context_);
        
        acceptor_.async_accept(new_connection->socket(),
            [this, new_connection](const std::error_code& error) {
                if (!error) {
                    handle_accept(new_connection);
                } else if (running_) {
                    std::cerr << "Accept error: " << error.message() << std::endl;
                }
                
                // Continue accepting if server is still running
                if (running_) {
                    start_accept();
                }
            });
    }

    /**
     * @brief Handle new accepted connection
     * @param new_connection The newly accepted connection
     */
    void handle_accept(connection::pointer new_connection) {
        // Add to active connections
        {
            std::lock_guard<std::mutex> lock(connections_mutex_);
            connections_.insert(new_connection);
        }
        
        // Start handling the connection
        new_connection->start();
        
        std::cout << "Active connections: " << get_connection_count() << std::endl;
        
        // Clean up closed connections periodically
        cleanup_closed_connections();
    }

    /**
     * @brief Remove closed connections from the active set
     */
    void cleanup_closed_connections() {
        std::lock_guard<std::mutex> lock(connections_mutex_);
        
        auto it = connections_.begin();
        while (it != connections_.end()) {
            if (!(*it)->socket().is_open()) {
                it = connections_.erase(it);
            } else {
                ++it;
            }
        }
    }
};

/**
 * @class async_tcp_client
 * @brief Asynchronous TCP client demonstrating modern async patterns
 */
class async_tcp_client : public std::enable_shared_from_this<async_tcp_client> {
public:
    using pointer = std::shared_ptr<async_tcp_client>;

    static pointer create(asio::io_context& io_context) {
        return pointer(new async_tcp_client(io_context));
    }

    /**
     * @brief Connect to server asynchronously
     * @param host Server hostname
     * @param service Port or service name
     * @param connect_handler Callback for connection result
     */
    void async_connect(const std::string& host, const std::string& service,
                      std::function<void(const std::error_code&)> connect_handler) {
        
        auto self = shared_from_this();
        resolver_.async_resolve(host, service,
            [this, self, connect_handler](const std::error_code& error,
                                         asio::ip::tcp::resolver::results_type endpoints) {
                if (!error) {
                    asio::async_connect(socket_, endpoints,
                        [this, self, connect_handler](const std::error_code& error,
                                                     const asio::ip::tcp::endpoint&) {
                            if (!error) {
                                start_read();
                            }
                            connect_handler(error);
                        });
                } else {
                    connect_handler(error);
                }
            });
    }

    /**
     * @brief Send message asynchronously
     * @param message Message to send
     * @param send_handler Callback for send completion
     */
    void async_send(const std::string& message,
                   std::function<void(const std::error_code&, std::size_t)> send_handler) {
        
        auto self = shared_from_this();
        asio::async_write(socket_, asio::buffer(message),
            [this, self, send_handler](const std::error_code& error, std::size_t bytes_transferred) {
                send_handler(error, bytes_transferred);
            });
    }

    /**
     * @brief Set callback for received data
     * @param handler Function to call when data is received
     */
    void set_receive_handler(std::function<void(const std::string&)> handler) {
        receive_handler_ = std::move(handler);
    }

    /**
     * @brief Close connection
     */
    void close() {
        std::error_code ec;
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }

private:
    asio::ip::tcp::socket socket_;
    asio::ip::tcp::resolver resolver_;
    std::array<char, 1024> buffer_;
    std::function<void(const std::string&)> receive_handler_;

    explicit async_tcp_client(asio::io_context& io_context)
        : socket_(io_context), resolver_(io_context) {}

    void start_read() {
        auto self = shared_from_this();
        
        socket_.async_read_some(asio::buffer(buffer_),
            [this, self](const std::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    if (receive_handler_) {
                        std::string data(buffer_.data(), bytes_transferred);
                        receive_handler_(data);
                    }
                    start_read(); // Continue reading
                } else if (error != asio::error::eof) {
                    std::cerr << "Read error: " << error.message() << std::endl;
                }
            });
    }
};

} // namespace async_examples

/**
 * @brief Demo showing async server usage
 */
void run_async_server_demo() {
    try {
        asio::io_context io_context;
        async_examples::async_tcp_server server(io_context, 8082);
        
        // Run server in background thread
        std::thread server_thread([&io_context]() {
            io_context.run();
        });
        
        // Let server run for demo
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        // Stop server
        server.stop();
        io_context.stop();
        
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Async server demo error: " << e.what() << std::endl;
    }
}

/**
 * @brief Demo showing async client usage
 */
void run_async_client_demo() {
    try {
        asio::io_context io_context;
        auto client = async_examples::async_tcp_client::create(io_context);
        
        // Set up receive handler
        client->set_receive_handler([](const std::string& data) {
            std::cout << "Received: " << data;
        });
        
        // Connect to server
        client->async_connect("localhost", "8082",
            [client](const std::error_code& error) {
                if (!error) {
                    std::cout << "Connected to server" << std::endl;
                    
                    // Send test messages
                    client->async_send("Hello from async client!\\n",
                        [](const std::error_code& error, std::size_t bytes_sent) {
                            if (!error) {
                                std::cout << "Sent " << bytes_sent << " bytes" << std::endl;
                            }
                        });
                } else {
                    std::cerr << "Connection failed: " << error.message() << std::endl;
                }
            });
        
        // Run for demo duration
        std::thread io_thread([&io_context]() {
            io_context.run();
        });
        
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        client->close();
        io_context.stop();
        
        if (io_thread.joinable()) {
            io_thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Async client demo error: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== Async TCP Server/Client Example ===" << std::endl;
    std::cout << "This example demonstrates:" << std::endl;
    std::cout << "- Asynchronous TCP server with multiple client support" << std::endl;
    std::cout << "- Proper lifetime management with shared_ptr" << std::endl;
    std::cout << "- Error handling in async operations" << std::endl;
    std::cout << "- Modern C++ async patterns" << std::endl;
    std::cout << "=======================================" << std::endl;
    
    std::cout << "\\nTo test this example:" << std::endl;
    std::cout << "1. Compile: g++ -std=c++20 async_tcp_server.cpp -pthread" << std::endl;
    std::cout << "2. Run server: ./a.out server" << std::endl;
    std::cout << "3. Run client: ./a.out client" << std::endl;
    
    return 0;
}

/**
 * Key Async Programming Concepts:
 * 
 * 1. **Lifetime Management**: Use shared_ptr and enable_shared_from_this
 * 2. **Callback Chains**: Async operations chain together
 * 3. **Error Handling**: Check error codes in every callback
 * 4. **Thread Safety**: Protect shared data with mutexes
 * 5. **Resource Cleanup**: Ensure proper cleanup in destructors
 * 
 * Best Practices:
 * - Always capture 'self' in async lambda callbacks
 * - Use RAII for resource management
 * - Handle all error conditions
 * - Avoid blocking operations in async callbacks
 * - Use strand for serializing access to shared data
 * 
 * Common Pitfalls:
 * - Forgetting to capture 'self' (object destruction)
 * - Not handling all error conditions
 * - Blocking in async callbacks
 * - Race conditions on shared data
 * - Memory leaks from improper cleanup
 */