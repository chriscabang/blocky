/* inspect_chain.c — Demo: display the current chain state and block history.
 *
 * Usage: inspect_chain [page_size]
 *   page_size  number of blocks to list from HEAD backwards (default: 10)
 *
 * Loads the chain, prints the tip summary via chain_info(), then walks
 * backwards printing one line per block via chain_list().
 */

#include <stdio.h>
#include <stdlib.h>

#include "chain.h"

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

    chain_info(chain);
    printf("\n");
    chain_list(chain, page);

    chain_unload(chain);
    return 0;
}
