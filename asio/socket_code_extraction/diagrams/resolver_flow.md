# DNS Resolver Flow

This diagram illustrates how ASIO handles DNS resolution, including both synchronous and asynchronous resolution paths, caching, and the internal thread pool.

```mermaid
flowchart TB
    %% User initiation
    User[User Code] -->|"resolver.async_resolve(query, handler)"| AsyncResolve[Async Resolve Request]
    User -->|"resolver.resolve(query)"| SyncResolve[Sync Resolve Request]
    
    %% Query parsing
    AsyncResolve --> ParseQuery[Parse Query<br/>- Host name<br/>- Service/Port<br/>- Flags<br/>- Protocol hints]
    SyncResolve --> ParseQuery
    
    %% Cache check
    ParseQuery --> CacheCheck{Check<br/>Cache?}
    CacheCheck -->|Hit| ReturnCached[Return Cached Results]
    CacheCheck -->|Miss| CheckNumeric{Numeric<br/>Address?}
    
    %% Numeric address handling
    CheckNumeric -->|"Yes (e.g., 192.168.1.1)"| ParseNumeric[Parse Numeric Address<br/>inet_pton()]
    CheckNumeric -->|"No (e.g., example.com)"| CheckService{Service<br/>Numeric?}
    
    ParseNumeric --> BuildEndpoint[Build Endpoint<br/>Address + Port]
    
    %% Service resolution
    CheckService -->|"Yes (e.g., 80)"| NumericPort[Use Numeric Port]
    CheckService -->|"No (e.g., 'http')"| ServiceLookup[Service Lookup<br/>getservbyname()]
    
    ServiceLookup --> ServiceResult[Service Port]
    NumericPort --> DNSQuery[DNS Query Required]
    ServiceResult --> DNSQuery
    
    %% Resolver implementation selection
    DNSQuery --> PlatformCheck{Platform?}
    
    %% Platform-specific paths
    PlatformCheck -->|Windows| WinResolver[Windows Resolver<br/>GetAddrInfoEx]
    PlatformCheck -->|POSIX| PosixResolver[POSIX Resolver<br/>getaddrinfo]
    PlatformCheck -->|Custom| CustomResolver[Custom Resolver<br/>Implementation]
    
    %% Async handling
    subgraph "Async Resolution"
        AsyncCheck{Async?}
        AsyncCheck -->|Yes| ThreadPoolCheck{Thread Pool<br/>Available?}
        ThreadPoolCheck -->|No| CreatePool[Create Resolver<br/>Thread Pool]
        ThreadPoolCheck -->|Yes| SubmitWork[Submit to Thread Pool]
        CreatePool --> SubmitWork
        
        SubmitWork --> WorkerThread[Worker Thread<br/>Executes Resolution]
        WorkerThread --> BlockingCall[Blocking DNS Call<br/>getaddrinfo()]
    end
    
    WinResolver --> AsyncCheck
    PosixResolver --> AsyncCheck
    CustomResolver --> AsyncCheck
    
    AsyncCheck -->|No| BlockingCall
    
    %% DNS resolution process
    subgraph "DNS Resolution Details"
        BlockingCall --> SystemResolver[System Resolver]
        SystemResolver --> CheckHosts{Check<br/>/etc/hosts?}
        CheckHosts -->|Found| HostsResult[Return from hosts]
        CheckHosts -->|Not Found| CheckNSCache{Check<br/>NS Cache?}
        
        CheckNSCache -->|Hit| NSCacheResult[Return from NS cache]
        CheckNSCache -->|Miss| DNSProtocol{DNS<br/>Protocol}
        
        DNSProtocol -->|UDP| UDPQuery[UDP Query<br/>Port 53]
        DNSProtocol -->|TCP| TCPQuery[TCP Query<br/>Port 53]
        DNSProtocol -->|DoT| DoTQuery[DNS over TLS<br/>Port 853]
        DNSProtocol -->|DoH| DoHQuery[DNS over HTTPS<br/>Port 443]
        
        UDPQuery --> DNSServer[DNS Server<br/>Query]
        TCPQuery --> DNSServer
        DoTQuery --> DNSServer
        DoHQuery --> DNSServer
        
        DNSServer --> DNSResponse{Response<br/>Type}
        DNSResponse -->|A/AAAA| AddressRecords[IP Addresses]
        DNSResponse -->|CNAME| CNAMEFollow[Follow CNAME]
        DNSResponse -->|NXDOMAIN| NoSuchDomain[Domain Not Found]
        DNSResponse -->|SERVFAIL| ServerFailure[Server Error]
        
        CNAMEFollow --> DNSServer
    end
    
    %% Result processing
    HostsResult --> ProcessResults[Process Results]
    NSCacheResult --> ProcessResults
    AddressRecords --> ProcessResults
    NoSuchDomain --> ErrorResult[Generate Error]
    ServerFailure --> ErrorResult
    
    ProcessResults --> SortResults{Sort<br/>Results?}
    SortResults -->|"Yes (RFC 3484)"| RFC3484[RFC 3484 Sorting<br/>- Prefer same scope<br/>- Prefer native transport<br/>- Prefer smaller scope<br/>- Prefer longer prefix match]
    SortResults -->|No| BuildResults[Build Result List]
    
    RFC3484 --> BuildResults
    BuildEndpoint --> BuildResults
    
    %% IPv4/IPv6 handling
    BuildResults --> IPVersionCheck{IP Version<br/>Preference?}
    IPVersionCheck -->|v4_mapped| V4Mapped[Convert IPv4 to<br/>IPv4-mapped IPv6]
    IPVersionCheck -->|v6_only| V6Only[Filter IPv6 Only]
    IPVersionCheck -->|v4_only| V4Only[Filter IPv4 Only]
    IPVersionCheck -->|Any| BothVersions[Include Both]
    
    V4Mapped --> FinalResults[Final Results<br/>List of Endpoints]
    V6Only --> FinalResults
    V4Only --> FinalResults
    BothVersions --> FinalResults
    
    %% Caching
    FinalResults --> CacheUpdate{Update<br/>Cache?}
    CacheUpdate -->|Yes| UpdateCache[Store in Cache<br/>with TTL]
    CacheUpdate -->|No| SkipCache[Skip Caching]
    
    %% Return path
    UpdateCache --> CompleteOp[Complete Operation]
    SkipCache --> CompleteOp
    ErrorResult --> CompleteOp
    ReturnCached --> CompleteOp
    
    %% Async completion
    CompleteOp --> AsyncComplete{Async<br/>Operation?}
    AsyncComplete -->|Yes| PostCompletion[Post to io_context]
    AsyncComplete -->|No| ReturnDirect[Return Directly]
    
    PostCompletion --> InvokeHandler[Invoke Handler<br/>handler(ec, results)]
    ReturnDirect --> ReturnResults[Return Results<br/>Iterator]
    
    WorkerThread -.->|Completes| CompleteOp
```

