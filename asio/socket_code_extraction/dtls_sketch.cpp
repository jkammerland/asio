/*
 * DTLS 1.2 Echo Server using OpenSSL
 *
 * This server implements a multi-threaded DTLS 1.2 echo server that:
 * - Listens for incoming DTLS connections on UDP
 * - Performs cookie exchange to prevent DoS attacks
 * - Uses X.509 certificate authentication
 * - Echoes back any received messages
 * - Handles multiple clients concurrently
 *
 * Compile with: gcc -o dtls_echo_server dtls_echo_server.c -lssl -lcrypto
 * -lpthread
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <openssl/bio.h>
#include <openssl/err.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "self_signed_cert.hpp"

#define SERVER_PORT 4433
#define BUFFER_SIZE 2048
#define COOKIE_SECRET_LENGTH 16

// Server configuration
static int verbose = 0;
static unsigned char cookie_secret[COOKIE_SECRET_LENGTH];

// In-memory certificates
static dtls::certificate_data server_cert;
static dtls::certificate_data ca_cert;

// Structure to pass client information to threads
typedef struct {
  SSL_CTX *ctx;
  int client_fd;
  struct sockaddr_storage client_addr;
  socklen_t client_addr_len;
} client_info_t;

// Function prototypes
int verify_certificate(int preverify_ok, X509_STORE_CTX *ctx);
int generate_cookie(SSL *ssl, unsigned char *cookie, unsigned int *cookie_len);
int verify_cookie(SSL *ssl, const unsigned char *cookie,
                  unsigned int cookie_len);
void *handle_client(void *arg);
void cleanup_openssl(void);
void signal_handler(int sig);
const char *addr_to_string(struct sockaddr_storage *addr);

// Initialize OpenSSL and generate cookie secret
int init_openssl(void) {
  // Initialize OpenSSL
  SSL_load_error_strings();
  OpenSSL_add_ssl_algorithms();

  // Generate random cookie secret
  if (RAND_bytes(cookie_secret, COOKIE_SECRET_LENGTH) != 1) {
    fprintf(stderr, "Failed to generate cookie secret\n");
    return 0;
  }

  return 1;
}

// Create and configure SSL context
SSL_CTX *create_context(void) {
  SSL_CTX *ctx;

  // Use version-flexible DTLS method
  ctx = SSL_CTX_new(DTLS_server_method());
  if (!ctx) {
    ERR_print_errors_fp(stderr);
    return NULL;
  }

  // Set DTLS 1.2 specifically
  SSL_CTX_set_min_proto_version(ctx, DTLS1_2_VERSION);
  SSL_CTX_set_max_proto_version(ctx, DTLS1_2_VERSION);

  // Generate certificates if not already done
  if (!server_cert.is_valid()) {
    if (verbose) {
      printf("Generating server certificate...\n");
    }
    server_cert = dtls::generate_self_signed_cert("dtls-server", 365, 2048);
    if (!server_cert.is_valid()) {
      fprintf(stderr, "Failed to generate server certificate\n");
      SSL_CTX_free(ctx);
      return NULL;
    }
  }

  if (!ca_cert.is_valid()) {
    if (verbose) {
      printf("Generating CA certificate...\n");
    }
    ca_cert = dtls::generate_ca_cert("Test DTLS CA", 3650, 2048);
    if (!ca_cert.is_valid()) {
      fprintf(stderr, "Failed to generate CA certificate\n");
      SSL_CTX_free(ctx);
      return NULL;
    }
  }

  // Use in-memory certificate and key
  if (!dtls::use_certificate_data(ctx, server_cert)) {
    fprintf(stderr, "Failed to use server certificate\n");
    ERR_print_errors_fp(stderr);
    SSL_CTX_free(ctx);
    return NULL;
  }

  // Set up certificate verification
  SSL_CTX_set_verify(ctx, SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                     verify_certificate);
  SSL_CTX_set_verify_depth(ctx, 4);

  // Add CA certificate for verification
  if (!dtls::add_ca_certificate(ctx, ca_cert)) {
    fprintf(stderr, "Failed to add CA certificate\n");
    ERR_print_errors_fp(stderr);
  }

  // Set cookie callbacks
  SSL_CTX_set_cookie_generate_cb(ctx, generate_cookie);
  SSL_CTX_set_cookie_verify_cb(ctx, verify_cookie);

  return ctx;
}

// Certificate verification callback
int verify_certificate(int preverify_ok, X509_STORE_CTX *ctx) {
  char buf[256];
  X509 *cert;
  int err, depth;

  cert = X509_STORE_CTX_get_current_cert(ctx);
  err = X509_STORE_CTX_get_error(ctx);
  depth = X509_STORE_CTX_get_error_depth(ctx);

  X509_NAME_oneline(X509_get_subject_name(cert), buf, sizeof(buf));

  if (verbose) {
    printf("Verify cert at depth %d: %s\n", depth, buf);
    if (!preverify_ok) {
      printf("Verification error: %s\n", X509_verify_cert_error_string(err));
    }
  }

  // For demo purposes, accept self-signed certificates
  if (err == X509_V_ERR_DEPTH_ZERO_SELF_SIGNED_CERT) {
    printf("Accepting self-signed certificate\n");
    return 1;
  }

  return preverify_ok;
}

// Generate cookie for DoS protection
int generate_cookie(SSL *ssl, unsigned char *cookie, unsigned int *cookie_len) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultlength;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in s4;
    struct sockaddr_in6 s6;
  } peer;

  // Get peer address
  BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);

  // Create HMAC of peer address and secret
  HMAC(EVP_sha1(), cookie_secret, COOKIE_SECRET_LENGTH,
       (const unsigned char *)&peer, sizeof(peer), result, &resultlength);

  memcpy(cookie, result, resultlength);
  *cookie_len = resultlength;

  return 1;
}

// Verify cookie
int verify_cookie(SSL *ssl, const unsigned char *cookie,
                  unsigned int cookie_len) {
  unsigned char result[EVP_MAX_MD_SIZE];
  unsigned int resultlength;
  union {
    struct sockaddr_storage ss;
    struct sockaddr_in s4;
    struct sockaddr_in6 s6;
  } peer;

  // Get peer address
  BIO_dgram_get_peer(SSL_get_rbio(ssl), &peer);

  // Create HMAC of peer address and secret
  HMAC(EVP_sha1(), cookie_secret, COOKIE_SECRET_LENGTH,
       (const unsigned char *)&peer, sizeof(peer), result, &resultlength);

  if (cookie_len == resultlength && memcmp(result, cookie, resultlength) == 0) {
    return 1;
  }

  return 0;
}

// Convert address to string for logging
const char *addr_to_string(struct sockaddr_storage *addr) {
  static char addr_str[INET6_ADDRSTRLEN + 8];
  void *sin_addr;
  unsigned short port;

  if (addr->ss_family == AF_INET) {
    struct sockaddr_in *s = (struct sockaddr_in *)addr;
    sin_addr = &s->sin_addr;
    port = ntohs(s->sin_port);
  } else {
    struct sockaddr_in6 *s = (struct sockaddr_in6 *)addr;
    sin_addr = &s->sin6_addr;
    port = ntohs(s->sin6_port);
  }

  inet_ntop(addr->ss_family, sin_addr, addr_str, sizeof(addr_str));
  sprintf(addr_str + strlen(addr_str), ":%d", port);

  return addr_str;
}

// Handle individual client connection
void *handle_client(void *arg) {
  client_info_t *info = (client_info_t *)arg;
  SSL *ssl = NULL;
  BIO *bio = NULL;
  char buffer[BUFFER_SIZE];
  int ret, ssl_err;
  struct timeval timeout;

  printf("New client thread started for %s\n",
         addr_to_string(&info->client_addr));

  // Create BIO for this client
  bio = BIO_new_dgram(info->client_fd, BIO_NOCLOSE);
  if (!bio) {
    fprintf(stderr, "Failed to create BIO\n");
    goto cleanup;
  }

  // Set connected address
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &info->client_addr);

  // Set receive timeout
  timeout.tv_sec = 5;
  timeout.tv_usec = 0;
  BIO_ctrl(bio, BIO_CTRL_DGRAM_SET_RECV_TIMEOUT, 0, &timeout);

  // Create SSL object
  ssl = SSL_new(info->ctx);
  if (!ssl) {
    fprintf(stderr, "Failed to create SSL object\n");
    goto cleanup;
  }

  SSL_set_bio(ssl, bio, bio);
  bio = NULL; // SSL takes ownership

  // Perform handshake
  if (verbose) {
    printf("Starting DTLS handshake with %s\n",
           addr_to_string(&info->client_addr));
  }

  ret = SSL_accept(ssl);
  if (ret <= 0) {
    ssl_err = SSL_get_error(ssl, ret);
    fprintf(stderr, "DTLS handshake failed with %s: ",
            addr_to_string(&info->client_addr));

    switch (ssl_err) {
    case SSL_ERROR_WANT_READ:
    case SSL_ERROR_WANT_WRITE:
      fprintf(stderr, "Handshake incomplete\n");
      break;
    case SSL_ERROR_SYSCALL:
      fprintf(stderr, "System call error: %s\n", strerror(errno));
      break;
    case SSL_ERROR_SSL:
      fprintf(stderr, "SSL protocol error\n");
      ERR_print_errors_fp(stderr);
      break;
    default:
      fprintf(stderr, "Unknown error (%d)\n", ssl_err);
      break;
    }
    goto cleanup;
  }

  printf("DTLS handshake completed with %s (Protocol: %s, Cipher: %s)\n",
         addr_to_string(&info->client_addr), SSL_get_version(ssl),
         SSL_get_cipher(ssl));

  // Echo loop
  while (1) {
    ret = SSL_read(ssl, buffer, sizeof(buffer) - 1);
    if (ret > 0) {
      buffer[ret] = '\0';
      printf("Received from %s: %s\n", addr_to_string(&info->client_addr),
             buffer);

      // Echo back the message
      ret = SSL_write(ssl, buffer, ret);
      if (ret <= 0) {
        ssl_err = SSL_get_error(ssl, ret);
        if (ssl_err != SSL_ERROR_WANT_WRITE) {
          fprintf(stderr, "SSL_write failed: %d\n", ssl_err);
          break;
        }
      } else {
        printf("Echoed to %s: %s\n", addr_to_string(&info->client_addr),
               buffer);
      }
    } else {
      ssl_err = SSL_get_error(ssl, ret);
      if (ssl_err == SSL_ERROR_WANT_READ) {
        // Check for timeout
        if (BIO_ctrl(SSL_get_rbio(ssl), BIO_CTRL_DGRAM_GET_RECV_TIMER_EXP, 0,
                     NULL)) {
          printf("Timeout waiting for data from %s\n",
                 addr_to_string(&info->client_addr));
          break;
        }
        continue;
      } else if (ssl_err == SSL_ERROR_ZERO_RETURN) {
        printf("Client %s closed connection\n",
               addr_to_string(&info->client_addr));
        break;
      } else {
        fprintf(stderr, "SSL_read failed: %d\n", ssl_err);
        break;
      }
    }
  }

cleanup:
  if (ssl) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
  }
  if (bio) {
    BIO_free(bio);
  }
  if (info->client_fd >= 0) {
    close(info->client_fd);
  }

  printf("Client thread ended for %s\n", addr_to_string(&info->client_addr));
  delete info;
  return NULL;
}

// Signal handler for graceful shutdown
void signal_handler(int sig) {
  printf("\nReceived signal %d, shutting down...\n", sig);
  cleanup_openssl();
  exit(0);
}

// Cleanup OpenSSL
void cleanup_openssl(void) {
  EVP_cleanup();
  ERR_free_strings();
}

int main(int argc, char *argv[]) {
  SSL_CTX *ctx;
  int server_fd;
  struct sockaddr_in6 server_addr;
  struct sockaddr_storage client_addr;
  socklen_t client_addr_len;
  SSL *ssl;
  BIO *bio;
  pthread_t thread;
  client_info_t *client_info;
  int port = SERVER_PORT;
  int opt, sockopt = 1;

  // Parse command line arguments
  while ((opt = getopt(argc, argv, "p:vh")) != -1) {
    switch (opt) {
    case 'p':
      port = atoi(optarg);
      break;
    case 'v':
      verbose = 1;
      break;
    case 'h':
    default:
      printf("Usage: %s [-p port] [-v] [-h]\n", argv[0]);
      printf("  -p port: Listen port (default: %d)\n", SERVER_PORT);
      printf("  -v: Verbose output\n");
      printf("  -h: Show this help\n");
      exit(opt == 'h' ? 0 : 1);
    }
  }

  // Set up signal handlers
  signal(SIGINT, signal_handler);
  signal(SIGTERM, signal_handler);

  // Initialize OpenSSL
  if (!init_openssl()) {
    fprintf(stderr, "Failed to initialize OpenSSL\n");
    exit(1);
  }

  // Create SSL context
  ctx = create_context();
  if (!ctx) {
    fprintf(stderr, "Failed to create SSL context\n");
    exit(1);
  }

  // Create server socket
  server_fd = socket(AF_INET6, SOCK_DGRAM, 0);
  if (server_fd < 0) {
    perror("socket");
    exit(1);
  }

  // Set socket options
  setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &sockopt, sizeof(sockopt));

  // Disable IPv6-only to allow IPv4 connections too
  sockopt = 0;
  setsockopt(server_fd, IPPROTO_IPV6, IPV6_V6ONLY, &sockopt, sizeof(sockopt));

  // Bind to address
  memset(&server_addr, 0, sizeof(server_addr));
  server_addr.sin6_family = AF_INET6;
  server_addr.sin6_addr = in6addr_any;
  server_addr.sin6_port = htons(port);

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) <
      0) {
    perror("bind");
    close(server_fd);
    exit(1);
  }

  printf("DTLS Echo Server listening on port %d\n", port);
  printf("Using in-memory generated certificates\n");
  if (verbose) {
    printf("Server certificate CN: dtls-server\n");
    printf("CA certificate CN: Test DTLS CA\n");
  }

  // Main server loop
  while (1) {
    client_addr_len = sizeof(client_addr);

    // Create BIO for listening
    bio = BIO_new_dgram(server_fd, BIO_NOCLOSE);
    if (!bio) {
      fprintf(stderr, "Failed to create listening BIO\n");
      continue;
    }

    // Create SSL for this connection attempt
    ssl = SSL_new(ctx);
    if (!ssl) {
      fprintf(stderr, "Failed to create SSL object\n");
      BIO_free(bio);
      continue;
    }

    SSL_set_bio(ssl, bio, bio);
    SSL_set_options(ssl, SSL_OP_COOKIE_EXCHANGE);

    // Wait for incoming connection with cookie exchange
    if (verbose) {
      printf("Waiting for DTLS connection...\n");
    }

    while (DTLSv1_listen(ssl, (BIO_ADDR *)&client_addr) <= 0) {
      // DTLSv1_listen returns 0 if no valid client yet, continue waiting
    }

    printf("Cookie exchange completed with %s\n", addr_to_string(&client_addr));

    // Create new socket for this client
    int client_fd = socket(client_addr.ss_family, SOCK_DGRAM, 0);
    if (client_fd < 0) {
      perror("client socket");
      SSL_free(ssl);
      continue;
    }

    // Bind new socket to server address
    if (client_addr.ss_family == AF_INET) {
      struct sockaddr_in bind_addr;
      memset(&bind_addr, 0, sizeof(bind_addr));
      bind_addr.sin_family = AF_INET;
      bind_addr.sin_addr.s_addr = INADDR_ANY;
      bind_addr.sin_port = htons(port);

      if (bind(client_fd, (struct sockaddr *)&bind_addr, sizeof(bind_addr)) <
          0) {
        perror("client bind");
        close(client_fd);
        SSL_free(ssl);
        continue;
      }
    } else {
      if (bind(client_fd, (struct sockaddr *)&server_addr,
               sizeof(server_addr)) < 0) {
        perror("client bind");
        close(client_fd);
        SSL_free(ssl);
        continue;
      }
    }

    // Connect to client
    client_addr_len = (client_addr.ss_family == AF_INET)
                          ? sizeof(struct sockaddr_in)
                          : sizeof(struct sockaddr_in6);

    if (connect(client_fd, (struct sockaddr *)&client_addr, client_addr_len) <
        0) {
      perror("connect to client");
      close(client_fd);
      SSL_free(ssl);
      continue;
    }

    // Prepare client info for thread
    client_info = new client_info_t;
    if (!client_info) {
      fprintf(stderr, "Failed to allocate client info\n");
      close(client_fd);
      SSL_free(ssl);
      continue;
    }

    client_info->ctx = ctx;
    client_info->client_fd = client_fd;
    client_info->client_addr = client_addr;
    client_info->client_addr_len = client_addr_len;

    // Set new fd in BIO
    BIO *client_bio = SSL_get_rbio(ssl);
    BIO_set_fd(client_bio, client_fd, BIO_NOCLOSE);
    BIO_ctrl(client_bio, BIO_CTRL_DGRAM_SET_CONNECTED, 0, &client_addr);

    // Transfer SSL object to client thread
    SSL_free(ssl); // This will be recreated in the client thread

    // Create thread to handle client
    if (pthread_create(&thread, NULL, handle_client, client_info) != 0) {
      perror("pthread_create");
      close(client_fd);
      delete client_info;
      continue;
    }

    // Detach thread so it can clean up itself
    pthread_detach(thread);
  }

  // Cleanup (should never reach here)
  close(server_fd);
  SSL_CTX_free(ctx);
  cleanup_openssl();

  return 0;
}