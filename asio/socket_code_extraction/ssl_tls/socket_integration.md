# SSL/TLS Socket Integration

## Stream Composition Architecture

### Layered Stream Design

```cpp
template <typename Stream>
class ssl::stream : public stream_base, private noncopyable
{
    Stream next_layer_;              // Underlying transport layer
    detail::stream_core core_;       // SSL state and buffer management
    
public:
    typedef remove_reference_t<Stream> next_layer_type;
    typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
    typedef typename lowest_layer_type::executor_type executor_type;
};
```

**Design benefits:**
- **Transparent composition**: SSL layer wraps any stream type
- **Type preservation**: Maintains underlying stream characteristics
- **Executor consistency**: Uses same executor as underlying stream

### Common Integration Patterns

#### TCP Socket Integration

```cpp
using tcp_ssl_stream = ssl::stream<tcp::socket>;

// Construction
io_context io_ctx;
ssl::context ssl_ctx(ssl::context::tls_client);
tcp_ssl_stream ssl_socket(io_ctx, ssl_ctx);

// Access layers
tcp::socket& tcp_sock = ssl_socket.next_layer();
tcp::socket& lowest = ssl_socket.lowest_layer();
```

#### UNIX Domain Socket Integration

```cpp
using unix_ssl_stream = ssl::stream<local::stream_protocol::socket>;

unix_ssl_stream ssl_unix_socket(io_ctx, ssl_ctx);
```

#### Serial Port Integration

```cpp
using serial_ssl_stream = ssl::stream<serial_port>;

serial_ssl_stream ssl_serial(io_ctx, ssl_ctx);
ssl_serial.next_layer().open("/dev/ttyUSB0");
```

## Stream Interface Compatibility

### Read Operations

```cpp
// Synchronous read
template <typename MutableBufferSequence>
std::size_t read_some(const MutableBufferSequence& buffers)
{
    asio::error_code ec;
    std::size_t n = read_some(buffers, ec);
    asio::detail::throw_error(ec, "read_some");
    return n;
}

// Asynchronous read
template <typename MutableBufferSequence, typename ReadToken>
auto async_read_some(const MutableBufferSequence& buffers, ReadToken&& token)
    -> decltype(async_initiate<ReadToken, void (asio::error_code, std::size_t)>(...))
{
    return async_initiate<ReadToken, void (asio::error_code, std::size_t)>(
        initiate_async_read_some(this), token, buffers);
}
```

### Write Operations

```cpp
// Synchronous write
template <typename ConstBufferSequence>
std::size_t write_some(const ConstBufferSequence& buffers)
{
    asio::error_code ec;
    std::size_t n = write_some(buffers, ec);
    asio::detail::throw_error(ec, "write_some");
    return n;
}

// Asynchronous write
template <typename ConstBufferSequence, typename WriteToken>
auto async_write_some(const ConstBufferSequence& buffers, WriteToken&& token)
    -> decltype(async_initiate<WriteToken, void (asio::error_code, std::size_t)>(...))
{
    return async_initiate<WriteToken, void (asio::error_code, std::size_t)>(
        initiate_async_write_some(this), token, buffers);
}
```

### Stream Concepts Compliance

The SSL stream satisfies standard ASIO stream concepts:

- **SyncReadStream**: Blocking read operations
- **SyncWriteStream**: Blocking write operations  
- **AsyncReadStream**: Non-blocking read operations
- **AsyncWriteStream**: Non-blocking write operations
- **Stream**: Combined read/write operations

## Executor Integration

### Executor Propagation

```cpp
executor_type get_executor() noexcept
{
    return next_layer_.lowest_layer().get_executor();
}
```

**Key principles:**
- **Single executor**: All operations use the same executor
- **Lowest layer binding**: Executor comes from transport layer
- **Consistency**: Ensures proper operation ordering

### Strand Integration

```cpp
// Using strand for thread safety
auto strand = make_strand(io_context);
ssl::stream<tcp::socket> ssl_socket(strand, ssl_ctx);

// All operations automatically serialized
ssl_socket.async_handshake(ssl::stream_base::client, 
    [](const error_code& ec) { /* handler */ });
```

## Buffer Management Integration

### Internal Buffer Architecture

