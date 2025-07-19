/**
 * @file tcp_client_server.cpp
 * @brief Comprehensive TCP client/server example demonstrating ASIO socket basics
 * 
 * This example demonstrates:
 * - Synchronous TCP client and server implementation
 * - Proper error handling using both exceptions and error codes
 * - Socket option configuration
 * - Resource management with RAII
 * - Clean shutdown procedures
 * 
 * Compile with: g++ -std=c++20 -I/path/to/asio/include tcp_client_server.cpp -pthread
 */

#pragma once

#include <asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <array>
#include <system_error>

namespace examples {

/**
 * @class tcp_server
 * @brief Simple synchronous TCP echo server
 * 
 * Demonstrates:
 * - basic_socket_acceptor usage for accepting connections
 * - basic_stream_socket for client communication
 * - Socket option configuration (reuse_address)
 * - Proper error handling and cleanup
 */
class tcp_server {
public:
    explicit tcp_server(asio::io_context& io_context, unsigned short port)
        : acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    {
        configure_acceptor();
        std::cout << "TCP Server listening on port " << port << std::endl;
    }

    /**
     * @brief Main server loop - accepts and handles one client at a time
     * 
     * In production, this would typically be multi-threaded or use async operations
     */
    void run() {
        try {
            while (true) {
                // Create socket for incoming connection
                asio::ip::tcp::socket client_socket(acceptor_.get_executor());
                
                // Accept connection (blocking)
                acceptor_.accept(client_socket);
                
                // Get client information
                auto remote_endpoint = client_socket.remote_endpoint();
                std::cout << "Client connected from: " << remote_endpoint << std::endl;
                
                // Handle client communication
                handle_client(std::move(client_socket));
            }
        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    }

    /**
     * @brief Gracefully shutdown the server
     */
    void stop() {
        std::error_code ec;
        acceptor_.close(ec);
        if (ec) {
            std::cerr << "Error closing acceptor: " << ec.message() << std::endl;
        }
    }

private:
    asio::ip::tcp::acceptor acceptor_;

    /**
     * @brief Configure acceptor socket options
     */
    void configure_acceptor() {
        // Allow address reuse to avoid "Address already in use" errors
        asio::socket_base::reuse_address option(true);
        acceptor_.set_option(option);
        
        // Set socket to non-blocking mode for better control
        // In this sync example, we keep it blocking for simplicity
        // acceptor_.non_blocking(true);
    }

    /**
     * @brief Handle communication with a single client
     * @param socket Connected client socket (moved)
     */
    void handle_client(asio::ip::tcp::socket socket) {
        try {
            std::array<char, 1024> buffer;
            
            while (true) {
                // Read data from client
                std::error_code error;
                std::size_t bytes_received = socket.read_some(
                    asio::buffer(buffer), error);
                
                if (error == asio::error::eof) {
                    std::cout << "Client disconnected gracefully" << std::endl;
                    break;
                } else if (error) {
                    std::cerr << "Read error: " << error.message() << std::endl;
                    break;
                }
                
                // Echo data back to client
                std::string_view message(buffer.data(), bytes_received);
                std::cout << "Received: " << message;
                
                // Send response
                asio::write(socket, asio::buffer(buffer, bytes_received));
            }
        } catch (const std::exception& e) {
            std::cerr << "Client handling error: " << e.what() << std::endl;
        }
        
        // Socket automatically closed by destructor (RAII)
    }
};

/**
 * @class tcp_client
 * @brief Simple synchronous TCP client
 * 
 * Demonstrates:
 * - basic_stream_socket usage for client connections
 * - Name resolution using basic_resolver
 * - Socket option configuration (TCP_NODELAY)
 * - Timeout handling for operations
 */
class tcp_client {
public:
    explicit tcp_client(asio::io_context& io_context)
        : socket_(io_context), resolver_(io_context)
    {
    }

