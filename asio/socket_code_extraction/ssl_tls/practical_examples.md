# SSL/TLS Practical Examples and Usage Patterns

## Basic SSL Client

### Simple HTTPS Client

```cpp
#include <iostream>
#include <string>
#include "asio.hpp"
#include "asio/ssl.hpp"

class simple_https_client
{
    asio::io_context& io_context_;
    asio::ssl::context ssl_context_;
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;
    asio::ip::tcp::resolver resolver_;
    std::string request_;
    std::string response_;
    std::array<char, 1024> buffer_;

public:
    simple_https_client(asio::io_context& io_context)
        : io_context_(io_context),
          ssl_context_(asio::ssl::context::sslv23),
          ssl_socket_(io_context, ssl_context_),
          resolver_(io_context)
    {
        // Configure SSL context
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
    }

    void request(const std::string& server, const std::string& path)
    {
        // Prepare HTTP request
        request_ = "GET " + path + " HTTP/1.1\r\n";
        request_ += "Host: " + server + "\r\n";
        request_ += "Connection: close\r\n\r\n";

        // Start hostname resolution
        resolver_.async_resolve(server, "443",
            [this](const asio::error_code& ec, 
                   asio::ip::tcp::resolver::results_type endpoints)
            {
                if (!ec) {
                    connect(endpoints);
                } else {
                    std::cerr << "Resolve error: " << ec.message() << std::endl;
                }
            });
    }

private:
    void connect(const asio::ip::tcp::resolver::results_type& endpoints)
    {
        asio::async_connect(ssl_socket_.lowest_layer(), endpoints,
            [this](const asio::error_code& ec, const asio::ip::tcp::endpoint&)
            {
                if (!ec) {
                    handshake();
                } else {
                    std::cerr << "Connect error: " << ec.message() << std::endl;
                }
            });
    }

    void handshake()
    {
        ssl_socket_.async_handshake(asio::ssl::stream_base::client,
            [this](const asio::error_code& ec)
            {
                if (!ec) {
                    write_request();
                } else {
                    std::cerr << "Handshake error: " << ec.message() << std::endl;
                }
            });
    }

    void write_request()
    {
        asio::async_write(ssl_socket_, asio::buffer(request_),
            [this](const asio::error_code& ec, std::size_t /*length*/)
            {
                if (!ec) {
                    read_response();
                } else {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                }
            });
    }

    void read_response()
    {
        ssl_socket_.async_read_some(asio::buffer(buffer_),
            [this](const asio::error_code& ec, std::size_t length)
            {
                if (!ec) {
                    response_.append(buffer_.data(), length);
                    read_response(); // Continue reading
                } else if (ec != asio::error::eof) {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                } else {
                    // End of response
                    std::cout << "Response:\n" << response_ << std::endl;
                }
            });
    }
};

int main()
{
    try {
        asio::io_context io_context;
        simple_https_client client(io_context);
        client.request("httpbin.org", "/get");
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
```

## SSL Server Implementation

### Basic HTTPS Server

