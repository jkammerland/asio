# IP Addressing System in ASIO

## Overview

ASIO provides a comprehensive and version-independent IP addressing system that supports both IPv4 and IPv6. The design allows for type-safe, efficient handling of IP addresses while maintaining compatibility with the underlying socket APIs.

## Core Components

### 1. address - Version-Independent IP Address

The `asio::ip::address` class provides a unified interface for both IPv4 and IPv6 addresses.

```cpp
class address {
public:
    // Constructors
    address() noexcept;                          // Default (IPv4 0.0.0.0)
    address(const address_v4& ipv4) noexcept;   // From IPv4
    address(const address_v6& ipv6) noexcept;   // From IPv6
    
    // Version checking
    bool is_v4() const noexcept;
    bool is_v6() const noexcept;
    
    // Version conversion
    address_v4 to_v4() const;  // Throws if not IPv4
    address_v6 to_v6() const;  // Throws if not IPv6
    
    // Properties
    bool is_loopback() const noexcept;
    bool is_unspecified() const noexcept;
    bool is_multicast() const noexcept;
    
    // String conversion
    std::string to_string() const;
};
```

### 2. address_v4 - IPv4 Address

Represents a 32-bit IPv4 address with rich functionality.

```cpp
class address_v4 {
public:
    typedef uint_least32_t uint_type;
    typedef array<unsigned char, 4> bytes_type;
    
    // Constructors
    address_v4() noexcept;                    // 0.0.0.0
    explicit address_v4(const bytes_type& bytes);
    explicit address_v4(uint_type addr);      // Host byte order
    
    // Conversions
    bytes_type to_bytes() const noexcept;     // Network byte order
    uint_type to_uint() const noexcept;       // Host byte order
    std::string to_string() const;
    
    // Properties
    bool is_loopback() const noexcept;       // 127.0.0.0/8
    bool is_unspecified() const noexcept;    // 0.0.0.0
    bool is_multicast() const noexcept;      // 224.0.0.0/4
    
    // Well-known addresses
    static address_v4 any() noexcept;         // 0.0.0.0
    static address_v4 loopback() noexcept;    // 127.0.0.1
    static address_v4 broadcast() noexcept;   // 255.255.255.255
};
```

### 3. address_v6 - IPv6 Address

Represents a 128-bit IPv6 address with scope ID support.

```cpp
class address_v6 {
public:
    typedef array<unsigned char, 16> bytes_type;
    typedef uint_least32_t scope_id_type;
    
    // Constructors
    address_v6() noexcept;                    // ::
    explicit address_v6(const bytes_type& bytes, 
                       scope_id_type scope_id = 0);
    
    // Scope ID management
    scope_id_type scope_id() const noexcept;
    void scope_id(scope_id_type id) noexcept;
    
    // Conversions
    bytes_type to_bytes() const noexcept;
    std::string to_string() const;
    
    // Properties
    bool is_loopback() const noexcept;           // ::1
    bool is_unspecified() const noexcept;        // ::
    bool is_link_local() const noexcept;         // fe80::/10
    bool is_site_local() const noexcept;         // fec0::/10
    bool is_v4_mapped() const noexcept;          // ::ffff:0:0/96
    bool is_multicast() const noexcept;          // ff00::/8
    bool is_multicast_global() const noexcept;
    bool is_multicast_link_local() const noexcept;
    bool is_multicast_node_local() const noexcept;
    bool is_multicast_org_local() const noexcept;
    bool is_multicast_site_local() const noexcept;
    
    // Well-known addresses
    static address_v6 any() noexcept;             // ::
    static address_v6 loopback() noexcept;        // ::1
};
```

### 4. basic_endpoint - Address + Port Combination

Combines an IP address with a port number for socket operations.

```cpp
template <typename InternetProtocol>
class basic_endpoint {
public:
    typedef InternetProtocol protocol_type;
    
    // Constructors
    basic_endpoint() noexcept;
    basic_endpoint(const InternetProtocol& protocol, port_type port);
    basic_endpoint(const address& addr, port_type port);
    
    // Protocol information
    protocol_type protocol() const noexcept;
    
    // Address and port access
    address address() const noexcept;
    void address(const address& addr) noexcept;
    port_type port() const noexcept;
    void port(port_type port_num) noexcept;
    
    // Native access
    data_type* data() noexcept;
    const data_type* data() const noexcept;
    std::size_t size() const noexcept;
    std::size_t capacity() const noexcept;
};
```

## Address Creation Functions

### Factory Functions

