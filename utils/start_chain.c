/* startchain.c — Demo: start a TLS P2P listener node.
 *
 * Usage: startchain <port> <cert.pem> <key.pem> [ca.pem]
 *
 *   port        TCP port to bind (e.g. 8333)
 *   cert.pem    Server certificate (PEM)
 *   key.pem     Corresponding private key (PEM)
 *   ca.pem      CA bundle for mutual TLS client verification (optional)
 *
 * Binds to all interfaces on the given port and enters the accept loop.
 * Each incoming connection is expected to carry a serialized block header
 * (see net_serialize_block).  Received data is logged; chain integration
 * is pending (ADR-014 TODO: deserialize + chain_add).
 *
 * To enable PQC hybrid key exchange, set pqc_group to NET_PQC_GROUP once
 * all peers have the OQS OpenSSL provider loaded.
 *
 * Press Ctrl-C to stop the server.
 */

#include <stdio.h>
#include <stdlib.h>

#include "network.h"

int main(int argc, char *argv[])
{
    if (argc < 4 || argc > 5) {
        fprintf(stderr,
                "usage: startchain <port> <cert.pem> <key.pem> [ca.pem]\n");
        return 1;
    }

    uint16_t    port      = (uint16_t)atoi(argv[1]);
    const char *cert_file = argv[2];
    const char *key_file  = argv[3];
    const char *ca_file   = (argc == 5) ? argv[4] : NULL;

    NetConfig cfg = {
        .cert_file = cert_file,
        .key_file  = key_file,
        .ca_file   = ca_file,
        .bind_addr = NULL,    /* all interfaces */
        .pqc_group = NULL,    /* set to NET_PQC_GROUP for PQC key exchange */
        .port      = port,
    };

    NetContext *ctx = net_context_server(&cfg);
    if (!ctx) {
        fprintf(stderr, "error: net_context_server failed\n");
        return 1;
    }

    printf("[node]   TLS server starting on port %u\n", port);
    printf("[node]   Press Ctrl-C to stop.\n");

    int rc = net_server_run(ctx);
    net_context_free(ctx);
    return (rc == 0) ? 0 : 1;
}
