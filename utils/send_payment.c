/* send_payment.c — Demo: sign and commit a Dilithium-3 payment transaction.
 *
 * Usage: send_payment <amount_tokens>
 *
 * Generates ephemeral Dilithium-3 keypairs for sender and recipient, builds
 * a signed transaction, mines a block at default difficulty, and appends the
 * block to the local chain.
 *
 * In production the keypairs would be loaded from persistent key files
 * (see the keygen utility).  The nonce would be a monotonic per-sender
 * counter stored alongside the key.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <oqs/oqs.h>

#include "block.h"
#include "chain.h"
#include "pow.h"
#include "transaction.h"

int main(int argc, char *argv[])
{
    if (argc != 2) {
        fprintf(stderr, "usage: send_payment <amount_tokens>\n");
        return 1;
    }

    uint64_t tokens = strtoull(argv[1], NULL, 10);
    if (tokens == 0) {
        fprintf(stderr, "error: amount must be > 0\n");
        return 1;
    }

    int rc = 1;

    /* ── 1. Generate ephemeral Dilithium-3 keypairs ─────────────────────── */
    OQS_SIG *sig         = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
    uint8_t *sender_pub  = NULL;
    uint8_t *sender_priv = NULL;
    uint8_t *recip_pub   = NULL;
    uint8_t *recip_priv  = NULL;
    Chain   *chain       = NULL;
    Block   *blk         = NULL;

    if (!sig) {
        fprintf(stderr, "error: OQS_SIG_new failed\n");
        return 1;
    }

    sender_pub  = malloc(sig->length_public_key);
    sender_priv = malloc(sig->length_secret_key);
    recip_pub   = malloc(sig->length_public_key);
    recip_priv  = malloc(sig->length_secret_key);

    if (!sender_pub || !sender_priv || !recip_pub || !recip_priv) {
        fprintf(stderr, "error: out of memory\n");
        goto done;
    }

    if (OQS_SIG_keypair(sig, sender_pub, sender_priv) != OQS_SUCCESS ||
        OQS_SIG_keypair(sig, recip_pub,  recip_priv)  != OQS_SUCCESS) {
        fprintf(stderr, "error: keypair generation failed\n");
        goto done;
    }

    printf("[keygen] Dilithium-3 keypairs ready.\n");

    /* ── 2. Build and sign transaction ────────────────────────────────── */
    Transaction tx;
    memset(&tx, 0, sizeof(tx));

    /*
     * sender / recipient fields hold raw public key bytes.
     * The full MAX_PUBLIC_KEY_LENGTH bytes are covered by build_message()
     * and therefore included in the signature.
     */
    memcpy(tx.sender,    sender_pub, sig->length_public_key);
    memcpy(tx.recipient, recip_pub,  sig->length_public_key);
    tx.amount = tokens * MICRO_PER_TOKEN;
    tx.nonce  = (uint64_t)time(NULL); /* use monotonic counter in production */

    printf("[tx]     %llu tokens (%llu micro)  nonce=%llu\n",
           (unsigned long long)tokens,
           (unsigned long long)tx.amount,
           (unsigned long long)tx.nonce);

    if (sign_transaction(&tx, sender_priv) != EXIT_SUCCESS) {
        fprintf(stderr, "error: sign_transaction failed\n");
        goto done;
    }
    printf("[sign]   signature_length=%zu bytes\n", tx.signature_length);

    if (verify_transaction(&tx, sender_pub) != EXIT_SUCCESS) {
        fprintf(stderr, "error: signature verification failed\n");
        goto done;
    }
    printf("[verify] Signature OK.\n");

    /* ── 3. Create, mine, and commit block ────────────────────────────── */
    chain = chain_load();
    if (!chain) {
        fprintf(stderr, "error: chain_load failed\n");
        goto done;
    }

    blk = block_create(chain->head->index + 1, chain->head->hash);
    if (!blk) {
        fprintf(stderr, "error: block_create failed\n");
        goto done;
    }

    blk->transactions[0]   = tx;
    blk->transaction_count = 1;

    printf("[mine]   difficulty=%u ...\n", DIFFICULTY);
    if (mine_block(blk, DIFFICULTY) != EXIT_SUCCESS) {
        fprintf(stderr, "error: mine_block failed\n");
        goto done;
    }
    printf("[mine]   block #%u  hash=%.16s...\n",
           blk->index, (char *)blk->hash);

    if (chain_add(chain, blk) != EXIT_SUCCESS) {
        fprintf(stderr, "error: chain_add failed\n");
        goto done;
    }
    printf("[chain]  Block committed to local chain.\n");
    rc = 0;

done:
    block_free(blk);
    chain_unload(chain);
    if (sender_priv) OQS_MEM_cleanse(sender_priv, sig->length_secret_key);
    if (recip_priv)  OQS_MEM_cleanse(recip_priv,  sig->length_secret_key);
    free(sender_pub); free(sender_priv);
    free(recip_pub);  free(recip_priv);
    OQS_SIG_free(sig);
    return rc;
}
