# OpenSSL Integration in ASIO SSL/TLS

## OpenSSL Integration Architecture

### Core Integration Components

```cpp
// OpenSSL type definitions
namespace asio { namespace ssl { namespace detail {
    typedef ::SSL SSL;
    typedef ::SSL_CTX SSL_CTX;
    typedef ::BIO BIO;
    typedef ::X509 X509;
    typedef ::X509_STORE X509_STORE;
    typedef ::X509_STORE_CTX X509_STORE_CTX;
    typedef ::EVP_PKEY EVP_PKEY;
    typedef ::RSA RSA;
    typedef ::DH DH;
}}}
```

### Initialization System

```cpp
template <bool Do_Init = true>
class openssl_init : private noncopyable
{
public:
    // Ensure OpenSSL is initialized exactly once
    openssl_init() : ref_(instance()) {}
    
    // Copy constructor for reference counting
    openssl_init(const openssl_init& other) : ref_(other.ref_) {}
    
private:
    class do_init
    {
    public:
        do_init() {
            // Initialize OpenSSL library
            SSL_library_init();
            SSL_load_error_strings();
            OpenSSL_add_all_algorithms();
        }
        
        ~do_init() {
            // Cleanup OpenSSL (if supported)
            #if OPENSSL_VERSION_NUMBER >= 0x10100000L
            // OpenSSL 1.1.0+ automatically cleans up
            #else
            ERR_free_strings();
            EVP_cleanup();
            #endif
        }
    };
    
    static std::shared_ptr<do_init> instance() {
        static std::weak_ptr<do_init> instance_;
        static std::mutex mutex_;
        
        std::lock_guard<std::mutex> lock(mutex_);
        auto shared_instance = instance_.lock();
        if (!shared_instance) {
            shared_instance = std::make_shared<do_init>();
            instance_ = shared_instance;
        }
        return shared_instance;
    }
    
    std::shared_ptr<do_init> ref_;
};
```

**Design benefits:**
- **Thread safety**: Mutex-protected initialization
- **Reference counting**: Automatic cleanup when no longer needed
- **Version compatibility**: Handles OpenSSL version differences

## SSL Engine Implementation

### Core Engine Structure

```cpp
class engine
{
public:
    enum want {
        want_input_and_retry = -2,    // Need more input data
        want_output_and_retry = -1,   // Need to write output data
        want_nothing = 0,             // Operation complete
        want_output = 1               // Write output and complete
    };
    
    // Construction from SSL context
    explicit engine(SSL_CTX* context);
    
    // Construction from existing SSL object
    explicit engine(SSL* ssl_impl);
    
    // Native handle access
    SSL* native_handle() { return ssl_; }
    
private:
    SSL* ssl_;           // OpenSSL SSL object
    BIO* ext_bio_;       // External BIO for data transport
};
```

### Engine Construction

```cpp
engine::engine(SSL_CTX* context)
    : ssl_(SSL_new(context)), ext_bio_(nullptr)
{
    if (!ssl_) {
        throw std::bad_alloc();
    }
    
    // Create BIO pair for data transport
    BIO* int_bio = nullptr;
    if (!BIO_new_bio_pair(&int_bio, 0, &ext_bio_, 0)) {
        SSL_free(ssl_);
        throw std::bad_alloc();
    }
    
    // Connect SSL object to internal BIO
    SSL_set_bio(ssl_, int_bio, int_bio);
}
```

**BIO pair architecture:**
- **Internal BIO**: Connected to SSL object for cryptographic operations
- **External BIO**: Interface for reading/writing encrypted data
- **Separation**: Allows ASIO to control data flow independently

### SSL Operations Implementation

```cpp
want engine::handshake(stream_base::handshake_type type, asio::error_code& ec)
{
    // Clear any previous errors
    ERR_clear_error();
    
    int result;
    if (type == stream_base::client) {
        result = SSL_connect(ssl_);
    } else {
        result = SSL_accept(ssl_);
    }
    
    return handle_ssl_result(result, ec);
}

want engine::read(const asio::mutable_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
    ERR_clear_error();
    
    int result = SSL_read(ssl_, 
        asio::buffer_cast<void*>(data),
        static_cast<int>(asio::buffer_size(data)));
    
    if (result > 0) {
        bytes_transferred = static_cast<std::size_t>(result);
        ec.clear();
        return want_nothing;
    }
    
    return handle_ssl_result(result, ec);
}

want engine::write(const asio::const_buffer& data,
    asio::error_code& ec, std::size_t& bytes_transferred)
{
    ERR_clear_error();
    
    int result = SSL_write(ssl_,
        asio::buffer_cast<const void*>(data),
        static_cast<int>(asio::buffer_size(data)));
    
    if (result > 0) {
        bytes_transferred = static_cast<std::size_t>(result);
        ec.clear();
        return want_nothing;
    }
    
    return handle_ssl_result(result, ec);
}
```

