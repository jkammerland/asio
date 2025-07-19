/**
 * @file udp_client_server.cpp
 * @brief Comprehensive UDP client/server example demonstrating datagram sockets
 * 
 * This example demonstrates:
 * - UDP server using basic_datagram_socket
 * - UDP client with both connected and unconnected modes
 * - Message boundary preservation in UDP
 * - Broadcast and multicast basics
 * - Error handling specific to UDP
 * 
 * Compile with: g++ -std=c++20 -I/path/to/asio/include udp_client_server.cpp -pthread
 */

#pragma once

#include <asio.hpp>
#include <iostream>
#include <string>
#include <thread>
#include <chrono>
#include <array>
#include <system_error>
#include <vector>

namespace examples {

/**
 * @class udp_server
 * @brief UDP echo server demonstrating datagram socket usage
 * 
 * Demonstrates:
 * - basic_datagram_socket for UDP operations
 * - receive_from/send_to for unconnected operations
 * - Message boundary preservation
 * - Client endpoint tracking
 */
class udp_server {
public:
    explicit udp_server(asio::io_context& io_context, unsigned short port)
        : socket_(io_context, asio::ip::udp::endpoint(asio::ip::udp::v4(), port))
    {
        configure_socket();
        std::cout << "UDP Server listening on port " << port << std::endl;
        std::cout << "Local endpoint: " << socket_.local_endpoint() << std::endl;
    }

    /**
     * @brief Main server loop - receives and echoes UDP datagrams
     */
    void run() {
        try {
            std::array<char, 1024> buffer;
            
            while (true) {
                // Receive datagram from any client
                asio::ip::udp::endpoint sender_endpoint;
                std::size_t bytes_received = socket_.receive_from(
                    asio::buffer(buffer), sender_endpoint);
                
                std::cout << "Received " << bytes_received << " bytes from: " 
                         << sender_endpoint << std::endl;
                
                // Display message content
                std::string_view message(buffer.data(), bytes_received);
                std::cout << "Message: " << message << std::endl;
                
                // Echo back to sender
                socket_.send_to(asio::buffer(buffer, bytes_received), sender_endpoint);
                std::cout << "Echoed back to sender" << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Server error: " << e.what() << std::endl;
        }
    }

    /**
     * @brief Stop server gracefully
     */
    void stop() {
        std::error_code ec;
        socket_.close(ec);
        if (ec) {
            std::cerr << "Error closing socket: " << ec.message() << std::endl;
        }
    }

    /**
     * @brief Get the local endpoint the server is bound to
     */
    asio::ip::udp::endpoint local_endpoint() const {
        return socket_.local_endpoint();
    }

private:
    asio::ip::udp::socket socket_;

    /**
     * @brief Configure socket options for UDP
     */
    void configure_socket() {
        // Allow address reuse
        asio::socket_base::reuse_address option(true);
        socket_.set_option(option);
        
        // Enable broadcast (for demonstration)
        asio::socket_base::broadcast broadcast_option(true);
        socket_.set_option(broadcast_option);
        
        // Set receive buffer size
        asio::socket_base::receive_buffer_size buffer_size_option(8192);
        socket_.set_option(buffer_size_option);
    }
};

/**
 * @class udp_client
 * @brief UDP client demonstrating both connected and unconnected modes
 * 
 * Demonstrates:
 * - Unconnected mode using send_to/receive_from
 * - Connected mode using connect/send/receive
 * - Timeout handling for UDP operations
 */
class udp_client {
public:
    explicit udp_client(asio::io_context& io_context)
        : socket_(io_context), resolver_(io_context)
    {
        // Open socket with IPv4
        socket_.open(asio::ip::udp::v4());
        configure_socket();
    }

