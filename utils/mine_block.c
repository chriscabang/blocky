/* mine_block.c — Demo: proof-of-work mining with timing.
 *
 * Usage: mine_block [difficulty]
 *   difficulty  leading hex zeros required (default: DIFFICULTY=4)
 *
 * Creates an empty block on top of the current chain tip, mines it at the
 * requested difficulty, reports elapsed time and estimated hash rate, then
 * commits the block to the local chain.
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "block.h"
#include "chain.h"
#include "pow.h"

int main(int argc, char *argv[])
{
    uint32_t difficulty = DIFFICULTY;

    if (argc == 2) {
        unsigned long d = strtoul(argv[1], NULL, 10);
        if (d == 0 || d > 64) {
            fprintf(stderr, "error: difficulty must be 1..64\n");
            return 1;
        }
        difficulty = (uint32_t)d;
    } else if (argc != 1) {
        fprintf(stderr, "usage: mine_block [difficulty]\n");
        return 1;
    }

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

    /* Empty block — valid for PoW demonstration. */
    blk->transaction_count = 0;

    printf("[mine]   Block #%u  difficulty=%u\n", blk->index, difficulty);
    printf("[mine]   prev=%.16s...\n", (char *)blk->previous_hash);

    struct timespec t0, t1;
    clock_gettime(CLOCK_MONOTONIC, &t0);

    int ok = mine_block(blk, difficulty);

    clock_gettime(CLOCK_MONOTONIC, &t1);

    if (ok != EXIT_SUCCESS) {
        fprintf(stderr,
                "error: mine_block failed (nonce space exhausted?)\n");
        block_free(blk);
        chain_unload(chain);
        return 1;
    }

    double elapsed = (double)(t1.tv_sec  - t0.tv_sec)
                   + (double)(t1.tv_nsec - t0.tv_nsec) / 1e9;
    double rate    = (elapsed > 0.0) ? (double)blk->nonce / elapsed : 0.0;

    printf("[mine]   Solved!  nonce=%u  hash=%.16s...\n",
           blk->nonce, (char *)blk->hash);
    printf("[mine]   Elapsed: %.3f s  (~%.0f H/s)\n", elapsed, rate);

    if (chain_add(chain, blk) != EXIT_SUCCESS) {
        fprintf(stderr, "error: chain_add failed\n");
        block_free(blk);
        chain_unload(chain);
        return 1;
    }
    printf("[chain]  Block committed to local chain.\n");

    block_free(blk);
    chain_unload(chain);
    return 0;
}
