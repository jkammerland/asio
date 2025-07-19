# ASIO Name Resolution - Implementation Details

This document provides an in-depth analysis of ASIO's name resolution implementation, including internal mechanisms, design decisions, and platform-specific details.

## Internal Architecture

### Component Relationships

```cpp
// User creates a resolver
tcp::resolver resolver(io_context);

// Internally creates:
// 1. resolver_service<tcp> (one per io_context)
// 2. resolver_thread_pool (one per io_context, shared)
// 3. implementation_type (cancellation token)
```

### Implementation Type

The resolver's implementation is a shared cancellation token:
```cpp
typedef socket_ops::shared_cancel_token_type implementation_type;
```

This allows:
- Cancellation of in-progress operations
- Lightweight resolver objects
- Safe destruction while operations are pending

## Synchronous Resolution Flow

### 1. User Call
```cpp
auto results = resolver.resolve("example.com", "80");
```

### 2. Service Dispatch
```cpp
// In basic_resolver::resolve()
results_type r = impl_.get_service().resolve(
    impl_.get_implementation(), q, ec);
```

### 3. Platform Call
```cpp
// In resolver_service::resolve()
socket_ops::getaddrinfo(qry.host_name().c_str(),
    qry.service_name().c_str(), qry.hints(), &address_info, ec);
```

### 4. Result Creation
```cpp
// Convert addrinfo linked list to results container
results_type::create(address_info, qry.host_name(), qry.service_name());
```

## Asynchronous Resolution Flow

### 1. Operation Creation

```cpp
template <typename Handler, typename IoExecutor>
void async_resolve(implementation_type& impl, const query_type& qry,
    Handler& handler, const IoExecutor& io_ex)
{
  // Create operation object
  typedef resolve_query_op<Protocol, Handler, IoExecutor> op;
  typename op::ptr p = { asio::detail::addressof(handler),
    op::ptr::allocate(handler), 0 };
  p.p = new (p.v) op(impl, qry, thread_pool_.scheduler(), handler, io_ex);
  
  // Post to thread pool
  thread_pool_.start_resolve_op(p.p);
}
```

### 2. Thread Pool Execution

```cpp
void resolver_thread_pool::start_resolve_op(resolve_op* op)
{
  start_work_threads();  // Ensure threads are running
  scheduler_.work_started();
  work_scheduler_.post_immediate_completion(op, false);
}
```

### 3. Background Resolution

```cpp
// In resolve_query_op::do_complete() - worker thread
socket_ops::background_getaddrinfo(o->cancel_token_,
    o->query_.host_name().c_str(), 
    o->query_.service_name().c_str(),
    o->query_.hints(), &o->addrinfo_, o->ec_);

// Post back to main scheduler
o->scheduler_.post_deferred_completion(o);
```

### 4. Handler Invocation

```cpp
// Back on main I/O context
handler_work<Handler, IoExecutor> w(o->work_);
detail::binder2<Handler, asio::error_code, results_type>
    handler(o->handler_, o->ec_, results_type());
    
if (o->addrinfo_)
{
  handler.arg2_ = results_type::create(o->addrinfo_,
      o->query_.host_name(), o->query_.service_name());
}

w.complete(handler, handler.handler_);
```

## Thread Pool Details

### Configuration Options

```cpp
// Via execution context configuration
io_context ctx;
ctx.config().set("resolver", "threads", 4);  // Use 4 resolver threads
ctx.config().set("scheduler", "locking", true);  // Enable scheduler locking
```

### Thread Lifecycle

1. **Lazy Creation**: Threads created on first use
2. **Work Loop**: Each thread runs `work_scheduler_.run()`
3. **Shutdown**: Threads joined on thread pool destruction

### Thread Safety

```cpp
class resolver_thread_pool
{
  asio::detail::mutex mutex_;  // Protects thread creation
  scheduler_impl& scheduler_;   // Main I/O scheduler
  scheduler_impl work_scheduler_;  // Private work scheduler
  thread_group<> work_threads_;    // Worker threads
};
```

## Platform-Specific Implementation

### Windows

```cpp
#if defined(ASIO_WINDOWS) || defined(__CYGWIN__)
  #if defined(ASIO_HAS_GETADDRINFO)
    // Modern Windows - direct API
    int error = ::getaddrinfo(host, service, &hints, result);
  #else
    // Windows 2000 - dynamic loading
    if (gai_t gai = (gai_t)::GetProcAddress(winsock_module, "getaddrinfo"))
    {
      int error = gai(host, service, &hints, result);
    }
    else
    {
      // Fall back to emulation
      int error = getaddrinfo_emulation(host, service, &hints, result);
    }
  #endif
#endif
```

