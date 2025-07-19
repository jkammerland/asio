# Protocol Comparison: TCP vs UDP vs ICMP

## Overview

ASIO provides three main IP protocol implementations, each serving different purposes and offering distinct characteristics. This document compares their features, use cases, and implementation details.

## Quick Comparison Table

| Feature | TCP | UDP | ICMP |
|---------|-----|-----|------|
| **Connection Model** | Connection-oriented | Connectionless | Connectionless |
| **Reliability** | Guaranteed delivery | Best effort | Best effort |
| **Ordering** | In-order delivery | No ordering | No ordering |
| **Socket Type** | SOCK_STREAM | SOCK_DGRAM | SOCK_RAW |
| **Message Boundaries** | Stream (no boundaries) | Preserved | Preserved |
| **Overhead** | High (headers, ACKs) | Low | Minimal |
| **Use Cases** | Web, email, file transfer | Gaming, VoIP, DNS | Diagnostics, ping |
| **Privileges Required** | User-level | User-level | Root/Admin |

## Detailed Comparison

### 1. Socket Types and Classes

#### TCP
```cpp
typedef basic_stream_socket<tcp> socket;
typedef basic_socket_acceptor<tcp> acceptor;
typedef basic_resolver<tcp> resolver;
typedef basic_socket_iostream<tcp> iostream;
```
- Stream-oriented socket
- Requires acceptor for servers
- Supports iostream interface

#### UDP
```cpp
typedef basic_datagram_socket<udp> socket;
typedef basic_resolver<udp> resolver;
// No acceptor or iostream
```
- Datagram-oriented socket
- No acceptor needed
- No iostream support

#### ICMP
```cpp
typedef basic_raw_socket<icmp> socket;
typedef basic_resolver<icmp> resolver;
// No acceptor or iostream
```
- Raw socket access
- Requires privileges
- Manual packet construction

### 2. Connection Semantics

#### TCP - Connection Required
```cpp
tcp::socket socket(io_context);
socket.connect(endpoint);  // Must connect first
socket.send(buffer);       // Then send/receive
socket.receive(buffer);
```

#### UDP - No Connection
```cpp
udp::socket socket(io_context, udp::v4());
socket.send_to(buffer, remote_endpoint);     // Specify destination each time
socket.receive_from(buffer, sender_endpoint); // Learn sender from packet
```

#### ICMP - No Connection
```cpp
icmp::socket socket(io_context, icmp::v4());
socket.send_to(buffer, destination);          // Like UDP
socket.receive_from(buffer, sender);
```

### 3. Data Transfer Patterns

#### TCP - Stream Model
```cpp
// Can send partial data
size_t sent = socket.send(buffer);

// May need multiple receives for one "message"
size_t received = socket.receive(buffer);
```
- No message boundaries
- Partial sends/receives possible
- Buffering by TCP stack

#### UDP - Datagram Model
```cpp
// One send = one datagram
socket.send_to(entire_message, endpoint);

// One receive = one complete datagram
size_t len = socket.receive_from(buffer, sender);
```
- Message boundaries preserved
- All-or-nothing delivery
- Size limited by MTU

#### ICMP - Packet Model
```cpp
// Must construct complete ICMP packet
icmp_header header;
header.type(icmp_header::echo_request);
socket.send_to(buffers(header, body), endpoint);
```
- Manual header construction
- Complete packet control
- Protocol-specific handling

### 4. Server Implementation

#### TCP Server
```cpp
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), 8080));
for (;;) {
    tcp::socket socket(io_context);
    acceptor.accept(socket);
    // Handle connected client
}
```
- Explicit accept phase
- One socket per client
- Connection state maintained

#### UDP Server
```cpp
udp::socket socket(io_context, udp::endpoint(udp::v4(), 8080));
for (;;) {
    udp::endpoint sender;
    socket.receive_from(buffer, sender);
    socket.send_to(reply, sender);
}
```
- No accept phase
- Single socket for all clients
- Stateless communication

#### ICMP Server
```cpp
icmp::socket socket(io_context, icmp::v4());
for (;;) {
    icmp::endpoint sender;
    socket.receive_from(buffer, sender);
    // Parse and handle ICMP packet
}
```
- Receives all ICMP traffic
- Must filter relevant packets
- Typically for specific tools

### 5. Error Handling and Reliability

