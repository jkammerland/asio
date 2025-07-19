# TCP Connection Lifecycle

This sequence diagram shows the complete lifecycle of a TCP connection in ASIO, from connection establishment through data transfer to closure.

```mermaid
sequenceDiagram
    participant Client as Client Application
    participant ClientSock as client::tcp::socket
    participant ASIO as ASIO Layer
    participant OS as OS/Kernel
    participant NetStack as Network Stack
    participant Server as Server Application
    participant Acceptor as server::tcp::acceptor
    participant ServerSock as server::tcp::socket

    %% Server setup
    Note over Server,Acceptor: Server Initialization
    Server->>Acceptor: tcp::acceptor(io_context, endpoint)
    Acceptor->>OS: socket() + bind()
    Acceptor->>OS: listen(backlog)
    Server->>Acceptor: async_accept(socket, handler)
    Acceptor->>ASIO: Register accept operation
    ASIO->>OS: Register with reactor/proactor
    
    %% Client connection
    Note over Client,ClientSock: Client Connection
    Client->>ClientSock: tcp::socket(io_context)
    ClientSock->>OS: socket()
    Client->>ClientSock: async_connect(endpoint, handler)
    ClientSock->>ASIO: Register connect operation
    ASIO->>OS: Begin connection (SYN)
    
    %% TCP handshake
    Note over OS,NetStack: TCP 3-Way Handshake
    OS->>NetStack: SYN →
    NetStack->>OS: ← SYN-ACK
    OS->>NetStack: ACK →
    
    %% Connection establishment
    OS->>ASIO: Connection established (client)
    ASIO->>ClientSock: Complete async_connect
    ClientSock->>Client: connect_handler(error_code)
    
    OS->>ASIO: New connection (server)
    ASIO->>Acceptor: Complete async_accept
    Acceptor->>ServerSock: Initialize new socket
    ServerSock->>OS: New socket fd
    Acceptor->>Server: accept_handler(error_code)
    
    %% Start next accept
    Server->>Acceptor: async_accept(next_socket, handler)
    
    %% Data transfer - Client to Server
    Note over Client,Server: Data Transfer Phase
    Client->>ClientSock: async_write(buffer, handler)
    ClientSock->>ASIO: Register write operation
    ASIO->>OS: Send data
    OS->>NetStack: TCP segments →
    
    Server->>ServerSock: async_read(buffer, handler)
    ServerSock->>ASIO: Register read operation
    ASIO->>OS: Register for read events
    
    NetStack->>OS: Data arrival
    OS->>ASIO: Data available
    ASIO->>ServerSock: Perform read
    ServerSock->>OS: recv() data
    ServerSock->>Server: read_handler(error_code, bytes)
    
    NetStack->>OS: ← ACK
    OS->>ASIO: Write complete
    ASIO->>ClientSock: Write confirmed
    ClientSock->>Client: write_handler(error_code, bytes)
    
    %% Data transfer - Server to Client
    Server->>ServerSock: async_write(response, handler)
    ServerSock->>ASIO: Register write operation
    ASIO->>OS: Send data
    OS->>NetStack: TCP segments →
    
    Client->>ClientSock: async_read(buffer, handler)
    ClientSock->>ASIO: Register read operation
    ASIO->>OS: Register for read events
    
    NetStack->>OS: Data arrival
    OS->>ASIO: Data available
    ASIO->>ClientSock: Perform read
    ClientSock->>OS: recv() data
    ClientSock->>Client: read_handler(error_code, bytes)
    
    %% Keep-alive (optional)
    Note over OS,NetStack: Optional Keep-Alive
    OS->>NetStack: Keep-alive probe →
    NetStack->>OS: ← Keep-alive ACK
    
    %% Connection closure - Client initiated
    Note over Client,Server: Connection Closure
    Client->>ClientSock: shutdown(send)
    ClientSock->>OS: shutdown(SHUT_WR)
    OS->>NetStack: FIN →
    
    NetStack->>OS: ← ACK
    
    Server->>ServerSock: async_read()
    ServerSock->>Server: read_handler(eof, 0)
    
    Server->>ServerSock: shutdown(send)
    ServerSock->>OS: shutdown(SHUT_WR)
    OS->>NetStack: FIN →
    
    NetStack->>OS: ← ACK
    
    %% Close sockets
    Client->>ClientSock: close()
    ClientSock->>OS: close(fd)
    Server->>ServerSock: close()
    ServerSock->>OS: close(fd)
    
    %% Error handling paths
    Note over Client,Server: Error Handling Paths
    
    alt Connection Refused
        OS->>NetStack: SYN →
        NetStack->>OS: ← RST
        OS->>ASIO: Connection refused
        ASIO->>ClientSock: Complete with error
        ClientSock->>Client: handler(connection_refused)
    else Connection Timeout
        OS->>NetStack: SYN → (no response)
        OS->>ASIO: Timeout
        ASIO->>ClientSock: Complete with error
        ClientSock->>Client: handler(timed_out)
    else Connection Reset
        NetStack->>OS: ← RST (during transfer)
        OS->>ASIO: Connection reset
        ASIO->>ClientSock: Complete with error
        ClientSock->>Client: handler(connection_reset)
    end
```

