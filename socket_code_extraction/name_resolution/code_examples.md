# ASIO Name Resolution - Code Examples

This document provides practical code examples for using ASIO's name resolution functionality.

## Basic Examples

### Simple TCP Client Resolution

```cpp
#include <asio.hpp>
#include <iostream>

int main()
{
    asio::io_context io_context;
    asio::ip::tcp::resolver resolver(io_context);
    
    try
    {
        // Resolve hostname to IP addresses
        auto endpoints = resolver.resolve("www.google.com", "80");
        
        // Print all resolved endpoints
        for (const auto& endpoint : endpoints)
        {
            std::cout << "Endpoint: " << endpoint.endpoint() << std::endl;
            std::cout << "  Host: " << endpoint.host_name() << std::endl;
            std::cout << "  Service: " << endpoint.service_name() << std::endl;
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Resolution failed: " << e.what() << std::endl;
    }
    
    return 0;
}
```

### UDP Resolution

```cpp
#include <asio.hpp>

int main()
{
    asio::io_context io_context;
    asio::ip::udp::resolver resolver(io_context);
    
    // Resolve for UDP
    auto endpoints = resolver.resolve(
        asio::ip::udp::v4(),  // Protocol hint
        "8.8.8.8",            // Google DNS
        "53"                  // DNS port
    );
    
    for (const auto& endpoint : endpoints)
    {
        std::cout << "UDP endpoint: " << endpoint.endpoint() << std::endl;
    }
    
    return 0;
}
```

## Asynchronous Examples

### Basic Async Resolution

```cpp
#include <asio.hpp>
#include <iostream>

int main()
{
    asio::io_context io_context;
    asio::ip::tcp::resolver resolver(io_context);
    
    resolver.async_resolve("example.com", "https",
        [](std::error_code ec, asio::ip::tcp::resolver::results_type results)
        {
            if (!ec)
            {
                std::cout << "Resolution succeeded:" << std::endl;
                for (const auto& endpoint : results)
                {
                    std::cout << "  " << endpoint.endpoint() << std::endl;
                }
            }
            else
            {
                std::cerr << "Resolution failed: " << ec.message() << std::endl;
            }
        });
    
    io_context.run();
    return 0;
}
```

### Async Resolution with Connection

```cpp
#include <asio.hpp>
#include <memory>

class AsyncClient : public std::enable_shared_from_this<AsyncClient>
{
    asio::ip::tcp::resolver resolver_;
    asio::ip::tcp::socket socket_;
    
public:
    AsyncClient(asio::io_context& io_context)
        : resolver_(io_context), socket_(io_context)
    {
    }
    
    void connect(const std::string& host, const std::string& service)
    {
        auto self = shared_from_this();
        
        resolver_.async_resolve(host, service,
            [this, self](std::error_code ec, 
                        asio::ip::tcp::resolver::results_type endpoints)
            {
                if (!ec)
                {
                    // Connect to the resolved endpoints
                    asio::async_connect(socket_, endpoints,
                        [this, self](std::error_code ec, asio::ip::tcp::endpoint)
                        {
                            if (!ec)
                            {
                                std::cout << "Connected!" << std::endl;
                                // Start async operations...
                            }
                        });
                }
            });
    }
};
```

## Advanced Resolution

### Using Resolution Flags

```cpp
#include <asio.hpp>

void resolve_with_flags()
{
    asio::io_context io_context;
    asio::ip::tcp::resolver resolver(io_context);
    
    // Example 1: Numeric host only (no DNS lookup)
    {
        auto results = resolver.resolve("192.168.1.1", "80",
            asio::ip::resolver_base::numeric_host);
        std::cout << "Numeric host: " << results->endpoint() << std::endl;
    }
    
    // Example 2: For server binding (passive flag)
    {
        auto results = resolver.resolve("", "8080",
            asio::ip::resolver_base::passive |
            asio::ip::resolver_base::address_configured);
        
        std::cout << "Bind addresses:" << std::endl;
        for (const auto& r : results)
            std::cout << "  " << r.endpoint() << std::endl;
    }
    
    // Example 3: Get canonical name
    {
        auto results = resolver.resolve("www.google.com", "80",
            asio::ip::resolver_base::canonical_name);
        
        if (!results.empty())
        {
            std::cout << "Canonical name: " 
                     << results->host_name() << std::endl;
        }
    }
    
    // Example 4: IPv4-mapped IPv6 addresses
    {
        auto results = resolver.resolve(
            asio::ip::tcp::v6(),  // IPv6 protocol
            "example.com", 
            "80",
            asio::ip::resolver_base::v4_mapped |
            asio::ip::resolver_base::all_matching);
        
        for (const auto& r : results)
            std::cout << "IPv6/mapped: " << r.endpoint() << std::endl;
    }
}
```