```cpp
#include <iostream>
#include <memory>
#include <string>
#include "asio.hpp"
#include "asio/ssl.hpp"

class ssl_session : public std::enable_shared_from_this<ssl_session>
{
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;
    std::array<char, 1024> buffer_;
    std::string response_;

public:
    ssl_session(asio::ssl::stream<asio::ip::tcp::socket> socket)
        : ssl_socket_(std::move(socket))
    {
        // Prepare HTTP response
        response_ = "HTTP/1.1 200 OK\r\n";
        response_ += "Content-Type: text/html\r\n";
        response_ += "Content-Length: 13\r\n";
        response_ += "Connection: close\r\n\r\n";
        response_ += "Hello, World!";
    }

    void start()
    {
        do_handshake();
    }

private:
    void do_handshake()
    {
        auto self(shared_from_this());
        ssl_socket_.async_handshake(asio::ssl::stream_base::server,
            [this, self](const asio::error_code& ec)
            {
                if (!ec) {
                    do_read();
                } else {
                    std::cerr << "Handshake failed: " << ec.message() << std::endl;
                }
            });
    }

    void do_read()
    {
        auto self(shared_from_this());
        ssl_socket_.async_read_some(asio::buffer(buffer_),
            [this, self](const asio::error_code& ec, std::size_t length)
            {
                if (!ec) {
                    // Process request (simplified - just echo)
                    std::cout << "Received: " << std::string(buffer_.data(), length) << std::endl;
                    do_write();
                } else if (ec != asio::error::eof) {
                    std::cerr << "Read error: " << ec.message() << std::endl;
                }
            });
    }

    void do_write()
    {
        auto self(shared_from_this());
        asio::async_write(ssl_socket_, asio::buffer(response_),
            [this, self](const asio::error_code& ec, std::size_t /*length*/)
            {
                if (!ec) {
                    // Graceful SSL shutdown
                    ssl_socket_.async_shutdown(
                        [this, self](const asio::error_code& /*ec*/)
                        {
                            // Connection will be closed automatically
                        });
                } else {
                    std::cerr << "Write error: " << ec.message() << std::endl;
                }
            });
    }
};

class ssl_server
{
    asio::ip::tcp::acceptor acceptor_;
    asio::ssl::context ssl_context_;

public:
    ssl_server(asio::io_context& io_context, short port)
        : acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
          ssl_context_(asio::ssl::context::sslv23)
    {
        // Configure SSL context
        ssl_context_.set_options(
            asio::ssl::context::default_workarounds |
            asio::ssl::context::no_sslv2 |
            asio::ssl::context::single_dh_use);

        // Set up server certificate and private key
        ssl_context_.use_certificate_chain_file("server.pem");
        ssl_context_.use_private_key_file("server.pem", asio::ssl::context::pem);

        do_accept();
    }

private:
    void do_accept()
    {
        acceptor_.async_accept(
            [this](const asio::error_code& ec, asio::ip::tcp::socket socket)
            {
                if (!ec) {
                    std::make_shared<ssl_session>(
                        asio::ssl::stream<asio::ip::tcp::socket>(
                            std::move(socket), ssl_context_))->start();
                }
                do_accept();
            });
    }
};

int main()
{
    try {
        asio::io_context io_context;
        ssl_server server(io_context, 8443);
        std::cout << "SSL server listening on port 8443" << std::endl;
        io_context.run();
    } catch (std::exception& e) {
        std::cerr << "Exception: " << e.what() << std::endl;
    }
    return 0;
}
```

## Advanced Client with Custom Verification

### Client with Certificate Pinning

```cpp
class pinned_certificate_client
{
    asio::io_context& io_context_;
    asio::ssl::context ssl_context_;
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;
    asio::ip::tcp::resolver resolver_;
    std::string expected_fingerprint_;

public:
    pinned_certificate_client(asio::io_context& io_context, 
                             const std::string& expected_fingerprint)
        : io_context_(io_context),
          ssl_context_(asio::ssl::context::sslv23),
          ssl_socket_(io_context, ssl_context_),
          resolver_(io_context),
          expected_fingerprint_(expected_fingerprint)
    {
        // Configure SSL context
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
        ssl_context_.set_verify_callback(
            [this](bool preverified, asio::ssl::verify_context& ctx) -> bool
            {
                return verify_certificate_pinning(preverified, ctx);
            });
    }

    void connect(const std::string& host, const std::string& port)
    {
        resolver_.async_resolve(host, port,
            [this, host](const asio::error_code& ec, 
                        asio::ip::tcp::resolver::results_type endpoints)
            {
                if (!ec) {
                    asio::async_connect(ssl_socket_.lowest_layer(), endpoints,
                        [this, host](const asio::error_code& connect_ec, 
                                    const asio::ip::tcp::endpoint&)
                        {
                            if (!connect_ec) {
                                // Set hostname verification
                                ssl_socket_.set_verify_callback(
                                    asio::ssl::host_name_verification(host));
                                
                                ssl_socket_.async_handshake(
                                    asio::ssl::stream_base::client,
                                    [this](const asio::error_code& handshake_ec)
                                    {
                                        if (handshake_ec) {
                                            std::cerr << "Handshake failed: " 
                                                     << handshake_ec.message() << std::endl;
                                        } else {
                                            std::cout << "Secure connection established!" << std::endl;
                                        }
                                    });
                            }
                        });
                }
            });
    }

private:
    bool verify_certificate_pinning(bool preverified, asio::ssl::verify_context& ctx)
    {
        // Get the certificate
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        if (!cert) {
            return false;
        }

        // Calculate SHA-256 fingerprint
        unsigned char fingerprint[EVP_MAX_MD_SIZE];
        unsigned int fingerprint_len;
        
        if (!X509_digest(cert, EVP_sha256(), fingerprint, &fingerprint_len)) {
            return false;
        }

        // Convert to hex string
        std::stringstream hex_stream;
        for (unsigned int i = 0; i < fingerprint_len; ++i) {
            hex_stream << std::hex << std::setw(2) << std::setfill('0') 
                      << static_cast<int>(fingerprint[i]);
        }
        
        std::string calculated_fingerprint = hex_stream.str();
        
        // Log verification details
        char subject_name[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        std::cout << "Verifying certificate: " << subject_name << std::endl;
        std::cout << "Expected fingerprint: " << expected_fingerprint_ << std::endl;
        std::cout << "Calculated fingerprint: " << calculated_fingerprint << std::endl;

        // Check if fingerprint matches
        bool fingerprint_match = (calculated_fingerprint == expected_fingerprint_);
        
        return preverified && fingerprint_match;
    }
};
```

