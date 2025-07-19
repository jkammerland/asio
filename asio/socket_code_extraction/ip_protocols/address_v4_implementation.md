# IPv4 Address Implementation Details

## Overview

The `address_v4` class provides a complete implementation for handling IPv4 addresses in ASIO. It encapsulates a 32-bit IPv4 address with efficient storage and rich functionality for address manipulation and validation.

## Internal Representation

### Storage
```cpp
private:
    asio::detail::in4_addr_type addr_;  // Platform-specific IPv4 address structure
```

The internal storage uses the platform's native IPv4 address structure (typically `in_addr` with `s_addr` member), ensuring compatibility with system calls.

## Construction Methods

### 1. Default Constructor
```cpp
address_v4() noexcept
{
    addr_.s_addr = 0;  // Initializes to 0.0.0.0
}
```

### 2. From Bytes Array
```cpp
explicit address_v4(const bytes_type& bytes)
{
    // Validates byte values (must be 0-255)
    // Constructs address in network byte order
    // May throw out_of_range if validation fails
}
```

### 3. From Unsigned Integer
```cpp
explicit address_v4(uint_type addr)
{
    // Takes address in host byte order
    // Converts to network byte order internally
}
```

## Conversion Functions

### 1. To Bytes
```cpp
bytes_type to_bytes() const noexcept
{
    // Returns 4-byte array in network byte order
    // Example: 192.168.1.1 -> {192, 168, 1, 1}
}
```

### 2. To Unsigned Integer
```cpp
uint_type to_uint() const noexcept
{
    // Returns address in host byte order
    // Handles endianness conversion
    // Example: 192.168.1.1 -> 0xC0A80101 (on little-endian)
}
```

### 3. To String
```cpp
std::string to_string() const
{
    // Returns dotted decimal notation
    // Uses platform's inet_ntop or equivalent
    // Example: "192.168.1.1"
}
```

## Address Classification

### 1. Loopback Detection
```cpp
bool is_loopback() const noexcept
{
    return (to_uint() & 0xFF000000) == 0x7F000000;
}
```
- Checks if address is in 127.0.0.0/8 range
- Most significant byte must be 127

### 2. Unspecified Check
```cpp
bool is_unspecified() const noexcept
{
    return to_uint() == 0;
}
```
- Returns true for 0.0.0.0
- Used as wildcard address for binding

### 3. Multicast Detection
```cpp
bool is_multicast() const noexcept
{
    return (to_uint() & 0xF0000000) == 0xE0000000;
}
```
- Checks if address is in 224.0.0.0/4 range
- First 4 bits must be 1110

## Special Addresses

### Static Factory Methods
```cpp
static address_v4 any() noexcept
{
    return address_v4();  // Returns 0.0.0.0
}

static address_v4 loopback() noexcept
{
    return address_v4(0x7F000001);  // Returns 127.0.0.1
}

static address_v4 broadcast() noexcept
{
    return address_v4(0xFFFFFFFF);  // Returns 255.255.255.255
}
```

## Comparison Operations

### Equality
```cpp
friend bool operator==(const address_v4& a1, const address_v4& a2) noexcept
{
    return a1.addr_.s_addr == a2.addr_.s_addr;
}
```
- Direct comparison of internal representation
- No endianness issues as both stored identically

### Ordering
```cpp
friend bool operator<(const address_v4& a1, const address_v4& a2) noexcept
{
    return a1.to_uint() < a2.to_uint();
}
```
- Compares addresses in host byte order
- Ensures consistent ordering across platforms

## String Parsing

### Make Functions
```cpp
address_v4 make_address_v4(const char* str)
{
    // Uses inet_pton or platform equivalent
    // Validates dotted decimal format
    // Throws on invalid format
}

address_v4 make_address_v4(const char* str, error_code& ec) noexcept
{
    // Non-throwing version
    // Sets ec on error
    // Returns default address on failure
}
```

## Platform Considerations

### 1. Byte Order
- Internal storage in network byte order
- Conversions handle endianness transparently
- `to_uint()` returns host byte order
- `to_bytes()` returns network byte order

### 2. Type Definitions
```cpp
typedef uint_least32_t uint_type;  // At least 32 bits
```
- Ensures portability across platforms
- May be larger than 32 bits on some systems

### 3. Array Type
```cpp
typedef asio::detail::array<unsigned char, 4> bytes_type;
```
- Uses ASIO's array implementation
- Falls back to std::array or boost::array

## Memory Layout

### Size and Alignment
- Size: Typically 4 bytes
- Alignment: Platform-dependent (usually 4 bytes)
- No dynamic allocation
- Trivially copyable

### Example Memory Layout
```
Address: 192.168.1.1
Memory (big-endian):    [C0] [A8] [01] [01]
Memory (little-endian): [01] [01] [A8] [C0] (in s_addr)
```

## Usage in Endpoints

### Integration
```cpp
tcp::endpoint ep(address_v4::loopback(), 8080);
// Creates endpoint for 127.0.0.1:8080
```

### Socket Binding
```cpp
acceptor.bind(tcp::endpoint(address_v4::any(), 8080));
// Binds to 0.0.0.0:8080 (all interfaces)
```

## Performance Characteristics

### 1. Construction
- Default: Single store instruction
- From bytes: Validation overhead
- From integer: Endianness conversion

### 2. Comparison
- Equality: Single 32-bit comparison
- Ordering: Conversion + comparison

### 3. String Operations
- Parsing: System call overhead
- Formatting: Buffer allocation + formatting

## Common Patterns

### 1. Address Validation
```cpp
error_code ec;
auto addr = make_address_v4("192.168.1.1", ec);
if (!ec) {
    // Valid address
}
```

### 2. Range Checking
```cpp
bool is_private(const address_v4& addr) {
    uint32_t ip = addr.to_uint();
    return ((ip & 0xFF000000) == 0x0A000000) ||    // 10.0.0.0/8
           ((ip & 0xFFF00000) == 0xAC100000) ||    // 172.16.0.0/12
           ((ip & 0xFFFF0000) == 0xC0A80000);      // 192.168.0.0/16
}
```

### 3. Subnet Operations
```cpp
address_v4 apply_netmask(const address_v4& addr, const address_v4& mask) {
    return address_v4(addr.to_uint() & mask.to_uint());
}
```

## Error Handling

### Invalid Addresses
- String parsing can fail for malformed input
- Byte array construction validates range
- No validation on integer construction

### Exception Safety
- Copy/move operations: `noexcept`
- String parsing: May throw `system_error`
- Byte construction: May throw `out_of_range`

## Hash Support

### std::hash Specialization
```cpp
template <>
struct hash<asio::ip::address_v4>
{
    std::size_t operator()(const asio::ip::address_v4& addr) const noexcept
    {
        return std::hash<unsigned int>()(addr.to_uint());
    }
};
```
- Enables use in unordered containers
- Hash based on integer representation