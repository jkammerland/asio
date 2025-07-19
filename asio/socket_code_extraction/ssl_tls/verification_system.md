# SSL/TLS Verification System

## Verification Architecture

### Core Components

```cpp
// Verification modes
typedef int verify_mode;
const int verify_none = SSL_VERIFY_NONE;
const int verify_peer = SSL_VERIFY_PEER;
const int verify_fail_if_no_peer_cert = SSL_VERIFY_FAIL_IF_NO_PEER_CERT;
const int verify_client_once = SSL_VERIFY_CLIENT_ONCE;

// Verification context
class verify_context : private noncopyable
{
public:
    typedef X509_STORE_CTX* native_handle_type;
    explicit verify_context(native_handle_type handle);
    native_handle_type native_handle();
};

// Hostname verification
class host_name_verification
{
public:
    typedef bool result_type;
    explicit host_name_verification(const std::string& host);
    bool operator()(bool preverified, verify_context& ctx) const;
};
```

## Verification Modes

### Mode Definitions

#### `verify_none`
- **Purpose**: Disable certificate verification
- **Use case**: Testing, development environments
- **Security**: Vulnerable to man-in-the-middle attacks
- **OpenSSL mapping**: `SSL_VERIFY_NONE`

```cpp
ctx.set_verify_mode(ssl::verify_none);
// No certificate verification performed
```

#### `verify_peer`
- **Purpose**: Verify peer certificate
- **Behavior**: Request and verify peer certificate
- **Failure handling**: Connection continues even if verification fails
- **OpenSSL mapping**: `SSL_VERIFY_PEER`

```cpp
ctx.set_verify_mode(ssl::verify_peer);
// Certificate verification performed, but failures don't abort connection
```

#### `verify_fail_if_no_peer_cert`
- **Purpose**: Require peer certificate
- **Behavior**: Fail connection if no certificate provided
- **Requirement**: Must be combined with `verify_peer`
- **Use case**: Server requiring client certificates
- **OpenSSL mapping**: `SSL_VERIFY_FAIL_IF_NO_PEER_CERT`

```cpp
ctx.set_verify_mode(ssl::verify_peer | ssl::verify_fail_if_no_peer_cert);
// Mutual TLS - client certificate required
```

#### `verify_client_once`
- **Purpose**: Verify client certificate only once
- **Behavior**: Don't re-verify on renegotiation
- **Performance**: Reduces verification overhead
- **OpenSSL mapping**: `SSL_VERIFY_CLIENT_ONCE`

```cpp
ctx.set_verify_mode(ssl::verify_peer | ssl::verify_client_once);
// Client certificate verified only on initial handshake
```

### Mode Combinations

```cpp
// Common client configuration
ssl::verify_peer

// Common server configuration (no client cert required)
ssl::verify_peer

// Mutual TLS server configuration
ssl::verify_peer | ssl::verify_fail_if_no_peer_cert

// Performance-optimized server
ssl::verify_peer | ssl::verify_client_once
```

## Verification Depth

### Depth Configuration

```cpp
void set_verify_depth(int depth);
ASIO_SYNC_OP_VOID set_verify_depth(int depth, asio::error_code& ec);
```

**Purpose**: Control maximum certificate chain length

**Default behavior**: OpenSSL default (typically 9)

**Usage examples:**
```cpp
// Allow deep certificate chains
ctx.set_verify_depth(20);

// Restrict to short chains for performance
ctx.set_verify_depth(3);

// Single certificate only (no intermediates)
ctx.set_verify_depth(1);
```

**Chain verification process:**
1. Start with peer certificate (depth 0)
2. Verify each intermediate certificate (depth 1, 2, ...)
3. End with root CA certificate
4. Fail if chain exceeds maximum depth

## Custom Verification Callbacks

### Callback Interface

```cpp
template <typename VerifyCallback>
void set_verify_callback(VerifyCallback callback);

// Callback signature
bool verify_callback(
    bool preverified,        // True if certificate passed OpenSSL verification
    verify_context& ctx      // Access to certificate and store context
);
```

### Implementation Pattern

```cpp
class custom_verifier
{
public:
    bool operator()(bool preverified, ssl::verify_context& ctx)
    {
        // Get certificate being verified
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        
        // Get verification depth
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
        
        // Get verification error (if any)
        int error = X509_STORE_CTX_get_error(ctx.native_handle());
        
        // Custom verification logic
        if (!preverified) {
            // Handle verification failure
            if (error == X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN && depth == 0) {
                // Allow self-signed certificates for testing
                return true;
            }
            return false;
        }
        
        // Additional custom checks
        return perform_custom_checks(cert, depth);
    }

private:
    bool perform_custom_checks(X509* cert, int depth)
    {
        // Custom verification logic
        return true;
    }
};

// Usage
ctx.set_verify_callback(custom_verifier{});
```