### SSL Result Handling

```cpp
want engine::handle_ssl_result(int result, asio::error_code& ec)
{
    if (result > 0) {
        ec.clear();
        return want_nothing;
    }
    
    int ssl_error = SSL_get_error(ssl_, result);
    
    switch (ssl_error) {
    case SSL_ERROR_WANT_READ:
        ec.clear();
        return want_input_and_retry;
        
    case SSL_ERROR_WANT_WRITE:
        ec.clear();
        return want_output_and_retry;
        
    case SSL_ERROR_ZERO_RETURN:
        ec = asio::error::eof;
        return want_nothing;
        
    case SSL_ERROR_SYSCALL:
        {
            unsigned long error_code = ERR_get_error();
            if (error_code == 0) {
                if (result == 0) {
                    ec = asio::ssl::error::stream_truncated;
                } else {
                    ec = asio::error_code(errno, asio::error::get_system_category());
                }
            } else {
                ec = asio::error_code(error_code, asio::error::get_ssl_category());
            }
        }
        return want_nothing;
        
    default:
        {
            unsigned long error_code = ERR_get_error();
            if (error_code != 0) {
                ec = asio::error_code(error_code, asio::error::get_ssl_category());
            } else {
                ec = asio::ssl::error::unspecified_system_error;
            }
        }
        return want_nothing;
    }
}
```

## BIO Integration for Data Transport

### BIO Buffer Management

```cpp
asio::mutable_buffer engine::get_output(const asio::mutable_buffer& data)
{
    // Read encrypted data from external BIO
    int bytes_read = BIO_read(ext_bio_,
        asio::buffer_cast<void*>(data),
        static_cast<int>(asio::buffer_size(data)));
    
    if (bytes_read > 0) {
        return asio::mutable_buffer(
            asio::buffer_cast<void*>(data),
            static_cast<std::size_t>(bytes_read));
    }
    
    return asio::mutable_buffer();
}

asio::const_buffer engine::put_input(const asio::const_buffer& data)
{
    // Write encrypted data to external BIO
    int bytes_written = BIO_write(ext_bio_,
        asio::buffer_cast<const void*>(data),
        static_cast<int>(asio::buffer_size(data)));
    
    if (bytes_written > 0) {
        return asio::const_buffer(
            asio::buffer_cast<const void*>(data) + bytes_written,
            asio::buffer_size(data) - static_cast<std::size_t>(bytes_written));
    }
    
    return data;
}
```

### Data Flow Through BIO

```
Application Data
      ↓
  SSL_write()
      ↓
 Internal BIO ←→ SSL Object (Encryption/Decryption)
      ↓
 External BIO
      ↓
  BIO_read() / BIO_write()
      ↓
  ASIO Buffers
      ↓
Transport Layer (TCP Socket)
```

## Context Integration

### SSL_CTX Management

```cpp
class context : public context_base, private noncopyable
{
public:
    explicit context(method m)
        : handle_(create_ssl_ctx(m)), init_()
    {
        if (!handle_) {
            throw std::bad_alloc();
        }
    }
    
    ~context()
    {
        if (handle_) {
            SSL_CTX_free(handle_);
        }
    }
    
    SSL_CTX* native_handle() { return handle_; }
    
private:
    static SSL_CTX* create_ssl_ctx(method m)
    {
        const SSL_METHOD* ssl_method = nullptr;
        
        switch (m) {
        case tls_client:
            ssl_method = TLS_client_method();
            break;
        case tls_server:
            ssl_method = TLS_server_method();
            break;
        case tls:
            ssl_method = TLS_method();
            break;
        // ... other methods
        default:
            return nullptr;
        }
        
        return SSL_CTX_new(ssl_method);
    }
    
    SSL_CTX* handle_;
    openssl_init<> init_;
};
```

### Certificate and Key Loading

```cpp
void context::use_certificate_file(const std::string& filename, file_format format)
{
    asio::error_code ec;
    use_certificate_file(filename, format, ec);
    asio::detail::throw_error(ec, "use_certificate_file");
}

ASIO_SYNC_OP_VOID context::use_certificate_file(
    const std::string& filename, file_format format, asio::error_code& ec)
{
    int file_type = (format == asn1) ? SSL_FILETYPE_ASN1 : SSL_FILETYPE_PEM;
    
    if (SSL_CTX_use_certificate_file(handle_, filename.c_str(), file_type) != 1) {
        ec = asio::error_code(ERR_get_error(), asio::error::get_ssl_category());
    } else {
        ec.clear();
    }
    
    ASIO_SYNC_OP_VOID_RETURN(ec);
}
```