## Mutual TLS (mTLS) Implementation

### mTLS Client

```cpp
class mtls_client
{
    asio::io_context& io_context_;
    asio::ssl::context ssl_context_;
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;

public:
    mtls_client(asio::io_context& io_context)
        : io_context_(io_context),
          ssl_context_(asio::ssl::context::sslv23),
          ssl_socket_(io_context, ssl_context_)
    {
        // Configure client certificate
        ssl_context_.use_certificate_chain_file("client.pem");
        ssl_context_.use_private_key_file("client.pem", asio::ssl::context::pem);
        
        // Configure CA for server verification
        ssl_context_.load_verify_file("ca.pem");
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
        
        // Optional: Set password callback for encrypted private key
        ssl_context_.set_password_callback(
            [](std::size_t max_length, asio::ssl::context_base::password_purpose purpose)
            {
                return "client_key_password";
            });
    }

    void connect_to_server(const std::string& host, const std::string& port)
    {
        asio::ip::tcp::resolver resolver(io_context_);
        auto endpoints = resolver.resolve(host, port);
        
        asio::async_connect(ssl_socket_.lowest_layer(), endpoints,
            [this](const asio::error_code& ec, const asio::ip::tcp::endpoint&)
            {
                if (!ec) {
                    ssl_socket_.async_handshake(asio::ssl::stream_base::client,
                        [this](const asio::error_code& handshake_ec)
                        {
                            if (!handshake_ec) {
                                std::cout << "mTLS handshake successful!" << std::endl;
                                // Now ready for secure communication
                            } else {
                                std::cerr << "mTLS handshake failed: " 
                                         << handshake_ec.message() << std::endl;
                            }
                        });
                }
            });
    }
};
```

### mTLS Server