```cpp
struct stream_core
{
    enum { max_tls_record_size = 17 * 1024 };
    
    // SSL engine for cryptographic operations
    engine engine_;
    
    // Timers for operation coordination
    asio::steady_timer pending_read_;
    asio::steady_timer pending_write_;
    
    // Buffer spaces
    std::vector<unsigned char> output_buffer_space_;
    asio::mutable_buffer output_buffer_;
    std::vector<unsigned char> input_buffer_space_;
    asio::mutable_buffer input_buffer_;
    asio::const_buffer input_;
};
```

### Buffer Flow Architecture

```
Application Buffers
        ↓
    SSL Layer
   (Encryption/Decryption)
        ↓
   Internal Buffers
        ↓
  Transport Layer
   (TCP Socket)
        ↓
    Network
```

### Read Operation Flow

1. **Application calls**: `ssl_stream.read_some(app_buffer)`
2. **SSL decryption**: Decrypt data from internal buffer
3. **Transport read**: If more data needed, read from socket
4. **Buffer management**: Store encrypted data in input buffer
5. **Completion**: Return decrypted data to application

### Write Operation Flow

1. **Application calls**: `ssl_stream.write_some(app_buffer)`
2. **SSL encryption**: Encrypt application data
3. **Buffer staging**: Store encrypted data in output buffer
4. **Transport write**: Write encrypted data to socket
5. **Completion**: Return bytes written to application

## Handshake Integration

### Connection Establishment Pattern

```cpp
// Typical client-side flow
tcp::resolver resolver(io_context);
auto endpoints = resolver.resolve("example.com", "443");

ssl::stream<tcp::socket> ssl_socket(io_context, ssl_ctx);

// Step 1: TCP connection
asio::async_connect(ssl_socket.lowest_layer(), endpoints,
    [&ssl_socket](const error_code& ec, const tcp::endpoint&) {
        if (!ec) {
            // Step 2: SSL handshake
            ssl_socket.async_handshake(ssl::stream_base::client,
                [](const error_code& handshake_ec) {
                    if (!handshake_ec) {
                        // Ready for encrypted communication
                    }
                });
        }
    });
```

### Server-Side Handshake

```cpp
// Server acceptor setup
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 443));

acceptor.async_accept(
    [&ssl_ctx](const error_code& ec, tcp::socket socket) {
        if (!ec) {
            // Create SSL stream from accepted socket
            auto ssl_stream = std::make_shared<ssl::stream<tcp::socket>>(
                std::move(socket), ssl_ctx);
            
            // Perform server handshake
            ssl_stream->async_handshake(ssl::stream_base::server,
                [ssl_stream](const error_code& handshake_ec) {
                    if (!handshake_ec) {
                        // Start reading encrypted data
                        start_read(ssl_stream);
                    }
                });
        }
    });
```

## Error Handling Integration

### Error Code Propagation

```cpp
// SSL errors
namespace ssl { namespace error {
    enum stream_errors {
        stream_truncated = 1,
        unspecified_system_error = 2,
        unexpected_result = 3
    };
}}

// Transport errors passed through
void handle_ssl_operation_error(const error_code& ec) {
    if (ec.category() == ssl::error::get_stream_category()) {
        // SSL-specific error
        handle_ssl_error(ec);
    } else if (ec.category() == asio::error::get_system_category()) {
        // Transport layer error
        handle_transport_error(ec);
    }
}
```

### Graceful Error Recovery

```cpp
void handle_read_error(const error_code& ec, std::size_t bytes_transferred) {
    if (ec == ssl::error::stream_truncated) {
        // Peer closed SSL connection ungracefully
        // Treat as normal shutdown
        close_connection();
    } else if (ec == asio::error::eof) {
        // Transport layer closed
        // Attempt graceful SSL shutdown
        async_shutdown();
    } else {
        // Other errors
        handle_error(ec);
    }
}
```

## Shutdown Integration

### Graceful Shutdown Sequence

```cpp
void graceful_shutdown(ssl::stream<tcp::socket>& ssl_socket) {
    // Step 1: SSL shutdown
    ssl_socket.async_shutdown([&ssl_socket](const error_code& ssl_ec) {
        // Step 2: TCP shutdown (optional)
        ssl_socket.lowest_layer().shutdown(tcp::socket::shutdown_both);
        
        // Step 3: Close socket
        ssl_socket.lowest_layer().close();
    });
}
```

