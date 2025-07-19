/**
 * @file http_client_server.cpp
 * @brief Integration example showing HTTP client/server implementation
 * 
 * This example demonstrates:
 * - Integration of multiple ASIO socket components
 * - HTTP protocol implementation using stream sockets
 * - Request/response parsing and handling
 * - Connection management and keep-alive
 * - Error handling in real-world scenarios
 * 
 * Compile with: g++ -std=c++20 -I/path/to/asio/include http_client_server.cpp -pthread
 */

#pragma once

#include <asio.hpp>
#include <iostream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <memory>
#include <thread>
#include <sstream>
#include <regex>
#include <chrono>

namespace integration_examples {

/**
 * @class http_request
 * @brief Simple HTTP request parser and container
 */
class http_request {
public:
    enum class method { GET, POST, PUT, DELETE, UNKNOWN };
    
    http_request() = default;
    
    bool parse(const std::string& raw_request) {
        std::istringstream stream(raw_request);
        std::string line;
        
        // Parse request line
        if (!std::getline(stream, line)) return false;
        
        std::istringstream request_line(line);
        std::string method_str, version;
        
        if (!(request_line >> method_str >> path_ >> version)) {
            return false;
        }
        
        method_ = parse_method(method_str);
        version_ = version;
        
        // Parse headers
        while (std::getline(stream, line) && line != "\\r" && !line.empty()) {
            auto colon_pos = line.find(':');
            if (colon_pos != std::string::npos) {
                std::string name = line.substr(0, colon_pos);
                std::string value = line.substr(colon_pos + 1);
                
                // Trim whitespace
                value.erase(0, value.find_first_not_of(" \\t"));
                value.erase(value.find_last_not_of(" \\t\\r\\n") + 1);
                
                headers_[name] = value;
            }
        }
        
        // Parse body (rest of the stream)
        std::ostringstream body_stream;
        body_stream << stream.rdbuf();
        body_ = body_stream.str();
        
        return true;
    }
    
    method get_method() const { return method_; }
    const std::string& get_path() const { return path_; }
    const std::string& get_version() const { return version_; }
    const std::string& get_body() const { return body_; }
    
    std::string get_header(const std::string& name) const {
        auto it = headers_.find(name);
        return it != headers_.end() ? it->second : "";
    }
    
    bool has_header(const std::string& name) const {
        return headers_.find(name) != headers_.end();
    }

private:
    method method_ = method::UNKNOWN;
    std::string path_;
    std::string version_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
    
    method parse_method(const std::string& method_str) {
        if (method_str == "GET") return method::GET;
        if (method_str == "POST") return method::POST;
        if (method_str == "PUT") return method::PUT;
        if (method_str == "DELETE") return method::DELETE;
        return method::UNKNOWN;
    }
};

/**
 * @class http_response
 * @brief Simple HTTP response builder
 */
class http_response {
public:
    http_response(int status_code = 200, const std::string& reason_phrase = "OK")
        : status_code_(status_code), reason_phrase_(reason_phrase) {
        
        set_header("Server", "ASIO-Example/1.0");
        set_header("Connection", "close");
    }
    
    void set_header(const std::string& name, const std::string& value) {
        headers_[name] = value;
    }
    
    void set_body(const std::string& body) {
        body_ = body;
        set_header("Content-Length", std::to_string(body.size()));
    }
    
    std::string to_string() const {
        std::ostringstream response;
        
        // Status line
        response << "HTTP/1.1 " << status_code_ << " " << reason_phrase_ << "\\r\\n";
        
        // Headers
        for (const auto& [name, value] : headers_) {
            response << name << ": " << value << "\\r\\n";
        }
        
        // Empty line
        response << "\\r\\n";
        
        // Body
        response << body_;
        
        return response.str();
    }

private:
    int status_code_;
    std::string reason_phrase_;
    std::unordered_map<std::string, std::string> headers_;
    std::string body_;
};

/**
 * @class http_connection
 * @brief Manages a single HTTP connection
 */
class http_connection : public std::enable_shared_from_this<http_connection> {
public:
    using pointer = std::shared_ptr<http_connection>;
    
