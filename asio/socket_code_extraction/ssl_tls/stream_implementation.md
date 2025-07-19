# SSL Stream Implementation Details

## Class Structure and Design

### Primary Template Class

```cpp
template <typename Stream>
class stream : public stream_base, private noncopyable
{
    // Core components
    Stream next_layer_;                    // Underlying transport stream
    detail::stream_core core_;             // SSL state and buffer management
    
    // Internal operation classes
    class initiate_async_handshake;
    class initiate_async_buffered_handshake;
    class initiate_async_shutdown;
    class initiate_async_write_some;
    class initiate_async_read_some;
};
```

## Type System and Traits

### Core Type Definitions

```cpp
// Native SSL handle
typedef SSL* native_handle_type;

// Stream hierarchy types
typedef remove_reference_t<Stream> next_layer_type;
typedef typename next_layer_type::lowest_layer_type lowest_layer_type;
typedef typename lowest_layer_type::executor_type executor_type;

// Legacy compatibility
struct impl_struct { SSL* ssl; };
```

**Design rationale:**
- **Type safety**: Strong typing prevents misuse
- **Executor consistency**: Ensures all operations use the same executor
- **Layer transparency**: Maintains clean abstraction layers

## Construction Patterns

### Primary Constructor

```cpp
template <typename Arg>
stream(Arg&& arg, context& ctx)
    : next_layer_(static_cast<Arg&&>(arg)),
      core_(ctx.native_handle(), next_layer_.lowest_layer().get_executor())
{
}
```

**Key features:**
- **Perfect forwarding**: Efficient parameter passing
- **SSL context integration**: Uses provided SSL context
- **Executor binding**: Inherits executor from lowest layer

### Native Handle Constructor

```cpp
template <typename Arg>
stream(Arg&& arg, native_handle_type handle)
    : next_layer_(static_cast<Arg&&>(arg)),
      core_(handle, next_layer_.lowest_layer().get_executor())
{
}
```

**Use case**: Taking ownership of existing SSL objects

### Move Semantics

```cpp
stream(stream&& other)
    : next_layer_(static_cast<Stream&&>(other.next_layer_)),
      core_(static_cast<detail::stream_core&&>(other.core_))
{
}
```

**Benefits:**
- **Efficient transfers**: No copying of SSL state
- **Resource management**: Proper ownership transfer
- **Exception safety**: Strong exception guarantee

## Layer Access Methods

### Next Layer Access

```cpp
const next_layer_type& next_layer() const { return next_layer_; }
next_layer_type& next_layer() { return next_layer_; }
```

### Lowest Layer Access

```cpp
lowest_layer_type& lowest_layer() { return next_layer_.lowest_layer(); }
const lowest_layer_type& lowest_layer() const { return next_layer_.lowest_layer(); }
```

**Design purpose:**
- **Stream composition**: Enables proper stream layering
- **Direct access**: When SSL layer needs to be bypassed
- **Compatibility**: Maintains standard stream interface

## SSL Configuration Methods

### Verification Configuration

```cpp
void set_verify_mode(verify_mode v)
{
    asio::error_code ec;
    set_verify_mode(v, ec);
    asio::detail::throw_error(ec, "set_verify_mode");
}

ASIO_SYNC_OP_VOID set_verify_mode(verify_mode v, asio::error_code& ec)
{
    core_.engine_.set_verify_mode(v, ec);
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

**Pattern analysis:**
- **Dual interface**: Throwing and non-throwing versions
- **Delegation**: Forwards to engine implementation
- **Error handling**: Consistent error propagation

### Verification Depth

```cpp
void set_verify_depth(int depth);
ASIO_SYNC_OP_VOID set_verify_depth(int depth, asio::error_code& ec);
```

### Custom Verification Callback

```cpp
template <typename VerifyCallback>
void set_verify_callback(VerifyCallback callback)
{
    asio::error_code ec;
    this->set_verify_callback(callback, ec);
    asio::detail::throw_error(ec, "set_verify_callback");
}