### Reverse Resolution

```cpp
#include <asio.hpp>

void reverse_dns_lookup()
{
    asio::io_context io_context;
    asio::ip::tcp::resolver resolver(io_context);
    
    // Create endpoint from IP address
    asio::ip::tcp::endpoint ep(
        asio::ip::address::from_string("8.8.8.8"), 
        53
    );
    
    try
    {
        // Reverse DNS lookup
        auto results = resolver.resolve(ep);
        
        std::cout << "Reverse DNS for " << ep << ":" << std::endl;
        std::cout << "  Hostname: " << results->host_name() << std::endl;
        std::cout << "  Service: " << results->service_name() << std::endl;
    }
    catch (std::exception& e)
    {
        std::cerr << "Reverse lookup failed: " << e.what() << std::endl;
    }
}
```

## Error Handling

### Comprehensive Error Handling

```cpp
#include <asio.hpp>

void resolve_with_error_handling()
{
    asio::io_context io_context;
    asio::ip::tcp::resolver resolver(io_context);
    
    // Using error_code instead of exceptions
    std::error_code ec;
    auto results = resolver.resolve("nonexistent.invalid", "80", ec);
    
    if (ec)
    {
        switch (ec.value())
        {
        case asio::error::host_not_found:
            std::cerr << "Host not found" << std::endl;
            break;
            
        case asio::error::host_not_found_try_again:
            std::cerr << "Temporary DNS failure" << std::endl;
            break;
            
        case asio::error::service_not_found:
            std::cerr << "Service not found" << std::endl;
            break;
            
        case asio::error::no_data:
            std::cerr << "No address associated with hostname" << std::endl;
            break;
            
        default:
            std::cerr << "Resolution error: " << ec.message() << std::endl;
        }
    }
}
```

### Async with Timeout

```cpp
#include <asio.hpp>
#include <chrono>

class ResolverWithTimeout
{
    asio::io_context& io_context_;
    asio::ip::tcp::resolver resolver_;
    asio::steady_timer timer_;
    
public:
    ResolverWithTimeout(asio::io_context& io_context)
        : io_context_(io_context)
        , resolver_(io_context)
        , timer_(io_context)
    {
    }
    
    template<typename Handler>
    void async_resolve_with_timeout(
        const std::string& host,
        const std::string& service,
        std::chrono::milliseconds timeout,
        Handler&& handler)
    {
        // Start timer
        timer_.expires_after(timeout);
        timer_.async_wait(
            [this](std::error_code ec)
            {
                if (!ec) // Timer expired
                {
                    resolver_.cancel();
                }
            });
        
        // Start resolution
        resolver_.async_resolve(host, service,
            [this, handler = std::forward<Handler>(handler)]
            (std::error_code ec, asio::ip::tcp::resolver::results_type results)
            {
                timer_.cancel();
                handler(ec, results);
            });
    }
};
```

## Custom Resolution

### Caching Resolver

```cpp
#include <asio.hpp>
#include <unordered_map>
#include <chrono>

class CachingResolver
{
    struct CacheEntry
    {
        asio::ip::tcp::resolver::results_type results;
        std::chrono::steady_clock::time_point expiry;
    };
    
    asio::ip::tcp::resolver resolver_;
    std::unordered_map<std::string, CacheEntry> cache_;
    std::chrono::seconds cache_ttl_{300}; // 5 minutes
    
public:
    CachingResolver(asio::io_context& io_context)
        : resolver_(io_context)
    {
    }
    
    asio::ip::tcp::resolver::results_type resolve(
        const std::string& host,
        const std::string& service)
    {
        std::string key = host + ":" + service;
        auto now = std::chrono::steady_clock::now();
        
        // Check cache
        auto it = cache_.find(key);
        if (it != cache_.end() && it->second.expiry > now)
        {
            return it->second.results;
        }
        
        // Resolve and cache
        auto results = resolver_.resolve(host, service);
        cache_[key] = {results, now + cache_ttl_};
        
        return results;
    }
};
```

### Multi-Resolver with Fallback

