# SSL Context Management and Configuration

## Context Class Design

### Core Structure

```cpp
class context : public context_base, private noncopyable
{
public:
    typedef SSL_CTX* native_handle_type;
    
    // Construction and destruction
    ASIO_DECL explicit context(method m);
    ASIO_DECL explicit context(native_handle_type native_handle);
    ASIO_DECL context(context&& other);
    ASIO_DECL context& operator=(context&& other);
    ASIO_DECL ~context();

private:
    native_handle_type handle_;                          // SSL_CTX*
    asio::ssl::detail::openssl_init<> init_;            // OpenSSL initialization
};
```

## Context Base Definitions

### SSL/TLS Protocol Methods

```cpp
enum method
{
    // Legacy protocols
    sslv2, sslv2_client, sslv2_server,
    sslv3, sslv3_client, sslv3_server,
    
    // TLS protocols
    tlsv1, tlsv1_client, tlsv1_server,
    tlsv11, tlsv11_client, tlsv11_server,
    tlsv12, tlsv12_client, tlsv12_server,
    tlsv13, tlsv13_client, tlsv13_server,
    
    // Generic protocols
    sslv23, sslv23_client, sslv23_server,  // Legacy generic
    tls, tls_client, tls_server             // Modern generic
};
```

**Design considerations:**
- **Backward compatibility**: Support for legacy protocols
- **Role-specific**: Separate client/server configurations
- **Future-proof**: Generic TLS for automatic protocol negotiation

### SSL Options

```cpp
typedef uint64_t options;

static const uint64_t default_workarounds = SSL_OP_ALL;
static const uint64_t single_dh_use = SSL_OP_SINGLE_DH_USE;
static const uint64_t no_sslv2 = SSL_OP_NO_SSLv2;
static const uint64_t no_sslv3 = SSL_OP_NO_SSLv3;
static const uint64_t no_tlsv1 = SSL_OP_NO_TLSv1;
static const uint64_t no_tlsv1_1 = SSL_OP_NO_TLSv1_1;
static const uint64_t no_tlsv1_2 = SSL_OP_NO_TLSv1_2;
static const uint64_t no_tlsv1_3 = SSL_OP_NO_TLSv1_3;
static const uint64_t no_compression = SSL_OP_NO_COMPRESSION;
```

**Option categories:**
- **Compatibility**: Workarounds for SSL implementation bugs
- **Security**: Disable insecure protocols and features
- **Performance**: Optimize for speed vs security trade-offs

### File Formats

```cpp
enum file_format
{
    asn1,  // ASN.1 DER format
    pem    // PEM text format
};
```

### Password Purposes

```cpp
enum password_purpose
{
    for_reading,    // Decrypting private keys
    for_writing     // Encrypting private keys
};
```

## Context Configuration Methods

### Option Management

```cpp
void clear_options(options o);
ASIO_SYNC_OP_VOID clear_options(options o, asio::error_code& ec);

void set_options(options o);
ASIO_SYNC_OP_VOID set_options(options o, asio::error_code& ec);
```

**Implementation pattern:**
```cpp
void set_options(options o)
{
    asio::error_code ec;
    set_options(o, ec);
    asio::detail::throw_error(ec, "set_options");
}
```

**Usage example:**
```cpp
context ctx(context::tls);
ctx.set_options(
    context::default_workarounds |
    context::no_sslv2 |
    context::no_sslv3 |
    context::single_dh_use
);
```

### Verification Configuration

```cpp
void set_verify_mode(verify_mode v);
ASIO_SYNC_OP_VOID set_verify_mode(verify_mode v, asio::error_code& ec);

void set_verify_depth(int depth);
ASIO_SYNC_OP_VOID set_verify_depth(int depth, asio::error_code& ec);

template <typename VerifyCallback>
void set_verify_callback(VerifyCallback callback);
template <typename VerifyCallback>
ASIO_SYNC_OP_VOID set_verify_callback(VerifyCallback callback, asio::error_code& ec);
```

**Verification callback signature:**
```cpp
bool verify_callback(
    bool preverified,        // True if certificate passed pre-verification
    verify_context& ctx      // Certificate and context information
);
```

## Certificate Management

