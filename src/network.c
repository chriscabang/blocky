/* network.c — TLS peer-to-peer network layer with post-quantum cryptography.
 * See network.h for design notes and security posture (ADR-014).
 */

#include <arpa/inet.h>
#include <openssl/err.h>
#include <openssl/ssl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"
#include "network.h"

/* ── Internal context ─────────────────────────────────────────────────── */

struct NetContext {
    SSL_CTX  *ssl_ctx;
    char      bind_addr[64];
    uint16_t  port;
};

/* ── TLS helpers ──────────────────────────────────────────────────────── */

/*
 * Apply security settings common to both server and client contexts:
 *   - Enforce TLS 1.3 minimum.
 *   - Set PQC hybrid key-exchange group (if cfg->pqc_group is non-NULL).
 * Returns 0 on success, -1 on failure.
 */
static int apply_common_security(SSL_CTX *ssl_ctx, const NetConfig *cfg)
{
    if (SSL_CTX_set_min_proto_version(ssl_ctx, TLS1_3_VERSION) != 1) {
        log_error("Failed to enforce TLS 1.3 minimum");
        return -1;
    }

    if (cfg->pqc_group) {
        if (SSL_CTX_set1_groups_list(ssl_ctx, cfg->pqc_group) != 1) {
            log_error("Failed to set PQC key-exchange group '%s'",
                      cfg->pqc_group);
            return -1;
        }
        log_info("PQC key-exchange group set: %s", cfg->pqc_group);
    }

    return 0;
}

/*
 * Perform a complete bidirectional TLS shutdown.
 *
 * The first SSL_shutdown sends our close_notify alert to the peer.
 * A return value of 0 means the peer's close_notify is still outstanding;
 * the second call waits for it.  Negative values indicate a transport error
 * (logged at warn level — the connection is being torn down regardless).
 */
static void tls_shutdown(SSL *ssl)
{
    int rc = SSL_shutdown(ssl);
    if (rc == 0) {
        rc = SSL_shutdown(ssl);
    }
    if (rc < 0) {
        log_warn("TLS shutdown did not complete cleanly (rc=%d)", rc);
    }
}

/* ── Lifecycle ────────────────────────────────────────────────────────── */

