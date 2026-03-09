/* propose_block.c — Demo: mine a block and broadcast it to a peer over TLS.
 *
 * Usage: proposeblock <peer_addr> <peer_port> <cert.pem> <key.pem> [ca.pem]
 *
 *   peer_addr   IPv4 address of the receiving node
 *   peer_port   TCP port the peer is listening on
 *   cert.pem    Client certificate for mutual TLS
 *   key.pem     Corresponding private key
 *   ca.pem      CA bundle to verify the peer certificate (optional;
 *               omit to use the system CA store — may fail for self-signed)
 *
 * Loads the chain tip, mines a new empty block, serializes the header, and
 * sends it to the peer via net_broadcast_block() over TLS 1.3.
 * After a successful broadcast the block is also committed to the local chain.
 *
 * To use PQC hybrid key exchange, change pqc_group below to NET_PQC_GROUP
 * once both nodes have the OQS OpenSSL provider loaded.
 */

#include <stdio.h>
#include <stdlib.h>

#include "block.h"
#include "chain.h"
#include "network.h"
#include "pow.h"

int main(int argc, char *argv[])
{
    if (argc < 5 || argc > 6) {
        fprintf(stderr,
                "usage: propose_block <peer_addr> <peer_port>"
                " <cert.pem> <key.pem> [ca.pem]\n");
        return 1;
    }

    const char *peer_addr = argv[1];
    uint16_t    peer_port = (uint16_t)atoi(argv[2]);
    const char *cert_file = argv[3];
    const char *key_file  = argv[4];
    const char *ca_file   = (argc == 6) ? argv[5] : NULL;

    /* ── 1. Mine a new block ──────────────────────────────────────────── */
    Chain *chain = chain_load();
    if (!chain) {
        fprintf(stderr, "error: chain_load failed\n");
        return 1;
    }

    Block *blk = block_create(chain->head->index + 1, chain->head->hash);
    if (!blk) {
        fprintf(stderr, "error: block_create failed\n");
        chain_unload(chain);
        return 1;
    }
    blk->transaction_count = 0;

    printf("[mine]   Mining block #%u (difficulty=%u)...\n",
           blk->index, DIFFICULTY);
    if (mine_block(blk, DIFFICULTY) != EXIT_SUCCESS) {
        fprintf(stderr, "error: mine_block failed\n");
        block_free(blk);
        chain_unload(chain);
        return 1;
    }
    printf("[mine]   hash=%.16s...\n", (char *)blk->hash);

    /* ── 2. Build TLS client context ──────────────────────────────────── */
    NetConfig cfg = {
        .cert_file = cert_file,
        .key_file  = key_file,
        .ca_file   = ca_file,
        .pqc_group = NULL,   /* set to NET_PQC_GROUP when peer supports PQC */
        .port      = peer_port,
    };

    NetContext *net = net_context_client(&cfg);
    if (!net) {
        fprintf(stderr, "error: net_context_client failed\n");
        block_free(blk);
        chain_unload(chain);
        return 1;
    }

    /* ── 3. Broadcast ─────────────────────────────────────────────────── */
    printf("[net]    Sending to %s:%u ...\n", peer_addr, peer_port);
    if (net_broadcast_block(net, blk, peer_addr, peer_port) != 0) {
        fprintf(stderr, "error: broadcast failed\n");
        net_context_free(net);
        block_free(blk);
        chain_unload(chain);
        return 1;
    }
    printf("[net]    Block broadcast successful.\n");

    /* ── 4. Commit locally ────────────────────────────────────────────── */
    if (chain_add(chain, blk) != EXIT_SUCCESS) {
        fprintf(stderr, "error: chain_add failed\n");
        net_context_free(net);
        block_free(blk);
        chain_unload(chain);
        return 1;
    }
    printf("[chain]  Block committed to local chain.\n");

    net_context_free(net);
    block_free(blk);
    chain_unload(chain);
    return 0;
}