### Loading Verification Certificates

```cpp
// From file
void load_verify_file(const std::string& filename);
ASIO_SYNC_OP_VOID load_verify_file(const std::string& filename, asio::error_code& ec);

// From memory
void add_certificate_authority(const const_buffer& ca);
ASIO_SYNC_OP_VOID add_certificate_authority(const const_buffer& ca, asio::error_code& ec);

// System defaults
void set_default_verify_paths();
ASIO_SYNC_OP_VOID set_default_verify_paths(asio::error_code& ec);

// Custom directory
void add_verify_path(const std::string& path);
ASIO_SYNC_OP_VOID add_verify_path(const std::string& path, asio::error_code& ec);
```

**Usage patterns:**
```cpp
// Load system CA certificates
ctx.set_default_verify_paths();

// Load custom CA file
ctx.load_verify_file("/path/to/ca-bundle.pem");

// Add CA from memory
std::string ca_cert = "-----BEGIN CERTIFICATE-----\n...";
ctx.add_certificate_authority(asio::buffer(ca_cert));
```

### Server Certificate Configuration

```cpp
// Single certificate
void use_certificate(const const_buffer& certificate, file_format format);
void use_certificate_file(const std::string& filename, file_format format);

// Certificate chain
void use_certificate_chain(const const_buffer& chain);
void use_certificate_chain_file(const std::string& filename);
```

**Certificate chain handling:**
- **Primary certificate**: Server's own certificate (first in chain)
- **Intermediate certificates**: Chain to root CA
- **Automatic ordering**: PEM format allows multiple certificates

### Private Key Configuration

```cpp
// Generic private key
void use_private_key(const const_buffer& private_key, file_format format);
void use_private_key_file(const std::string& filename, file_format format);

// RSA-specific (legacy)
void use_rsa_private_key(const const_buffer& private_key, file_format format);
void use_rsa_private_key_file(const std::string& filename, file_format format);
```

**Key management considerations:**
- **Format support**: Both PEM and DER formats
- **Encryption**: Password-protected keys supported
- **Key types**: RSA, DSA, ECDSA, EdDSA support

### Diffie-Hellman Parameters

```cpp
void use_tmp_dh(const const_buffer& dh);
ASIO_SYNC_OP_VOID use_tmp_dh(const const_buffer& dh, asio::error_code& ec);

void use_tmp_dh_file(const std::string& filename);
ASIO_SYNC_OP_VOID use_tmp_dh_file(const std::string& filename, asio::error_code& ec);
```

**Purpose**: Forward secrecy for key exchange

## Password Callback System

### Template-Based Callback

```cpp
template <typename PasswordCallback>
void set_password_callback(PasswordCallback callback);
template <typename PasswordCallback>
ASIO_SYNC_OP_VOID set_password_callback(PasswordCallback callback, asio::error_code& ec);
```

**Callback signature:**
```cpp
std::string password_callback(
    std::size_t max_length,      // Maximum password length
    password_purpose purpose     // Reading or writing
);
```

**Implementation example:**
```cpp
ctx.set_password_callback([](std::size_t max_length, password_purpose purpose) {
    if (purpose == for_reading) {
        return std::string("secret_password");
    }
    return std::string();
});
```

### Internal Implementation

```cpp
// Type erasure for password callbacks
ASIO_SYNC_OP_VOID do_set_password_callback(
    detail::password_callback_base* callback, asio::error_code& ec);

// OpenSSL callback adapter
static int password_callback_function(char* buf, int size, int purpose, void* data);
```

## Resource Management

### RAII and Lifetime Management

```cpp
explicit context(method m)
    : handle_(SSL_CTX_new(/* method */)),
      init_()  // Ensures OpenSSL initialization
{
    if (!handle_) {
        // Handle allocation failure
    }
}

~context()
{
    if (handle_) {
        SSL_CTX_free(handle_);
    }
}
```

### Move Semantics

