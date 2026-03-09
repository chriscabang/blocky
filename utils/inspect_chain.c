/* inspect_chain.c — Demo: display the current chain state and block history.
 *
 * Usage: inspect_chain [page_size]
 *   page_size  number of blocks to list from HEAD backwards (default: 10)
 *
 * Loads the chain tip, prints a tip summary, then lists blocks via
 * storage_scan() and storage_read().
 */

#include <stdio.h>
#include <stdlib.h>

#include "block.h"
#include "chain.h"
#include "storage.h"

int main(int argc, char *argv[])
{
    unsigned int page = 10;

    if (argc == 2) {
        unsigned long n = strtoul(argv[1], NULL, 10);
        if (n > 0 && n < 10000)
            page = (unsigned int)n;
    } else if (argc != 1) {
        fprintf(stderr, "usage: inspect_chain [page_size]\n");
        return 1;
    }

    Chain *chain = chain_load();
    if (!chain) {
        fprintf(stderr, "error: chain_load failed\n");
        return 1;
    }

    printf("Chain tip: block #%u  %.16s...\n\n",
           chain->head->index, (char *)chain->head->hash);
    chain_unload(chain);

    unsigned int count = page;
    char **hashes = storage_scan(0, &count);
    if (!hashes || count == 0) {
        printf("(chain is empty)\n");
        return 0;
    }

    for (unsigned int i = 0; i < count; i++) {
        Block *b = storage_read(hashes[i]);
        if (b) {
            printf("  [%u] %.16s...\n", b->index, hashes[i]);
            block_free(b);
        }
        free(hashes[i]);
    }
    free(hashes);

    return 0;
}