```cpp
class mtls_server
{
    asio::ip::tcp::acceptor acceptor_;
    asio::ssl::context ssl_context_;

public:
    mtls_server(asio::io_context& io_context, short port)
        : acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
          ssl_context_(asio::ssl::context::sslv23)
    {
        // Configure server certificate
        ssl_context_.use_certificate_chain_file("server.pem");
        ssl_context_.use_private_key_file("server.pem", asio::ssl::context::pem);
        
        // Configure client certificate verification
        ssl_context_.load_verify_file("client_ca.pem");
        ssl_context_.set_verify_mode(
            asio::ssl::verify_peer | 
            asio::ssl::verify_fail_if_no_peer_cert);
        
        // Custom client certificate verification
        ssl_context_.set_verify_callback(
            [this](bool preverified, asio::ssl::verify_context& ctx) -> bool
            {
                return verify_client_certificate(preverified, ctx);
            });

        do_accept();
    }

private:
    bool verify_client_certificate(bool preverified, asio::ssl::verify_context& ctx)
    {
        if (!preverified) {
            return false;
        }

        // Additional custom verification logic
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
        
        // Log client certificate details
        char subject_name[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        std::cout << "Client certificate at depth " << depth 
                  << ": " << subject_name << std::endl;

        // For peer certificate (depth 0), check additional constraints
        if (depth == 0) {
            // Example: Check if client certificate has required CN
            X509_NAME* subject = X509_get_subject_name(cert);
            char cn[256];
            int cn_len = X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));
            
            if (cn_len > 0) {
                std::string client_cn(cn, cn_len);
                std::cout << "Client CN: " << client_cn << std::endl;
                
                // Allow specific clients
                return (client_cn == "authorized_client" || 
                       client_cn == "another_authorized_client");
            }
        }

        return true;
    }

    void do_accept()
    {
        acceptor_.async_accept(
            [this](const asio::error_code& ec, asio::ip::tcp::socket socket)
            {
                if (!ec) {
                    auto session = std::make_shared<mtls_session>(
                        asio::ssl::stream<asio::ip::tcp::socket>(
                            std::move(socket), ssl_context_));
                    session->start();
                }
                do_accept();
            });
    }
};

class mtls_session : public std::enable_shared_from_this<mtls_session>
{
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;
    std::array<char, 1024> buffer_;

public:
    mtls_session(asio::ssl::stream<asio::ip::tcp::socket> socket)
        : ssl_socket_(std::move(socket)) {}

    void start()
    {
        auto self(shared_from_this());
        ssl_socket_.async_handshake(asio::ssl::stream_base::server,
            [this, self](const asio::error_code& ec)
            {
                if (!ec) {
                    std::cout << "mTLS server handshake successful!" << std::endl;
                    
                    // Extract client certificate information
                    SSL* ssl = ssl_socket_.native_handle();
                    X509* client_cert = SSL_get_peer_certificate(ssl);
                    if (client_cert) {
                        char subject[256];
                        X509_NAME_oneline(X509_get_subject_name(client_cert), subject, 256);
                        std::cout << "Authenticated client: " << subject << std::endl;
                        X509_free(client_cert);
                    }
                    
                    do_read();
                } else {
                    std::cerr << "mTLS server handshake failed: " 
                             << ec.message() << std::endl;
                }
            });
    }

private:
    void do_read()
    {
        auto self(shared_from_this());
        ssl_socket_.async_read_some(asio::buffer(buffer_),
            [this, self](const asio::error_code& ec, std::size_t length)
            {
                if (!ec) {
                    std::cout << "Received from authenticated client: " 
                             << std::string(buffer_.data(), length) << std::endl;
                    // Echo back
                    asio::async_write(ssl_socket_, asio::buffer(buffer_, length),
                        [this, self](const asio::error_code& write_ec, std::size_t)
                        {
                            if (!write_ec) {
                                do_read();
                            }
                        });
                }
            });
    }
};
```

## WebSocket over SSL

### WSS Client Implementation