NetContext *net_context_server(const NetConfig *cfg)
{
    if (!cfg) {
        log_error("net_context_server: NULL config");
        return NULL;
    }
    if (!cfg->cert_file || !cfg->key_file) {
        log_error("net_context_server: cert_file and key_file are required");
        return NULL;
    }

    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_server_method());
    if (!ssl_ctx) {
        log_error("Failed to create SSL server context");
        return NULL;
    }

    if (apply_common_security(ssl_ctx, cfg) != 0) {
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (SSL_CTX_use_certificate_file(ssl_ctx, cfg->cert_file,
                                     SSL_FILETYPE_PEM) != 1) {
        log_error("Failed to load certificate: %s", cfg->cert_file);
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (SSL_CTX_use_PrivateKey_file(ssl_ctx, cfg->key_file,
                                    SSL_FILETYPE_PEM) != 1) {
        log_error("Failed to load private key: %s", cfg->key_file);
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
        log_error("Certificate and private key do not match");
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    /* Optional mutual TLS: verify client certificates against a CA bundle. */
    if (cfg->ca_file) {
        if (SSL_CTX_load_verify_locations(ssl_ctx, cfg->ca_file, NULL) != 1) {
            log_error("Failed to load CA bundle: %s", cfg->ca_file);
            SSL_CTX_free(ssl_ctx);
            return NULL;
        }
        SSL_CTX_set_verify(ssl_ctx,
                           SSL_VERIFY_PEER | SSL_VERIFY_FAIL_IF_NO_PEER_CERT,
                           NULL);
        log_info("Peer certificate verification enabled (CA: %s)", cfg->ca_file);
    }

    NetContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        log_error("net_context_server: out of memory");
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    ctx->ssl_ctx = ssl_ctx;
    ctx->port    = cfg->port;

    if (cfg->bind_addr)
        snprintf(ctx->bind_addr, sizeof(ctx->bind_addr), "%s", cfg->bind_addr);
    else
        snprintf(ctx->bind_addr, sizeof(ctx->bind_addr), "0.0.0.0");

    log_info("Server TLS context created (port %u)", cfg->port);
    return ctx;
}

NetContext *net_context_client(const NetConfig *cfg)
{
    if (!cfg) {
        log_error("net_context_client: NULL config");
        return NULL;
    }

    SSL_CTX *ssl_ctx = SSL_CTX_new(TLS_client_method());
    if (!ssl_ctx) {
        log_error("Failed to create SSL client context");
        return NULL;
    }

    if (apply_common_security(ssl_ctx, cfg) != 0) {
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    /*
     * Peer certificate verification is always required on the client side.
     * Use an explicit CA bundle when available; fall back to the system
     * default CA store otherwise.
     */
    if (cfg->ca_file) {
        if (SSL_CTX_load_verify_locations(ssl_ctx, cfg->ca_file, NULL) != 1) {
            log_error("Failed to load CA bundle: %s", cfg->ca_file);
            SSL_CTX_free(ssl_ctx);
            return NULL;
        }
    } else {
        if (SSL_CTX_set_default_verify_paths(ssl_ctx) != 1) {
            log_warn("Failed to load system CA store; peer verification may fail");
        }
    }
    SSL_CTX_set_verify(ssl_ctx, SSL_VERIFY_PEER, NULL);

    /* Optional client certificate for mutual TLS. */
    if (cfg->cert_file && cfg->key_file) {
        if (SSL_CTX_use_certificate_file(ssl_ctx, cfg->cert_file,
                                         SSL_FILETYPE_PEM) != 1) {
            log_error("Failed to load client certificate: %s", cfg->cert_file);
            SSL_CTX_free(ssl_ctx);
            return NULL;
        }
        if (SSL_CTX_use_PrivateKey_file(ssl_ctx, cfg->key_file,
                                        SSL_FILETYPE_PEM) != 1) {
            log_error("Failed to load client private key: %s", cfg->key_file);
            SSL_CTX_free(ssl_ctx);
            return NULL;
        }
        if (SSL_CTX_check_private_key(ssl_ctx) != 1) {
            log_error("Client certificate and private key do not match");
            SSL_CTX_free(ssl_ctx);
            return NULL;
        }
    }

    NetContext *ctx = calloc(1, sizeof(*ctx));
    if (!ctx) {
        log_error("net_context_client: out of memory");
        SSL_CTX_free(ssl_ctx);
        return NULL;
    }

    ctx->ssl_ctx = ssl_ctx;
    ctx->port    = cfg->port;
    log_info("Client TLS context created");
    return ctx;
}

void net_context_free(NetContext *ctx)
{
    if (!ctx) return;
    SSL_CTX_free(ctx->ssl_ctx);
    free(ctx);
}

/* ── Serialization ────────────────────────────────────────────────────── */

int net_serialize_block(const Block *block, char *buf, size_t bufsz)
{
    if (!block || !buf || bufsz == 0) {
        log_error("net_serialize_block: invalid arguments");
        return -1;
    }

    int n = snprintf(buf, bufsz,
                     "index:%u\n"
                     "timestamp:%ld\n"
                     "prev_hash:%s\n"
                     "merkle_root:%s\n"
                     "nonce:%u\n"
                     "consensus:%u\n"
                     "hash:%s\n"
                     "tx_count:%u\n",
                     block->index,
                     (long)block->timestamp,
                     (const char *)block->previous_hash,
                     (const char *)block->merkle_root,
                     block->nonce,
                     (unsigned)block->consensus,
                     (const char *)block->hash,
                     block->transaction_count);

    if (n < 0 || (size_t)n >= bufsz) {
        log_error("net_serialize_block: buffer too small (needed %d, have %zu)",
                  n, bufsz);
        return -1;
    }

    return n;
}

/* ── Broadcast ────────────────────────────────────────────────────────── */

int net_broadcast_block(NetContext  *ctx,
                        const Block *block,
                        const char  *peer_addr,
                        uint16_t     peer_port)
{
    if (!ctx || !block || !peer_addr) {
        log_error("net_broadcast_block: invalid arguments");
        return -1;
    }

    char buf[NET_BLOCK_BUF_SIZE];
    int  len = net_serialize_block(block, buf, sizeof(buf));
    if (len < 0) return -1;

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        log_error("net_broadcast_block: socket creation failed");
        return -1;
    }

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(peer_port);

    if (inet_pton(AF_INET, peer_addr, &addr.sin_addr) != 1) {
        log_error("net_broadcast_block: invalid peer address: %s", peer_addr);
        close(sock);
        return -1;
    }

    if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        log_error("net_broadcast_block: connect to %s:%u failed",
                  peer_addr, peer_port);
        close(sock);
        return -1;
    }

    SSL *ssl = SSL_new(ctx->ssl_ctx);
    if (!ssl) {
        log_error("net_broadcast_block: SSL_new failed");
        close(sock);
        return -1;
    }
    SSL_set_fd(ssl, sock);

    if (SSL_connect(ssl) != 1) {
        log_error("net_broadcast_block: TLS handshake with %s:%u failed",
                  peer_addr, peer_port);
        SSL_free(ssl);
        close(sock);
        return -1;
    }

    int written = SSL_write(ssl, buf, len);
    if (written <= 0) {
        log_error("net_broadcast_block: SSL_write to %s:%u failed",
                  peer_addr, peer_port);
        tls_shutdown(ssl);
        SSL_free(ssl);
        close(sock);
        return -1;
    }

    log_info("Block %u broadcast to %s:%u (%d bytes)",
             block->index, peer_addr, peer_port, written);
    tls_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    return 0;
}

/* ── Server ───────────────────────────────────────────────────────────── */

int net_server_run(NetContext *ctx)
{
    if (!ctx) {
        log_error("net_server_run: NULL context");
        return -1;
    }

    int server_sock = socket(AF_INET, SOCK_STREAM, 0);
    if (server_sock < 0) {
        log_error("net_server_run: socket creation failed");
        return -1;
    }

    /* Avoid TIME_WAIT blocking restart on the same port. */
    int opt = 1;
    setsockopt(server_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port   = htons(ctx->port);

    if (ctx->bind_addr[0] == '\0' ||
        strcmp(ctx->bind_addr, "0.0.0.0") == 0) {
        server_addr.sin_addr.s_addr = INADDR_ANY;
    } else {
        if (inet_pton(AF_INET, ctx->bind_addr, &server_addr.sin_addr) != 1) {
            log_error("net_server_run: invalid bind address: %s",
                      ctx->bind_addr);
            close(server_sock);
            return -1;
        }
    }

    if (bind(server_sock, (struct sockaddr *)&server_addr,
             sizeof(server_addr)) < 0) {
        log_error("net_server_run: bind to port %u failed", ctx->port);
        close(server_sock);
        return -1;
    }

    if (listen(server_sock, NET_LISTEN_BACKLOG) < 0) {
        log_error("net_server_run: listen failed");
        close(server_sock);
        return -1;
    }

    log_info("Server listening on %s:%u", ctx->bind_addr, ctx->port);

    while (1) {
        struct sockaddr_in client_addr;
        socklen_t          client_len = sizeof(client_addr);

        int client_sock = accept(server_sock,
                                 (struct sockaddr *)&client_addr,
                                 &client_len);
        if (client_sock < 0) {
            log_warn("net_server_run: accept failed; continuing");
            continue;
        }

        char peer_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &client_addr.sin_addr, peer_ip, sizeof(peer_ip));
        log_info("Connection from %s:%u", peer_ip, ntohs(client_addr.sin_port));

        SSL *ssl = SSL_new(ctx->ssl_ctx);
        if (!ssl) {
            log_error("net_server_run: SSL_new failed");
            close(client_sock);
            continue;
        }
        SSL_set_fd(ssl, client_sock);

        if (SSL_accept(ssl) <= 0) {
            log_warn("net_server_run: TLS handshake failed with %s", peer_ip);
            SSL_free(ssl);
            close(client_sock);
            continue;
        }

        char buf[NET_BLOCK_BUF_SIZE];
        memset(buf, 0, sizeof(buf));
        int n = SSL_read(ssl, buf, (int)sizeof(buf) - 1);
        if (n <= 0) {
            log_warn("net_server_run: SSL_read failed from %s", peer_ip);
        } else {
            buf[n] = '\0';
            log_info("Received %d bytes from %s", n, peer_ip);
            log_debug("Block data preview: %.128s", buf);
            /* TODO(ADR-014): deserialize buf into Block, call chain_add(). */
        }

        tls_shutdown(ssl);
        SSL_free(ssl);
        close(client_sock);
    }

    /* Unreachable — loop runs until the process is terminated. */
    close(server_sock);
    return 0;
}