    static pointer create(asio::io_context& io_context) {
        return pointer(new http_connection(io_context));
    }
    
    asio::ip::tcp::socket& socket() { return socket_; }
    
    void start() {
        auto remote = socket_.remote_endpoint();
        std::cout << "HTTP connection from: " << remote << std::endl;
        
        start_read();
    }

private:
    asio::ip::tcp::socket socket_;
    asio::streambuf request_buffer_;
    
    explicit http_connection(asio::io_context& io_context) : socket_(io_context) {}
    
    void start_read() {
        auto self = shared_from_this();
        
        // Read until we have a complete HTTP request (ending with \\r\\n\\r\\n)
        asio::async_read_until(socket_, request_buffer_, "\\r\\n\\r\\n",
            [this, self](const std::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    handle_request(bytes_transferred);
                } else {
                    std::cout << "Read error: " << error.message() << std::endl;
                }
            });
    }
    
    void handle_request(std::size_t bytes_transferred) {
        // Convert streambuf to string
        std::string raw_request(
            asio::buffers_begin(request_buffer_.data()),
            asio::buffers_begin(request_buffer_.data()) + bytes_transferred);
        
        // Parse HTTP request
        http_request request;
        if (!request.parse(raw_request)) {
            send_error_response(400, "Bad Request");
            return;
        }
        
        // Route request
        route_request(request);
    }
    
    void route_request(const http_request& request) {
        http_response response;
        
        if (request.get_method() == http_request::method::GET) {
            if (request.get_path() == "/") {
                response.set_body(
                    "<html><body>"
                    "<h1>ASIO HTTP Server</h1>"
                    "<p>This is a simple HTTP server built with ASIO sockets.</p>"
                    "<p>Try these endpoints:</p>"
                    "<ul>"
                    "<li><a href=\\\"/hello\\\">/hello</a></li>"
                    "<li><a href=\\\"/time\\\">/time</a></li>"
                    "<li><a href=\\\"/info\\\">/info</a></li>"
                    "</ul>"
                    "</body></html>");
                response.set_header("Content-Type", "text/html");
                
            } else if (request.get_path() == "/hello") {
                response.set_body("Hello, World from ASIO!");
                response.set_header("Content-Type", "text/plain");
                
            } else if (request.get_path() == "/time") {
                auto now = std::chrono::system_clock::now();
                auto time_t = std::chrono::system_clock::to_time_t(now);
                response.set_body(std::ctime(&time_t));
                response.set_header("Content-Type", "text/plain");
                
            } else if (request.get_path() == "/info") {
                std::ostringstream info;
                info << "Connection Info:\\n";
                info << "Remote: " << socket_.remote_endpoint() << "\\n";
                info << "Local: " << socket_.local_endpoint() << "\\n";
                info << "User-Agent: " << request.get_header("User-Agent") << "\\n";
                info << "Host: " << request.get_header("Host") << "\\n";
                
                response.set_body(info.str());
                response.set_header("Content-Type", "text/plain");
                
            } else {
                response = http_response(404, "Not Found");
                response.set_body("404 - Page not found");
                response.set_header("Content-Type", "text/plain");
            }
        } else {
            response = http_response(405, "Method Not Allowed");
            response.set_body("Method not allowed");
            response.set_header("Content-Type", "text/plain");
        }
        
        send_response(response);
    }
    
    void send_response(const http_response& response) {
        auto self = shared_from_this();
        auto response_str = std::make_shared<std::string>(response.to_string());
        
        asio::async_write(socket_, asio::buffer(*response_str),
            [this, self, response_str](const std::error_code& error, std::size_t bytes_transferred) {
                if (error) {
                    std::cout << "Write error: " << error.message() << std::endl;
                } else {
                    std::cout << "Sent " << bytes_transferred << " bytes" << std::endl;
                }
                
                // Close connection (HTTP/1.0 style)
                socket_.close();
            });
    }
    
