/*
 * test_integration_miner.c — Integration tests: mining blocks.
 *
 * These tests cover the miner's perspective end-to-end:
 *
 *   1. chain_load()           → load or create the chain (genesis)
 *   2. block_create()         → build a candidate block on top of head
 *   3. compute_merkle_root()  → commit any transactions into the Merkle root
 *   4. mine_block(DIFFICULTY) → find a nonce with DIFFICULTY leading hex zeros
 *   5. chain_add()            → validate PoW + persist + advance chain tip
 *   6. validate_block_pow()   → standalone verification of a mined block
 *
 * Security tests confirm that the chain rejects:
 *   - Blocks that were not mined (hash does not meet difficulty)
 *   - Blocks whose hash was tampered after mining
 *   - Blocks that do not extend the current chain tip
 *
 * Mining at DIFFICULTY=4 averages ~65 000 SHA-256 hashes per block.
 * Three consecutive mines (miner/chain_growth) take well under one second
 * on any modern CPU.
 */

#include <stdarg.h>
#include <stddef.h>

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#include <setjmp.h>
#include <cmocka.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chain.h"
#include "block.h"
#include "consensus.h"
#include "crypto.h"
#include "pow.h"
#include "storage.h"
#include "transaction.h"
#include "log.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

/*
 * Mine an empty PoW block that extends c->head.
 * Sets CONSENSUS_POW, computes an empty Merkle root ("0"), then mines.
 * Returns a heap-allocated block; caller must block_free().
 */
static Block *mine_next(const Chain *c)
{
    Block *b = block_create(c->head->index + 1, c->head->hash);
    if (!b) return NULL;

    b->timestamp = (time_t)(1700000000 + (long)c->head->index + 1);
    b->consensus = CONSENSUS_POW;

    compute_merkle_root(b, (char *)b->merkle_root); /* writes "0" for no txns */

    if (mine_block(b, DIFFICULTY) != EXIT_SUCCESS) {
        block_free(b);
        return NULL;
    }
    return b;
}

/*
 * Mine a PoW block with one transaction extending c->head.
 * Returns a heap-allocated block; caller must block_free().
 */
static Block *mine_next_with_tx(const Chain *c,
                                 const char *from, const char *to,
                                 uint64_t tokens)
{
    Block *b = block_create(c->head->index + 1, c->head->hash);
    if (!b) return NULL;

    b->timestamp = (time_t)(1700000000 + (long)c->head->index + 1);
    b->consensus = CONSENSUS_POW;

    strncpy(b->transactions[0].sender,    from, sizeof(b->transactions[0].sender)    - 1);
    strncpy(b->transactions[0].recipient, to,   sizeof(b->transactions[0].recipient) - 1);
    b->transactions[0].amount = tokens * MICRO_PER_TOKEN;
    b->transactions[0].nonce  = 1;
    b->transaction_count      = 1;

    compute_merkle_root(b, (char *)b->merkle_root);

    if (mine_block(b, DIFFICULTY) != EXIT_SUCCESS) {
        block_free(b);
        return NULL;
    }
    return b;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state)
{
    system("rm -rf .chain");
    Chain *c = chain_load();
    if (!c) return -1;
    *state = c;
    return 0;
}

static int teardown(void **state)
{
    if (*state) {
        chain_unload(*state);
        *state = NULL;
    }
    system("rm -rf .chain");
    return 0;
}

/* ── miner/proof_of_work ──────────────────────────────────────────────── */

/*
 * mine_block must return EXIT_SUCCESS and write a hash with at least
 * DIFFICULTY leading hex '0' characters.
 * This is the miner's primary guarantee: the hash satisfies the puzzle.
 */
static void test_mine_block_returns_success(void **state)
{
    Chain *c = *state;

    Block *b = mine_next(c);
    assert_non_null(b);

    /* All DIFFICULTY leading characters must be '0'. */
    for (int i = 0; i < DIFFICULTY; i++)
        assert_int_equal(b->hash[i], '0');

    /* Hash must be a full 64-char hex string. */
    assert_int_equal((int)strlen((char *)b->hash), 64);

    block_free(b);
}

/*
 * A mined block must pass validate_block_pow at the same difficulty.
 * Confirms both the difficulty gate and hash integrity check in pow.c.
 */