```cpp
#include <asio.hpp>
#include <vector>

class MultiResolver
{
    asio::io_context& io_context_;
    std::vector<std::string> dns_servers_;
    
public:
    MultiResolver(asio::io_context& io_context,
                  std::vector<std::string> dns_servers)
        : io_context_(io_context)
        , dns_servers_(std::move(dns_servers))
    {
    }
    
    template<typename Handler>
    void async_resolve(const std::string& host,
                      const std::string& service,
                      Handler&& handler)
    {
        async_resolve_recursive(host, service, 0,
            std::forward<Handler>(handler));
    }
    
private:
    template<typename Handler>
    void async_resolve_recursive(
        const std::string& host,
        const std::string& service,
        size_t server_index,
        Handler&& handler)
    {
        if (server_index >= dns_servers_.size())
        {
            // All servers failed
            handler(asio::error::host_not_found,
                   asio::ip::tcp::resolver::results_type{});
            return;
        }
        
        // Try current DNS server
        // (This is simplified - actual implementation would need
        // to configure DNS server for the resolver)
        asio::ip::tcp::resolver resolver(io_context_);
        
        resolver.async_resolve(host, service,
            [this, host, service, server_index, 
             handler = std::forward<Handler>(handler)]
            (std::error_code ec, asio::ip::tcp::resolver::results_type results)
            {
                if (!ec)
                {
                    handler(ec, results);
                }
                else
                {
                    // Try next server
                    async_resolve_recursive(host, service, 
                        server_index + 1, std::move(handler));
                }
            });
    }
};
```

## Performance Optimization

### Batch Resolution

```cpp
#include <asio.hpp>
#include <vector>
#include <future>

class BatchResolver
{
    asio::io_context& io_context_;
    
public:
    BatchResolver(asio::io_context& io_context)
        : io_context_(io_context)
    {
    }
    
    std::vector<asio::ip::tcp::resolver::results_type>
    resolve_batch(const std::vector<std::pair<std::string, std::string>>& queries)
    {
        std::vector<std::promise<asio::ip::tcp::resolver::results_type>> promises(queries.size());
        std::vector<std::future<asio::ip::tcp::resolver::results_type>> futures;
        
        for (auto& p : promises)
            futures.push_back(p.get_future());
        
        // Start all resolutions
        for (size_t i = 0; i < queries.size(); ++i)
        {
            asio::ip::tcp::resolver resolver(io_context_);
            
            resolver.async_resolve(queries[i].first, queries[i].second,
                [&promise = promises[i]]
                (std::error_code ec, asio::ip::tcp::resolver::results_type results)
                {
                    if (!ec)
                        promise.set_value(results);
                    else
                        promise.set_exception(
                            std::make_exception_ptr(
                                asio::system_error(ec)));
                });
        }
        
        // Run until all complete
        io_context_.run();
        
        // Collect results
        std::vector<asio::ip::tcp::resolver::results_type> results;
        for (auto& f : futures)
        {
            try
            {
                results.push_back(f.get());
            }
            catch (...)
            {
                results.push_back(asio::ip::tcp::resolver::results_type{});
            }
        }
        
        return results;
    }
};
```

## Integration Examples

### HTTP Client with Resolution

```cpp
#include <asio.hpp>
#include <iostream>
#include <string>

class SimpleHttpClient
{
    asio::io_context& io_context_;
    asio::ip::tcp::resolver resolver_;
    asio::ip::tcp::socket socket_;
    asio::streambuf request_;
    asio::streambuf response_;
    
public:
    SimpleHttpClient(asio::io_context& io_context)
        : io_context_(io_context)
        , resolver_(io_context)
        , socket_(io_context)
    {
    }
    
    void get(const std::string& host, const std::string& path)
    {
        // Prepare request
        std::ostream request_stream(&request_);
        request_stream << "GET " << path << " HTTP/1.1\r\n";
        request_stream << "Host: " << host << "\r\n";
        request_stream << "Connection: close\r\n\r\n";
        
        // Resolve host
        resolver_.async_resolve(host, "http",
            [this](std::error_code ec, asio::ip::tcp::resolver::results_type endpoints)
            {
                if (!ec)
                {
                    async_connect(socket_, endpoints,
                        [this](std::error_code ec, asio::ip::tcp::endpoint)
                        {
                            if (!ec)
                            {
                                send_request();
                            }
                        });
                }
            });
    }
    
private:
    void send_request()
    {
        asio::async_write(socket_, request_,
            [this](std::error_code ec, std::size_t)
            {
                if (!ec)
                {
                    read_response();
                }
            });
    }
    
    void read_response()
    {
        asio::async_read_until(socket_, response_, "\r\n\r\n",
            [this](std::error_code ec, std::size_t)
            {
                if (!ec)
                {
                    std::istream response_stream(&response_);
                    std::string line;
                    while (std::getline(response_stream, line))
                    {
                        std::cout << line << std::endl;
                    }
                }
            });
    }
};
```