    void send_error_response(int status_code, const std::string& reason) {
        http_response error_response(status_code, reason);
        error_response.set_body(std::to_string(status_code) + " " + reason);
        error_response.set_header("Content-Type", "text/plain");
        send_response(error_response);
    }
};

/**
 * @class http_server
 * @brief Asynchronous HTTP server
 */
class http_server {
public:
    explicit http_server(asio::io_context& io_context, unsigned short port)
        : io_context_(io_context)
        , acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port))
    {
        acceptor_.set_option(asio::socket_base::reuse_address(true));
        std::cout << "HTTP Server listening on port " << port << std::endl;
        
        start_accept();
    }

private:
    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    
    void start_accept() {
        auto new_connection = http_connection::create(io_context_);
        
        acceptor_.async_accept(new_connection->socket(),
            [this, new_connection](const std::error_code& error) {
                if (!error) {
                    new_connection->start();
                } else {
                    std::cout << "Accept error: " << error.message() << std::endl;
                }
                
                start_accept(); // Continue accepting
            });
    }
};

/**
 * @class http_client
 * @brief Simple HTTP client
 */
class http_client {
public:
    explicit http_client(asio::io_context& io_context)
        : io_context_(io_context), resolver_(io_context), socket_(io_context) {}
    
    void get(const std::string& host, const std::string& port, const std::string& path,
             std::function<void(const std::string&)> response_handler) {
        
        // Build HTTP request
        std::ostringstream request_stream;
        request_stream << "GET " << path << " HTTP/1.1\\r\\n";
        request_stream << "Host: " << host << "\\r\\n";
        request_stream << "User-Agent: ASIO-Client/1.0\\r\\n";
        request_stream << "Connection: close\\r\\n";
        request_stream << "\\r\\n";
        
        request_ = request_stream.str();
        response_handler_ = std::move(response_handler);
        
        // Resolve hostname
        resolver_.async_resolve(host, port,
            [this](const std::error_code& error, asio::ip::tcp::resolver::results_type endpoints) {
                if (!error) {
                    connect_to_server(endpoints);
                } else {
                    std::cerr << "Resolve error: " << error.message() << std::endl;
                }
            });
    }

private:
    asio::io_context& io_context_;
    asio::ip::tcp::resolver resolver_;
    asio::ip::tcp::socket socket_;
    std::string request_;
    asio::streambuf response_buffer_;
    std::function<void(const std::string&)> response_handler_;
    
    void connect_to_server(const asio::ip::tcp::resolver::results_type& endpoints) {
        asio::async_connect(socket_, endpoints,
            [this](const std::error_code& error, const asio::ip::tcp::endpoint& endpoint) {
                if (!error) {
                    std::cout << "Connected to: " << endpoint << std::endl;
                    send_request();
                } else {
                    std::cerr << "Connect error: " << error.message() << std::endl;
                }
            });
    }
    
    void send_request() {
        asio::async_write(socket_, asio::buffer(request_),
            [this](const std::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    std::cout << "Sent request (" << bytes_transferred << " bytes)" << std::endl;
                    read_response();
                } else {
                    std::cerr << "Write error: " << error.message() << std::endl;
                }
            });
    }
    
    void read_response() {
        asio::async_read_until(socket_, response_buffer_, "\\r\\n\\r\\n",
            [this](const std::error_code& error, std::size_t bytes_transferred) {
                if (!error) {
                    // Read the rest of the response
                    read_response_body();
                } else {
                    std::cerr << "Read error: " << error.message() << std::endl;
                }
            });
    }
    
    void read_response_body() {
        asio::async_read(socket_, response_buffer_,
            [this](const std::error_code& error, std::size_t bytes_transferred) {
                // EOF is expected when server closes connection
                if (error != asio::error::eof && error) {
                    std::cerr << "Read body error: " << error.message() << std::endl;
                    return;
                }
                
                // Convert response to string
                std::string response(
                    asio::buffers_begin(response_buffer_.data()),
                    asio::buffers_end(response_buffer_.data()));
                
                if (response_handler_) {
                    response_handler_(response);
                }
            });
    }
};

} // namespace integration_examples