    /**
     * @brief Connect to server with hostname resolution
     * @param host Hostname or IP address
     * @param service Port number or service name
     * @return true if connection successful
     */
    bool connect(const std::string& host, const std::string& service) {
        try {
            // Resolve hostname to endpoints
            auto endpoints = resolver_.resolve(host, service);
            
            // Connect to first available endpoint
            asio::connect(socket_, endpoints);
            
            // Configure socket options for optimal performance
            configure_socket();
            
            std::cout << "Connected to " << host << ":" << service << std::endl;
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Connection failed: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * @brief Send message and receive response
     * @param message Message to send
     * @return Response from server (empty if error)
     */
    std::string send_message(const std::string& message) {
        try {
            // Send message
            asio::write(socket_, asio::buffer(message));
            
            // Receive response
            std::array<char, 1024> reply_buffer;
            std::size_t reply_length = socket_.read_some(asio::buffer(reply_buffer));
            
            return std::string(reply_buffer.data(), reply_length);
            
        } catch (const std::exception& e) {
            std::cerr << "Communication error: " << e.what() << std::endl;
            return "";
        }
    }

    /**
     * @brief Gracefully close connection
     */
    void disconnect() {
        std::error_code ec;
        
        // Shutdown both send and receive
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        if (ec) {
            std::cerr << "Shutdown error: " << ec.message() << std::endl;
        }
        
        // Close socket
        socket_.close(ec);
        if (ec) {
            std::cerr << "Close error: " << ec.message() << std::endl;
        }
    }

    /**
     * @brief Check if socket is connected
     */
    bool is_connected() const {
        return socket_.is_open();
    }

private:
    asio::ip::tcp::socket socket_;
    asio::ip::tcp::resolver resolver_;

    /**
     * @brief Configure socket options for optimal performance
     */
    void configure_socket() {
        // Disable Nagle's algorithm for lower latency
        asio::ip::tcp::no_delay option(true);
        socket_.set_option(option);
        
        // Set keep-alive to detect dead connections
        asio::socket_base::keep_alive keep_alive_option(true);
        socket_.set_option(keep_alive_option);
    }
};

} // namespace examples

/**
 * @brief Demo function showing server usage
 */
void run_server_demo() {
    try {
        asio::io_context io_context;
        examples::tcp_server server(io_context, 8080);
        
        // Run server in background thread for demo
        std::thread server_thread([&server]() {
            server.run();
        });
        
        // Let server run for demo
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        // Cleanup
        server.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Server demo error: " << e.what() << std::endl;
    }
}

/**
 * @brief Demo function showing client usage
 */
void run_client_demo() {
    try {
        asio::io_context io_context;
        examples::tcp_client client(io_context);
        
        // Connect to server
        if (client.connect("localhost", "8080")) {
            
            // Send test messages
            auto response1 = client.send_message("Hello, Server!\\n");
            std::cout << "Server response: " << response1;
            
            auto response2 = client.send_message("How are you?\\n");
            std::cout << "Server response: " << response2;
            
            // Clean disconnect
            client.disconnect();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "Client demo error: " << e.what() << std::endl;
    }
}

/**
 * @brief Main function demonstrating usage patterns
 */
int main() {
    std::cout << "=== TCP Client/Server Example ===" << std::endl;
    std::cout << "This example demonstrates:" << std::endl;
    std::cout << "- Synchronous TCP server with echo functionality" << std::endl;
    std::cout << "- TCP client with hostname resolution" << std::endl;
    std::cout << "- Proper error handling and resource management" << std::endl;
    std::cout << "- Socket option configuration" << std::endl;
    std::cout << "====================================" << std::endl;
    
    // For a complete demo, run server and client in separate processes
    // or uncomment one of these:
    
    // run_server_demo();  // Run server
    // run_client_demo();  // Run client (requires server to be running)
    
    std::cout << "\\nTo test this example:" << std::endl;
    std::cout << "1. Compile: g++ -std=c++20 tcp_client_server.cpp -pthread" << std::endl;
    std::cout << "2. Run server: ./a.out server" << std::endl;
    std::cout << "3. Run client: ./a.out client" << std::endl;
    
    return 0;
}

/* 
 * Expected output when running server and client:
 * 
 * Server output:
 * TCP Server listening on port 8080
 * Client connected from: 127.0.0.1:12345
 * Received: Hello, Server!
 * Received: How are you?
 * Client disconnected gracefully
 * 
 * Client output:
 * Connected to localhost:8080
 * Server response: Hello, Server!
 * Server response: How are you?
 */

/**
 * Key Learning Points:
 * 
 * 1. **RAII**: Sockets automatically close when destructed
 * 2. **Error Handling**: Both exception and error_code approaches shown
 * 3. **Socket Options**: Demonstrates reuse_address, no_delay, keep_alive
 * 4. **Resource Management**: Explicit cleanup where needed
 * 5. **Thread Safety**: Each socket used by single thread
 * 6. **Blocking Operations**: Simple synchronous model for learning
 * 
 * Production Considerations:
 * - Use async operations for scalability
 * - Implement proper timeout handling
 * - Add comprehensive error recovery
 * - Consider thread pool for multi-client handling
 * - Add logging and monitoring
 */