    /**
     * @brief Send message to server using unconnected mode
     * @param host Server hostname or IP
     * @param service Port number or service name
     * @param message Message to send
     * @return Server response (empty if error)
     */
    std::string send_unconnected(const std::string& host, 
                                const std::string& service,
                                const std::string& message) {
        try {
            // Resolve server endpoint
            auto endpoints = resolver_.resolve(asio::ip::udp::v4(), host, service);
            asio::ip::udp::endpoint server_endpoint = *endpoints.begin();
            
            std::cout << "Sending to: " << server_endpoint << std::endl;
            
            // Send message
            socket_.send_to(asio::buffer(message), server_endpoint);
            
            // Receive response
            std::array<char, 1024> reply_buffer;
            asio::ip::udp::endpoint sender_endpoint;
            std::size_t reply_length = socket_.receive_from(
                asio::buffer(reply_buffer), sender_endpoint);
            
            // Verify response came from expected server
            if (sender_endpoint != server_endpoint) {
                std::cerr << "Warning: Response from unexpected endpoint: " 
                         << sender_endpoint << std::endl;
            }
            
            return std::string(reply_buffer.data(), reply_length);
            
        } catch (const std::exception& e) {
            std::cerr << "Unconnected send error: " << e.what() << std::endl;
            return "";
        }
    }

    /**
     * @brief Connect to server and use connected mode
     * @param host Server hostname or IP
     * @param service Port number or service name
     * @return true if connection successful
     */
    bool connect_to_server(const std::string& host, const std::string& service) {
        try {
            // Resolve server endpoint
            auto endpoints = resolver_.resolve(asio::ip::udp::v4(), host, service);
            asio::ip::udp::endpoint server_endpoint = *endpoints.begin();
            
            // Connect socket (sets default destination)
            socket_.connect(server_endpoint);
            
            std::cout << "Connected to: " << server_endpoint << std::endl;
            std::cout << "Local endpoint: " << socket_.local_endpoint() << std::endl;
            
            return true;
            
        } catch (const std::exception& e) {
            std::cerr << "Connection failed: " << e.what() << std::endl;
            return false;
        }
    }

    /**
     * @brief Send message using connected mode
     * @param message Message to send
     * @return Server response (empty if error)
     */
    std::string send_connected(const std::string& message) {
        try {
            // Send to connected endpoint
            socket_.send(asio::buffer(message));
            
            // Receive response
            std::array<char, 1024> reply_buffer;
            std::size_t reply_length = socket_.receive(asio::buffer(reply_buffer));
            
            return std::string(reply_buffer.data(), reply_length);
            
        } catch (const std::exception& e) {
            std::cerr << "Connected send error: " << e.what() << std::endl;
            return "";
        }
    }

    /**
     * @brief Send broadcast message
     * @param port Broadcast port
     * @param message Message to broadcast
     */
    void send_broadcast(unsigned short port, const std::string& message) {
        try {
            // Create broadcast endpoint
            asio::ip::udp::endpoint broadcast_endpoint(
                asio::ip::address_v4::broadcast(), port);
            
            std::cout << "Broadcasting to: " << broadcast_endpoint << std::endl;
            
            // Send broadcast
            socket_.send_to(asio::buffer(message), broadcast_endpoint);
            
        } catch (const std::exception& e) {
            std::cerr << "Broadcast error: " << e.what() << std::endl;
        }
    }

    /**
     * @brief Disconnect from server
     */
    void disconnect() {
        std::error_code ec;
        socket_.close(ec);
        if (ec) {
            std::cerr << "Disconnect error: " << ec.message() << std::endl;
        }
    }

private:
    asio::ip::udp::socket socket_;
    asio::ip::udp::resolver resolver_;

    /**
     * @brief Configure socket options
     */
    void configure_socket() {
        // Enable broadcast capability
        asio::socket_base::broadcast broadcast_option(true);
        socket_.set_option(broadcast_option);
        
        // Set send buffer size
        asio::socket_base::send_buffer_size buffer_size_option(8192);
        socket_.set_option(buffer_size_option);
    }
};

/**
 * @class multicast_receiver
 * @brief Demonstrates UDP multicast reception
 */
class multicast_receiver {
public:
    multicast_receiver(asio::io_context& io_context, 
                      const asio::ip::address& multicast_address,
                      unsigned short port)
        : socket_(io_context)
    {
        // Create endpoint for any address on the specified port
        asio::ip::udp::endpoint listen_endpoint(asio::ip::address_v4::any(), port);
        
        socket_.open(listen_endpoint.protocol());
        socket_.set_option(asio::socket_base::reuse_address(true));
        socket_.bind(listen_endpoint);
        
        // Join multicast group
        socket_.set_option(asio::ip::multicast::join_group(multicast_address));
        
        std::cout << "Joined multicast group: " << multicast_address 
                 << " on port " << port << std::endl;
    }