template <typename VerifyCallback>
ASIO_SYNC_OP_VOID set_verify_callback(VerifyCallback callback, asio::error_code& ec)
{
    core_.engine_.set_verify_callback(
        new detail::verify_callback<VerifyCallback>(callback), ec);
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

**Template design:**
- **Type erasure**: Converts callback to internal representation
- **Memory management**: Engine takes ownership of callback
- **Flexibility**: Supports any callable with correct signature

## Handshake Implementation

### Synchronous Handshake

```cpp
void handshake(handshake_type type)
{
    asio::error_code ec;
    handshake(type, ec);
    asio::detail::throw_error(ec, "handshake");
}

ASIO_SYNC_OP_VOID handshake(handshake_type type, asio::error_code& ec)
{
    detail::io(next_layer_, core_, detail::handshake_op(type), ec);
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

### Buffered Handshake

```cpp
template <typename ConstBufferSequence>
void handshake(handshake_type type, const ConstBufferSequence& buffers)
{
    asio::error_code ec;
    handshake(type, buffers, ec);
    asio::detail::throw_error(ec, "handshake");
}

template <typename ConstBufferSequence>
ASIO_SYNC_OP_VOID handshake(handshake_type type,
    const ConstBufferSequence& buffers, asio::error_code& ec)
{
    detail::io(next_layer_, core_,
        detail::buffered_handshake_op<ConstBufferSequence>(type, buffers), ec);
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

**Buffered handshake benefits:**
- **Performance**: Reuses existing data for handshake
- **Efficiency**: Reduces round trips
- **Compatibility**: Works with existing protocols

### Asynchronous Handshake

```cpp
template <typename HandshakeToken>
auto async_handshake(handshake_type type, HandshakeToken&& token)
    -> decltype(async_initiate<HandshakeToken, void (asio::error_code)>(
          declval<initiate_async_handshake>(), token, type))
{
    return async_initiate<HandshakeToken, void (asio::error_code)>(
        initiate_async_handshake(this), token, type);
}
```

**Design elements:**
- **Universal async pattern**: Uses async_initiate for composability
- **Type deduction**: Automatic return type deduction
- **Completion token support**: Works with any completion token type

## Data Transfer Operations

### Synchronous Write

```cpp
template <typename ConstBufferSequence>
std::size_t write_some(const ConstBufferSequence& buffers)
{
    asio::error_code ec;
    std::size_t n = write_some(buffers, ec);
    asio::detail::throw_error(ec, "write_some");
    return n;
}

template <typename ConstBufferSequence>
std::size_t write_some(const ConstBufferSequence& buffers, asio::error_code& ec)
{
    return detail::io(next_layer_, core_,
        detail::write_op<ConstBufferSequence>(buffers), ec);
}
```

### Synchronous Read

```cpp
template <typename MutableBufferSequence>
std::size_t read_some(const MutableBufferSequence& buffers)
{
    asio::error_code ec;
    std::size_t n = read_some(buffers, ec);
    asio::detail::throw_error(ec, "read_some");
    return n;
}

template <typename MutableBufferSequence>
std::size_t read_some(const MutableBufferSequence& buffers, asio::error_code& ec)
{
    return detail::io(next_layer_, core_,
        detail::read_op<MutableBufferSequence>(buffers), ec);
}
```

### Asynchronous Operations

```cpp
template <typename ConstBufferSequence, typename WriteToken>
auto async_write_some(const ConstBufferSequence& buffers, WriteToken&& token)
    -> decltype(async_initiate<WriteToken, void (asio::error_code, std::size_t)>(
          declval<initiate_async_write_some>(), token, buffers))
{
    return async_initiate<WriteToken, void (asio::error_code, std::size_t)>(
        initiate_async_write_some(this), token, buffers);
}
```

## Shutdown Implementation

### Graceful SSL Shutdown

```cpp
void shutdown()
{
    asio::error_code ec;
    shutdown(ec);
    asio::detail::throw_error(ec, "shutdown");
}

ASIO_SYNC_OP_VOID shutdown(asio::error_code& ec)
{
    detail::io(next_layer_, core_, detail::shutdown_op(), ec);
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

**Shutdown process:**
1. Send SSL close_notify alert
2. Wait for peer's close_notify
3. Complete when both sides have closed SSL session

## Async Operation Initiators

### Handshake Initiator

```cpp
class initiate_async_handshake
{
public:
    typedef typename stream::executor_type executor_type;

    explicit initiate_async_handshake(stream* self) : self_(self) {}

    executor_type get_executor() const noexcept
    {
        return self_->get_executor();
    }

    template <typename HandshakeHandler>
    void operator()(HandshakeHandler&& handler, handshake_type type) const
    {
        ASIO_HANDSHAKE_HANDLER_CHECK(HandshakeHandler, handler) type_check;

        asio::detail::non_const_lvalue<HandshakeHandler> handler2(handler);
        detail::async_io(self_->next_layer_, self_->core_,
            detail::handshake_op(type), handler2.value);
    }

private:
    stream* self_;
};
```

**Initiator pattern benefits:**
- **Type safety**: Compile-time handler verification
- **Executor awareness**: Proper executor propagation
- **Composition**: Works with ASIO's composition framework

### Write Initiator

```cpp
class initiate_async_write_some
{
public:
    typedef typename stream::executor_type executor_type;

    explicit initiate_async_write_some(stream* self) : self_(self) {}

    executor_type get_executor() const noexcept
    {
        return self_->get_executor();
    }

    template <typename WriteHandler, typename ConstBufferSequence>
    void operator()(WriteHandler&& handler, const ConstBufferSequence& buffers) const
    {
        ASIO_WRITE_HANDLER_CHECK(WriteHandler, handler) type_check;

        asio::detail::non_const_lvalue<WriteHandler> handler2(handler);
        detail::async_io(self_->next_layer_, self_->core_,
            detail::write_op<ConstBufferSequence>(buffers), handler2.value);
    }

private:
    stream* self_;
};
```

## Integration with I/O Framework

### Operation Dispatch

All SSL operations use the same dispatch mechanism:

```cpp
detail::io(next_layer_, core_, operation, ec);           // Synchronous
detail::async_io(next_layer_, core_, operation, handler); // Asynchronous
```

**Benefits:**
- **Consistency**: Same logic for all operations
- **Maintainability**: Single implementation to maintain
- **Performance**: Optimized dispatch mechanism

### Buffer Management

The stream core manages buffers for SSL operations:

```cpp
struct stream_core
{
    enum { max_tls_record_size = 17 * 1024 };
    
    std::vector<unsigned char> output_buffer_space_;
    asio::mutable_buffer output_buffer_;
    std::vector<unsigned char> input_buffer_space_;
    asio::mutable_buffer input_buffer_;
    asio::const_buffer input_;
};
```

**Buffer design:**
- **Fixed sizes**: Based on maximum TLS record size
- **Reusable**: Avoid frequent allocations
- **Typed buffers**: Proper const/mutable distinction

## Error Handling and Resource Management

### RAII Design

The stream follows strict RAII principles:

```cpp
~stream()
{
    // Automatic cleanup through member destructors
    // core_ destructor handles SSL cleanup
    // next_layer_ destructor handles transport cleanup
}
```

### Error Propagation

All operations consistently handle errors:

```cpp
void operation()
{
    asio::error_code ec;
    operation(ec);
    asio::detail::throw_error(ec, "operation");
}

ASIO_SYNC_OP_VOID operation(asio::error_code& ec)
{
    // Perform operation
    // Set ec on error
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

### Move-Only Semantics

```cpp
// Deleted copy operations
stream(const stream&) = delete;
stream& operator=(const stream&) = delete;

// Implemented move operations
stream(stream&& other);
stream& operator=(stream&& other);
```

**Benefits:**
- **Clear ownership**: No accidental copying
- **Efficient transfers**: Move semantics for performance
- **Resource safety**: Prevents double-cleanup issues

This implementation demonstrates sophisticated C++ design principles while maintaining the simplicity and efficiency that makes ASIO's SSL streams both powerful and easy to use.