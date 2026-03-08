/*
 * test_integration_payment.c — Integration tests: sending and receiving payments.
 *
 * These tests exercise the full payment flow end-to-end using the library API,
 * mirroring the CLI's cmd_send + cmd_commit workflow:
 *
 *   1. chain_load()           → initialize chain (genesis block)
 *   2. Build Transaction(s)   → fill sender / recipient / amount / nonce
 *   3. block_create()         → create block extending head
 *   4. compute_merkle_root()  → hash all transactions into merkle root
 *   5. mine_block(DIFFICULTY) → PoW: find nonce with DIFFICULTY leading zeros
 *   6. chain_add()            → validate + persist + advance chain tip
 *   7. storage_read()         → verify on-chain data matches what was sent
 *
 * NOTE: Each test mines one block at DIFFICULTY (default 4). Expected to
 * complete in well under one second on any modern CPU (~65 000 hashes avg).
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
 * Construct a Transaction from plain label strings and a token amount.
 * This mirrors the cmd_send parsing step (--from / --to / --amount).
 * sender and recipient hold label strings here; real deployments use
 * Dilithium-3 public keys (ADR-003).
 */
static Transaction make_tx(const char *from, const char *to,
                            uint64_t tokens, uint64_t nonce)
{
    Transaction tx = {0};
    strncpy(tx.sender,    from, sizeof(tx.sender)    - 1);
    strncpy(tx.recipient, to,   sizeof(tx.recipient) - 1);
    tx.amount = tokens * MICRO_PER_TOKEN;
    tx.nonce  = nonce;
    return tx;
}

/*
 * Build, mine, and return a PoW block that extends c->head.
 * Mirrors cmd_commit: CONSENSUS_POW + mine_block(DIFFICULTY).
 * Caller must block_free() the returned block.
 * Returns NULL if allocation or mining fails (test will fail via assert).
 */
static Block *commit_block(const Chain *c,
                            const Transaction *txns, int count)
{
    Block *b = block_create(c->head->index + 1, c->head->hash);
    if (!b) return NULL;

    b->timestamp  = (time_t)(1700000000 + (long)c->head->index + 1);
    b->consensus  = CONSENSUS_POW;

    for (int i = 0; i < count; i++)
        b->transactions[i] = txns[i];
    b->transaction_count = (uint32_t)count;

    compute_merkle_root(b, (char *)b->merkle_root);

    if (mine_block(b, DIFFICULTY) != EXIT_SUCCESS) {
        block_free(b);
        return NULL;
    }
    return b;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

/* Each test gets a fresh chain starting from genesis. */
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

/* ── payment/send ─────────────────────────────────────────────────────── */

/*
 * Use case: Alice sends 50 tokens to Bob.
 *
 * After committing the payment block:
 *   - chain_add must succeed
 *   - chain tip advances to block #1
 *   - The stored block must contain exactly one transaction
 *   - sender, recipient, and amount must be preserved byte-for-byte
 */
static void test_alice_sends_50_to_bob(void **state)
{
    Chain *c = *state;

    Transaction tx = make_tx("alice", "bob", 50, 1);
    Block *b = commit_block(c, &tx, 1);
    assert_non_null(b);

    /* The mined hash must satisfy the difficulty target. */
    for (int i = 0; i < DIFFICULTY; i++)
        assert_int_equal(b->hash[i], '0');

    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);
    assert_int_equal((int)c->head->index, 1);

    /* Read the block back from storage — confirm the payment is on-chain. */
    Block *stored = storage_read((char *)b->hash);
    assert_non_null(stored);

    assert_int_equal((int)stored->transaction_count, 1);
    assert_string_equal(stored->transactions[0].sender,    "alice");
    assert_string_equal(stored->transactions[0].recipient, "bob");
    assert_int_equal((long long)stored->transactions[0].amount,
                     50LL * MICRO_PER_TOKEN);

    free(stored);
    block_free(b);
}

/*
 * After a payment is committed, the storage HEAD must point to the new block.
 * This confirms that the payment is permanently recorded as the chain tip.
 */
static void test_chain_tip_advances_after_payment(void **state)
{
    Chain *c = *state;

    Transaction tx = make_tx("carol", "dave", 10, 1);
    Block *b = commit_block(c, &tx, 1);
    assert_non_null(b);

    unsigned char genesis_hash[HASH_SIZE];
    memcpy(genesis_hash, c->head->hash, HASH_SIZE);

    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

    /* In-memory tip must be the new block. */
    assert_int_equal((int)c->head->index, 1);
    assert_memory_equal(c->head->hash, b->hash, HASH_SIZE);

    /* On-disk HEAD must also point to the new block. */
    char disk_head[HASH_SIZE];
    assert_int_equal(storage_head(disk_head, sizeof(disk_head)), EXIT_SUCCESS);
    assert_string_equal(disk_head, (char *)b->hash);

    block_free(b);
}