### POSIX

```cpp
#else
  // Direct POSIX call
  int error = ::getaddrinfo(host, service, &hints, result);
  
  #if defined(__MACH__) && defined(__APPLE__)
    // macOS workaround for numeric service names
    if (error == 0 && service && isdigit(service[0]))
    {
      // Fix port numbers in results
    }
  #endif
#endif
```

### Windows Runtime

```cpp
#if defined(ASIO_WINDOWS_RUNTIME)
// Special handling using Windows::Networking APIs
IVectorView<EndpointPair^>^ endpoints = /* ... */;
for (unsigned int i = 0; i < endpoints->Size; ++i)
{
  auto pair = endpoints->GetAt(i);
  // Convert to standard endpoint format
}
#endif
```

## Cancellation Mechanism

### Cancel Token Usage

```cpp
// In resolver_service_base
void cancel(implementation_type& impl)
{
  // Reset the shared pointer, causing weak pointers to expire
  impl.reset(static_cast<void*>(0), socket_ops::noop_deleter());
}
```

### Background Operation Check

```cpp
asio::error_code background_getaddrinfo(
    const weak_cancel_token_type& cancel_token, ...)
{
  if (cancel_token.expired())
    ec = asio::error::operation_aborted;
  else
    socket_ops::getaddrinfo(host, service, hints, result, ec);
  return ec;
}
```

## Memory Management

### Result Allocation

```cpp
// Results use shared_ptr for reference counting
typedef std::vector<basic_resolver_entry<InternetProtocol>> values_type;
typedef asio::detail::shared_ptr<values_type> values_ptr_type;
```

### Operation Allocation

```cpp
// Handler memory allocation
typedef resolve_query_op<Protocol, Handler, IoExecutor> op;
typename op::ptr p = { 
  asio::detail::addressof(handler),
  op::ptr::allocate(handler),  // Uses handler's allocator
  0 
};
```

## Query Hints Structure

### addrinfo_type Mapping

```cpp
struct addrinfo_type
{
  int ai_flags;       // Input flags (AI_PASSIVE, etc.)
  int ai_family;      // AF_INET, AF_INET6, AF_UNSPEC
  int ai_socktype;    // SOCK_STREAM, SOCK_DGRAM
  int ai_protocol;    // Protocol number
  // ... platform-specific fields
};
```

### Flag Combinations

```cpp
// For server binding
flags = passive | address_configured;

// For client connection
flags = address_configured;

// For numeric-only resolution
flags = numeric_host | numeric_service;
```

## Error Translation

### Platform to ASIO Errors

```cpp
inline asio::error_code translate_addrinfo_error(int error)
{
  switch (error)
  {
  case 0:
    return asio::error_code();
  case EAI_AGAIN:
    return asio::error::host_not_found_try_again;
  case EAI_BADFLAGS:
    return asio::error::invalid_argument;
  case EAI_FAIL:
    return asio::error::no_recovery;
  case EAI_FAMILY:
    return asio::error::address_family_not_supported;
  case EAI_MEMORY:
    return asio::error::no_memory;
  case EAI_NONAME:
    return asio::error::host_not_found;
  case EAI_SERVICE:
    return asio::error::service_not_found;
  // ... more mappings
  }
}
```

## Emulation Mode Details

For systems without native getaddrinfo:

### Host Resolution
```cpp
// Uses gethostbyname for forward resolution
hostent* retval = ::gethostbyname(name);

// Convert hostent to addrinfo format
addrinfo_type* ai = /* allocate and populate */;
```

### Service Resolution
```cpp
// Uses getservbyname for service lookup
servent* serv = ::getservbyname(service, proto);

// Extract port number
port = ntohs(serv->s_port);
```

## Performance Considerations

### Thread Pool Sizing
- Default: 1 thread (lazy creation)
- Recommendation: Match expected concurrent DNS queries
- Too many threads: Wasted resources
- Too few threads: Resolution bottleneck

### Caching
- ASIO does not cache DNS results
- Relies on OS DNS resolver cache
- Applications should implement their own caching if needed

### Optimization Tips
1. Use numeric addresses when possible
2. Reuse resolver objects (they're lightweight)
3. Configure appropriate thread pool size
4. Consider batch resolution for multiple hosts