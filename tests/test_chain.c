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
#include "storage.h"
#include "crypto.h"
#include "validator.h"
#include "vrf.h"
#include "sha256.h"
#include "log.h"

#include <oqs/oqs.h>

/* ── module-level test proposer keypair (generated once in main) ──────── */

static uint8_t  g_proposer_pk[MAX_PUBLIC_KEY_LENGTH];
static uint8_t *g_proposer_sk     = NULL;
static size_t   g_proposer_sk_len = 0;

static void init_test_proposer(void) {
  OQS_SIG *sig = OQS_SIG_new(OQS_SIG_alg_dilithium_3);
  assert_non_null(sig);
  g_proposer_sk_len = sig->length_secret_key;
  g_proposer_sk     = malloc(g_proposer_sk_len);
  assert_non_null(g_proposer_sk);
  OQS_SIG_keypair(sig, g_proposer_pk, g_proposer_sk);
  OQS_SIG_free(sig);
}

static void register_test_proposer(void) {
  ValidatorRegistry *reg = validator_registry_load();
  assert_non_null(reg);
  Validator v;
  memset(&v, 0, sizeof v);
  strncpy(v.id, "test-proposer", VALIDATOR_ID_SIZE - 1);
  memcpy(v.public_key, g_proposer_pk, MAX_PUBLIC_KEY_LENGTH);
  v.stake = VALIDATOR_MIN_STAKE;
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  validator_registry_free(reg);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

/*
 * Build a block that correctly extends prev (or genesis if NULL).
 * Uses CONSENSUS_POS so chain_validate passes without mining:
 * PoS only requires hash integrity (block_verify_hash), not a difficulty target.
 * Tests that specifically exercise PoW consensus belong in test_consensus.c.
 */
static Block *make_next(uint32_t index, const unsigned char *prev_hash) {
  Block *b = block_create(index, prev_hash);
  assert_non_null(b);
  b->timestamp = (time_t)(1700000000 + index);
  b->consensus = CONSENSUS_POS;

  /* Build the VRF slot message from index + prev_hash raw bytes. */
  uint8_t prev_raw[SHA256_DIGEST_LEN];
  memset(prev_raw, 0, SHA256_DIGEST_LEN);
  if (prev_hash && strlen((const char *)prev_hash) == SHA256_HEX_LEN)
    sha256_from_hex((const char *)prev_hash, prev_raw, SHA256_DIGEST_LEN);

  uint8_t  slot_msg[VRF_OUTPUT_LEN];
  VRFProof proof;
  vrf_slot_message((uint64_t)index, prev_raw, slot_msg);
  assert_int_equal(vrf_prove("test-proposer", slot_msg,
                             g_proposer_sk, g_proposer_sk_len, &proof),
                   EXIT_SUCCESS);
  assert_int_equal(block_sign(b, "test-proposer",
                              g_proposer_sk, g_proposer_sk_len, &proof),
                   EXIT_SUCCESS);
  return b;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

/* No .chain directory, no state */
static int setup_empty(void **state) {
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* chain_load on an empty dir → creates genesis. state = Chain* */
static int setup_loaded(void **state) {
  system("rm -rf .chain");
  Chain *c = chain_load();
  assert_non_null(c);
  register_test_proposer();
  *state = c;
  return 0;
}

static int teardown(void **state) {
  if (*state) {
    chain_unload(*state);
    *state = NULL;
  }
  system("rm -rf .chain");
  return 0;
}

static int teardown_empty(void **state) {
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* ── chain_load ───────────────────────────────────────────────────────── */

static void test_load_creates_genesis(void **state) {
  (void)state;
  Chain *c = chain_load();
  assert_non_null(c);
  assert_non_null(c->head);
  assert_int_equal(c->head->index, 0);
  assert_int_equal(c->head->previous_hash[0], '0');
  assert_int_equal(c->head->previous_hash[1], '\0');
  assert_null(c->head->next);

  /* Storage HEAD must be set */
  char head[HASH_SIZE];
  assert_int_equal(storage_head(head, sizeof(head)), EXIT_SUCCESS);
  assert_string_equal(head, (char *)c->head->hash);

  chain_unload(c);
}

static void test_load_existing_chain(void **state) {
  /* Insert + checkout a genesis block independently */
  system("rm -rf .chain");
  Block *genesis = make_next(0, NULL);
  assert_int_equal(storage_insert(genesis), EXIT_SUCCESS);
  assert_int_equal(storage_checkout((char *)genesis->hash), EXIT_SUCCESS);

  Chain *c = chain_load();
  assert_non_null(c);
  assert_non_null(c->head);
  assert_string_equal((char *)c->head->hash, (char *)genesis->hash);
  assert_null(c->head->next);

  block_free(genesis);
  *state = c;
}

static void test_load_twice_independent(void **state) {
  (void)state;
  Chain *c1 = chain_load();
  Chain *c2 = chain_load(); /* second load of the same on-disk chain */
  assert_non_null(c1);
  assert_non_null(c2);
  assert_string_equal((char *)c1->head->hash, (char *)c2->head->hash);
  chain_unload(c1);
  chain_unload(c2);
}

/* ── chain_unload ─────────────────────────────────────────────────────── */

static void test_unload_null(void **state) {
  (void)state;
  chain_unload(NULL); /* must not crash */
}

/* ── chain_validate ───────────────────────────────────────────────────── */

static void test_validate_null_chain(void **state) {
  (void)state;
  Block *b = make_next(1, NULL);
  assert_int_equal(chain_validate(NULL, b), EXIT_FAILURE);
  block_free(b);
}

static void test_validate_null_block(void **state) {
  Chain *c = *state;
  assert_int_equal(chain_validate(c, NULL), EXIT_FAILURE);
}

static void test_validate_valid_next_block(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, c->head->hash);
  assert_int_equal(chain_validate(c, b), EXIT_SUCCESS);
  block_free(b);
}

static void test_validate_wrong_previous_hash(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, NULL); /* sentinel "0" instead of real hash */
  /* Recompute hash with the wrong prev so verify_hash still passes */
  b->timestamp = 1700000001;
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
  /* Validate must fail: previous_hash doesn't match head */
  assert_int_equal(chain_validate(c, b), EXIT_FAILURE);
  block_free(b);
}

static void test_validate_tampered_hash(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, c->head->hash);
  b->hash[0] ^= 0xFF; /* corrupt the stored hash */
  assert_int_equal(chain_validate(c, b), EXIT_FAILURE);
  block_free(b);
}

/* ── chain_add ────────────────────────────────────────────────────────── */

static void test_add_null_args(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, c->head->hash);
  assert_int_equal(chain_add(NULL, b), EXIT_FAILURE);
  assert_int_equal(chain_add(c, NULL), EXIT_FAILURE);
  block_free(b);
}

