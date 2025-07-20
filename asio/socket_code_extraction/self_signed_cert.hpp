#pragma once

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include <memory>
#include <string>

namespace dtls {

// RAII wrappers for OpenSSL objects
using EVP_PKEY_ptr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using X509_ptr = std::unique_ptr<X509, decltype(&X509_free)>;
using X509_NAME_ptr = std::unique_ptr<X509_NAME, decltype(&X509_NAME_free)>;
using BIO_ptr = std::unique_ptr<BIO, decltype(&BIO_free)>;
using EVP_PKEY_CTX_ptr = std::unique_ptr<EVP_PKEY_CTX, decltype(&EVP_PKEY_CTX_free)>;

struct certificate_data {
  std::string cert_pem;
  std::string key_pem;

  bool is_valid() const { return !cert_pem.empty() && !key_pem.empty(); }
};

inline certificate_data
generate_self_signed_cert(const std::string &common_name = "localhost",
                          int days_valid = 365, int key_size = 2048) {

  certificate_data result;

  // Initialize OpenSSL
  OpenSSL_add_all_algorithms();
  ERR_load_crypto_strings();

  // Generate RSA key using EVP_PKEY API (OpenSSL 3.0+)
  EVP_PKEY_CTX_ptr keygen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
  if (!keygen_ctx) {
    return result;
  }

  if (EVP_PKEY_keygen_init(keygen_ctx.get()) <= 0) {
    return result;
  }

  if (EVP_PKEY_CTX_set_rsa_keygen_bits(keygen_ctx.get(), key_size) <= 0) {
    return result;
  }

  EVP_PKEY *pkey_raw = nullptr;
  if (EVP_PKEY_keygen(keygen_ctx.get(), &pkey_raw) <= 0) {
    return result;
  }

  EVP_PKEY_ptr pkey(pkey_raw, EVP_PKEY_free);

  // Create X509 certificate
  X509_ptr cert(X509_new(), X509_free);
  if (!cert) {
    return result;
  }

  // Set certificate version (v3)
  X509_set_version(cert.get(), 2);

  // Set serial number
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);

  // Set validity period
  X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(cert.get()), days_valid * 24 * 3600);

  // Set public key
  X509_set_pubkey(cert.get(), pkey.get());

  // Create subject name
  X509_NAME *name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"US", -1,
                             -1, 0);
  X509_NAME_add_entry_by_txt(name, "ST", MBSTRING_ASC, (unsigned char *)"State",
                             -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC, (unsigned char *)"City",
                             -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                             (unsigned char *)"Test Organization", -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             (unsigned char *)common_name.c_str(), -1, -1, 0);

  // Self-signed: issuer is same as subject
  X509_set_issuer_name(cert.get(), name);

  // Add extensions
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, cert.get(), cert.get(), nullptr, nullptr, 0);

  // Add Subject Alternative Name for localhost
  X509_EXTENSION *ext =
      X509V3_EXT_conf_nid(nullptr, &ctx, NID_subject_alt_name,
                          const_cast<char *>("DNS:localhost,IP:127.0.0.1"));
  if (ext) {
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
  }

  // Add basic constraints (CA:FALSE)
  ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
                            const_cast<char *>("critical,CA:FALSE"));
  if (ext) {
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
  }

  // Add key usage
  ext = X509V3_EXT_conf_nid(
      nullptr, &ctx, NID_key_usage,
      const_cast<char *>("critical,digitalSignature,keyEncipherment"));
  if (ext) {
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
  }

  // Sign the certificate
  if (!X509_sign(cert.get(), pkey.get(), EVP_sha256())) {
    return result;
  }

  // Convert certificate to PEM
  BIO_ptr cert_bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!cert_bio || !PEM_write_bio_X509(cert_bio.get(), cert.get())) {
    return result;
  }

  char *cert_data;
  long cert_len = BIO_get_mem_data(cert_bio.get(), &cert_data);
  result.cert_pem.assign(cert_data, cert_len);

  // Convert private key to PEM
  BIO_ptr key_bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!key_bio || !PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(), nullptr,
                                            nullptr, 0, nullptr, nullptr)) {
    return result;
  }

  char *key_data;
  long key_len = BIO_get_mem_data(key_bio.get(), &key_data);
  result.key_pem.assign(key_data, key_len);

  return result;
}