/* ── payment/batch ────────────────────────────────────────────────────── */

/*
 * Use case: Three payments batched into one block.
 *
 *   alice  → bob    50 tokens
 *   carol  → dave   30 tokens
 *   eve    → frank  20 tokens
 *
 * All three transactions must be retrievable on-chain in order.
 * This mirrors a user running cmd_send three times before cmd_commit.
 */
static void test_three_payments_in_one_block(void **state)
{
    Chain *c = *state;

    Transaction txns[3] = {
        make_tx("alice", "bob",   50, 1),
        make_tx("carol", "dave",  30, 1),
        make_tx("eve",   "frank", 20, 1),
    };

    Block *b = commit_block(c, txns, 3);
    assert_non_null(b);

    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);
    assert_int_equal((int)c->head->index, 1);

    Block *stored = storage_read((char *)b->hash);
    assert_non_null(stored);
    assert_int_equal((int)stored->transaction_count, 3);

    /* Verify each payment is intact. */
    assert_string_equal(stored->transactions[0].sender,    "alice");
    assert_string_equal(stored->transactions[0].recipient, "bob");
    assert_int_equal((long long)stored->transactions[0].amount,
                     50LL * MICRO_PER_TOKEN);

    assert_string_equal(stored->transactions[1].sender,    "carol");
    assert_string_equal(stored->transactions[1].recipient, "dave");
    assert_int_equal((long long)stored->transactions[1].amount,
                     30LL * MICRO_PER_TOKEN);

    assert_string_equal(stored->transactions[2].sender,    "eve");
    assert_string_equal(stored->transactions[2].recipient, "frank");
    assert_int_equal((long long)stored->transactions[2].amount,
                     20LL * MICRO_PER_TOKEN);

    free(stored);
    block_free(b);
}

/* ── payment/multi_block ──────────────────────────────────────────────── */

/*
 * Use case: Two separate payment rounds — each becomes its own block.
 *
 *   Block 1: alice → bob    50 tokens
 *   Block 2: carol → dave   25 tokens
 *
 * Block 2 must correctly extend Block 1 (previous_hash linkage).
 * Both payments must be independently retrievable from storage.
 */
static void test_two_payment_blocks_in_sequence(void **state)
{
    Chain *c = *state;

    /* First payment: alice → bob */
    Transaction tx1 = make_tx("alice", "bob", 50, 1);
    Block *b1 = commit_block(c, &tx1, 1);
    assert_non_null(b1);
    assert_int_equal(chain_add(c, b1), EXIT_SUCCESS);
    assert_int_equal((int)c->head->index, 1);

    /* Second payment: carol → dave (extends b1) */
    Transaction tx2 = make_tx("carol", "dave", 25, 1);
    Block *b2 = commit_block(c, &tx2, 1); /* commit_block reads c->head which is now b1 */
    assert_non_null(b2);

    /* b2's previous_hash must equal b1's hash. */
    assert_memory_equal(b2->previous_hash, b1->hash, HASH_SIZE);

    assert_int_equal(chain_add(c, b2), EXIT_SUCCESS);
    assert_int_equal((int)c->head->index, 2);

    /* Both blocks must be independently readable from storage. */
    Block *s1 = storage_read((char *)b1->hash);
    Block *s2 = storage_read((char *)b2->hash);
    assert_non_null(s1);
    assert_non_null(s2);

    assert_string_equal(s1->transactions[0].sender, "alice");
    assert_string_equal(s2->transactions[0].sender, "carol");

    free(s1);
    free(s2);
    block_free(b2);
    block_free(b1);
}

/* ── payment/invalid ──────────────────────────────────────────────────── */

/*
 * A transaction with amount = 0 must be rejected by chain_validate.
 * The rejection happens in the transaction-amount check, before consensus.
 * Use CONSENSUS_POS (no mining needed) to isolate the amount check.
 */
static void test_zero_amount_transfer_rejected(void **state)
{
    Chain *c = *state;

    Block *b = block_create(c->head->index + 1, c->head->hash);
    assert_non_null(b);

    b->timestamp              = 1700000001;
    b->consensus              = CONSENSUS_POS; /* skip mining — rejected earlier */
    b->transactions[0].amount = 0;             /* invalid */
    b->transaction_count      = 1;
    strncpy(b->transactions[0].sender,    "alice", sizeof(b->transactions[0].sender)    - 1);
    strncpy(b->transactions[0].recipient, "bob",   sizeof(b->transactions[0].recipient) - 1);

    compute_merkle_root(b, (char *)b->merkle_root);
    assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);

    assert_int_equal(chain_add(c, b), EXIT_FAILURE);

    /* Chain tip must not have changed. */
    assert_int_equal((int)c->head->index, 0);

    block_free(b);
}