static void test_add_advances_head(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, c->head->hash);

  assert_int_equal(chain_add(c, b), EXIT_SUCCESS);
  assert_int_equal(c->head->index, 1);
  assert_string_equal((char *)c->head->hash, (char *)b->hash);
  assert_null(c->head->next);

  block_free(b);
}

static void test_add_updates_storage_head(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, c->head->hash);
  assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

  char disk_head[HASH_SIZE];
  assert_int_equal(storage_head(disk_head, sizeof(disk_head)), EXIT_SUCCESS);
  assert_string_equal(disk_head, (char *)b->hash);

  block_free(b);
}

/* Key fix: Block.next must be NULL when read back from disk */
static void test_add_next_null_on_disk(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, c->head->hash);
  assert_int_equal(chain_add(c, b), EXIT_SUCCESS);

  Block *loaded = storage_read((char *)b->hash);
  assert_non_null(loaded);
  assert_null(loaded->next);
  free(loaded);

  block_free(b);
}

static void test_add_invalid_block_rejected(void **state) {
  Chain *c = *state;
  Block *b = make_next(1, NULL); /* wrong previous_hash */
  b->timestamp = 1700000001;
  assert_int_equal(block_compute_hash(b), EXIT_SUCCESS);
  assert_int_equal(chain_add(c, b), EXIT_FAILURE);
  /* head must not have changed */
  assert_int_equal(c->head->index, 0);
  block_free(b);
}

