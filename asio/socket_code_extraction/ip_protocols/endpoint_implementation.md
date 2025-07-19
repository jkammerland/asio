# Endpoint Implementation in ASIO

## Overview

The `basic_endpoint` template class represents a network endpoint - the combination of an IP address and a port number. It serves as the fundamental addressing mechanism for all IP-based protocols in ASIO.

## Class Template Structure

```cpp
template <typename InternetProtocol>
class basic_endpoint
{
public:
    typedef InternetProtocol protocol_type;
    typedef asio::detail::socket_addr_type data_type;
    
    // Constructors
    basic_endpoint() noexcept;
    basic_endpoint(const InternetProtocol& protocol, port_type port) noexcept;
    basic_endpoint(const asio::ip::address& addr, port_type port) noexcept;
    
    // Protocol information
    protocol_type protocol() const noexcept;
    
    // Address and port access
    asio::ip::address address() const noexcept;
    void address(const asio::ip::address& addr) noexcept;
    port_type port() const noexcept;
    void port(port_type port_num) noexcept;
    
    // Native access
    data_type* data() noexcept;
    const data_type* data() const noexcept;
    std::size_t size() const noexcept;
    void resize(std::size_t new_size);
    std::size_t capacity() const noexcept;
    
private:
    asio::ip::detail::endpoint impl_;
};
```

## Internal Implementation

### Storage Details

The endpoint internally uses a platform-specific socket address structure:
- For IPv4: `sockaddr_in` (16 bytes)
- For IPv6: `sockaddr_in6` (28 bytes)
- Generic storage: `sockaddr_storage` (128 bytes, accommodates both)

### Implementation Class
```cpp
namespace detail {
    class endpoint {
        union data_union {
            asio::detail::socket_addr_type base;
            asio::detail::sockaddr_in4_type v4;
            asio::detail::sockaddr_in6_type v6;
            asio::detail::sockaddr_storage_type storage;
        } data_;
    };
}
```

## Construction Patterns

### 1. Default Construction
```cpp
basic_endpoint() noexcept
{
    // Creates endpoint with any address (0.0.0.0 or ::)
    // Port set to 0
    // Protocol family unspecified
}
```

### 2. Protocol-Specific Construction
```cpp
basic_endpoint(const InternetProtocol& protocol, port_type port) noexcept
{
    // Uses protocol to determine family (AF_INET or AF_INET6)
    // Address set to any (INADDR_ANY or in6addr_any)
    // Commonly used for server binding
}
```

Example:
```cpp
tcp::endpoint ep1(tcp::v4(), 8080);  // 0.0.0.0:8080
tcp::endpoint ep2(tcp::v6(), 8080);  // [::]:8080
```

### 3. Address-Specific Construction
```cpp
basic_endpoint(const asio::ip::address& addr, port_type port) noexcept
{
    // Protocol family determined from address type
    // Specific address and port
    // Used for client connections or specific interface binding
}
```

Example:
```cpp
tcp::endpoint ep3(ip::make_address("192.168.1.1"), 8080);
tcp::endpoint ep4(ip::address_v6::loopback(), 8080);
```

## Protocol Determination

### Automatic Protocol Selection
```cpp
protocol_type protocol() const noexcept
{
    if (impl_.is_v4())
        return InternetProtocol::v4();
    return InternetProtocol::v6();
}
```

The protocol is inferred from the address family stored in the endpoint.

## Native Interface Access

### Purpose
Provides direct access to the underlying socket address structure for:
- System calls (bind, connect, accept, etc.)
- Compatibility with C APIs
- Custom socket operations

### Methods
```cpp
data_type* data() noexcept;           // Mutable access
const data_type* data() const noexcept; // Const access
std::size_t size() const noexcept;    // Actual size of address structure
std::size_t capacity() const noexcept; // Storage capacity
void resize(std::size_t new_size);    // Adjust size after modification
```

### Usage Example
```cpp
tcp::endpoint ep(ip::make_address("192.168.1.1"), 8080);
::bind(socket_fd, ep.data(), ep.size());  // Direct system call
```

## Port Type and Byte Order

### Port Storage
```cpp
typedef uint_least16_t port_type;  // At least 16 bits
```

### Byte Order Handling
- Ports stored in **network byte order** internally
- API uses **host byte order** for convenience
- Automatic conversion handled by implementation

Example:
```cpp
endpoint.port(8080);        // Host byte order input
// Internally stored as htons(8080)
uint16_t p = endpoint.port(); // Returns 8080 (host byte order)
```

## Comparison Operations

### Equality
```cpp
friend bool operator==(const basic_endpoint<IP>& e1, const basic_endpoint<IP>& e2) noexcept
{
    return e1.impl_ == e2.impl_;
    // Compares address family, address, and port
}
```