### Forced Shutdown

```cpp
void force_shutdown(ssl::stream<tcp::socket>& ssl_socket) {
    error_code ec;
    
    // Close transport immediately
    ssl_socket.lowest_layer().close(ec);
    // SSL context automatically cleaned up by destructor
}
```

## Performance Optimization

### Buffer Size Optimization

```cpp
class optimized_ssl_stream {
    ssl::stream<tcp::socket> stream_;
    std::vector<char> read_buffer_;
    
public:
    optimized_ssl_stream(io_context& io_ctx, ssl::context& ssl_ctx, size_t buffer_size = 16384)
        : stream_(io_ctx, ssl_ctx), read_buffer_(buffer_size) {}
    
    template <typename Handler>
    void async_read_optimized(Handler&& handler) {
        stream_.async_read_some(asio::buffer(read_buffer_),
            [this, handler = std::forward<Handler>(handler)]
            (const error_code& ec, std::size_t bytes_transferred) {
                // Process full buffer efficiently
                handler(ec, bytes_transferred);
            });
    }
};
```

### Zero-Copy Operations

```cpp
// Minimize copying by using buffer references
class zero_copy_ssl_handler {
    ssl::stream<tcp::socket>& stream_;
    std::vector<asio::const_buffer> write_buffers_;
    
public:
    template <typename... Buffers>
    void async_write_gather(Buffers&&... buffers) {
        write_buffers_.clear();
        (write_buffers_.push_back(asio::buffer(buffers)), ...);
        
        stream_.async_write_some(write_buffers_,
            [this](const error_code& ec, std::size_t bytes_transferred) {
                // Handle completion
            });
    }
};
```

## Advanced Integration Patterns

### SSL over Proxy

```cpp
class ssl_over_proxy {
    tcp::socket proxy_socket_;
    ssl::stream<tcp::socket&> ssl_stream_;
    
public:
    ssl_over_proxy(io_context& io_ctx, ssl::context& ssl_ctx)
        : proxy_socket_(io_ctx), ssl_stream_(proxy_socket_, ssl_ctx) {}
    
    template <typename ConnectHandler>
    void async_connect_through_proxy(
        const tcp::endpoint& proxy_endpoint,
        const std::string& target_host,
        const std::string& target_port,
        ConnectHandler&& handler) {
        
        // Step 1: Connect to proxy
        proxy_socket_.async_connect(proxy_endpoint,
            [this, target_host, target_port, handler = std::forward<ConnectHandler>(handler)]
            (const error_code& ec) {
                if (!ec) {
                    // Step 2: CONNECT request
                    send_connect_request(target_host, target_port, handler);
                } else {
                    handler(ec);
                }
            });
    }
    
private:
    template <typename Handler>
    void send_connect_request(const std::string& host, const std::string& port, Handler&& handler) {
        std::string connect_request = 
            "CONNECT " + host + ":" + port + " HTTP/1.1\r\n"
            "Host: " + host + ":" + port + "\r\n\r\n";
        
        asio::async_write(proxy_socket_, asio::buffer(connect_request),
            [this, handler = std::forward<Handler>(handler)]
            (const error_code& ec, std::size_t) {
                if (!ec) {
                    // Step 3: Read CONNECT response
                    read_connect_response(handler);
                } else {
                    handler(ec);
                }
            });
    }
    
    template <typename Handler>
    void read_connect_response(Handler&& handler) {
        // Read and parse HTTP CONNECT response
        // On success, initiate SSL handshake
        ssl_stream_.async_handshake(ssl::stream_base::client, handler);
    }
};
```

### SSL with Custom Transport

```cpp
template <typename CustomTransport>
class ssl_custom_transport {
    CustomTransport transport_;
    ssl::stream<CustomTransport&> ssl_stream_;
    
public:
    ssl_custom_transport(io_context& io_ctx, ssl::context& ssl_ctx)
        : transport_(io_ctx), ssl_stream_(transport_, ssl_ctx) {}
    
    // Expose SSL stream interface
    template <typename... Args>
    auto async_read_some(Args&&... args) 
        -> decltype(ssl_stream_.async_read_some(std::forward<Args>(args)...)) {
        return ssl_stream_.async_read_some(std::forward<Args>(args)...);
    }
    
    template <typename... Args>
    auto async_write_some(Args&&... args)
        -> decltype(ssl_stream_.async_write_some(std::forward<Args>(args)...)) {
        return ssl_stream_.async_write_some(std::forward<Args>(args)...);
    }
    
    // Custom transport configuration
    CustomTransport& transport() { return transport_; }
    ssl::stream<CustomTransport&>& ssl_stream() { return ssl_stream_; }
};
```