static void test_add_pool_exhaustion(void **state) {
  Chain *c = *state;

  /* Mark all pool slots used except the current head's slot */
  for (int i = 0; i < CHAIN_POOL_SIZE; i++)
    c->pool_used[i] = 1;

  /* Build a valid next block */
  Block *b = make_next(1, c->head->hash);

  /* chain_add must fail: no free pool slot */
  assert_int_equal(chain_add(c, b), EXIT_FAILURE);

  block_free(b);
}

/* ── chain_fork_choice ────────────────────────────────────────────────── */

/* NULL chain must return EXIT_FAILURE */
static void test_fork_choice_null(void **state) {
  (void)state;
  assert_int_equal(chain_fork_choice(NULL), EXIT_FAILURE);
}

/* Only genesis in store — no fork, head hash unchanged */
static void test_fork_choice_genesis_only(void **state) {
  Chain *c = *state;
  unsigned char orig[HASH_SIZE];
  memcpy(orig, c->head->hash, HASH_SIZE);
  assert_int_equal(chain_fork_choice(c), EXIT_SUCCESS);
  assert_memory_equal(c->head->hash, orig, HASH_SIZE);
}

/* Linear G→B1→B2 — no fork, head stays at B2 */
static void test_fork_choice_linear_no_reorg(void **state) {
  Chain *c = *state;
  Block *b1 = make_next(1, c->head->hash);
  Block *b2 = make_next(2, b1->hash);
  assert_int_equal(chain_add(c, b1), EXIT_SUCCESS);
  assert_int_equal(chain_add(c, b2), EXIT_SUCCESS);

  unsigned char tip[HASH_SIZE];
  memcpy(tip, c->head->hash, HASH_SIZE);
  assert_int_equal(chain_fork_choice(c), EXIT_SUCCESS);
  assert_memory_equal(c->head->hash, tip, HASH_SIZE);

  block_free(b1);
  block_free(b2);
}

/*
 * Fork: G → B1           (subtree weight 1)
 *       G → B2 → B3      (subtree weight 2)
 * GHOST must select B3 as the canonical tip.
 */
static void test_fork_choice_heavy_branch_wins(void **state) {
  Chain *c = *state;

  Block *b1 = make_next(1, c->head->hash);
  assert_int_equal(storage_insert(b1), EXIT_SUCCESS);

  /* B2 shares genesis as parent but has a different timestamp → different hash */
  Block *b2 = make_next(1, c->head->hash);
  b2->timestamp += 1;
  assert_int_equal(block_compute_hash(b2), EXIT_SUCCESS);
  assert_int_equal(storage_insert(b2), EXIT_SUCCESS);

  Block *b3 = make_next(2, b2->hash);
  assert_int_equal(storage_insert(b3), EXIT_SUCCESS);

  assert_int_equal(chain_fork_choice(c), EXIT_SUCCESS);
  assert_string_equal((char *)c->head->hash, (char *)b3->hash);

  block_free(b1);
  block_free(b2);
  block_free(b3);
}

/*
 * Equal-weight fork: G → B1 (weight 1) and G → B2 (weight 1).
 * Tie-break: lexicographically smaller hash wins (deterministic).
 */
static void test_fork_choice_tiebreak_by_hash(void **state) {
  Chain *c = *state;

  Block *b1 = make_next(1, c->head->hash);
  assert_int_equal(storage_insert(b1), EXIT_SUCCESS);

  Block *b2 = make_next(1, c->head->hash);
  b2->timestamp += 1;
  assert_int_equal(block_compute_hash(b2), EXIT_SUCCESS);
  assert_int_equal(storage_insert(b2), EXIT_SUCCESS);

  assert_int_equal(chain_fork_choice(c), EXIT_SUCCESS);

  const char *expected = (strcmp((char *)b1->hash, (char *)b2->hash) < 0)
                         ? (char *)b1->hash : (char *)b2->hash;
  assert_string_equal((char *)c->head->hash, expected);

  block_free(b1);
  block_free(b2);
}

