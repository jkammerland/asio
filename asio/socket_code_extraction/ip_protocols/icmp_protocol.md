# ICMP Protocol Implementation in ASIO

## Overview

The ICMP (Internet Control Message Protocol) implementation in ASIO provides low-level access to ICMP functionality. ICMP is primarily used for diagnostic and control purposes in IP networks, most notably for ping and traceroute utilities.

## Key Components

### ICMP Class Structure

```cpp
class icmp {
public:
    // Type aliases for ICMP components
    typedef basic_endpoint<icmp> endpoint;
    typedef basic_raw_socket<icmp> socket;
    typedef basic_resolver<icmp> resolver;
    
    // Protocol family constructors
    static icmp v4() noexcept;  // ICMPv4 (IPPROTO_ICMP)
    static icmp v6() noexcept;  // ICMPv6 (IPPROTO_ICMPV6)
    
    // Protocol identifiers
    int type() const noexcept;     // Returns SOCK_RAW
    int protocol() const noexcept; // Returns IPPROTO_ICMP or IPPROTO_ICMPV6
    int family() const noexcept;   // Returns AF_INET or AF_INET6
};
```

## Protocol Characteristics

### 1. Raw Socket Based
- Uses `SOCK_RAW` socket type
- Requires elevated privileges (root/administrator)
- Direct access to IP layer
- Application must construct ICMP headers

### 2. Control and Diagnostic Protocol
- Used for network diagnostics (ping, traceroute)
- Error reporting (destination unreachable, time exceeded)
- Network management and discovery
- Not for general data transfer

### 3. Connectionless
- Similar to UDP in being connectionless
- Each ICMP message is independent
- No session establishment

## Integration with Socket Classes

### Socket Type
ICMP uses `basic_raw_socket<icmp>` which provides:
- Raw packet send/receive capabilities
- Access to IP headers (depending on OS)
- `send_to()` and `receive_from()` operations
- Asynchronous variants for non-blocking I/O

### No Acceptor
Like UDP, ICMP doesn't use acceptors:
- No connection concept
- Single socket handles all ICMP traffic
- Filtering done at application level

### Resolver Type
ICMP uses `basic_resolver<icmp>` for name resolution:
- Resolves hostnames to IP addresses
- Same interface as TCP/UDP resolvers
- Returns endpoints suitable for ICMP

## IPv4 vs IPv6 Support

### Protocol Differences
```cpp
// ICMPv4
icmp::socket socket_v4(io_context, icmp::v4());
// Uses IPPROTO_ICMP (protocol number 1)

// ICMPv6
icmp::socket socket_v6(io_context, icmp::v6());
// Uses IPPROTO_ICMPV6 (protocol number 58)
```

### Key Differences
- **ICMPv4**: Part of IPv4, separate protocol number
- **ICMPv6**: Integral part of IPv6, required for operation
- **Message Types**: Different type codes between versions
- **Functionality**: ICMPv6 includes neighbor discovery, MLD

## ICMP Message Structure

### Basic Message Format
```cpp
struct icmp_header {
    uint8_t type;     // Message type
    uint8_t code;     // Message subtype
    uint16_t checksum;// Error checking
    // Additional fields depend on type/code
};
```

### Common Message Types

#### ICMPv4
- Echo Request (8) / Echo Reply (0) - Ping
- Destination Unreachable (3)
- Time Exceeded (11) - TTL expired
- Redirect (5)

#### ICMPv6
- Echo Request (128) / Echo Reply (129)
- Destination Unreachable (1)
- Packet Too Big (2)
- Time Exceeded (3)
- Neighbor Solicitation/Advertisement (135/136)

## Usage Examples

### Basic Ping Implementation
```cpp
class icmp_header {
    // Helper class for ICMP header manipulation
public:
    enum { echo_reply = 0, echo_request = 8 };
    
    unsigned char type() const { return rep_[0]; }
    unsigned char code() const { return rep_[1]; }
    unsigned short checksum() const { return decode(2, 3); }
    unsigned short identifier() const { return decode(4, 5); }
    unsigned short sequence_number() const { return decode(6, 7); }
    
    void type(unsigned char n) { rep_[0] = n; }
    void code(unsigned char n) { rep_[1] = n; }
    void checksum(unsigned short n) { encode(2, 3, n); }
    void identifier(unsigned short n) { encode(4, 5, n); }
    void sequence_number(unsigned short n) { encode(6, 7, n); }
    
private:
    unsigned char rep_[8];
};

// Send ping
icmp::socket socket(io_context, icmp::v4());
icmp::endpoint destination(asio::ip::make_address("8.8.8.8"), 0);

icmp_header echo_request;
echo_request.type(icmp_header::echo_request);
echo_request.code(0);
echo_request.identifier(get_identifier());
echo_request.sequence_number(sequence_number++);

// Calculate checksum and send
compute_checksum(echo_request, body.begin(), body.end());
socket.send_to(asio::buffer(echo_request, body), destination);
```

### Receiving ICMP Replies
```cpp
// Receive ICMP messages
icmp::endpoint sender;
size_t length = socket.receive_from(asio::buffer(reply_buffer), sender);

// Parse ICMP header from reply
ipv4_header ipv4_hdr;
icmp_header icmp_hdr;
std::istream is(&reply_buffer);
is >> ipv4_hdr >> icmp_hdr;

if (icmp_hdr.type() == icmp_header::echo_reply) {
    // Process ping reply
    if (icmp_hdr.identifier() == get_identifier()) {
        // This is our reply
        std::cout << "Reply from " << ipv4_hdr.source_address() 
                  << ": seq=" << icmp_hdr.sequence_number() << std::endl;
    }
}
```

## Special Considerations

### 1. Privileges
- Raw sockets typically require root/administrator privileges
- Some systems allow unprivileged ICMP echo (ping)
- Security implications of raw socket access

### 2. Packet Construction
- Application must build valid ICMP headers
- Checksum calculation required
- Proper type/code combinations

### 3. Filtering
- Kernel delivers all ICMP messages to raw socket
- Application must filter for relevant messages
- Check identifier/sequence for echo replies

### 4. Platform Differences
- Header inclusion varies by platform
- Some OSes include IP header, others don't
- Checksum handling may differ

## Thread Safety

- **Distinct objects**: Safe to use from multiple threads
- **Shared objects**: Safe for const operations only
- Standard ASIO thread safety model

## Common Use Cases

### 1. Network Diagnostics
- Ping implementations
- Traceroute utilities
- Network monitoring tools
- Connectivity testing

### 2. Network Discovery
- Host discovery
- Router discovery (ICMPv6)
- Path MTU discovery

### 3. Error Handling
- Receiving destination unreachable messages
- Detecting network issues
- Time exceeded notifications

## Integration Points

1. **Endpoint**: Uses `basic_endpoint<icmp>` (port typically 0)
2. **Socket**: Integrates with `basic_raw_socket` template
3. **Resolver**: Compatible with `basic_resolver` template
4. **Raw Access**: Direct IP packet manipulation
5. **No High-Level Abstractions**: No acceptor or iostream support

## Security Considerations

1. **Privilege Requirements**: Usually needs elevated privileges
2. **Spoofing Risks**: Raw sockets can spoof source addresses
3. **DoS Potential**: Can generate high packet rates
4. **Firewall Issues**: ICMP often filtered or rate-limited
5. **Input Validation**: Must validate received packet data