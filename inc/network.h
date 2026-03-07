/* network.h — TLS peer-to-peer network layer with post-quantum cryptography.
 *
 * Security posture
 * ────────────────
 * Security is prioritised over throughput and latency. Specifically:
 *   - TLS 1.3 minimum enforced; older protocol versions are rejected outright.
 *   - Hybrid post-quantum key exchange (p256_kyber768) when pqc_group is set.
 *   - Peer certificate verification (SSL_VERIFY_PEER) enabled on clients.
 *   - Bidirectional TLS close_notify on every connection tear-down.
 *   - No global mutable state — each NetContext is fully independent.
 *   - No exit() calls — callers receive error codes and decide next action.
 *
 * Block serialization
 * ────────────────────
 * Only block header fields (index, timestamp, hashes, nonce, consensus,
 * tx_count) are broadcast.  Full transaction bodies are fetched on demand.
 * This keeps broadcast messages small and avoids transmitting raw key material
 * or signature bytes over the wire before the receiving peer has validated
 * the block header.
 */

#ifndef NETWORK_H
#define NETWORK_H

#include <stddef.h>
#include <stdint.h>

#include "block.h"
#include "chain.h"

/* ── Constants ────────────────────────────────────────────────────────── */

/* Buffer size for a serialized block header (no transaction bodies). */
#define NET_BLOCK_BUF_SIZE  4096

/* TCP listen backlog. */
#define NET_LISTEN_BACKLOG  16

/* Recommended PQC key-exchange group: hybrid classical P-256 + Kyber-768.
 * Requires the OQS OpenSSL provider to be loaded at runtime.
 * Pass as cfg.pqc_group in production; use NULL to disable PQC enforcement
 * (e.g. in test environments without the OQS provider). */
#define NET_PQC_GROUP  "p256_kyber768"

/* ── Configuration ────────────────────────────────────────────────────── */

typedef struct {
    const char *cert_file;  /* PEM certificate path.
                               Required for servers.
                               Optional for clients (mutual TLS only).   */
    const char *key_file;   /* PEM private key path.
                               Required for servers.
                               Optional for clients (mutual TLS only).   */
    const char *ca_file;    /* CA bundle for peer certificate verification.
                               NULL = use the system default CA store.   */
    const char *bind_addr;  /* Bind address for servers. NULL = all ifaces. */
    const char *pqc_group;  /* Key-exchange group passed to
                               SSL_CTX_set1_groups_list().
                               NULL = skip (no PQC group enforcement).
                               Use NET_PQC_GROUP for production nodes.   */
    uint16_t    port;
} NetConfig;

/* ── Opaque context ───────────────────────────────────────────────────── */

typedef struct NetContext NetContext;

/* ── Lifecycle ────────────────────────────────────────────────────────── */

/*
 * Create a TLS server context from cfg.
 * cert_file and key_file are required; returns NULL if either is absent
 * or fails to load.
 * Returns NULL on any error (details logged via log_error).
 */
NetContext *net_context_server(const NetConfig *cfg);

/*
 * Create a TLS client context from cfg.
 * cert_file / key_file are optional (set NULL unless doing mutual TLS).
 * ca_file controls peer verification; NULL uses the system CA store.
 * Returns NULL on any error (details logged via log_error).
 */
NetContext *net_context_client(const NetConfig *cfg);

/*
 * Free a context and its underlying SSL_CTX.
 * Safe to call with NULL.
 */
void net_context_free(NetContext *ctx);

/* ── Serialization ────────────────────────────────────────────────────── */

/*
 * Serialize block header fields into buf as NUL-terminated text.
 * Returns the number of bytes written (excluding NUL) on success, -1 on
 * error (invalid arguments or buf too small).
 * buf must be at least NET_BLOCK_BUF_SIZE bytes for reliable operation.
 */
int net_serialize_block(const Block *block, char *buf, size_t bufsz);

/*
 * Deserialize a block header from a NUL-terminated buffer received over the
 * network (the inverse of net_serialize_block).
 * Writes fields into out, then calls block_verify_hash to confirm integrity.
 * Returns EXIT_SUCCESS on success, EXIT_FAILURE if arguments are invalid or
 * hash verification fails.
 * Note: only header fields are populated; out->transactions is zeroed.
 */
int net_deserialize_block(const char *buf, size_t len, Block *out);

/* ── Broadcast ────────────────────────────────────────────────────────── */

/*
 * Broadcast a serialized block header to a single peer over TLS.
 * Returns 0 on success, -1 on error.
 */
int net_broadcast_block(NetContext       *ctx,
                        const Block      *block,
                        const char       *peer_addr,
                        uint16_t          peer_port);

/* ── Server ───────────────────────────────────────────────────────────── */

/*
 * Bind to cfg->port and run the blocking accept loop.
 * Handles one connection at a time (single-threaded; appropriate for
 * Raspberry Pi / low-concurrency nodes — see ADR-014).
 *
 * chain: if non-NULL, received blocks are deserialized and added to the chain
 *        via chain_validate + chain_add.  Pass NULL for log-only/monitor mode.
 *
 * Returns -1 on a fatal socket or TLS setup error.
 */
int net_server_run(NetContext *ctx, Chain *chain);

#endif /* NETWORK_H */