## Resolver Components Explained

### 1. Query Types

**Basic Query**:
```cpp
tcp::resolver::query query("example.com", "http");
tcp::resolver::query query("example.com", "80");
tcp::resolver::query query(tcp::v4(), "example.com", "80");
```

**Query Flags**:
```cpp
// Common flags
query::passive          // For servers (AI_PASSIVE)
query::canonical_name   // Request canonical name
query::numeric_host     // Don't resolve, numeric only
query::numeric_service  // Don't resolve service names
query::v4_mapped        // Map IPv4 to IPv6
query::all_matching     // Return all addresses
```

### 2. Resolution Process

**Numeric Address Fast Path**:
- Detects numeric IP addresses (e.g., "192.168.1.1")
- Uses `inet_pton()` for parsing
- Bypasses DNS entirely
- Much faster than DNS lookup

**Service Resolution**:
- Numeric ports used directly
- Service names resolved via `getservbyname()`
- Common services: "http" (80), "https" (443), etc.

### 3. Thread Pool Management

**Async Resolution**:
```cpp
// ASIO manages a thread pool for blocking DNS calls
// Default: 1 thread per CPU core (new in recent versions)
// Configurable via ASIO_RESOLVER_THREAD_COUNT
```

**Thread Pool Benefits**:
- Prevents blocking the io_context
- Allows parallel DNS queries
- Handles slow DNS servers gracefully

### 4. Platform Differences

**Windows (GetAddrInfoEx)**:
- Native async support (Vista+)
- Can avoid thread pool on modern Windows
- Integrated with Windows DNS cache

**POSIX (getaddrinfo)**:
- Blocking call, requires thread pool
- Respects `/etc/hosts`, `/etc/resolv.conf`
- Uses nsswitch.conf for resolution order

### 5. Caching Strategy

**Internal Cache** (Implementation-dependent):
- Caches successful resolutions
- Respects DNS TTL values
- Reduces repeated lookups
- Per-resolver or global cache

**System Cache**:
- OS-level DNS caching
- nscd on Linux
- DNS Client service on Windows

### 6. Result Sorting (RFC 3484/6724)

**Destination Address Selection**:
1. Prefer same address family
2. Prefer appropriate scope
3. Avoid deprecated addresses
4. Prefer home addresses
5. Prefer matching label
6. Prefer higher precedence
7. Prefer native transport
8. Prefer smaller scope
9. Use longest matching prefix

### 7. Error Handling

**Common Errors**:
```cpp
error::host_not_found      // NXDOMAIN
error::host_not_found_try_again  // Temporary failure
error::service_not_found   // Unknown service name
error::no_data            // No address records
```

### 8. IPv4/IPv6 Handling

**Dual Stack**:
```cpp
// Query both IPv4 and IPv6
tcp::resolver::query query(tcp::v4(), "example.com", "80");
tcp::resolver::query query(tcp::v6(), "example.com", "80");

// Or use unspecified for both
tcp::resolver::query query("example.com", "80");
```

**IPv4-mapped IPv6**:
```cpp
// Allows IPv6 sockets to handle IPv4 connections
query::v4_mapped flag
// Results in ::ffff:192.168.1.1 format
```

### 9. Advanced Features

**Reverse DNS**:
```cpp
// Resolve IP to hostname
tcp::endpoint ep(address::from_string("8.8.8.8"), 80);
resolver.async_resolve(ep, handler);
```

**SRV Records** (Manual):
```cpp
// ASIO doesn't directly support SRV
// Use system resolver or third-party library
```

### 10. Best Practices

**Connection Attempts**:
```cpp
// Try all returned addresses
async_connect(socket, resolver_results, handler);
```

**Timeout Handling**:
```cpp
// DNS queries can be slow
deadline_timer timeout(io_context);
timeout.expires_from_now(seconds(5));
```

**Error Recovery**:
```cpp
// Retry with different query flags
// Fall back to numeric addresses
// Implement exponential backoff
```

This resolver flow demonstrates how ASIO abstracts the complexity of DNS resolution while providing flexibility for different use cases and platforms.