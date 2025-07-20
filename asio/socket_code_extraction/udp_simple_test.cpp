// Simple UDP echo test with connected sockets
#include <iostream>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

std::atomic<bool> server_running{true};
std::atomic<bool> test_passed{false};

void server_thread() {
    int server_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (server_fd < 0) {
        std::cerr << "Server: Failed to create socket\n";
        return;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(8080);
    
    if (bind(server_fd, (sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Server: Failed to bind\n";
        close(server_fd);
        return;
    }
    
    std::cout << "Server: Listening on port 8080\n";
    
    char buffer[1024];
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    // Set timeout
    struct timeval tv;
    tv.tv_sec = 0;
    tv.tv_usec = 500000; // 500ms
    setsockopt(server_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    while (server_running) {
        ssize_t n = recvfrom(server_fd, buffer, sizeof(buffer), 0,
                            (sockaddr*)&client_addr, &client_len);
        
        if (n > 0) {
            std::cout << "Server: Received " << n << " bytes\n";
            
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

void client_thread() {
    // Wait for server to start
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    int client_fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (client_fd < 0) {
        std::cerr << "Client: Failed to create socket\n";
        return;
    }
    
    sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    server_addr.sin_port = htons(8080);
    
    const char* test_msg = "Hello, UDP Echo Server!";
    size_t msg_len = strlen(test_msg);
    
    // Send test message
    ssize_t sent = sendto(client_fd, test_msg, msg_len, 0,
                         (sockaddr*)&server_addr, sizeof(server_addr));
    
    if (sent != static_cast<ssize_t>(msg_len)) {
        std::cerr << "Client: Failed to send\n";
        close(client_fd);
        return;
    }
    
    std::cout << "Client: Sent '" << test_msg << "'\n";
    
    // Set receive timeout
    struct timeval tv;
    tv.tv_sec = 2;
    tv.tv_usec = 0;
    setsockopt(client_fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    
    // Receive echo
    char buffer[1024];
    ssize_t received = recvfrom(client_fd, buffer, sizeof(buffer), 0,
                               nullptr, nullptr);
    
    if (received == static_cast<ssize_t>(msg_len) &&
        memcmp(buffer, test_msg, msg_len) == 0) {
        std::cout << "Client: Received correct echo!\n";
        test_passed = true;
    } else {
        std::cerr << "Client: Echo test failed\n";
    }
    
    close(client_fd);
}

int main() {
    std::thread server(server_thread);
    std::thread client(client_thread);
    
    client.join();
    server_running = false;
    server.join();
    
    if (test_passed) {
        std::cout << "\nTEST PASSED!\n";
        return 0;
    } else {
        std::cerr << "\nTEST FAILED!\n";
        return 1;
    }
}