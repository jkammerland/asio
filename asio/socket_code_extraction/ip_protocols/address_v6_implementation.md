# IPv6 Address Implementation Details

## Overview

The `address_v6` class provides comprehensive support for IPv6 addresses in ASIO. It handles 128-bit addresses with additional support for scope IDs, which are essential for link-local addresses.

## Internal Representation

### Storage
```cpp
private:
    asio::detail::in6_addr_type addr_;  // 128-bit IPv6 address (16 bytes)
    scope_id_type scope_id_;            // Interface identifier (4 bytes)
```

The class stores both the IPv6 address and an optional scope ID, totaling 20 bytes of storage.

## Construction Methods

### 1. Default Constructor
```cpp
address_v6() noexcept
{
    // Initializes to :: (all zeros)
    // scope_id_ = 0
}
```

### 2. From Bytes with Scope ID
```cpp
explicit address_v6(const bytes_type& bytes, scope_id_type scope_id = 0)
{
    // Validates byte values (must be 0-255)
    // Stores 16 bytes in network byte order
    // Associates scope ID with address
}
```

## Scope ID Management

### Purpose
- Required for link-local addresses (fe80::/10)
- Identifies network interface
- Disambiguates identical link-local addresses on different interfaces

### API
```cpp
scope_id_type scope_id() const noexcept { return scope_id_; }
void scope_id(scope_id_type id) noexcept { scope_id_ = id; }
```

## Address Classification

### 1. Loopback Check
```cpp
bool is_loopback() const noexcept
{
    // Returns true for ::1
    // All bytes zero except last byte = 1
}
```

### 2. Unspecified Check
```cpp
bool is_unspecified() const noexcept
{
    // Returns true for ::
    // All 128 bits are zero
}
```

### 3. Link-Local Detection
```cpp
bool is_link_local() const noexcept
{
    // Checks for fe80::/10 prefix
    // First 10 bits: 1111 1110 10
}
```

### 4. Site-Local Detection (Deprecated)
```cpp
bool is_site_local() const noexcept
{
    // Checks for fec0::/10 prefix
    // Deprecated but still recognized
}
```

### 5. IPv4-Mapped Detection
```cpp
bool is_v4_mapped() const noexcept
{
    // Checks for ::ffff:0:0/96 prefix
    // Used for IPv4 compatibility
}
```

### 6. Multicast Classifications
```cpp
bool is_multicast() const noexcept                 // ff00::/8
bool is_multicast_global() const noexcept          // ff0e::/16
bool is_multicast_link_local() const noexcept      // ff02::/16
bool is_multicast_node_local() const noexcept      // ff01::/16
bool is_multicast_org_local() const noexcept       // ff08::/16
bool is_multicast_site_local() const noexcept      // ff05::/16
```

## Special Addresses

### Static Factory Methods
```cpp
static address_v6 any() noexcept
{
    return address_v6();  // Returns :: (all zeros)
}

static address_v6 loopback() noexcept
{
    // Returns ::1
    // Last byte set to 1, all others zero
}
```

## String Representation

### Format Examples
```
2001:db8::1                    // Standard notation
fe80::1%eth0                   // With scope ID
::ffff:192.168.1.1            // IPv4-mapped
::                            // Unspecified
```

### Conversion
```cpp
std::string to_string() const
{
    // Uses inet_ntop or equivalent
    // Appends %scope_id if non-zero
    // Handles zero compression (::)
}
```

## IPv4 Compatibility

### IPv4-Mapped Addresses
```cpp
// Convert IPv4 to IPv4-mapped IPv6
address_v6 make_address_v6(v4_mapped_t, const address_v4& v4_addr)
{
    // Creates ::ffff:a.b.c.d format
    // Preserves IPv4 address in last 32 bits
}

// Extract IPv4 from IPv4-mapped IPv6
address_v4 make_address_v4(v4_mapped_t, const address_v6& v6_addr)
{
    // Extracts last 32 bits as IPv4
    // Throws if not IPv4-mapped
}
```