// Helper to generate CA certificate
inline certificate_data
generate_ca_cert(const std::string &common_name = "Test CA",
                 int days_valid = 3650, int key_size = 2048) {

  certificate_data result;

  // Initialize OpenSSL
  OpenSSL_add_all_algorithms();
  ERR_load_crypto_strings();

  // Generate RSA key using EVP_PKEY API (OpenSSL 3.0+)
  EVP_PKEY_CTX_ptr keygen_ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr), EVP_PKEY_CTX_free);
  if (!keygen_ctx) {
    return result;
  }

  if (EVP_PKEY_keygen_init(keygen_ctx.get()) <= 0) {
    return result;
  }

  if (EVP_PKEY_CTX_set_rsa_keygen_bits(keygen_ctx.get(), key_size) <= 0) {
    return result;
  }

  EVP_PKEY *pkey_raw = nullptr;
  if (EVP_PKEY_keygen(keygen_ctx.get(), &pkey_raw) <= 0) {
    return result;
  }

  EVP_PKEY_ptr pkey(pkey_raw, EVP_PKEY_free);

  // Create X509 certificate
  X509_ptr cert(X509_new(), X509_free);
  if (!cert) {
    return result;
  }

  // Set certificate version (v3)
  X509_set_version(cert.get(), 2);

  // Set serial number
  ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);

  // Set validity period
  X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
  X509_gmtime_adj(X509_get_notAfter(cert.get()), days_valid * 24 * 3600);

  // Set public key
  X509_set_pubkey(cert.get(), pkey.get());

  // Create subject name
  X509_NAME *name = X509_get_subject_name(cert.get());
  X509_NAME_add_entry_by_txt(name, "C", MBSTRING_ASC, (unsigned char *)"US", -1,
                             -1, 0);
  X509_NAME_add_entry_by_txt(name, "ST", MBSTRING_ASC, (unsigned char *)"State",
                             -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "L", MBSTRING_ASC, (unsigned char *)"City",
                             -1, -1, 0);
  X509_NAME_add_entry_by_txt(name, "O", MBSTRING_ASC,
                             (unsigned char *)"Test CA Organization", -1, -1,
                             0);
  X509_NAME_add_entry_by_txt(name, "CN", MBSTRING_ASC,
                             (unsigned char *)common_name.c_str(), -1, -1, 0);

  // Self-signed: issuer is same as subject
  X509_set_issuer_name(cert.get(), name);

  // Add extensions for CA
  X509V3_CTX ctx;
  X509V3_set_ctx_nodb(&ctx);
  X509V3_set_ctx(&ctx, cert.get(), cert.get(), nullptr, nullptr, 0);

  // Add basic constraints (CA:TRUE)
  X509_EXTENSION *ext =
      X509V3_EXT_conf_nid(nullptr, &ctx, NID_basic_constraints,
                          const_cast<char *>("critical,CA:TRUE"));
  if (ext) {
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
  }

  // Add key usage for CA
  ext = X509V3_EXT_conf_nid(nullptr, &ctx, NID_key_usage,
                            const_cast<char *>("critical,keyCertSign,cRLSign"));
  if (ext) {
    X509_add_ext(cert.get(), ext, -1);
    X509_EXTENSION_free(ext);
  }

  // Sign the certificate
  if (!X509_sign(cert.get(), pkey.get(), EVP_sha256())) {
    return result;
  }

  // Convert certificate to PEM
  BIO_ptr cert_bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!cert_bio || !PEM_write_bio_X509(cert_bio.get(), cert.get())) {
    return result;
  }

  char *cert_data;
  long cert_len = BIO_get_mem_data(cert_bio.get(), &cert_data);
  result.cert_pem.assign(cert_data, cert_len);

  // Convert private key to PEM
  BIO_ptr key_bio(BIO_new(BIO_s_mem()), BIO_free);
  if (!key_bio || !PEM_write_bio_PrivateKey(key_bio.get(), pkey.get(), nullptr,
                                            nullptr, 0, nullptr, nullptr)) {
    return result;
  }

  char *key_data;
  long key_len = BIO_get_mem_data(key_bio.get(), &key_data);
  result.key_pem.assign(key_data, key_len);

  return result;
}

// Helper to use in-memory certificates with SSL context
inline bool use_certificate_data(SSL_CTX *ctx, const certificate_data &data) {
  if (!data.is_valid()) {
    return false;
  }

  // Load certificate
  BIO_ptr cert_bio(BIO_new_mem_buf(data.cert_pem.data(), data.cert_pem.size()),
                   BIO_free);
  if (!cert_bio) {
    return false;
  }

  X509_ptr cert(PEM_read_bio_X509(cert_bio.get(), nullptr, nullptr, nullptr),
                X509_free);
  if (!cert || SSL_CTX_use_certificate(ctx, cert.get()) != 1) {
    return false;
  }

  // Load private key
  BIO_ptr key_bio(BIO_new_mem_buf(data.key_pem.data(), data.key_pem.size()),
                  BIO_free);
  if (!key_bio) {
    return false;
  }

  EVP_PKEY_ptr pkey(
      PEM_read_bio_PrivateKey(key_bio.get(), nullptr, nullptr, nullptr),
      EVP_PKEY_free);
  if (!pkey || SSL_CTX_use_PrivateKey(ctx, pkey.get()) != 1) {
    return false;
  }

  // Check private key matches certificate
  return SSL_CTX_check_private_key(ctx) == 1;
}

// Helper to add CA certificate for verification
inline bool add_ca_certificate(SSL_CTX *ctx, const certificate_data &ca_data) {
  if (!ca_data.is_valid()) {
    return false;
  }

  BIO_ptr bio(BIO_new_mem_buf(ca_data.cert_pem.data(), ca_data.cert_pem.size()),
              BIO_free);
  if (!bio) {
    return false;
  }

  X509_ptr cert(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr),
                X509_free);
  if (!cert) {
    return false;
  }

  X509_STORE *store = SSL_CTX_get_cert_store(ctx);
  return X509_STORE_add_cert(store, cert.get()) == 1;
}

} // namespace dtls