### Memory-Based Certificate Loading

```cpp
void context::use_certificate(const const_buffer& certificate, file_format format,
    asio::error_code& ec)
{
    BIO* bio = make_buffer_bio(certificate);
    if (!bio) {
        ec = asio::error_code(ERR_get_error(), asio::error::get_ssl_category());
        return;
    }
    
    std::unique_ptr<BIO, bio_cleanup> bio_cleanup(bio);
    
    X509* cert = nullptr;
    if (format == pem) {
        cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
    } else {
        cert = d2i_X509_bio(bio, nullptr);
    }
    
    if (!cert) {
        ec = asio::error_code(ERR_get_error(), asio::error::get_ssl_category());
        return;
    }
    
    std::unique_ptr<X509, x509_cleanup> cert_cleanup(cert);
    
    if (SSL_CTX_use_certificate(handle_, cert) != 1) {
        ec = asio::error_code(ERR_get_error(), asio::error::get_ssl_category());
    } else {
        ec.clear();
    }
}
```

## Verification Integration

### Verification Callback Bridge

```cpp
class verify_callback_base
{
public:
    virtual ~verify_callback_base() {}
    virtual bool call(bool preverified, X509_STORE_CTX* ctx) = 0;
};

template <typename VerifyCallback>
class verify_callback : public verify_callback_base
{
public:
    explicit verify_callback(VerifyCallback callback)
        : callback_(callback) {}
    
    bool call(bool preverified, X509_STORE_CTX* ctx) override
    {
        verify_context verify_ctx(ctx);
        return callback_(preverified, verify_ctx);
    }
    
private:
    VerifyCallback callback_;
};

// OpenSSL callback function
int context::verify_callback_function(int preverified, X509_STORE_CTX* ctx)
{
    // Retrieve the verify_callback_base pointer from SSL context
    SSL* ssl = static_cast<SSL*>(X509_STORE_CTX_get_ex_data(ctx, 
        SSL_get_ex_data_X509_STORE_CTX_idx()));
    
    if (ssl) {
        SSL_CTX* ssl_ctx = SSL_get_SSL_CTX(ssl);
        verify_callback_base* callback = static_cast<verify_callback_base*>(
            SSL_CTX_get_ex_data(ssl_ctx, 0));
        
        if (callback) {
            return callback->call(preverified != 0, ctx) ? 1 : 0;
        }
    }
    
    return preverified;
}
```

### Hostname Verification Implementation

```cpp
bool host_name_verification::operator()(bool preverified, verify_context& ctx) const
{
    // Only verify hostname if basic verification passed
    if (!preverified) {
        return false;
    }
    
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    if (!cert) {
        return false;
    }
    
    // Check Subject Alternative Names first
    STACK_OF(GENERAL_NAME)* san_stack = static_cast<STACK_OF(GENERAL_NAME)*>(
        X509_get_ext_d2i(cert, NID_subject_alt_name, nullptr, nullptr));
    
    if (san_stack) {
        bool found_match = false;
        int san_count = sk_GENERAL_NAME_num(san_stack);
        
        for (int i = 0; i < san_count && !found_match; ++i) {
            GENERAL_NAME* san = sk_GENERAL_NAME_value(san_stack, i);
            if (san->type == GEN_DNS) {
                char* dns_name = reinterpret_cast<char*>(
                    ASN1_STRING_get0_data(san->d.dNSName));
                found_match = match_hostname(dns_name, host_);
            }
        }
        
        sk_GENERAL_NAME_pop_free(san_stack, GENERAL_NAME_free);
        return found_match;
    }
    
    // Fall back to Common Name
    X509_NAME* subject = X509_get_subject_name(cert);
    char common_name[256];
    int cn_len = X509_NAME_get_text_by_NID(subject, NID_commonName, 
        common_name, sizeof(common_name));
    
    if (cn_len > 0) {
        return match_hostname(std::string(common_name, cn_len), host_);
    }
    
    return false;
}

bool host_name_verification::match_hostname(const std::string& pattern, 
    const std::string& hostname) const
{
    // Handle wildcard certificates
    if (pattern.size() > 2 && pattern.substr(0, 2) == "*.") {
        std::string pattern_domain = pattern.substr(2);
        std::size_t dot_pos = hostname.find('.');
        
        if (dot_pos != std::string::npos) {
            std::string hostname_domain = hostname.substr(dot_pos + 1);
            return pattern_domain == hostname_domain;
        }
    }
    
    // Exact match
    return pattern == hostname;
}
```