    /**
     * @brief Receive multicast messages
     */
    void receive_messages() {
        try {
            std::array<char, 1024> buffer;
            
            while (true) {
                asio::ip::udp::endpoint sender_endpoint;
                std::size_t bytes_received = socket_.receive_from(
                    asio::buffer(buffer), sender_endpoint);
                
                std::cout << "Multicast message from " << sender_endpoint 
                         << ": " << std::string_view(buffer.data(), bytes_received) 
                         << std::endl;
            }
        } catch (const std::exception& e) {
            std::cerr << "Multicast receive error: " << e.what() << std::endl;
        }
    }

private:
    asio::ip::udp::socket socket_;
};

} // namespace examples

/**
 * @brief Demo function for UDP server
 */
void run_udp_server_demo() {
    try {
        asio::io_context io_context;
        examples::udp_server server(io_context, 8081);
        
        std::thread server_thread([&server]() {
            server.run();
        });
        
        // Let server run for demo
        std::this_thread::sleep_for(std::chrono::seconds(30));
        
        server.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "UDP server demo error: " << e.what() << std::endl;
    }
}

/**
 * @brief Demo function for UDP client
 */
void run_udp_client_demo() {
    try {
        asio::io_context io_context;
        examples::udp_client client(io_context);
        
        // Test unconnected mode
        std::cout << "=== Testing Unconnected Mode ===" << std::endl;
        auto response1 = client.send_unconnected("localhost", "8081", 
                                                "Hello from unconnected client!");
        std::cout << "Server response: " << response1 << std::endl;
        
        // Test connected mode
        std::cout << "\\n=== Testing Connected Mode ===" << std::endl;
        if (client.connect_to_server("localhost", "8081")) {
            auto response2 = client.send_connected("Hello from connected client!");
            std::cout << "Server response: " << response2 << std::endl;
            
            auto response3 = client.send_connected("Another message!");
            std::cout << "Server response: " << response3 << std::endl;
        }
        
        // Test broadcast (be careful with this on networks)
        std::cout << "\\n=== Testing Broadcast ===" << std::endl;
        client.send_broadcast(8081, "Broadcast message!");
        
        client.disconnect();
        
    } catch (const std::exception& e) {
        std::cerr << "UDP client demo error: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== UDP Client/Server Example ===" << std::endl;
    std::cout << "This example demonstrates:" << std::endl;
    std::cout << "- UDP datagram socket usage" << std::endl;
    std::cout << "- Connected vs unconnected UDP modes" << std::endl;
    std::cout << "- Message boundary preservation" << std::endl;
    std::cout << "- Broadcast and multicast basics" << std::endl;
    std::cout << "=================================" << std::endl;
    
    // For complete demo, run server and client in separate processes
    // run_udp_server_demo();  // Run server
    // run_udp_client_demo();  // Run client
    
    std::cout << "\\nTo test this example:" << std::endl;
    std::cout << "1. Compile: g++ -std=c++20 udp_client_server.cpp -pthread" << std::endl;
    std::cout << "2. Run server: ./a.out server" << std::endl;
    std::cout << "3. Run client: ./a.out client" << std::endl;
    
    return 0;
}

/**
 * Key UDP Concepts Demonstrated:
 * 
 * 1. **Message Boundaries**: UDP preserves message boundaries
 * 2. **Connectionless**: No connection establishment required
 * 3. **Unconnected Mode**: send_to/receive_from with explicit endpoints
 * 4. **Connected Mode**: connect() then send/receive (for convenience)
 * 5. **Broadcast**: Send to all hosts on network segment
 * 6. **Multicast**: Send to group of interested receivers
 * 
 * UDP vs TCP Differences:
 * - No connection establishment/teardown
 * - No reliability guarantees
 * - Message boundaries preserved
 * - Lower overhead
 * - Suitable for real-time applications
 * 
 * Common UDP Use Cases:
 * - DNS queries
 * - DHCP
 * - Real-time gaming
 * - Video streaming
 * - IoT sensor data
 */