/**
 * @brief Demo HTTP server
 */
void run_http_server_demo() {
    try {
        asio::io_context io_context;
        integration_examples::http_server server(io_context, 8080);
        
        std::cout << "\\nHTTP Server started. Try:" << std::endl;
        std::cout << "  curl http://localhost:8080/" << std::endl;
        std::cout << "  curl http://localhost:8080/hello" << std::endl;
        std::cout << "  curl http://localhost:8080/time" << std::endl;
        std::cout << "  curl http://localhost:8080/info" << std::endl;
        
        // Run for demo duration
        std::thread server_thread([&io_context]() {
            io_context.run();
        });
        
        std::this_thread::sleep_for(std::chrono::seconds(60)); // Run for 1 minute
        
        io_context.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "HTTP server demo error: " << e.what() << std::endl;
    }
}

/**
 * @brief Demo HTTP client
 */
void run_http_client_demo() {
    try {
        asio::io_context io_context;
        integration_examples::http_client client(io_context);
        
        // Make HTTP request
        client.get("httpbin.org", "80", "/get",
            [](const std::string& response) {
                std::cout << "HTTP Response received:" << std::endl;
                std::cout << response << std::endl;
            });
        
        // Run io_context
        std::thread io_thread([&io_context]() {
            io_context.run();
        });
        
        std::this_thread::sleep_for(std::chrono::seconds(10));
        
        io_context.stop();
        if (io_thread.joinable()) {
            io_thread.join();
        }
        
    } catch (const std::exception& e) {
        std::cerr << "HTTP client demo error: " << e.what() << std::endl;
    }
}

int main() {
    std::cout << "=== HTTP Client/Server Integration Example ===" << std::endl;
    std::cout << "This example demonstrates:" << std::endl;
    std::cout << "- HTTP protocol implementation using ASIO sockets" << std::endl;
    std::cout << "- Request/response parsing and handling" << std::endl;
    std::cout << "- Integration of multiple socket components" << std::endl;
    std::cout << "- Real-world async programming patterns" << std::endl;
    std::cout << "=============================================" << std::endl;
    
    std::cout << "\\nChoose demo mode:" << std::endl;
    std::cout << "1. Run HTTP server (default)" << std::endl;
    std::cout << "2. Run HTTP client test" << std::endl;
    std::cout << "\\nUsage: ./program [server|client]" << std::endl;
    
    // Default to server demo
    // run_http_server_demo();
    
    return 0;
}

/**
 * Integration Concepts Demonstrated:
 * 
 * 1. **Protocol Layering**:
 *    - HTTP built on top of TCP sockets
 *    - Request/response abstraction
 *    - Connection management
 * 
 * 2. **Component Integration**:
 *    - Socket + Resolver + Buffer management
 *    - Async operations coordination
 *    - Error handling across layers
 * 
 * 3. **Real-World Patterns**:
 *    - Connection pooling concepts
 *    - Request routing
 *    - Content-Type handling
 *    - Keep-alive vs connection close
 * 
 * 4. **Async Programming**:
 *    - Callback chains for complex operations
 *    - Lifetime management with shared_ptr
 *    - Graceful error handling
 * 
 * 5. **Performance Considerations**:
 *    - Streaming reads for large content
 *    - Buffer management
 *    - Connection reuse opportunities
 * 
 * Extensions to Consider:
 * - HTTP/1.1 keep-alive support
 * - Chunked transfer encoding
 * - SSL/TLS support (HTTPS)
 * - WebSocket upgrade
 * - HTTP/2 multiplexing
 * - Request/response compression
 * - Session management
 * - Load balancing
 */