## Error Handling and Translation

### Error Category Implementation

```cpp
class ssl_category_impl : public asio::error_category
{
public:
    const char* name() const noexcept override
    {
        return "asio.ssl";
    }
    
    std::string message(int value) const override
    {
        const char* reason = ERR_reason_error_string(value);
        if (reason) {
            return reason;
        }
        return "Unknown SSL error";
    }
};

const asio::error_category& get_ssl_category()
{
    static ssl_category_impl instance;
    return instance;
}
```

### Error Code Translation

```cpp
asio::error_code context::translate_error(long error)
{
    switch (error) {
    case SSL_ERROR_NONE:
        return asio::error_code();
        
    case SSL_ERROR_SSL:
        return asio::error_code(static_cast<int>(ERR_get_error()),
            asio::error::get_ssl_category());
        
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
        return asio::error::would_block;
        
    case SSL_ERROR_ZERO_RETURN:
        return asio::error::eof;
        
    case SSL_ERROR_SYSCALL:
        {
            unsigned long openssl_error = ERR_get_error();
            if (openssl_error == 0) {
                return asio::error_code(errno, asio::error::get_system_category());
            }
            return asio::error_code(static_cast<int>(openssl_error),
                asio::error::get_ssl_category());
        }
        
    default:
        return asio::ssl::error::unspecified_system_error;
    }
}
```

## Thread Safety Considerations

### SSL Object Thread Safety

```cpp
#if (OPENSSL_VERSION_NUMBER < 0x10100000L)
// For OpenSSL < 1.1.0, SSL_accept may not be thread-safe
static asio::detail::static_mutex& accept_mutex()
{
    static asio::detail::static_mutex mutex;
    return mutex;
}

int engine::do_accept(void*, std::size_t)
{
    std::lock_guard<asio::detail::static_mutex> lock(accept_mutex());
    return SSL_accept(ssl_);
}
#else
// OpenSSL 1.1.0+ is thread-safe
int engine::do_accept(void*, std::size_t)
{
    return SSL_accept(ssl_);
}
#endif
```

### Locking Strategy

```cpp
class thread_safe_ssl_engine
{
    mutable std::mutex ssl_mutex_;
    engine ssl_engine_;
    
public:
    template <typename... Args>
    auto thread_safe_operation(Args&&... args) -> decltype(ssl_engine_.operation(args...))
    {
        std::lock_guard<std::mutex> lock(ssl_mutex_);
        return ssl_engine_.operation(std::forward<Args>(args)...);
    }
};
```

## Performance Optimizations

### Buffer Management Optimization

```cpp
class optimized_bio_manager
{
    static constexpr size_t buffer_size = 17 * 1024; // Max TLS record size
    std::unique_ptr<unsigned char[]> input_buffer_;
    std::unique_ptr<unsigned char[]> output_buffer_;
    
public:
    optimized_bio_manager()
        : input_buffer_(std::make_unique<unsigned char[]>(buffer_size)),
          output_buffer_(std::make_unique<unsigned char[]>(buffer_size)) {}
    
    asio::mutable_buffer get_input_buffer()
    {
        return asio::mutable_buffer(input_buffer_.get(), buffer_size);
    }
    
    asio::mutable_buffer get_output_buffer()
    {
        return asio::mutable_buffer(output_buffer_.get(), buffer_size);
    }
};
```

### Memory Pool for SSL Objects

```cpp
class ssl_object_pool
{
    std::stack<std::unique_ptr<SSL, decltype(&SSL_free)>> pool_;
    std::mutex pool_mutex_;
    SSL_CTX* context_;
    
public:
    explicit ssl_object_pool(SSL_CTX* ctx) : context_(ctx) {}
    
    std::unique_ptr<SSL, decltype(&SSL_free)> acquire()
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        if (!pool_.empty()) {
            auto ssl = std::move(pool_.top());
            pool_.pop();
            SSL_clear(ssl.get()); // Reset state
            return ssl;
        }
        
        return std::unique_ptr<SSL, decltype(&SSL_free)>(
            SSL_new(context_), &SSL_free);
    }
    
    void release(std::unique_ptr<SSL, decltype(&SSL_free)> ssl)
    {
        std::lock_guard<std::mutex> lock(pool_mutex_);
        pool_.push(std::move(ssl));
    }
};
```

This comprehensive OpenSSL integration demonstrates how ASIO provides a robust, efficient, and secure interface to OpenSSL while maintaining the library's characteristic ease of use and performance.