# ASIO Socket Hierarchy

This diagram illustrates the class hierarchy and relationships between ASIO's socket classes, showing how different socket types inherit from base classes and specialize for different protocols.

```mermaid
classDiagram
    %% Base classes
    class socket_base {
        <<abstract>>
        +enum shutdown_type
        +class broadcast
        +class debug
        +class do_not_route
        +class enable_connection_aborted
        +class keep_alive
        +class linger
        +class out_of_band_inline
        +class receive_buffer_size
        +class receive_low_watermark
        +class reuse_address
        +class send_buffer_size
        +class send_low_watermark
        +message_flags
        +max_listen_connections
    }

    class basic_io_object~Service~ {
        <<template>>
        #Service service
        #implementation_type implementation
        +get_executor()
        +get_service()
        +get_implementation()
    }

    %% Main socket template
    class basic_socket~Protocol,Executor~ {
        <<template>>
        +typedef protocol_type
        +typedef endpoint_type
        +typedef native_handle_type
        +typedef executor_type
        +open()
        +assign()
        +close()
        +cancel()
        +bind()
        +connect()
        +async_connect()
        +set_option()
        +get_option()
        +io_control()
        +available()
        +at_mark()
        +non_blocking()
        +native_non_blocking()
        +local_endpoint()
        +remote_endpoint()
        +shutdown()
        +wait()
        +async_wait()
    }

    %% Socket specializations
    class basic_stream_socket~Protocol,Executor~ {
        <<template>>
        +send()
        +async_send()
        +receive()
        +async_receive()
        +write_some()
        +async_write_some()
        +read_some()
        +async_read_some()
    }

    class basic_datagram_socket~Protocol,Executor~ {
        <<template>>
        +send()
        +async_send()
        +send_to()
        +async_send_to()
        +receive()
        +async_receive()
        +receive_from()
        +async_receive_from()
    }

    class basic_raw_socket~Protocol,Executor~ {
        <<template>>
        +send()
        +async_send()
        +send_to()
        +async_send_to()
        +receive()
        +async_receive()
        +receive_from()
        +async_receive_from()
    }

    class basic_seq_packet_socket~Protocol,Executor~ {
        <<template>>
        +send()
        +async_send()
        +receive()
        +async_receive()
    }

    class basic_socket_acceptor~Protocol,Executor~ {
        <<template>>
        +typedef protocol_type
        +typedef endpoint_type
        +typedef native_handle_type
        +typedef executor_type
        +open()
        +bind()
        +listen()
        +close()
        +accept()
        +async_accept()
        +set_option()
        +get_option()
        +io_control()
        +non_blocking()
        +native_non_blocking()
        +local_endpoint()
        +wait()
        +async_wait()
    }

    %% Concrete socket types
    class tcp_socket {
        <<typedef>>
        basic_stream_socket~tcp~
    }

    class udp_socket {
        <<typedef>>
        basic_datagram_socket~udp~
    }

    class icmp_socket {
        <<typedef>>
        basic_raw_socket~icmp~
    }

    class tcp_acceptor {
        <<typedef>>
        basic_socket_acceptor~tcp~
    }

    class unix_stream_socket {
        <<typedef>>
        basic_stream_socket~stream_protocol~
    }

    class unix_datagram_socket {
        <<typedef>>
        basic_datagram_socket~datagram_protocol~
    }

    %% Inheritance relationships
    socket_base <|-- basic_socket
    basic_io_object <|-- basic_socket
    basic_socket <|-- basic_stream_socket
    basic_socket <|-- basic_datagram_socket
    basic_socket <|-- basic_raw_socket
    basic_socket <|-- basic_seq_packet_socket
    socket_base <|-- basic_socket_acceptor
    basic_io_object <|-- basic_socket_acceptor

    %% Instantiation relationships
    basic_stream_socket ..> tcp_socket : instantiates
    basic_datagram_socket ..> udp_socket : instantiates
    basic_raw_socket ..> icmp_socket : instantiates
    basic_socket_acceptor ..> tcp_acceptor : instantiates
    basic_stream_socket ..> unix_stream_socket : instantiates
    basic_datagram_socket ..> unix_datagram_socket : instantiates

    %% Protocol associations
    class tcp {
        <<protocol>>
        +v4()
        +v6()
        +type()
        +protocol()
        +family()
        +socket()
        +acceptor()
        +endpoint()
        +resolver()
    }

    class udp {
        <<protocol>>
        +v4()
        +v6()
        +type()
        +protocol()
        +family()
        +socket()
        +endpoint()
        +resolver()
    }

    tcp_socket --> tcp : uses
    udp_socket --> udp : uses
    tcp_acceptor --> tcp : uses
```

## Key Components Explained

### Base Classes

1. **socket_base**: The fundamental base class that defines:
   - Socket options (SO_REUSEADDR, SO_KEEPALIVE, etc.)
   - Shutdown types (receive, send, both)
   - Message flags for send/receive operations
   - Common constants like max_listen_connections

2. **basic_io_object**: Template base class that:
   - Manages the underlying I/O service implementation
   - Provides access to the executor
   - Handles the lifetime of the I/O object

### Core Socket Templates

1. **basic_socket**: The main socket template that:
   - Inherits from both socket_base and basic_io_object
   - Provides core socket operations (open, bind, connect, close)
   - Handles both synchronous and asynchronous operations
   - Manages socket options and I/O control commands
   - Provides endpoint information (local and remote)

2. **basic_stream_socket**: Specializes basic_socket for stream-oriented protocols:
   - TCP sockets
   - Unix domain stream sockets
   - Provides send/receive operations for connected sockets
   - Implements read_some/write_some for partial I/O

3. **basic_datagram_socket**: Specializes basic_socket for datagram protocols:
   - UDP sockets
   - Unix domain datagram sockets
   - Provides send_to/receive_from for connectionless communication

4. **basic_raw_socket**: For raw socket protocols:
   - ICMP sockets
   - Custom protocol implementations
   - Direct access to IP layer

5. **basic_seq_packet_socket**: For sequenced packet protocols:
   - SCTP-like protocols
   - Preserves message boundaries with reliable delivery

6. **basic_socket_acceptor**: Specialized for accepting connections:
   - Used with stream-oriented protocols
   - Provides listen and accept operations
   - Manages the server-side socket lifecycle

### Concrete Types

The diagram shows how concrete types like `tcp::socket`, `udp::socket`, etc., are typically typedef'd from the basic templates, providing convenient aliases for common use cases.

### Protocol Classes

Protocol classes (tcp, udp, etc.) define:
- Protocol-specific constants
- Factory methods for creating endpoints
- Type definitions for sockets and acceptors
- IPv4/IPv6 specific variants

This hierarchy allows ASIO to provide a consistent interface across different socket types while maintaining type safety and enabling protocol-specific optimizations.