static void test_mined_block_passes_pow_validation(void **state)
{
    Chain *c = *state;

    Block *b = mine_next(c);
    assert_non_null(b);

    assert_int_equal(validate_block_pow(b, DIFFICULTY), EXIT_SUCCESS);

    block_free(b);
}

/*
 * chain_add must accept a properly mined PoW block and advance the chain tip.
 * The miner's block becomes the new HEAD both in memory and on disk.
 */
static void test_mined_block_accepted_by_chain(void **state)
{
    Chain *c = *state;

    Block *b = mine_next(c);
    assert_non_null(b);

    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

    /* In-memory tip is the new block. */
    assert_int_equal((int)c->head->index, 1);
    assert_memory_equal(c->head->hash, b->hash, HASH_SIZE);

    /* Disk HEAD matches. */
    char disk_head[HASH_SIZE];
    assert_int_equal(storage_head(disk_head, sizeof(disk_head)), EXIT_SUCCESS);
    assert_string_equal(disk_head, (char *)b->hash);

    block_free(b);
}

/*
 * A miner can include transactions in a mined block.
 * The block must be accepted, and the transaction must appear in storage.
 */
static void test_miner_includes_transactions(void **state)
{
    Chain *c = *state;

    Block *b = mine_next_with_tx(c, "miner", "treasury", 100);
    assert_non_null(b);

    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

    Block *stored = storage_read((char *)b->hash);
    assert_non_null(stored);
    assert_int_equal((int)stored->transaction_count, 1);
    assert_string_equal(stored->transactions[0].sender,    "miner");
    assert_string_equal(stored->transactions[0].recipient, "treasury");

    free(stored);
    block_free(b);
}

/* ── miner/chain_growth ───────────────────────────────────────────────── */

/*
 * Mine three consecutive blocks.
 * After all three are committed, the chain tip must be at index 3 and
 * each block must be independently readable from storage.
 */
static void test_mine_three_consecutive_blocks(void **state)
{
    Chain *c = *state;

    Block *b1 = mine_next(c);
    assert_non_null(b1);
    assert_int_equal(chain_add(c, b1), EXIT_SUCCESS);

    Block *b2 = mine_next(c);
    assert_non_null(b2);
    assert_int_equal(chain_add(c, b2), EXIT_SUCCESS);

    Block *b3 = mine_next(c);
    assert_non_null(b3);
    assert_int_equal(chain_add(c, b3), EXIT_SUCCESS);

    assert_int_equal((int)c->head->index, 3);

    /* Each block must be readable from storage. */
    Block *s1 = storage_read((char *)b1->hash);
    Block *s2 = storage_read((char *)b2->hash);
    Block *s3 = storage_read((char *)b3->hash);
    assert_non_null(s1);
    assert_non_null(s2);
    assert_non_null(s3);
    assert_int_equal((int)s1->index, 1);
    assert_int_equal((int)s2->index, 2);
    assert_int_equal((int)s3->index, 3);

    free(s1); free(s2); free(s3);
    block_free(b3); block_free(b2); block_free(b1);
}

/*
 * Each mined block must reference the previous block's hash.
 * This verifies that the chain is properly linked and that block_verify_hash
 * confirms the linkage for each stored block.
 */
static void test_chain_link_integrity(void **state)
{
    Chain *c = *state;

    /* Save genesis hash before it is recycled by chain_add. */
    unsigned char hash0[HASH_SIZE];
    memcpy(hash0, c->head->hash, HASH_SIZE);

    Block *b1 = mine_next(c);
    assert_non_null(b1);
    assert_int_equal(chain_add(c, b1), EXIT_SUCCESS);

    Block *b2 = mine_next(c);
    assert_non_null(b2);
    assert_int_equal(chain_add(c, b2), EXIT_SUCCESS);

    /* b1.previous_hash == genesis hash */
    assert_memory_equal(b1->previous_hash, hash0, HASH_SIZE);

    /* b2.previous_hash == b1.hash */
    assert_memory_equal(b2->previous_hash, b1->hash, HASH_SIZE);

    /* Both mined blocks pass standalone PoW validation. */
    assert_int_equal(validate_block_pow(b1, DIFFICULTY), EXIT_SUCCESS);
    assert_int_equal(validate_block_pow(b2, DIFFICULTY), EXIT_SUCCESS);

    block_free(b2);
    block_free(b1);
}