### Lambda-Based Callbacks

```cpp
ctx.set_verify_callback([](bool preverified, ssl::verify_context& ctx) -> bool {
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    
    // Log certificate information
    char subject[256];
    X509_NAME_oneline(X509_get_subject_name(cert), subject, 256);
    std::cout << "Verifying: " << subject << std::endl;
    
    return preverified;
});
```

## Hostname Verification

### RFC 6125 Compliance

```cpp
class host_name_verification
{
public:
    explicit host_name_verification(const std::string& host);
    bool operator()(bool preverified, verify_context& ctx) const;
};
```

**Purpose**: Verify certificate matches expected hostname

**Standards compliance**: RFC 6125 - Certificate Hostname Verification

### Usage Pattern

```cpp
// Client-side hostname verification
ssl::context ctx(ssl::context::tls_client);
ctx.set_verify_mode(ssl::verify_peer);
ctx.set_verify_callback(ssl::host_name_verification("example.com"));

// In SSL stream
ssl::stream<tcp::socket> socket(io_context, ctx);
socket.set_verify_callback(ssl::host_name_verification("example.com"));
```

### Verification Process

1. **Certificate extraction**: Get peer certificate from SSL handshake
2. **Subject name check**: Compare hostname with certificate CN
3. **Subject Alternative Names**: Check SAN extension for DNS names
4. **Wildcard matching**: Handle wildcard certificates (*.example.com)
5. **IP address handling**: Special handling for IP address connections

### Implementation Details

```cpp
bool host_name_verification::operator()(bool preverified, verify_context& ctx) const
{
    // Only verify hostname if certificate passed basic verification
    if (!preverified) {
        return false;
    }
    
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    
    // Check Subject Alternative Names first
    if (check_subject_alt_names(cert)) {
        return true;
    }
    
    // Fall back to Common Name
    return check_common_name(cert);
}
```

## Verification Context Details

### Context Information Access

```cpp
class verify_context
{
public:
    typedef X509_STORE_CTX* native_handle_type;
    
    native_handle_type native_handle() { return handle_; }
    
private:
    native_handle_type handle_;
};
```

### OpenSSL Store Context Operations

```cpp
// Get current certificate being verified
X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());

// Get certificate chain depth
int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());

// Get verification error code
int error = X509_STORE_CTX_get_error(ctx.native_handle());

// Get error string
const char* error_string = X509_verify_cert_error_string(error);

// Get certificate store
X509_STORE* store = X509_STORE_CTX_get0_store(ctx.native_handle());

// Get untrusted certificate chain
STACK_OF(X509)* chain = X509_STORE_CTX_get0_untrusted(ctx.native_handle());
```

## Certificate Information Extraction

### Subject and Issuer Information

```cpp
bool extract_certificate_info(verify_context& ctx)
{
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    
    // Subject information
    char subject[256];
    X509_NAME_oneline(X509_get_subject_name(cert), subject, 256);
    
    // Issuer information
    char issuer[256];
    X509_NAME_oneline(X509_get_issuer_name(cert), issuer, 256);
    
    // Certificate serial number
    ASN1_INTEGER* serial = X509_get_serialNumber(cert);
    
    // Validity period
    ASN1_TIME* not_before = X509_get_notBefore(cert);
    ASN1_TIME* not_after = X509_get_notAfter(cert);
    
    // Public key information
    EVP_PKEY* pkey = X509_get_pubkey(cert);
    int key_type = EVP_PKEY_type(EVP_PKEY_id(pkey));
    EVP_PKEY_free(pkey);
    
    return true;
}
```

### Extension Processing

```cpp
bool check_certificate_extensions(verify_context& ctx)
{
    X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
    
    // Key usage extension
    int key_usage = X509_get_key_usage(cert);
    if (key_usage & X509v3_KU_DIGITAL_SIGNATURE) {
        // Certificate allows digital signatures
    }
    
    // Extended key usage
    int ext_count = X509_get_ext_count(cert);
    for (int i = 0; i < ext_count; ++i) {
        X509_EXTENSION* ext = X509_get_ext(cert, i);
        ASN1_OBJECT* obj = X509_EXTENSION_get_object(ext);
        // Process extension based on OID
    }
    
    return true;
}
```

## Error Handling in Verification

### Verification Error Codes