## Lifecycle Phases Explained

### 1. Server Initialization

**Socket Creation and Binding**:
```cpp
tcp::acceptor acceptor(io_context, tcp::endpoint(tcp::v4(), port));
```
- Creates a socket
- Binds to specified address and port
- Sets socket options (e.g., SO_REUSEADDR)

**Listen State**:
```cpp
acceptor.listen(backlog);
```
- Transitions socket to listening state
- Sets maximum pending connection queue size

**Accept Loop**:
```cpp
acceptor.async_accept(socket, handler);
```
- Registers for incoming connections
- Creates new socket for each connection

### 2. Client Connection

**Socket Creation**:
```cpp
tcp::socket socket(io_context);
```
- Creates unconnected socket
- Selects appropriate protocol family

**Connection Initiation**:
```cpp
socket.async_connect(endpoint, handler);
```
- Initiates TCP handshake
- Non-blocking operation

### 3. TCP Three-Way Handshake

1. **SYN**: Client sends synchronize packet
2. **SYN-ACK**: Server acknowledges and synchronizes
3. **ACK**: Client acknowledges server's SYN

This establishes:
- Initial sequence numbers
- Window sizes
- TCP options (MSS, window scaling, etc.)

### 4. Data Transfer

**Async Write Operations**:
```cpp
async_write(socket, buffer, handler);
```
- May require multiple send() calls
- Handles partial writes automatically
- Completion handler called when all data sent

**Async Read Operations**:
```cpp
async_read(socket, buffer, handler);
```
- Reads until buffer full or error
- Handles partial reads
- Various read functions available:
  - `async_read_some()`: Partial reads
  - `async_read_until()`: Read until delimiter
  - `async_read()`: Read exact amount

**Flow Control**:
- TCP window management
- Nagle's algorithm (can be disabled)
- Kernel buffer management

### 5. Connection Closure

**Graceful Shutdown**:
```cpp
socket.shutdown(tcp::socket::shutdown_send);
```
- Sends FIN packet
- Half-close: can still receive
- Full-close: shutdown both directions

**Socket Close**:
```cpp
socket.close();
```
- Releases all resources
- Immediate termination
- Any pending data may be lost

### 6. Error Handling

**Common Errors**:
- `connection_refused`: No listener on port
- `connection_reset`: RST packet received
- `timed_out`: Connection attempt timeout
- `broken_pipe`: Write to closed connection
- `eof`: Graceful closure by peer

## Important Considerations

### Socket Options

Common options set during lifecycle:
```cpp
socket.set_option(tcp::no_delay(true));        // Disable Nagle
socket.set_option(socket_base::keep_alive(true)); // Enable keep-alive
socket.set_option(socket_base::reuse_address(true)); // Allow reuse
```

### Timeouts

ASIO doesn't have built-in socket timeouts. Implement using:
```cpp
deadline_timer timer(io_context);
timer.expires_from_now(seconds(30));
timer.async_wait([&](error_code ec) {
    if (!ec) socket.cancel();
});
```

### Connection Pooling

For client applications:
- Reuse connections when possible
- Implement connection pool with idle timeout
- Handle connection failures gracefully

### Server Scalability

- Accept loop should always be running
- Handle multiple concurrent connections
- Consider using `SO_REUSEPORT` on Linux
- Use appropriate executor/thread pool

### SSL/TLS Considerations

When using SSL:
```cpp
ssl::stream<tcp::socket> socket(io_context, ssl_context);
// Perform TCP connect first
// Then SSL handshake
socket.async_handshake(ssl::stream_base::client, handler);
```

This lifecycle diagram illustrates the complete flow from connection establishment to termination, including error scenarios and best practices for robust TCP communication using ASIO.