/* ── miner/security ───────────────────────────────────────────────────── */

/*
 * A block submitted without mining (just block_compute_hash) has no leading
 * zeros in its hash and must be rejected by chain_add.
 * The consensus layer (verify_pow_rules → validate_block_pow) enforces this.
 *
 * NOTE: There is a 1 in 16^DIFFICULTY chance the unhashed block accidentally
 * satisfies the difficulty target. This risk is negligible (1 in 65 536 for
 * DIFFICULTY=4) and is accepted here.
 */
static void test_unmined_pow_block_rejected(void **state)
{
    Chain *c = *state;

    Block *b = block_create(c->head->index + 1, c->head->hash);
    assert_non_null(b);

    b->timestamp = 1700000001;
    b->consensus = CONSENSUS_POW;
    compute_merkle_root(b, (char *)b->merkle_root);
    /* block_compute_hash instead of mine_block — no leading zeros */
    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);

    assert_int_equal(chain_add(c, b), EXIT_FAILURE);
    assert_int_equal((int)c->head->index, 0); /* tip must not have changed */

    block_free(b);
}

/*
 * Corrupting a byte of a mined block's hash after mining must cause chain_add
 * to reject it. block_verify_hash recomputes and detects the mismatch.
 */
static void test_tampered_hash_after_mining_rejected(void **state)
{
    Chain *c = *state;

    Block *b = mine_next(c);
    assert_non_null(b);

    b->hash[8] = (b->hash[8] == '0') ? '1' : '0'; /* flip a mid-hash byte */

    assert_int_equal(chain_add(c, b), EXIT_FAILURE);
    assert_int_equal((int)c->head->index, 0);

    block_free(b);
}

/*
 * A block with a previous_hash that does not match the chain tip must be
 * rejected immediately by the link check in chain_validate.
 * The miner's block must extend the canonical head — not an arbitrary block.
 */
static void test_wrong_previous_hash_rejected(void **state)
{
    Chain *c = *state;

    /* Mine block 1 and add it so the head advances. */
    Block *b1 = mine_next(c);
    assert_non_null(b1);
    assert_int_equal(chain_add(c, b1), EXIT_SUCCESS);

    /*
     * Build block 2 but make it point back to genesis (index=0 sentinel)
     * instead of b1. Mine it so the hash meets difficulty — the rejection
     * must happen at the link check, not the PoW check.
     */
    Block *b2 = block_create(2, (const unsigned char *)"0");
    assert_non_null(b2);
    b2->timestamp = 1700000002;
    b2->consensus = CONSENSUS_POW;
    compute_merkle_root(b2, (char *)b2->merkle_root);
    assert_int_equal(mine_block(b2, DIFFICULTY), EXIT_SUCCESS);

    assert_int_equal(chain_add(c, b2), EXIT_FAILURE);
    assert_int_equal((int)c->head->index, 1); /* tip stayed at b1 */

    block_free(b2);
    block_free(b1);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest pow_tests[] = {
        cmocka_unit_test_setup_teardown(test_mine_block_returns_success,       setup, teardown),
        cmocka_unit_test_setup_teardown(test_mined_block_passes_pow_validation, setup, teardown),
        cmocka_unit_test_setup_teardown(test_mined_block_accepted_by_chain,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_miner_includes_transactions,      setup, teardown),
    };

    const struct CMUnitTest growth_tests[] = {
        cmocka_unit_test_setup_teardown(test_mine_three_consecutive_blocks, setup, teardown),
        cmocka_unit_test_setup_teardown(test_chain_link_integrity,          setup, teardown),
    };

    const struct CMUnitTest security_tests[] = {
        cmocka_unit_test_setup_teardown(test_unmined_pow_block_rejected,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_tampered_hash_after_mining_rejected, setup, teardown),
        cmocka_unit_test_setup_teardown(test_wrong_previous_hash_rejected,        setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("miner/proof_of_work",  pow_tests,      NULL, NULL);
    failures += cmocka_run_group_tests_name("miner/chain_growth",   growth_tests,   NULL, NULL);
    failures += cmocka_run_group_tests_name("miner/security",       security_tests, NULL, NULL);
    return failures;
}