```cpp
#include "asio.hpp"
#include "asio/ssl.hpp"
#include <beast/core.hpp>
#include <beast/websocket.hpp>
#include <beast/websocket/ssl.hpp>

class wss_client
{
    asio::io_context& io_context_;
    asio::ssl::context ssl_context_;
    beast::websocket::stream<asio::ssl::stream<asio::ip::tcp::socket>> ws_stream_;
    asio::ip::tcp::resolver resolver_;
    
public:
    wss_client(asio::io_context& io_context)
        : io_context_(io_context),
          ssl_context_(asio::ssl::context::sslv23_client),
          ws_stream_(io_context, ssl_context_),
          resolver_(io_context)
    {
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
    }

    void connect(const std::string& host, const std::string& port, const std::string& target)
    {
        resolver_.async_resolve(host, port,
            [this, host, target](const asio::error_code& ec, 
                                asio::ip::tcp::resolver::results_type results)
            {
                if (!ec) {
                    // Connect to TCP endpoint
                    beast::get_lowest_layer(ws_stream_).async_connect(results,
                        [this, host, target](const asio::error_code& connect_ec,
                                            asio::ip::tcp::resolver::results_type::endpoint_type)
                        {
                            if (!connect_ec) {
                                // Perform SSL handshake
                                ws_stream_.next_layer().async_handshake(
                                    asio::ssl::stream_base::client,
                                    [this, host, target](const asio::error_code& handshake_ec)
                                    {
                                        if (!handshake_ec) {
                                            // Perform WebSocket handshake
                                            ws_stream_.async_handshake(host, target,
                                                [this](const asio::error_code& ws_ec)
                                                {
                                                    if (!ws_ec) {
                                                        std::cout << "WSS connection established!" << std::endl;
                                                        start_read();
                                                    }
                                                });
                                        }
                                    });
                            }
                        });
                }
            });
    }

    void send_message(const std::string& message)
    {
        ws_stream_.async_write(asio::buffer(message),
            [](const asio::error_code& ec, std::size_t bytes_transferred)
            {
                if (!ec) {
                    std::cout << "Sent " << bytes_transferred << " bytes" << std::endl;
                }
            });
    }

private:
    void start_read()
    {
        auto buffer = std::make_shared<beast::flat_buffer>();
        ws_stream_.async_read(*buffer,
            [this, buffer](const asio::error_code& ec, std::size_t bytes_transferred)
            {
                if (!ec) {
                    std::cout << "Received: " << beast::make_printable(buffer->data()) << std::endl;
                    buffer->consume(bytes_transferred);
                    start_read();
                }
            });
    }
};
```

## Performance-Optimized SSL Server

### High-Performance SSL Echo Server

```cpp
class high_performance_ssl_server
{
    asio::io_context& io_context_;
    asio::ip::tcp::acceptor acceptor_;
    asio::ssl::context ssl_context_;
    std::atomic<size_t> connection_count_{0};

public:
    high_performance_ssl_server(asio::io_context& io_context, short port)
        : io_context_(io_context),
          acceptor_(io_context, asio::ip::tcp::endpoint(asio::ip::tcp::v4(), port)),
          ssl_context_(asio::ssl::context::tls_server)
    {
        configure_ssl_context();
        
        // Set socket options for performance
        acceptor_.set_option(asio::ip::tcp::acceptor::reuse_address(true));
        
        start_accept();
    }

private:
    void configure_ssl_context()
    {
        // Security options
        ssl_context_.set_options(
            asio::ssl::context::default_workarounds |
            asio::ssl::context::no_sslv2 |
            asio::ssl::context::no_sslv3 |
            asio::ssl::context::no_tlsv1 |
            asio::ssl::context::single_dh_use);

        // Load certificates
        ssl_context_.use_certificate_chain_file("server.pem");
        ssl_context_.use_private_key_file("server.pem", asio::ssl::context::pem);
        ssl_context_.use_tmp_dh_file("dh2048.pem");

        // Cipher configuration for performance
        SSL_CTX_set_cipher_list(ssl_context_.native_handle(), 
            "ECDHE+AESGCM:ECDHE+CHACHA20:DHE+AESGCM:DHE+CHACHA20:!aNULL:!MD5:!DSS");
    }

    void start_accept()
    {
        acceptor_.async_accept(
            [this](const asio::error_code& ec, asio::ip::tcp::socket socket)
            {
                if (!ec) {
                    // Configure socket for performance
                    socket.set_option(asio::ip::tcp::no_delay(true));
                    socket.set_option(asio::socket_base::keep_alive(true));
                    
                    auto session = std::make_shared<optimized_ssl_session>(
                        std::move(socket), ssl_context_, connection_count_);
                    session->start();
                }
                start_accept();
            });
    }
};

class optimized_ssl_session : public std::enable_shared_from_this<optimized_ssl_session>
{
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;
    std::atomic<size_t>& connection_count_;
    std::vector<char> buffer_;
    static constexpr size_t buffer_size = 64 * 1024; // 64KB buffer

public:
    optimized_ssl_session(asio::ip::tcp::socket socket, 
                         asio::ssl::context& context,
                         std::atomic<size_t>& conn_count)
        : ssl_socket_(std::move(socket), context),
          connection_count_(conn_count),
          buffer_(buffer_size)
    {
        ++connection_count_;
    }

    ~optimized_ssl_session()
    {
        --connection_count_;
    }

    void start()
    {
        // Set timeouts
        ssl_socket_.lowest_layer().set_option(
            asio::detail::socket_option::integer<SOL_SOCKET, SO_RCVTIMEO>{30000});
        ssl_socket_.lowest_layer().set_option(
            asio::detail::socket_option::integer<SOL_SOCKET, SO_SNDTIMEO>{30000});

        auto self(shared_from_this());
        ssl_socket_.async_handshake(asio::ssl::stream_base::server,
            [this, self](const asio::error_code& ec)
            {
                if (!ec) {
                    start_read();
                }
            });
    }

private:
    void start_read()
    {
        auto self(shared_from_this());
        ssl_socket_.async_read_some(asio::buffer(buffer_),
            [this, self](const asio::error_code& ec, std::size_t length)
            {
                if (!ec && length > 0) {
                    // Echo the data back
                    asio::async_write(ssl_socket_, asio::buffer(buffer_, length),
                        [this, self](const asio::error_code& write_ec, std::size_t)
                        {
                            if (!write_ec) {
                                start_read();
                            }
                        });
                }
            });
    }
};
```