```cpp
context(context&& other)
    : handle_(other.handle_),
      init_(static_cast<asio::ssl::detail::openssl_init<>&&>(other.init_))
{
    other.handle_ = nullptr;
}

context& operator=(context&& other)
{
    if (this != &other) {
        if (handle_) {
            SSL_CTX_free(handle_);
        }
        handle_ = other.handle_;
        init_ = static_cast<asio::ssl::detail::openssl_init<>&&>(other.init_);
        other.handle_ = nullptr;
    }
    return *this;
}
```

**Benefits:**
- **Efficient transfers**: No copying of SSL_CTX
- **Exception safety**: Strong exception guarantee
- **Clear ownership**: No ambiguity about resource ownership

## OpenSSL Integration

### Native Handle Access

```cpp
native_handle_type native_handle()
{
    return handle_;
}
```

**Use cases:**
- **Direct OpenSSL API access**: When ASIO wrapper is insufficient
- **Third-party library integration**: Pass SSL_CTX to other libraries
- **Advanced configuration**: Access to OpenSSL-specific features

### Error Translation

```cpp
static asio::error_code translate_error(long error);
```

**Error categories:**
- **SSL errors**: Map OpenSSL error codes
- **System errors**: Handle underlying system failures
- **Custom errors**: ASIO-specific error conditions

### BIO Integration

```cpp
// Helper for memory buffer operations
BIO* make_buffer_bio(const const_buffer& b);

// Cleanup helpers
struct bio_cleanup { void operator()(BIO* bio) { BIO_free(bio); } };
struct x509_cleanup { void operator()(X509* cert) { X509_free(cert); } };
struct evp_pkey_cleanup { void operator()(EVP_PKEY* key) { EVP_PKEY_free(key); } };
```

## Common Configuration Patterns

### Client Configuration

```cpp
ssl::context ctx(ssl::context::tls_client);

// Use system CA certificates
ctx.set_default_verify_paths();

// Verify server certificate
ctx.set_verify_mode(ssl::verify_peer);

// Optional: custom verification
ctx.set_verify_callback(ssl::host_name_verification("example.com"));
```

### Server Configuration

```cpp
ssl::context ctx(ssl::context::tls_server);

// Security options
ctx.set_options(
    ssl::context::default_workarounds |
    ssl::context::no_sslv2 |
    ssl::context::no_sslv3 |
    ssl::context::single_dh_use
);

// Server certificate and key
ctx.use_certificate_chain_file("server.pem");
ctx.use_private_key_file("server.pem", ssl::context::pem);

// DH parameters for forward secrecy
ctx.use_tmp_dh_file("dh2048.pem");

// Password callback for encrypted keys
ctx.set_password_callback([](std::size_t, password_purpose) {
    return "server_key_password";
});
```

### Mutual TLS (mTLS) Configuration

```cpp
// Server side
ssl::context server_ctx(ssl::context::tls_server);
server_ctx.set_verify_mode(
    ssl::verify_peer | ssl::verify_fail_if_no_peer_cert
);
server_ctx.load_verify_file("client_ca.pem");

// Client side
ssl::context client_ctx(ssl::context::tls_client);
client_ctx.use_certificate_chain_file("client.pem");
client_ctx.use_private_key_file("client.pem", ssl::context::pem);
client_ctx.load_verify_file("server_ca.pem");
```

## Error Handling Best Practices

### Comprehensive Error Checking

```cpp
asio::error_code ec;

ctx.use_certificate_file("server.crt", ssl::context::pem, ec);
if (ec) {
    std::cerr << "Failed to load certificate: " << ec.message() << std::endl;
    return;
}

ctx.use_private_key_file("server.key", ssl::context::pem, ec);
if (ec) {
    std::cerr << "Failed to load private key: " << ec.message() << std::endl;
    return;
}
```

### Exception-Safe Configuration

```cpp
try {
    ssl::context ctx(ssl::context::tls_server);
    ctx.set_options(ssl::context::default_workarounds | ssl::context::no_sslv2);
    ctx.use_certificate_chain_file("server.pem");
    ctx.use_private_key_file("server.pem", ssl::context::pem);
    
    // Context is now ready for use
} catch (const std::exception& e) {
    std::cerr << "SSL context configuration failed: " << e.what() << std::endl;
}
```

This comprehensive context management system provides a robust foundation for SSL/TLS configuration while maintaining ease of use and flexibility for diverse deployment scenarios.