## Comparison Operations

### Equality
```cpp
friend bool operator==(const address_v6& a1, const address_v6& a2) noexcept
{
    // Compares all 16 bytes
    // Also compares scope IDs
}
```

### Ordering
```cpp
friend bool operator<(const address_v6& a1, const address_v6& a2) noexcept
{
    // Lexicographical comparison of bytes
    // Scope ID used as tie-breaker
}
```

## Memory Layout

### Structure (20 bytes total)
```
Offset  Size    Description
0       16      IPv6 address (network byte order)
16      4       Scope ID (host byte order)
```

### Example: fe80::1%5
```
Address bytes: FE 80 00 00 00 00 00 00 00 00 00 00 00 00 00 01
Scope ID:      00 00 00 05 (assuming little-endian)
```

## Hash Support

### std::hash Specialization
```cpp
template <>
struct hash<asio::ip::address_v6>
{
    std::size_t operator()(const asio::ip::address_v6& addr) const noexcept
    {
        // Combines 4-byte chunks of address
        // Includes scope ID in hash
        // Uses hash combining algorithm
    }
};
```

## Platform Considerations

### 1. Scope ID Interpretation
- Platform-specific interface index
- May differ between OS implementations
- String format uses interface names on some platforms

### 2. String Parsing
- Accepts standard RFC notation
- Scope ID delimiter varies (% or platform-specific)
- Zone ID support depends on OS

### 3. Type Portability
```cpp
typedef uint_least32_t scope_id_type;  // At least 32 bits
```

## Performance Characteristics

### 1. Size and Copying
- 20 bytes total (16 + 4)
- Larger than IPv4 (4 bytes)
- Still suitable for pass-by-value in many cases

### 2. Comparison Cost
- 16-byte comparison vs 4-byte for IPv4
- May require multiple CPU instructions
- Scope ID adds additional comparison

### 3. String Operations
- More complex parsing than IPv4
- Zero compression adds complexity
- Scope ID formatting platform-dependent

## Common Usage Patterns

### 1. Creating Link-Local Addresses
```cpp
auto addr = make_address_v6("fe80::1");
addr.scope_id(5);  // Set interface index
```

### 2. Multicast Group Joining
```cpp
if (addr.is_multicast()) {
    if (addr.is_multicast_link_local()) {
        // No routing needed
    } else if (addr.is_multicast_global()) {
        // May need routing configuration
    }
}
```

### 3. Dual-Stack Handling
```cpp
address_v6 v6_addr = ...;
if (v6_addr.is_v4_mapped()) {
    // Can extract IPv4 address
    address_v4 v4_addr = make_address_v4(v4_mapped, v6_addr);
    // Handle as IPv4
}
```

### 4. Address Validation
```cpp
bool is_valid_global_unicast(const address_v6& addr) {
    return !addr.is_unspecified() &&
           !addr.is_loopback() &&
           !addr.is_multicast() &&
           !addr.is_link_local() &&
           !addr.is_site_local();
}
```

## Special Considerations

### 1. Scope ID Requirements
- Must be set for link-local addresses
- Ignored for global addresses
- Critical for correct routing

### 2. Zero Compression
- Multiple zero sequences compressed to ::
- Only longest sequence compressed
- Affects string representation only

### 3. Address Selection
- OS may prefer IPv4 or IPv6
- Dual-stack considerations
- Happy Eyeballs algorithm for connections

## Error Handling

### Construction Errors
- Invalid byte values (>255) throw out_of_range
- String parsing may throw system_error
- Invalid scope ID silently accepted

### Conversion Errors
- v4_mapped conversions throw if preconditions not met
- bad_address_cast for invalid version conversions

## Integration with Sockets

### Endpoint Creation
```cpp
tcp::endpoint ep(address_v6::loopback(), 8080);
// Creates [::1]:8080 endpoint
```

### Scope ID in Practice
```cpp
udp::endpoint multicast_ep(
    make_address_v6("ff02::1"),  // All nodes multicast
    12345
);
// May need scope_id for sending
```