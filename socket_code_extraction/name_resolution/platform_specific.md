# ASIO Name Resolution - Platform-Specific Details

This document covers platform-specific aspects of ASIO's name resolution implementation across different operating systems.

## Platform Detection

ASIO uses preprocessor macros to detect the platform and available APIs:

```cpp
// Key platform detection macros
#if defined(ASIO_WINDOWS) || defined(__CYGWIN__)
  // Windows platforms
#elif defined(ASIO_WINDOWS_RUNTIME)
  // Windows Runtime (UWP apps)
#elif defined(ASIO_HAS_GETADDRINFO)
  // POSIX systems with getaddrinfo
#else
  // Systems requiring emulation
#endif
```

## Windows Implementation

### Modern Windows (XP and later)

```cpp
// Direct use of Winsock2 getaddrinfo
int error = ::getaddrinfo(host, service, &hints, result);

// Error translation
asio::error_code translate_addrinfo_error(int error)
{
  switch (error)
  {
  case WSAHOST_NOT_FOUND:
    return asio::error::host_not_found;
  case WSATRY_AGAIN:
    return asio::error::host_not_found_try_again;
  case WSANO_RECOVERY:
    return asio::error::no_recovery;
  case WSANO_DATA:
    return asio::error::no_data;
  // ...
  }
}
```

### Windows 2000 Compatibility

```cpp
// Dynamic loading of getaddrinfo
typedef int (WSAAPI *gai_t)(const char*, const char*, 
                            const addrinfo_type*, addrinfo_type**);

if (HMODULE winsock_module = ::GetModuleHandleA("ws2_32"))
{
  if (gai_t gai = (gai_t)::GetProcAddress(winsock_module, "getaddrinfo"))
  {
    // Use dynamically loaded function
    int error = gai(host, service, &hints, result);
  }
  else
  {
    // Fall back to emulation
    int error = getaddrinfo_emulation(host, service, &hints, result);
  }
}
```

### Windows Runtime (UWP)

```cpp
#if defined(ASIO_WINDOWS_RUNTIME)
// Special implementation for Windows Runtime
try
{
  using namespace Windows::Foundation::Collections;
  using namespace Windows::Networking;
  using namespace Windows::Networking::Connectivity;
  
  // Use Windows Runtime APIs
  IVectorView<EndpointPair^>^ endpoints = /* resolve */;
  
  for (unsigned int i = 0; i < endpoints->Size; ++i)
  {
    auto pair = endpoints->GetAt(i);
    
    // Convert to standard format
    results.values_->push_back(
      basic_resolver_entry<InternetProtocol>(
        typename InternetProtocol::endpoint(
          ip::make_address(
            asio::detail::winrt_utils::string(
              pair->RemoteHostName->CanonicalName)),
          asio::detail::winrt_utils::integer(
            pair->RemoteServiceName)),
        host_name, service_name));
  }
}
catch (Platform::Exception^ e)
{
  ec = asio::error_code(e->HResult, asio::system_category());
}
#endif
```

## POSIX Implementation

### Standard POSIX

```cpp
// Direct getaddrinfo call
int error = ::getaddrinfo(host, service, &hints, result);

// Error mapping
switch (error)
{
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
case EAI_SOCKTYPE:
  return asio::error::socket_type_not_supported;
}
```

### macOS Specific

```cpp
#if defined(__MACH__) && defined(__APPLE__)
// macOS numeric service name workaround
if (error == 0 && service && isdigit(static_cast<unsigned char>(service[0])))
{
  u_short_type port = host_to_network_short(atoi(service));
  for (addrinfo_type* ai = *result; ai; ai = ai->ai_next)
  {
    switch (ai->ai_family)
    {
    case ASIO_OS_DEF(AF_INET):
      {
        sockaddr_in4_type* sinptr =
          reinterpret_cast<sockaddr_in4_type*>(ai->ai_addr);
        if (sinptr->sin_port == 0)
          sinptr->sin_port = port;
        break;
      }
    case ASIO_OS_DEF(AF_INET6):
      {
        sockaddr_in6_type* sin6ptr =
          reinterpret_cast<sockaddr_in6_type*>(ai->ai_addr);
        if (sin6ptr->sin6_port == 0)
          sin6ptr->sin6_port = port;
        break;
      }
    }
  }
}
#endif
```

### Linux Specific

```cpp
// getnameinfo with proper flags
int flags = 0;
if (numeric_host)
  flags |= NI_NUMERICHOST;
if (numeric_service)
  flags |= NI_NUMERICSERV;

int error = ::getnameinfo(addr, addrlen, host, hostlen, 
                         serv, servlen, flags);
```

## Emulation Implementation

For systems without native getaddrinfo support:

### Host Resolution Emulation

```cpp
// Using gethostbyname for forward resolution
#if defined(ASIO_WINDOWS) || defined(__CYGWIN__)
  hostent* retval = ::gethostbyname(name);
#elif defined(__sun) || defined(__QNX__)
  int error = 0;
  hostent* retval = ::gethostbyname_r(name, result, buffer, buflength, &error);
#elif defined(__MACH__) && defined(__APPLE__)
  int error = 0;
  hostent* retval = ::getipnodebyname(name, af, ai_flags, &error);
#else
  hostent* retval = 0;
  int error = 0;
  ::gethostbyname_r(name, result, buffer, buflength, &retval, &error);
#endif
```

