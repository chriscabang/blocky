/* key_gen.c — CLI entry point: generate a Dilithium-3 keypair for a zuno identity.
 *
 * Usage: key_gen <id>
 *
 * Thin wrapper over key_generate() in src/key.c.
 * All logic (directory creation, OQS keygen, file writes) lives there.
 */

#include <stdio.h>
#include <stdlib.h>

#include "key.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: key_gen <id>\n");
        return 1;
    }

    if (key_generate(argv[1]) != EXIT_SUCCESS) {
        fprintf(stderr, "error: keygen failed for '%s' "
                        "(key may already exist, or id contains '/')\n",
                argv[1]);
        return 1;
    }

    printf("Generated keypair for '%s'\n", argv[1]);
    printf("  public key : %s/%s.pk\n",             KEYS_DIR, argv[1]);
    printf("  secret key : %s/%s.sk  (keep private)\n", KEYS_DIR, argv[1]);
    return 0;
}