#### TCP
- Automatic retransmission
- Connection timeout detection
- Flow control and congestion control
- Errors: connection_refused, connection_reset, broken_pipe

#### UDP
- No automatic retransmission
- No connection errors
- Possible ICMP errors (port unreachable)
- Application must handle reliability

#### ICMP
- No reliability mechanisms
- Used to report errors itself
- May be filtered by firewalls
- Rate limiting common

### 6. Performance Characteristics

#### TCP
```cpp
// Higher latency due to:
- Three-way handshake for connection
- Acknowledgment packets
- Retransmission delays
- Head-of-line blocking

// Higher overhead:
- 20-byte TCP header + 20-byte IP header
- ACK packets
- Window management
```

#### UDP
```cpp
// Lower latency:
- No connection setup
- No acknowledgments
- Fire-and-forget model

// Lower overhead:
- 8-byte UDP header + 20-byte IP header
- No control packets
```

#### ICMP
```cpp
// Minimal overhead:
- 8-byte ICMP header + IP header
- No transport layer

// Special handling:
- Often processed in kernel
- May bypass normal routing
```

### 7. Multicast and Broadcast Support

#### TCP
- **No multicast support** (connection-oriented)
- **No broadcast support**
- Only unicast (point-to-point)

#### UDP
```cpp
// Broadcast (IPv4 only)
socket.set_option(socket_base::broadcast(true));
socket.send_to(data, udp::endpoint(address_v4::broadcast(), 8080));

// Multicast
socket.set_option(multicast::join_group(multicast_address));
socket.send_to(data, udp::endpoint(multicast_address, 8080));
```

#### ICMP
- Limited multicast support
- Typically used for neighbor discovery (IPv6)
- Not for general multicast data

### 8. Buffering and Flow Control

#### TCP
- Kernel buffers for send/receive
- Automatic flow control (window sizing)
- Backpressure handling
- Can query buffer sizes

#### UDP
- Limited kernel buffering
- No flow control
- Packets dropped if buffer full
- Application must handle pacing

#### ICMP
- Minimal buffering
- No flow control
- Rate limiting by kernel/firewall
- Manual pacing required

### 9. Security Considerations

#### TCP
- SYN flood attacks possible
- Connection hijacking risks
- Supports TLS/SSL encryption
- Stateful firewall friendly

#### UDP
- Amplification attacks possible
- Spoofing easier (no handshake)
- DTLS for encryption
- Stateless firewall challenges

#### ICMP
- Requires elevated privileges
- Can be used for network scanning
- Often blocked by firewalls
- Spoofing risks

### 10. Typical Socket Options

#### TCP-Specific
```cpp
tcp::no_delay option(true);          // Disable Nagle algorithm
socket.set_option(option);

socket_base::keep_alive keep(true);  // Enable TCP keepalive
socket.set_option(keep);
```

#### UDP-Specific
```cpp
socket_base::broadcast bcast(true);  // Enable broadcast
socket.set_option(bcast);

multicast::hops hops(4);             // Set multicast TTL
socket.set_option(hops);
```

#### ICMP-Specific
- Few socket options
- Mostly controlled through packet headers
- Filter options may be available

## Use Case Recommendations

### Choose TCP When:
- Reliable delivery is required
- Order matters
- Large data transfers
- Connection state is useful
- Standard protocols (HTTP, SMTP, etc.)

### Choose UDP When:
- Low latency is critical
- Packet loss is acceptable
- Multicast/broadcast needed
- Simple request/response
- Real-time data (audio/video)

### Choose ICMP When:
- Network diagnostics needed
- Implementing ping/traceroute
- Network discovery
- Error reporting
- Low-level network tools

## Implementation Complexity

### TCP
- **Simplest** for applications
- Complexity hidden in kernel
- Connection management required
- Error handling straightforward

### UDP
- **Moderate** complexity
- Must handle reliability if needed
- Simpler server implementation
- Manual timeout handling

### ICMP
- **Most complex** for applications
- Manual packet construction
- Privilege management
- Platform-specific behavior

## Summary

Each protocol serves distinct purposes in network programming:

- **TCP**: The reliable workhorse for most applications
- **UDP**: The performance choice for real-time systems
- **ICMP**: The diagnostic tool for network utilities

ASIO provides consistent interfaces across all three, hiding platform differences while exposing protocol-specific features where needed.