### Service Resolution Emulation

```cpp
// Service name to port number
servent* sptr = ::getservbyname(service, 
  (hints.ai_socktype == SOCK_DGRAM) ? "udp" : 0);

if (sptr && sptr->s_port != 0)
{
  port = sptr->s_port;
}
else
{
  // Try numeric conversion
  port = htons(atoi(service));
}
```

## Thread Safety Considerations

### Windows
```cpp
// Windows gethostbyname is not thread-safe
// ASIO uses critical sections for emulation mode
#if defined(ASIO_WINDOWS)
static ::CRITICAL_SECTION gethostbyname_cs;
::EnterCriticalSection(&gethostbyname_cs);
// ... perform lookup ...
::LeaveCriticalSection(&gethostbyname_cs);
#endif
```

### POSIX
```cpp
// Use thread-safe variants where available
#if defined(_POSIX_THREAD_SAFE_FUNCTIONS)
  // Use gethostbyname_r, getservbyname_r
#else
  // Use mutex protection
  static asio::detail::mutex resolver_mutex;
  asio::detail::mutex::scoped_lock lock(resolver_mutex);
#endif
```

## IPv6 Support

### Scope ID Handling

```cpp
// Extract scope ID from IPv6 addresses
const char* if_name = strchr(src, '%');  // Look for %scope_id
if (if_name != 0)
{
  // Parse scope ID
  in6_addr_type* ipv6_address = static_cast<in6_addr_type*>(dest);
  bool is_link_local = ((ipv6_address->s6_addr[0] == 0xfe)
      && ((ipv6_address->s6_addr[1] & 0xc0) == 0x80));
      
  if (is_link_local)
  {
    // Convert interface name to index
    *scope_id = if_nametoindex(if_name + 1);
    if (*scope_id == 0)
      *scope_id = atoi(if_name + 1);
  }
}
```

### Dual Stack Support

```cpp
// IPv4-mapped IPv6 addresses
if (hints->ai_flags & AI_V4MAPPED)
{
  // If no IPv6 addresses found, map IPv4 to IPv6
  if (hints->ai_family == AF_INET6 && !found_ipv6)
  {
    // Convert IPv4 results to IPv4-mapped IPv6
    // ::ffff:192.168.1.1
  }
}
```

## Performance Characteristics

### Windows
- Native getaddrinfo: Fast, cached by Windows DNS Client
- Emulation mode: Slower, less caching
- Windows Runtime: Async by design, different performance profile

### Linux
- Uses system resolver (/etc/resolv.conf)
- Respects nsswitch.conf configuration
- Can use nscd (Name Service Cache Daemon) for caching

### macOS
- Uses mDNSResponder for resolution
- Supports Bonjour/Zeroconf names
- Caches aggressively

### BSD Systems
- Similar to Linux but may have different resolver implementations
- Some BSDs use unbound as system resolver

## Debugging and Diagnostics

### Windows Debug Output
```cpp
#if defined(ASIO_WINDOWS) && defined(_DEBUG)
// Enable Winsock debug output
int debug = 1;
::setsockopt(sock, SOL_SOCKET, SO_DEBUG, 
            (char*)&debug, sizeof(debug));
#endif
```

### POSIX Debug Environment
```bash
# Enable resolver debugging on Linux/BSD
export RES_OPTIONS="debug"

# macOS DNS debugging
sudo killall -USR1 mDNSResponder  # Dump cache
```

### ASIO Handler Tracking
```cpp
// Enable handler tracking
#define ASIO_ENABLE_HANDLER_TRACKING

// In resolver operations
ASIO_HANDLER_CREATION((thread_pool_.context(),
      *p.p, "resolver", &impl, 0, "async_resolve"));
```

## Common Platform Issues

### Windows
1. **Firewall blocking**: Windows Firewall may block DNS queries
2. **IPv6 transition**: Older Windows versions have limited IPv6 support
3. **DNS suffix search**: Controlled by Windows network settings

### Linux
1. **resolv.conf changes**: File may be overwritten by NetworkManager
2. **systemd-resolved**: Modern systems use systemd for DNS
3. **Docker/containers**: Special DNS configuration needed

### macOS
1. **Sandbox restrictions**: App sandboxing may limit DNS access
2. **VPN interactions**: VPN software may interfere with resolution
3. **Split DNS**: macOS supports per-interface DNS configuration

## Platform-Specific Optimizations

### Windows
```cpp
// Use DNS caching hint
hints.ai_flags |= AI_FQDN;  // Request fully qualified domain name
```

### Linux
```cpp
// Use specific address families to avoid unnecessary lookups
if (ipv4_only)
  hints.ai_family = AF_INET;
else if (ipv6_only)
  hints.ai_family = AF_INET6;
```

### All Platforms
```cpp
// Numeric resolution bypass
if (is_numeric_address(host))
{
  hints.ai_flags |= AI_NUMERICHOST;
  // This avoids DNS lookup entirely
}
```