## Real-World Integration Examples

### HTTPS Client

```cpp
class https_client {
    io_context& io_ctx_;
    ssl::context ssl_ctx_;
    ssl::stream<tcp::socket> ssl_stream_;
    tcp::resolver resolver_;
    
public:
    https_client(io_context& io_ctx)
        : io_ctx_(io_ctx),
          ssl_ctx_(ssl::context::tls_client),
          ssl_stream_(io_ctx, ssl_ctx_),
          resolver_(io_ctx) {
        
        // Configure SSL context
        ssl_ctx_.set_default_verify_paths();
        ssl_ctx_.set_verify_mode(ssl::verify_peer);
    }
    
    template <typename ResponseHandler>
    void async_get(const std::string& host, const std::string& path, ResponseHandler&& handler) {
        // Resolve hostname
        resolver_.async_resolve(host, "443",
            [this, host, path, handler = std::forward<ResponseHandler>(handler)]
            (const error_code& ec, tcp::resolver::results_type endpoints) {
                if (!ec) {
                    async_connect(endpoints, host, path, handler);
                } else {
                    handler(ec, "");
                }
            });
    }
    
private:
    template <typename ResponseHandler>
    void async_connect(tcp::resolver::results_type endpoints, 
                      const std::string& host, const std::string& path,
                      ResponseHandler&& handler) {
        
        asio::async_connect(ssl_stream_.lowest_layer(), endpoints,
            [this, host, path, handler = std::forward<ResponseHandler>(handler)]
            (const error_code& ec, const tcp::endpoint&) {
                if (!ec) {
                    // Set hostname for verification
                    ssl_stream_.set_verify_callback(ssl::host_name_verification(host));
                    
                    // SSL handshake
                    ssl_stream_.async_handshake(ssl::stream_base::client,
                        [this, host, path, handler]
                        (const error_code& handshake_ec) {
                            if (!handshake_ec) {
                                send_request(host, path, handler);
                            } else {
                                handler(handshake_ec, "");
                            }
                        });
                } else {
                    handler(ec, "");
                }
            });
    }
    
    template <typename ResponseHandler>
    void send_request(const std::string& host, const std::string& path, ResponseHandler&& handler) {
        std::string request = 
            "GET " + path + " HTTP/1.1\r\n"
            "Host: " + host + "\r\n"
            "Connection: close\r\n\r\n";
        
        asio::async_write(ssl_stream_, asio::buffer(request),
            [this, handler = std::forward<ResponseHandler>(handler)]
            (const error_code& ec, std::size_t) {
                if (!ec) {
                    read_response(handler);
                } else {
                    handler(ec, "");
                }
            });
    }
    
    template <typename ResponseHandler>
    void read_response(ResponseHandler&& handler) {
        auto response = std::make_shared<std::string>();
        auto buffer = std::make_shared<std::array<char, 1024>>();
        
        read_response_impl(response, buffer, handler);
    }
    
    template <typename ResponseHandler>
    void read_response_impl(std::shared_ptr<std::string> response,
                           std::shared_ptr<std::array<char, 1024>> buffer,
                           ResponseHandler&& handler) {
        
        ssl_stream_.async_read_some(asio::buffer(*buffer),
            [this, response, buffer, handler = std::forward<ResponseHandler>(handler)]
            (const error_code& ec, std::size_t bytes_transferred) {
                if (!ec) {
                    response->append(buffer->data(), bytes_transferred);
                    read_response_impl(response, buffer, handler);
                } else if (ec == asio::error::eof) {
                    handler(error_code{}, *response);
                } else {
                    handler(ec, "");
                }
            });
    }
};
```

This comprehensive integration demonstrates how SSL/TLS streams seamlessly integrate with ASIO's socket infrastructure while providing secure, encrypted communication channels.