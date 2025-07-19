# ASIO Name Resolution (DNS) Implementation

This document provides a comprehensive overview of ASIO's name resolution implementation, including DNS lookups for both hostname-to-address (forward) and address-to-hostname (reverse) resolution.

## Table of Contents

1. [Architecture Overview](#architecture-overview)
2. [Key Components](#key-components)
3. [Synchronous Resolution](#synchronous-resolution)
4. [Asynchronous Resolution](#asynchronous-resolution)
5. [Thread Pool Implementation](#thread-pool-implementation)
6. [Platform Differences](#platform-differences)
7. [Query and Result Handling](#query-and-result-handling)
8. [Usage Examples](#usage-examples)

## Architecture Overview

ASIO's name resolution is built on a layered architecture:

```
┌─────────────────────────────────────┐
│      basic_resolver<Protocol>       │  User-facing interface
├─────────────────────────────────────┤
│      resolver_service<Protocol>     │  Service implementation
├─────────────────────────────────────┤
│      resolver_service_base          │  Common service functionality
├─────────────────────────────────────┤
│      resolver_thread_pool           │  Thread pool for async operations
├─────────────────────────────────────┤
│         socket_ops                  │  Platform-specific DNS calls
└─────────────────────────────────────┘
```

## Key Components

### 1. basic_resolver
- **Location**: `asio/ip/basic_resolver.hpp`
- **Purpose**: Main user-facing resolver class template
- **Key Features**:
  - Supports both synchronous and asynchronous resolution
  - Protocol-independent (works with IPv4, IPv6)
  - Thread-safe for distinct objects

### 2. resolver_service
- **Location**: `asio/detail/resolver_service.hpp`
- **Purpose**: Service implementation for resolver operations
- **Responsibilities**:
  - Manages resolver implementation objects
  - Dispatches sync/async resolution operations
  - Handles cancellation tokens

### 3. resolver_thread_pool
- **Location**: `asio/detail/resolver_thread_pool.hpp`
- **Purpose**: Manages background threads for blocking DNS operations
- **Features**:
  - Configurable number of threads
  - Prevents blocking the main I/O context
  - Shared across all resolvers in an execution context

### 4. Query and Results Classes
- **basic_resolver_query**: Encapsulates resolution parameters
- **basic_resolver_results**: Container for resolution results
- **basic_resolver_entry**: Individual result entry

## Synchronous Resolution

### Forward Resolution (hostname → address)

```cpp
// Implementation in resolver_service::resolve()
results_type resolve(implementation_type&, const query_type& qry,
    asio::error_code& ec)
{
  asio::detail::addrinfo_type* address_info = 0;
  
  // Call platform's getaddrinfo
  socket_ops::getaddrinfo(qry.host_name().c_str(),
      qry.service_name().c_str(), qry.hints(), &address_info, ec);
  
  auto_addrinfo auto_address_info(address_info);
  
  return ec ? results_type() : results_type::create(
      address_info, qry.host_name(), qry.service_name());
}
```

### Reverse Resolution (address → hostname)

```cpp
// Implementation in resolver_service::resolve()
results_type resolve(implementation_type&,
    const endpoint_type& endpoint, asio::error_code& ec)
{
  char host_name[NI_MAXHOST];
  char service_name[NI_MAXSERV];
  
  // Call platform's getnameinfo
  socket_ops::sync_getnameinfo(endpoint.data(), endpoint.size(),
      host_name, NI_MAXHOST, service_name, NI_MAXSERV,
      endpoint.protocol().type(), ec);
  
  return ec ? results_type() : results_type::create(
      endpoint, host_name, service_name);
}
```

## Asynchronous Resolution

### How Async Resolution Works

1. **Operation Creation**: When `async_resolve()` is called, a resolve operation object is created
2. **Thread Pool Dispatch**: Operation is posted to the resolver thread pool
3. **Background Resolution**: Worker thread performs blocking DNS call
4. **Completion Dispatch**: Result is posted back to the original I/O context
5. **Handler Invocation**: User's completion handler is called with results

### Forward Resolution Operation

```cpp
template <typename Protocol, typename Handler, typename IoExecutor>
class resolve_query_op : public resolve_op
{
  static void do_complete(void* owner, operation* base,
      const asio::error_code& /*ec*/,
      std::size_t /*bytes_transferred*/)
  {
    resolve_query_op* o(static_cast<resolve_query_op*>(base));
    
    if (owner && owner != &o->scheduler_)
    {
      // Running on worker thread - perform DNS lookup
      socket_ops::background_getaddrinfo(o->cancel_token_,
          o->query_.host_name().c_str(), 
          o->query_.service_name().c_str(),
          o->query_.hints(), &o->addrinfo_, o->ec_);
      
      // Post back to main context
      o->scheduler_.post_deferred_completion(o);
    }
    else
    {
      // Back on main context - invoke handler
      handler(o->ec_, results_type::create(o->addrinfo_,
          o->query_.host_name(), o->query_.service_name()));
    }
  }
};
```

## Thread Pool Implementation

### Configuration

The resolver thread pool can be configured via the execution context:

```cpp
resolver_thread_pool::resolver_thread_pool(execution_context& context)
  : num_work_threads_(config(context).get("resolver", "threads", 0U))
{
  // If threads > 0, starts that many threads
  // If threads = 0 (default), uses 1 thread lazily created
}
```

### Thread Management

```cpp
void resolver_thread_pool::start_resolve_op(resolve_op* op)
{
  if (scheduler_locking_)
  {
    start_work_threads();  // Lazy thread creation
    scheduler_.work_started();
    work_scheduler_.post_immediate_completion(op, false);
  }
  else
  {
    // Scheduler locking disabled - fail the operation
    op->ec_ = asio::error::operation_not_supported;
    scheduler_.post_immediate_completion(op, false);
  }
}
```

## Platform Differences

### Windows
- Uses native `getaddrinfo()` and `getnameinfo()` when available
- Falls back to emulation on Windows 2000 and earlier
- Special handling for Windows Runtime

### POSIX Systems
- Direct use of POSIX `getaddrinfo()` and `getnameinfo()`
- Platform-specific quirks handled (e.g., macOS port number issues)

### Emulation Mode
For systems without native `getaddrinfo()`:
- Custom implementation based on `gethostbyname()` and `gethostbyaddr()`
- Supports basic resolution but may lack some modern features

## Query and Result Handling

### Query Construction

```cpp
basic_resolver_query<protocol_type> q(
    "example.com",           // hostname
    "http",                  // service
    resolver_base::flags()   // resolution flags
);
```

### Resolution Flags

```cpp
enum flags
{
  canonical_name = AI_CANONNAME,      // Request canonical name
  passive = AI_PASSIVE,               // For bind() operations
  numeric_host = AI_NUMERICHOST,      // No DNS lookup
  numeric_service = AI_NUMERICSERV,   // No service lookup
  v4_mapped = AI_V4MAPPED,            // Map IPv4 to IPv6
  all_matching = AI_ALL,              // Return all addresses
  address_configured = AI_ADDRCONFIG   // Only configured addresses
};
```

### Results Iteration

```cpp
// Results are a range of resolver entries
for (const auto& entry : results)
{
  endpoint_type ep = entry.endpoint();
  std::string host = entry.host_name();
  std::string service = entry.service_name();
}
```

## Usage Examples

### Basic Synchronous Resolution

```cpp
asio::io_context io_context;
tcp::resolver resolver(io_context);

// Forward resolution
tcp::resolver::results_type results = 
    resolver.resolve("www.example.com", "80");

for (const auto& entry : results)
{
  std::cout << entry.endpoint() << std::endl;
}
```

### Asynchronous Resolution

```cpp
resolver.async_resolve("www.example.com", "80",
    [](std::error_code ec, tcp::resolver::results_type results)
    {
        if (!ec)
        {
            for (const auto& entry : results)
            {
                std::cout << entry.endpoint() << std::endl;
            }
        }
    });
```

### Reverse Resolution

```cpp
tcp::endpoint ep(address::from_string("93.184.216.34"), 80);
auto results = resolver.resolve(ep);
std::cout << "Hostname: " << results->host_name() << std::endl;
```

### With Custom Flags

```cpp
// Numeric resolution only (no DNS lookup)
auto results = resolver.resolve("192.168.1.1", "8080",
    resolver_base::numeric_host | resolver_base::numeric_service);
```

## Thread Safety

- **Distinct objects**: Safe to use concurrently
- **Shared objects**: Not safe without external synchronization
- The resolver thread pool is shared and thread-safe
- Each resolver maintains its own cancellation token

## Error Handling

Common error codes:
- `host_not_found`: DNS lookup failed
- `service_not_found`: Service name lookup failed
- `operation_aborted`: Resolution cancelled
- `no_data`: No address found for hostname