/*
 * Tampering with the block's merkle_root field after mining must cause
 * chain_add to reject it.
 *
 * The stored hash was computed over the original merkle_root.
 * block_verify_hash re-hashes all header fields including the now-modified
 * merkle_root and detects the mismatch.
 *
 * NOTE: chain_validate checks block_verify_hash (header integrity), not
 * transaction-level merkle re-verification. The merkle_root field is the
 * commitment to the transactions; tampering it breaks the commitment.
 */
static void test_tampered_merkle_root_rejected(void **state)
{
    Chain *c = *state;

    Transaction tx = make_tx("alice", "bob", 50, 1);
    Block *b = commit_block(c, &tx, 1);
    assert_non_null(b);

    /* Tamper the merkle_root header field — hash was computed with the original */
    b->merkle_root[0] ^= 0xFF;

    assert_int_equal(chain_add(c, b), EXIT_FAILURE);
    assert_int_equal((int)c->head->index, 0);

    block_free(b);
}

/* ── payment/propose ──────────────────────────────────────────────────── */

/*
 * Propose after a committed block with no peers file.
 * chain_propose must return EXIT_SUCCESS — no peers is not an error
 * (nothing to broadcast is fine in a P2P design, ADR-014).
 */
static void test_propose_after_commit_no_peers(void **state)
{
    Chain *c = *state;

    Transaction tx = make_tx("alice", "bob", 10, 1);
    Block *b = commit_block(c, &tx, 1);
    assert_non_null(b);
    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

    /* No .chain/peers file — nothing to broadcast, EXIT_SUCCESS by design. */
    assert_int_equal(chain_propose(c, b), EXIT_SUCCESS);

    block_free(b);
}

/*
 * Propose with an unreachable peer.
 * chain_propose must return EXIT_SUCCESS — peer failure is silenced
 * (partial broadcast is acceptable in a P2P network, ADR-014).
 */
static void test_propose_with_unreachable_peer(void **state)
{
    Chain *c = *state;

    Transaction tx = make_tx("carol", "dave", 20, 2);
    Block *b = commit_block(c, &tx, 1);
    assert_non_null(b);
    assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

    /* Register an unreachable peer — connect will get ECONNREFUSED immediately. */
    FILE *pf = fopen(".chain/peers", "w");
    assert_non_null(pf);
    fprintf(pf, "127.0.0.1:9999\n");
    fclose(pf);

    /* Broadcast failure is silenced — EXIT_SUCCESS regardless of peer reachability. */
    assert_int_equal(chain_propose(c, b), EXIT_SUCCESS);

    block_free(b);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    log_set_stream(stderr);

    const struct CMUnitTest send_tests[] = {
        cmocka_unit_test_setup_teardown(test_alice_sends_50_to_bob,           setup, teardown),
        cmocka_unit_test_setup_teardown(test_chain_tip_advances_after_payment, setup, teardown),
    };

    const struct CMUnitTest batch_tests[] = {
        cmocka_unit_test_setup_teardown(test_three_payments_in_one_block, setup, teardown),
    };

    const struct CMUnitTest multiblock_tests[] = {
        cmocka_unit_test_setup_teardown(test_two_payment_blocks_in_sequence, setup, teardown),
    };

    const struct CMUnitTest invalid_tests[] = {
        cmocka_unit_test_setup_teardown(test_zero_amount_transfer_rejected, setup, teardown),
        cmocka_unit_test_setup_teardown(test_tampered_merkle_root_rejected, setup, teardown),
    };

    const struct CMUnitTest propose_tests[] = {
        cmocka_unit_test_setup_teardown(test_propose_after_commit_no_peers,    setup, teardown),
        cmocka_unit_test_setup_teardown(test_propose_with_unreachable_peer,    setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("payment/send",        send_tests,       NULL, NULL);
    failures += cmocka_run_group_tests_name("payment/batch",       batch_tests,      NULL, NULL);
    failures += cmocka_run_group_tests_name("payment/multi_block", multiblock_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("payment/invalid",     invalid_tests,    NULL, NULL);
    failures += cmocka_run_group_tests_name("payment/propose",     propose_tests,    NULL, NULL);
    return failures;
}