## Error Handling Best Practices

### Comprehensive Error Handling

```cpp
class robust_ssl_client
{
    asio::io_context& io_context_;
    asio::ssl::context ssl_context_;
    asio::ssl::stream<asio::ip::tcp::socket> ssl_socket_;
    asio::steady_timer retry_timer_;
    int retry_count_{0};
    static constexpr int max_retries = 3;

public:
    robust_ssl_client(asio::io_context& io_context)
        : io_context_(io_context),
          ssl_context_(asio::ssl::context::tls_client),
          ssl_socket_(io_context, ssl_context_),
          retry_timer_(io_context)
    {
        configure_ssl_context();
    }

    void connect_with_retry(const std::string& host, const std::string& port)
    {
        asio::ip::tcp::resolver resolver(io_context_);
        
        try {
            auto endpoints = resolver.resolve(host, port);
            attempt_connect(endpoints, host);
        } catch (const std::exception& e) {
            handle_error("DNS resolution failed", e);
        }
    }

private:
    void configure_ssl_context()
    {
        ssl_context_.set_default_verify_paths();
        ssl_context_.set_verify_mode(asio::ssl::verify_peer);
        
        // Custom error handling in verification
        ssl_context_.set_verify_callback(
            [this](bool preverified, asio::ssl::verify_context& ctx) -> bool
            {
                try {
                    return verify_certificate(preverified, ctx);
                } catch (const std::exception& e) {
                    std::cerr << "Certificate verification error: " << e.what() << std::endl;
                    return false;
                }
            });
    }

    bool verify_certificate(bool preverified, asio::ssl::verify_context& ctx)
    {
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        int error = X509_STORE_CTX_get_error(ctx.native_handle());
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());

        // Log verification details
        char subject_name[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subject_name, 256);
        
        std::cout << "Verifying certificate at depth " << depth 
                  << ": " << subject_name << std::endl;

        if (!preverified) {
            std::cerr << "Certificate verification failed: " 
                      << X509_verify_cert_error_string(error) << std::endl;
            
            // Handle specific errors
            switch (error) {
            case X509_V_ERR_CERT_HAS_EXPIRED:
                std::cerr << "Certificate has expired" << std::endl;
                break;
            case X509_V_ERR_CERT_NOT_YET_VALID:
                std::cerr << "Certificate is not yet valid" << std::endl;
                break;
            case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
                std::cerr << "Self-signed certificate in chain" << std::endl;
                break;
            default:
                std::cerr << "Other certificate error" << std::endl;
                break;
            }
        }

        return preverified;
    }

    void attempt_connect(const asio::ip::tcp::resolver::results_type& endpoints,
                        const std::string& host)
    {
        asio::async_connect(ssl_socket_.lowest_layer(), endpoints,
            [this, host](const asio::error_code& ec, const asio::ip::tcp::endpoint& endpoint)
            {
                if (!ec) {
                    std::cout << "Connected to " << endpoint << std::endl;
                    perform_handshake(host);
                } else {
                    handle_connect_error(ec, host, endpoints);
                }
            });
    }

    void perform_handshake(const std::string& host)
    {
        // Set hostname for SNI
        SSL_set_tlsext_host_name(ssl_socket_.native_handle(), host.c_str());
        
        ssl_socket_.async_handshake(asio::ssl::stream_base::client,
            [this](const asio::error_code& ec)
            {
                if (!ec) {
                    std::cout << "SSL handshake successful!" << std::endl;
                    retry_count_ = 0; // Reset retry count on success
                    
                    // Log connection details
                    log_ssl_info();
                } else {
                    handle_handshake_error(ec);
                }
            });
    }

    void handle_connect_error(const asio::error_code& ec,
                             const std::string& host,
                             const asio::ip::tcp::resolver::results_type& endpoints)
    {
        std::cerr << "Connection failed: " << ec.message() << std::endl;
        
        if (retry_count_ < max_retries) {
            std::cout << "Retrying connection in 5 seconds..." << std::endl;
            schedule_retry([this, endpoints, host]() {
                attempt_connect(endpoints, host);
            });
        } else {
            std::cerr << "Max retries exceeded. Giving up." << std::endl;
        }
    }

    void handle_handshake_error(const asio::error_code& ec)
    {
        std::cerr << "SSL handshake failed: " << ec.message() << std::endl;
        
        // Analyze specific SSL errors
        if (ec.category() == asio::error::get_ssl_category()) {
            std::cerr << "SSL-specific error occurred" << std::endl;
            
            // Get detailed OpenSSL error
            unsigned long ssl_error = ERR_get_error();
            if (ssl_error != 0) {
                char error_buffer[256];
                ERR_error_string_n(ssl_error, error_buffer, sizeof(error_buffer));
                std::cerr << "OpenSSL error: " << error_buffer << std::endl;
            }
        }
        
        if (retry_count_ < max_retries) {
            std::cout << "Retrying handshake in 3 seconds..." << std::endl;
            schedule_retry([this]() {
                // Close and reconnect
                ssl_socket_.lowest_layer().close();
                // Would need to re-establish TCP connection
            });
        }
    }

    void schedule_retry(std::function<void()> retry_func)
    {
        ++retry_count_;
        retry_timer_.expires_after(std::chrono::seconds(retry_count_ * 2)); // Exponential backoff
        retry_timer_.async_wait([retry_func](const asio::error_code& timer_ec) {
            if (!timer_ec) {
                retry_func();
            }
        });
    }

    void log_ssl_info()
    {
        SSL* ssl = ssl_socket_.native_handle();
        
        // Log protocol version
        std::cout << "SSL Protocol: " << SSL_get_version(ssl) << std::endl;
        
        // Log cipher suite
        const char* cipher = SSL_get_cipher(ssl);
        if (cipher) {
            std::cout << "Cipher Suite: " << cipher << std::endl;
        }
        
        // Log peer certificate info
        X509* peer_cert = SSL_get_peer_certificate(ssl);
        if (peer_cert) {
            char subject[256];
            X509_NAME_oneline(X509_get_subject_name(peer_cert), subject, 256);
            std::cout << "Peer Certificate Subject: " << subject << std::endl;
            X509_free(peer_cert);
        }
    }

    void handle_error(const std::string& context, const std::exception& e)
    {
        std::cerr << context << ": " << e.what() << std::endl;
    }
};
```

These practical examples demonstrate real-world usage patterns for ASIO's SSL/TLS implementation, covering everything from basic connections to advanced features like certificate pinning, mutual TLS, and robust error handling.