```cpp
// Version-independent address creation
address make_address(const char* str);
address make_address(const std::string& str);
address make_address(string_view str);

// IPv4-specific creation
address_v4 make_address_v4(const char* str);
address_v4 make_address_v4(const bytes_type& bytes);
address_v4 make_address_v4(uint_type addr);

// IPv6-specific creation
address_v6 make_address_v6(const char* str);
address_v6 make_address_v6(const bytes_type& bytes, scope_id_type scope = 0);

// IPv4-mapped IPv6 addresses
address_v4 make_address_v4(v4_mapped_t, const address_v6& v6);
address_v6 make_address_v6(v4_mapped_t, const address_v4& v4);
```

### Error Handling Variants

All factory functions have error_code variants:
```cpp
address make_address(const char* str, error_code& ec) noexcept;
address_v4 make_address_v4(const char* str, error_code& ec) noexcept;
address_v6 make_address_v6(const char* str, error_code& ec) noexcept;
```

## Design Principles

### 1. Type Safety
- Separate types for IPv4 and IPv6
- Compile-time protocol checking
- No implicit conversions between versions

### 2. Value Semantics
- All address types are value types
- Cheap to copy (IPv4: 4 bytes, IPv6: 20 bytes)
- No dynamic memory allocation

### 3. Immutability
- Address objects are logically immutable
- Thread-safe for concurrent read access
- Modifications create new objects

### 4. Platform Independence
- Hides platform-specific details
- Consistent byte ordering handling
- Portable string representations

## Common Usage Patterns

### 1. Creating Addresses
```cpp
// From string
auto addr1 = ip::make_address("192.168.1.1");
auto addr2 = ip::make_address("2001:db8::1");

// From bytes
ip::address_v4::bytes_type bytes = {192, 168, 1, 1};
auto addr3 = ip::make_address_v4(bytes);

// Well-known addresses
auto any_v4 = ip::address_v4::any();
auto loopback_v6 = ip::address_v6::loopback();
```

### 2. Version Handling
```cpp
ip::address addr = ip::make_address("::1");

if (addr.is_v6()) {
    ip::address_v6 v6_addr = addr.to_v6();
    if (v6_addr.is_v4_mapped()) {
        // Handle IPv4-mapped IPv6 address
        ip::address_v4 v4_addr = ip::make_address_v4(ip::v4_mapped, v6_addr);
    }
}
```

### 3. Endpoint Creation
```cpp
// TCP endpoint
tcp::endpoint ep1(ip::make_address("192.168.1.1"), 8080);
tcp::endpoint ep2(tcp::v6(), 8080);  // Any IPv6 address

// UDP endpoint  
udp::endpoint ep3(ip::address_v4::any(), 0);  // Any port
```

### 4. Address Properties
```cpp
ip::address addr = ip::make_address("127.0.0.1");

if (addr.is_loopback()) {
    // Handle loopback address
}

if (addr.is_multicast()) {
    // Set up multicast options
}
```

## IPv4/IPv6 Compatibility

### 1. IPv4-Mapped IPv6 Addresses
- Format: `::ffff:192.168.1.1`
- Allows IPv6 sockets to handle IPv4 connections
- Conversion functions provided

### 2. Dual-Stack Support
- Single socket can handle both IPv4 and IPv6 (OS-dependent)
- Address type determines protocol version
- Transparent to application code

### 3. Scope IDs
- IPv6 link-local addresses require scope ID
- Identifies network interface
- Format: `fe80::1%eth0`

## Performance Considerations

### 1. Memory Layout
- IPv4: 4 bytes (efficient, often passed by value)
- IPv6: 16 bytes + 4 bytes scope ID
- Version-independent: Union of both + discriminator

### 2. String Parsing
- Validates format during parsing
- Error codes available for performance-critical code
- Caching recommended for repeated conversions

### 3. Comparison Operations
- Efficient bitwise comparisons
- IPv4 uses integer comparison
- IPv6 uses byte-by-byte comparison

## Thread Safety

- **Address objects**: Immutable, safe for concurrent reads
- **Creation functions**: Thread-safe
- **String conversions**: Thread-safe
- **No shared state**: Each address is independent

## Integration with Sockets

1. **Endpoint binding**: Addresses used in endpoint construction
2. **Protocol selection**: Address version determines socket family
3. **Name resolution**: Resolver returns appropriate address types
4. **Automatic selection**: Can use version-independent addresses

## Best Practices

1. **Use make_address()** for parsing user input
2. **Check error codes** in performance-critical paths
3. **Prefer specific types** (address_v4/v6) when version is known
4. **Handle both versions** in generic code
5. **Validate addresses** before use in security contexts
6. **Consider scope IDs** for IPv6 link-local addresses
7. **Cache parsed addresses** when used repeatedly