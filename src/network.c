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

#include "block.h"
#include "chain.h"
#include "crypto.h"
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

/* ── Deserialization ──────────────────────────────────────────────────── */

int net_deserialize_block(const char *buf, size_t len, Block *out)
{
    if (!buf || len == 0 || !out) {
        log_error("net_deserialize_block: invalid arguments");
        return EXIT_FAILURE;
    }

    memset(out, 0, sizeof(Block));

    /*
     * Parse lines of the form "key:value\n".
     * Both key and value buffers are bounded conservatively:
     *   key   — at most 31 chars (all known keys are <= 12)
     *   value — HASH_SIZE (65) bytes covers all hash strings and integers
     */
    const char *p   = buf;
    const char *end = buf + len;

    while (p < end) {
        const char *nl    = memchr(p, '\n', (size_t)(end - p));
        if (!nl) nl = end;

        const char *colon = memchr(p, ':', (size_t)(nl - p));
        if (!colon) { p = nl + 1; continue; }

        size_t key_len = (size_t)(colon - p);
        const char *val  = colon + 1;
        size_t val_len   = (size_t)(nl - val);

        char key[32];
        if (key_len == 0 || key_len >= sizeof(key)) { p = nl + 1; continue; }
        memcpy(key, p, key_len);
        key[key_len] = '\0';

        char value[HASH_SIZE];
        if (val_len == 0 || val_len >= sizeof(value)) { p = nl + 1; continue; }
        memcpy(value, val, val_len);
        value[val_len] = '\0';

        if      (strcmp(key, "index")       == 0)
            out->index = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "timestamp")   == 0)
            out->timestamp = (time_t)strtol(value, NULL, 10);
        else if (strcmp(key, "prev_hash")   == 0)
            strncpy((char *)out->previous_hash, value,
                    sizeof(out->previous_hash) - 1);
        else if (strcmp(key, "merkle_root") == 0)
            strncpy((char *)out->merkle_root, value,
                    sizeof(out->merkle_root) - 1);
        else if (strcmp(key, "nonce")       == 0)
            out->nonce = (uint32_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "consensus")   == 0)
            out->consensus = (uint8_t)strtoul(value, NULL, 10);
        else if (strcmp(key, "hash")        == 0)
            strncpy((char *)out->hash, value, sizeof(out->hash) - 1);
        else if (strcmp(key, "tx_count")    == 0)
            out->transaction_count = (uint32_t)strtoul(value, NULL, 10);

        p = nl + 1;
    }

    /* Verify integrity: stored hash must match a freshly computed hash. */
    if (block_verify_hash(out) != EXIT_SUCCESS) {
        log_error("net_deserialize_block: hash integrity check failed");
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}

/* ── GETBODY helpers ──────────────────────────────────────────────────── */

/*
 * GETBODY protocol — two-phase block propagation:
 *
 * 1. Broadcaster sends a serialized header (net_serialize_block).
 * 2. If the receiver needs the transaction bodies it replies with:
 *      "GETBODY:<hash>\n"
 * 3. Broadcaster sends:  uint32_t count  +  binary Transaction[count].
 * 4. Receiver attaches transactions and calls chain_add.
 *
 * This keeps header broadcasts small and avoids transmitting raw key
 * material before the peer has validated the block header (ADR-014).
 */

#define GETBODY_PREFIX     "GETBODY:"
#define GETBODY_PREFIX_LEN 8
#define GETBODY_BUF_SIZE   128   /* "GETBODY:" + 64-char hash + "\n\0" */

/* Read exactly len bytes from ssl into buf.  Returns 0 on success, -1 on error. */
static int ssl_read_exact(SSL *ssl, void *buf, int len)
{
    char *p = buf;
    while (len > 0) {
        int n = SSL_read(ssl, p, len);
        if (n <= 0) return -1;
        p   += n;
        len -= n;
    }
    return 0;
}