### Ordering
```cpp
friend bool operator<(const basic_endpoint<IP>& e1, const basic_endpoint<IP>& e2) noexcept
{
    return e1.impl_ < e2.impl_;
    // Lexicographical comparison: family, address, port
}
```

## Platform-Specific Details

### IPv4 Endpoint Structure
```cpp
struct sockaddr_in {
    sa_family_t sin_family;    // AF_INET
    in_port_t sin_port;        // Port (network byte order)
    struct in_addr sin_addr;   // IPv4 address
    char sin_zero[8];          // Padding
};
```

### IPv6 Endpoint Structure
```cpp
struct sockaddr_in6 {
    sa_family_t sin6_family;   // AF_INET6
    in_port_t sin6_port;       // Port (network byte order)
    uint32_t sin6_flowinfo;    // Flow information
    struct in6_addr sin6_addr; // IPv6 address
    uint32_t sin6_scope_id;    // Scope ID
};
```

## Common Usage Patterns

### 1. Server Binding
```cpp
tcp::acceptor acceptor(io_context);
tcp::endpoint endpoint(tcp::v4(), 8080);  // Any IPv4 address
acceptor.open(endpoint.protocol());
acceptor.bind(endpoint);
acceptor.listen();
```

### 2. Client Connection
```cpp
tcp::socket socket(io_context);
tcp::endpoint server_endpoint(ip::make_address("192.168.1.100"), 8080);
socket.connect(server_endpoint);
```

### 3. UDP Communication
```cpp
udp::socket socket(io_context, udp::v4());
udp::endpoint remote_endpoint(ip::make_address("224.0.0.1"), 12345);
socket.send_to(buffer, remote_endpoint);
```

### 4. Endpoint Resolution
```cpp
tcp::resolver resolver(io_context);
tcp::resolver::results_type endpoints = resolver.resolve("example.com", "http");
for (const tcp::endpoint& ep : endpoints) {
    std::cout << ep.address() << ":" << ep.port() << std::endl;
}
```

## String Representation

### Output Stream Support
```cpp
template <typename Elem, typename Traits, typename InternetProtocol>
std::basic_ostream<Elem, Traits>& operator<<(
    std::basic_ostream<Elem, Traits>& os,
    const basic_endpoint<InternetProtocol>& endpoint);
```

Format:
- IPv4: `192.168.1.1:8080`
- IPv6: `[2001:db8::1]:8080` (brackets for IPv6)

## Hash Support

### std::hash Specialization
```cpp
template <typename InternetProtocol>
struct hash<asio::ip::basic_endpoint<InternetProtocol>>
{
    std::size_t operator()(const basic_endpoint<InternetProtocol>& ep) const noexcept
    {
        // Combines hash of address and port
        // Uses XOR and bit shifting for distribution
    }
};
```

## Performance Considerations

### 1. Size and Copying
- Size varies by address type (16-28 bytes typically)
- Larger than just address + port due to structure padding
- Still efficient to copy (no dynamic allocation)

### 2. Construction Cost
- Default construction: Minimal (zeroing memory)
- From address: Address type checking + copying
- Native operations: Direct pointer access (no overhead)

### 3. Comparison Cost
- Depends on address type
- IPv4 faster than IPv6 due to size
- Ordering may require full structure comparison

## Thread Safety

- **Endpoint objects**: Not thread-safe for modification
- **Const operations**: Safe for concurrent access
- **Copying**: Safe (no shared state)
- **Typical usage**: Create, use, discard (no sharing needed)

## Design Rationale

### 1. Template Design
- Allows protocol-specific behavior
- Type safety at compile time
- No runtime protocol switching overhead

### 2. Value Semantics
- Simple copy/assignment
- No dynamic memory
- Suitable for containers

### 3. Native Access
- Zero-overhead abstraction
- Direct system call compatibility
- Platform independence through abstraction

## Error Handling

### Construction
- No exceptions thrown from constructors
- Invalid ports accepted (validation elsewhere)
- Address validation done at address construction

### Size Management
- `resize()` may throw if size invalid
- Capacity checks prevent buffer overruns
- Platform limits respected

## Integration Examples

### With Acceptor
```cpp
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 0));
tcp::endpoint local_ep = acceptor.local_endpoint();
std::cout << "Listening on port: " << local_ep.port() << std::endl;
```

### With Socket
```cpp
tcp::socket socket(io_context);
socket.connect(tcp::endpoint(ip::make_address("::1"), 8080));
tcp::endpoint remote_ep = socket.remote_endpoint();
tcp::endpoint local_ep = socket.local_endpoint();
```

### With Resolver
```cpp
tcp::resolver::results_type results = resolver.resolve("example.com", "80");
// Each result is a tcp::endpoint
asio::connect(socket, results);  // Tries each endpoint
```