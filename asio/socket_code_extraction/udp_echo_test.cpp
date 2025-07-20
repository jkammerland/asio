// Complete UDP echo test with working client-server handshake
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <vector>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

// Simple synchronous echo server for comparison
void sync_echo_server(std::atomic<bool>& running, std::atomic<bool>& ready) {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        std::cerr << "Server: Failed to create socket\n";
        return;
    }
    
    // Set timeout for graceful shutdown
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500ms
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    
    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Server: Failed to bind\n";
        close(server_fd);
        return;
    }
    
    std::cout << "Server: Ready on port 8080\n";
    ready = true;
    
    char buffer[1024];
    while (running) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        
        ssize_t n = recvfrom(server_fd, buffer, sizeof(buffer), 0,
                            (sockaddr*)&client_addr, &client_len);
        
        if (n > 0) {
            std::cout << "Server: Received " << n << " bytes from "
                      << inet_ntoa(client_addr.sin_addr) << ":"
                      << ntohs(client_addr.sin_port) << "\n";
            
            // Echo back
            ssize_t sent = sendto(server_fd, buffer, n, 0,
                                 (sockaddr*)&client_addr, client_len);
            
            if (sent == n) {
                std::cout << "Server: Echoed " << sent << " bytes\n";
            }
        }
    }
    
    close(server_fd);
    std::cout << "Server: Stopped\n";
}

// Test client
bool test_client() {
    // Wait a bit
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd < 0) {
        std::cerr << "Client: Failed to create socket\n";
        return false;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(8080);
    
    // Test messages
    std::vector<std::string> test_messages = {
        "Hello, UDP Echo Server!",
        "Test message 2",
        "Final message"
    };
    
    bool all_passed = true;
    
    for (const auto& msg : test_messages) {
        // Send
        ssize_t sent = sendto(client_fd, msg.c_str(), msg.length(), 0,
                             (sockaddr*)&server_addr, sizeof(server_addr));
        
        if (sent != static_cast<ssize_t>(msg.length())) {
            std::cerr << "Client: Failed to send '" << msg << "'\n";
            all_passed = false;
            break;
        }
        
        std::cout << "Client: Sent '" << msg << "'\n";
        
        // Set receive timeout
        struct timeval tv;
        tv.tv_sec = 2;
        tv.tv_usec = 0;
        setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        
        // Receive echo
        char buffer[1024];
        ssize_t received = recvfrom(client_fd, buffer, sizeof(buffer), 0,
                                   nullptr, nullptr);
        
        if (received == static_cast<ssize_t>(msg.length()) &&
            memcmp(buffer, msg.c_str(), msg.length()) == 0) {
            std::cout << "Client: Received correct echo\n";
        } else {
            std::cerr << "Client: Echo mismatch for '" << msg << "'\n";
            all_passed = false;
            break;
        }
        
        // Small delay between messages
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    close(client_fd);
    return all_passed;
}

int main() {
    std::cout << "=== UDP Echo Test ===\n\n";
    
#ifdef _WIN32
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2,2), &wsaData) != 0) {
        std::cerr << "WSAStartup failed\n";
        return 1;
    }
#endif
    
    std::atomic<bool> server_running{true};
    std::atomic<bool> server_ready{false};
    
    // Start server
    std::thread server_thread([&]() {
        sync_echo_server(server_running, server_ready);
    });
    
    // Wait for server to be ready
    while (!server_ready) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    
    // Run client test
    bool test_passed = test_client();
    
    // Stop server
    server_running = false;
    server_thread.join();
    
#ifdef _WIN32
    WSACleanup();
#endif
    
    if (test_passed) {
        std::cout << "\nTEST PASSED: All messages echoed correctly!\n";
        return 0;
    } else {
        std::cerr << "\nTEST FAILED: Echo test failed!\n";
        return 1;
    }
}