/* Send uint32_t count followed by binary Transaction[count] over ssl. */
static int send_transactions(SSL *ssl, const Block *block)
{
    uint32_t count = block->transaction_count;

    if (SSL_write(ssl, &count, (int)sizeof(count)) <= 0) {
        log_error("send_transactions: failed to send count");
        return -1;
    }

    if (count == 0) return 0;

    size_t      tx_bytes  = count * sizeof(Transaction);
    const char *ptr       = (const char *)block->transactions;
    size_t      remaining = tx_bytes;

    while (remaining > 0) {
        int chunk   = (remaining > 65536) ? 65536 : (int)remaining;
        int written = SSL_write(ssl, ptr, chunk);
        if (written <= 0) {
            log_error("send_transactions: SSL_write failed");
            return -1;
        }
        ptr       += written;
        remaining -= (size_t)written;
    }

    return 0;
}

/* Read uint32_t count then binary Transaction[count] from ssl into block. */
static int recv_transactions(SSL *ssl, Block *block)
{
    uint32_t count = 0;
    if (ssl_read_exact(ssl, &count, (int)sizeof(count)) != 0) {
        log_error("recv_transactions: failed to read count");
        return -1;
    }

    if (count > MAX_TRANSACTIONS) {
        log_error("recv_transactions: count %u exceeds MAX_TRANSACTIONS %d",
                  count, MAX_TRANSACTIONS);
        return -1;
    }

    if (count == 0) {
        block->transaction_count = 0;
        return 0;
    }

    size_t tx_bytes = count * sizeof(Transaction);
    if (ssl_read_exact(ssl, block->transactions, (int)tx_bytes) != 0) {
        log_error("recv_transactions: failed to read transaction data");
        return -1;
    }

    block->transaction_count = count;
    return 0;
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

    /* GETBODY: if the peer requests transaction bodies, send them. */
    if (block->transaction_count > 0) {
        char req[GETBODY_BUF_SIZE];
        memset(req, 0, sizeof(req));
        int rb = SSL_read(ssl, req, (int)sizeof(req) - 1);
        if (rb > 0 &&
            strncmp(req, GETBODY_PREFIX, GETBODY_PREFIX_LEN) == 0) {
            if (send_transactions(ssl, block) < 0) {
                log_warn("net_broadcast_block: failed to send transactions to %s:%u",
                         peer_addr, peer_port);
            } else {
                log_info("net_broadcast_block: sent %u transaction(s) to %s:%u",
                         block->transaction_count, peer_addr, peer_port);
            }
        }
    }

    tls_shutdown(ssl);
    SSL_free(ssl);
    close(sock);
    return 0;
}

/* ── Server ───────────────────────────────────────────────────────────── */

int net_server_run(NetContext *ctx, Chain *chain)
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

            Block block;
            if (net_deserialize_block(buf, (size_t)n, &block) != EXIT_SUCCESS) {
                log_warn("net_server_run: invalid block data from %s", peer_ip);
            } else {
                /* GETBODY: fetch transaction bodies if the block has any. */
                if (block.transaction_count > 0) {
                    char req[GETBODY_BUF_SIZE];
                    int req_len = snprintf(req, sizeof(req),
                                          GETBODY_PREFIX "%s\n",
                                          (const char *)block.hash);
                    if (SSL_write(ssl, req, req_len) <= 0) {
                        log_warn("net_server_run: GETBODY send failed to %s",
                                 peer_ip);
                        block.transaction_count = 0; /* safe: no partial state */
                    } else if (recv_transactions(ssl, &block) < 0) {
                        log_warn("net_server_run: failed to receive transactions "
                                 "from %s", peer_ip);
                        block.transaction_count = 0;
                    } else {
                        log_info("net_server_run: received %u transaction(s) "
                                 "from %s", block.transaction_count, peer_ip);
                    }
                }

                if (chain != NULL) {
                    if (chain_add(chain, &block) == EXIT_SUCCESS) {
                        log_info("net_server_run: block %u from %s added to chain",
                                 block.index, peer_ip);
                    } else {
                        log_warn("net_server_run: block %u from %s rejected",
                                 block.index, peer_ip);
                    }
                } else {
                    log_info("net_server_run: block %u received (monitor mode)",
                             block.index);
                }
            }
        }

        tls_shutdown(ssl);
        SSL_free(ssl);
        close(client_sock);
    }

    /* Unreachable — loop runs until the process is terminated. */
    close(server_sock);
    return 0;
}