/* After a reorg, storage HEAD must also point to the new canonical tip */
static void test_fork_choice_updates_storage_head(void **state) {
  Chain *c = *state;

  Block *b1 = make_next(1, c->head->hash);
  assert_int_equal(storage_insert(b1), EXIT_SUCCESS);

  Block *b2 = make_next(1, c->head->hash);
  b2->timestamp += 1;
  assert_int_equal(block_compute_hash(b2), EXIT_SUCCESS);
  assert_int_equal(storage_insert(b2), EXIT_SUCCESS);

  Block *b3 = make_next(2, b2->hash);
  assert_int_equal(storage_insert(b3), EXIT_SUCCESS);

  assert_int_equal(chain_fork_choice(c), EXIT_SUCCESS);

  char disk_head[HASH_SIZE];
  assert_int_equal(storage_head(disk_head, sizeof(disk_head)), EXIT_SUCCESS);
  assert_string_equal(disk_head, (char *)b3->hash);

  block_free(b1);
  block_free(b2);
  block_free(b3);
}

/* ── chain_propose ────────────────────────────────────────────────────── */

/* NULL chain must return EXIT_FAILURE. */
static void test_propose_null_chain(void **state) {
  (void)state;
  Block *b = make_next(1, NULL);
  assert_int_equal(chain_propose(NULL, b), EXIT_FAILURE);
  block_free(b);
}

/* NULL block must return EXIT_FAILURE. */
static void test_propose_null_block(void **state) {
  Chain *c = *state;
  assert_int_equal(chain_propose(c, NULL), EXIT_FAILURE);
}

/*
 * No .chain/peers file: propose must return EXIT_SUCCESS with zero broadcasts.
 * This is the normal initial state — no peers configured yet.
 */
static void test_propose_no_peers(void **state) {
  Chain *c = *state;
  /* Ensure no peers file exists (teardown removes .chain, but be explicit). */
  remove(".chain/peers");
  assert_int_equal(chain_propose(c, c->head), EXIT_SUCCESS);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
  log_set_stream(stderr);
  init_test_proposer();

  const struct CMUnitTest load_tests[] = {
    cmocka_unit_test_setup_teardown(test_load_creates_genesis,   setup_empty, teardown_empty),
    cmocka_unit_test_setup_teardown(test_load_existing_chain,    setup_empty, teardown),
    cmocka_unit_test_setup_teardown(test_load_twice_independent, setup_empty, teardown_empty),
  };

  const struct CMUnitTest unload_tests[] = {
    cmocka_unit_test_setup_teardown(test_unload_null, setup_empty, teardown_empty),
  };

  const struct CMUnitTest validate_tests[] = {
    cmocka_unit_test_setup_teardown(test_validate_null_chain,        setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_validate_null_block,        setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_validate_valid_next_block,  setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_validate_wrong_previous_hash, setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_validate_tampered_hash,     setup_loaded, teardown),
  };

  const struct CMUnitTest add_tests[] = {
    cmocka_unit_test_setup_teardown(test_add_null_args,           setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_add_advances_head,       setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_add_updates_storage_head, setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_add_next_null_on_disk,   setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_add_invalid_block_rejected, setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_add_pool_exhaustion,     setup_loaded, teardown),
  };

  const struct CMUnitTest propose_tests[] = {
    cmocka_unit_test_setup_teardown(test_propose_null_chain, setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_propose_null_block, setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_propose_no_peers,   setup_loaded, teardown),
  };

  const struct CMUnitTest fork_choice_tests[] = {
    cmocka_unit_test_setup_teardown(test_fork_choice_null,                setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_fork_choice_genesis_only,        setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_fork_choice_linear_no_reorg,     setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_fork_choice_heavy_branch_wins,   setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_fork_choice_tiebreak_by_hash,    setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_fork_choice_updates_storage_head, setup_loaded, teardown),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("load",        load_tests,        NULL, NULL);
  failures += cmocka_run_group_tests_name("unload",      unload_tests,      NULL, NULL);
  failures += cmocka_run_group_tests_name("validate",    validate_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("add",         add_tests,         NULL, NULL);
  failures += cmocka_run_group_tests_name("propose",     propose_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("fork_choice", fork_choice_tests, NULL, NULL);
  return failures;
}
