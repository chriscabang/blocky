/* insertblock.c — Demo: read and inspect a block from the object store.
 *
 * Usage: insertblock <hash>
 *        insertblock HEAD          (resolves the current chain tip)
 *
 * Reads the block from .chain/blocks/<hash>, prints every header field
 * and transaction summary, then verifies the stored hash against a freshly
 * computed hash to confirm on-disk integrity.
 *
 * Exit code: 0 = block found and hash valid; 1 = any error or hash mismatch.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "block.h"
#include "storage.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: insertblock <hash|HEAD>\n");
        return 1;
    }

    char hash[HASH_SIZE];
    if (strcmp(argv[1], "HEAD") == 0) {
        if (storage_head(hash, sizeof(hash)) != EXIT_SUCCESS) {
            fprintf(stderr, "error: chain is empty (no HEAD)\n");
            return 1;
        }
    } else {
        snprintf(hash, sizeof(hash), "%s", argv[1]);
    }

    Block *blk = storage_read(hash);
    if (!blk) {
        fprintf(stderr, "error: block not found: %s\n", hash);
        return 1;
    }

    printf("index            : %u\n",  blk->index);
    printf("timestamp        : %ld\n", (long)blk->timestamp);
    printf("previous_hash    : %s\n",  (char *)blk->previous_hash);
    printf("merkle_root      : %s\n",  (char *)blk->merkle_root);
    printf("nonce            : %u\n",  blk->nonce);
    printf("consensus        : %s\n",  blk->consensus ? "PoS" : "PoW");
    printf("hash             : %s\n",  (char *)blk->hash);
    printf("transaction_count: %u\n",  blk->transaction_count);

    for (uint32_t i = 0; i < blk->transaction_count; i++) {
        const Transaction *tx = &blk->transactions[i];
        printf("  tx[%u] amount=%llu  nonce=%llu  sig_len=%zu\n",
               i,
               (unsigned long long)tx->amount,
               (unsigned long long)tx->nonce,
               tx->signature_length);
    }

    int rc = 0;
    if (block_verify_hash(blk) != EXIT_SUCCESS) {
        fprintf(stderr, "warning: hash integrity check FAILED\n");
        rc = 1;
    } else {
        printf("integrity        : OK\n");
    }

    block_free(blk);
    return rc;
}