```cpp
// Common X.509 verification errors
#define X509_V_OK                               0
#define X509_V_ERR_UNABLE_TO_GET_ISSUER_CERT   2
#define X509_V_ERR_UNABLE_TO_GET_CRL            3
#define X509_V_ERR_CERT_SIGNATURE_FAILURE       7
#define X509_V_ERR_CERT_NOT_YET_VALID           9
#define X509_V_ERR_CERT_HAS_EXPIRED             10
#define X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN    19
#define X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT  18
#define X509_V_ERR_HOSTNAME_MISMATCH            62
```

### Error-Specific Handling

```cpp
bool handle_verification_error(verify_context& ctx)
{
    int error = X509_STORE_CTX_get_error(ctx.native_handle());
    int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
    
    switch (error) {
    case X509_V_ERR_CERT_HAS_EXPIRED:
        if (allow_expired_certificates_) {
            return true;
        }
        break;
        
    case X509_V_ERR_SELF_SIGNED_CERT_IN_CHAIN:
        if (depth > 0 && allow_self_signed_ca_) {
            return true;
        }
        break;
        
    case X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT:
        if (allow_self_signed_peer_) {
            return true;
        }
        break;
        
    default:
        break;
    }
    
    return false;
}
```

## Complete Verification Examples

### Comprehensive Client Verification

```cpp
class comprehensive_client_verifier
{
    std::string expected_hostname_;
    std::vector<std::string> allowed_ca_fingerprints_;
    
public:
    explicit comprehensive_client_verifier(const std::string& hostname)
        : expected_hostname_(hostname) {}
    
    bool operator()(bool preverified, ssl::verify_context& ctx)
    {
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
        int error = X509_STORE_CTX_get_error(ctx.native_handle());
        
        // Log verification details
        log_certificate_info(cert, depth, error);
        
        // For root certificates, check against pinned CA list
        if (depth > 0 && !allowed_ca_fingerprints_.empty()) {
            return check_ca_pinning(cert);
        }
        
        // For peer certificate, verify hostname
        if (depth == 0) {
            if (!preverified) {
                return false;
            }
            return verify_hostname(cert);
        }
        
        return preverified;
    }
    
private:
    bool verify_hostname(X509* cert) {
        // Use host_name_verification or custom implementation
        ssl::host_name_verification hostname_verifier(expected_hostname_);
        // This would need adaptation to work with X509* directly
        return true; // Simplified
    }
    
    bool check_ca_pinning(X509* cert) {
        // Calculate certificate fingerprint
        std::string fingerprint = calculate_fingerprint(cert);
        return std::find(allowed_ca_fingerprints_.begin(), 
                        allowed_ca_fingerprints_.end(), 
                        fingerprint) != allowed_ca_fingerprints_.end();
    }
    
    void log_certificate_info(X509* cert, int depth, int error) {
        char subject[256];
        X509_NAME_oneline(X509_get_subject_name(cert), subject, 256);
        
        std::cout << "Depth " << depth << ": " << subject;
        if (error != X509_V_OK) {
            std::cout << " (Error: " << X509_verify_cert_error_string(error) << ")";
        }
        std::cout << std::endl;
    }
    
    std::string calculate_fingerprint(X509* cert) {
        // Calculate SHA-256 fingerprint
        unsigned char digest[SHA256_DIGEST_LENGTH];
        unsigned int digest_len;
        
        if (X509_digest(cert, EVP_sha256(), digest, &digest_len)) {
            std::stringstream ss;
            for (unsigned int i = 0; i < digest_len; ++i) {
                ss << std::hex << std::setw(2) << std::setfill('0') 
                   << static_cast<int>(digest[i]);
            }
            return ss.str();
        }
        return "";
    }
};
```

### Server-Side Client Certificate Verification

```cpp
class server_client_verifier
{
    std::set<std::string> allowed_client_cns_;
    
public:
    bool operator()(bool preverified, ssl::verify_context& ctx)
    {
        if (!preverified) {
            return false;
        }
        
        X509* cert = X509_STORE_CTX_get_current_cert(ctx.native_handle());
        int depth = X509_STORE_CTX_get_error_depth(ctx.native_handle());
        
        // Only check client certificate (depth 0)
        if (depth == 0) {
            return verify_client_certificate(cert);
        }
        
        return true;
    }
    
private:
    bool verify_client_certificate(X509* cert) {
        // Extract Common Name
        X509_NAME* subject = X509_get_subject_name(cert);
        char cn[256];
        int cn_len = X509_NAME_get_text_by_NID(subject, NID_commonName, cn, sizeof(cn));
        
        if (cn_len > 0) {
            std::string client_cn(cn, cn_len);
            return allowed_client_cns_.count(client_cn) > 0;
        }
        
        return false;
    }
};
```

This comprehensive verification system provides the flexibility to implement custom security policies while maintaining compatibility with standard SSL/TLS verification procedures.