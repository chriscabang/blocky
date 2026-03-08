/**
 * @file test_validator.c
 * @brief Unit tests for the validator registry module.
 *
 * Groups:
 *   registry/load     — cold-start and reload-from-disk behaviour
 *   registry/register — upsert semantics, NULL guards, persistence
 *   registry/lookup   — exact-match lookup and miss cases
 *   registry/stake    — minimum-stake eligibility checks
 *   registry/count    — entry count helpers
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

#include "validator.h"
#include "log.h"

/* ── helpers ──────────────────────────────────────────────────────────── */

/*
 * Build a Validator with a known test pattern in the public key.
 * Tests do not exercise OQS key generation — the registry stores and
 * retrieves raw bytes; key validity is the concern of verify_transaction.
 */
static Validator make_validator(const char *id, uint64_t stake)
{
  Validator v;
  memset(&v, 0, sizeof(v));
  strncpy(v.id, id, VALIDATOR_ID_SIZE - 1);
  memset(v.public_key, 0xAB, sizeof(v.public_key)); /* recognisable test pattern */
  v.stake = stake;
  return v;
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup_empty(void **state)
{
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* Load a fresh registry; store pointer in *state. */
static int setup_loaded(void **state)
{
  system("rm -rf .chain");
  ValidatorRegistry *reg = validator_registry_load();
  assert_non_null(reg);
  *state = reg;
  return 0;
}

static int teardown(void **state)
{
  if (*state) {
    validator_registry_free(*state);
    *state = NULL;
  }
  system("rm -rf .chain");
  return 0;
}

static int teardown_empty(void **state)
{
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* ── registry/load ────────────────────────────────────────────────────── */

/* No .chain/validators directory — must return empty registry, not NULL. */
static void test_load_no_dir(void **state)
{
  (void)state;
  ValidatorRegistry *reg = validator_registry_load();
  assert_non_null(reg);
  assert_int_equal(validator_count(reg), 0);
  validator_registry_free(reg);
}

/* Register a validator, free the registry, reload — entry must survive. */
static void test_load_existing(void **state)
{
  (void)state;
  ValidatorRegistry *reg = validator_registry_load();
  assert_non_null(reg);

  Validator v = make_validator("alice", 5 * MICRO_PER_TOKEN);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  validator_registry_free(reg);

  ValidatorRegistry *reg2 = validator_registry_load();
  assert_non_null(reg2);
  assert_int_equal(validator_count(reg2), 1);

  const Validator *found = validator_lookup(reg2, "alice");
  assert_non_null(found);
  assert_string_equal(found->id, "alice");
  assert_int_equal((int)found->stake, (int)(5 * MICRO_PER_TOKEN));

  validator_registry_free(reg2);
}

/* ── registry/register ────────────────────────────────────────────────── */

static void test_register_null_reg(void **state)
{
  (void)state;
  Validator v = make_validator("alice", VALIDATOR_MIN_STAKE);
  assert_int_equal(validator_register(NULL, &v), EXIT_FAILURE);
}

static void test_register_null_validator(void **state)
{
  ValidatorRegistry *reg = *state;
  assert_int_equal(validator_register(reg, NULL), EXIT_FAILURE);
}

static void test_register_empty_id(void **state)
{
  ValidatorRegistry *reg = *state;
  Validator v = make_validator("", VALIDATOR_MIN_STAKE);
  assert_int_equal(validator_register(reg, &v), EXIT_FAILURE);
  assert_int_equal(validator_count(reg), 0);
}

static void test_register_valid(void **state)
{
  ValidatorRegistry *reg = *state;
  Validator v = make_validator("alice", VALIDATOR_MIN_STAKE);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  assert_int_equal(validator_count(reg), 1);
}

/* Entry must survive a free + reload cycle. */
static void test_register_persists(void **state)
{
  (void)state;
  ValidatorRegistry *reg = validator_registry_load();
  assert_non_null(reg);

  Validator v = make_validator("bob", 2 * MICRO_PER_TOKEN);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  validator_registry_free(reg);

  ValidatorRegistry *reg2 = validator_registry_load();
  assert_non_null(reg2);
  assert_non_null(validator_lookup(reg2, "bob"));
  validator_registry_free(reg2);
}

/* Re-registering the same ID must update the record, not append. */
static void test_register_duplicate_updates(void **state)
{
  ValidatorRegistry *reg = *state;

  Validator v1 = make_validator("carol", VALIDATOR_MIN_STAKE);
  assert_int_equal(validator_register(reg, &v1), EXIT_SUCCESS);
  assert_int_equal(validator_count(reg), 1);

  Validator v2 = make_validator("carol", 99 * MICRO_PER_TOKEN);
  assert_int_equal(validator_register(reg, &v2), EXIT_SUCCESS);
  assert_int_equal(validator_count(reg), 1); /* still 1 — not a duplicate insert */

  const Validator *found = validator_lookup(reg, "carol");
  assert_non_null(found);
  assert_int_equal((int)found->stake, (int)(99 * MICRO_PER_TOKEN));
}

/* ── registry/lookup ──────────────────────────────────────────────────── */

static void test_lookup_null_reg(void **state)
{
  (void)state;
  assert_null(validator_lookup(NULL, "alice"));
}

static void test_lookup_null_id(void **state)
{
  ValidatorRegistry *reg = *state;
  assert_null(validator_lookup(reg, NULL));
}

static void test_lookup_found(void **state)
{
  ValidatorRegistry *reg = *state;
  Validator v = make_validator("dave", 3 * MICRO_PER_TOKEN);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);

  const Validator *found = validator_lookup(reg, "dave");
  assert_non_null(found);
  assert_string_equal(found->id, "dave");
  assert_int_equal((int)found->stake, (int)(3 * MICRO_PER_TOKEN));
  /* Public key bytes must match the test pattern */
  assert_int_equal(found->public_key[0], 0xAB);
}

static void test_lookup_not_found(void **state)
{
  ValidatorRegistry *reg = *state;
  assert_null(validator_lookup(reg, "nobody"));
}

/* ── registry/stake ───────────────────────────────────────────────────── */

static void test_stake_null_reg(void **state)
{
  (void)state;
  assert_int_equal(validator_check_stake(NULL, "alice"), EXIT_FAILURE);
}

static void test_stake_not_found(void **state)
{
  ValidatorRegistry *reg = *state;
  assert_int_equal(validator_check_stake(reg, "ghost"), EXIT_FAILURE);
}

static void test_stake_insufficient(void **state)
{
  ValidatorRegistry *reg = *state;
  Validator v = make_validator("poor", VALIDATOR_MIN_STAKE - 1);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  assert_int_equal(validator_check_stake(reg, "poor"), EXIT_FAILURE);
}

static void test_stake_at_minimum(void **state)
{
  ValidatorRegistry *reg = *state;
  Validator v = make_validator("exact", VALIDATOR_MIN_STAKE);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  assert_int_equal(validator_check_stake(reg, "exact"), EXIT_SUCCESS);
}

static void test_stake_above_minimum(void **state)
{
  ValidatorRegistry *reg = *state;
  Validator v = make_validator("rich", 100 * MICRO_PER_TOKEN);
  assert_int_equal(validator_register(reg, &v), EXIT_SUCCESS);
  assert_int_equal(validator_check_stake(reg, "rich"), EXIT_SUCCESS);
}

/* ── registry/count ───────────────────────────────────────────────────── */

static void test_count_null(void **state)
{
  (void)state;
  assert_int_equal(validator_count(NULL), 0);
}

static void test_count_after_register(void **state)
{
  ValidatorRegistry *reg = *state;
  assert_int_equal(validator_count(reg), 0);

  Validator a = make_validator("v1", VALIDATOR_MIN_STAKE);
  Validator b = make_validator("v2", VALIDATOR_MIN_STAKE);
  Validator c = make_validator("v3", VALIDATOR_MIN_STAKE);

  validator_register(reg, &a);
  assert_int_equal(validator_count(reg), 1);
  validator_register(reg, &b);
  assert_int_equal(validator_count(reg), 2);
  validator_register(reg, &c);
  assert_int_equal(validator_count(reg), 3);
}

/* ── registry/total_stake ─────────────────────────────────────────────── */

static void test_total_stake_null(void **state)
{
  (void)state;
  assert_int_equal((int)validator_total_stake(NULL), 0);
}

static void test_total_stake_sum(void **state)
{
  ValidatorRegistry *reg = *state;
  assert_int_equal((uint64_t)validator_total_stake(reg), 0);

  Validator a = make_validator("s1", 3 * MICRO_PER_TOKEN);
  Validator b = make_validator("s2", 7 * MICRO_PER_TOKEN);
  validator_register(reg, &a);
  validator_register(reg, &b);

  assert_int_equal((uint64_t)validator_total_stake(reg), 10 * MICRO_PER_TOKEN);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
  log_set_stream(stderr);

  const struct CMUnitTest load_tests[] = {
    cmocka_unit_test_setup_teardown(test_load_no_dir,   setup_empty, teardown_empty),
    cmocka_unit_test_setup_teardown(test_load_existing, setup_empty, teardown_empty),
  };

  const struct CMUnitTest register_tests[] = {
    cmocka_unit_test_setup_teardown(test_register_null_reg,           setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_register_null_validator,     setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_register_empty_id,           setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_register_valid,              setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_register_persists,           setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_register_duplicate_updates,  setup_loaded, teardown),
  };

  const struct CMUnitTest lookup_tests[] = {
    cmocka_unit_test_setup_teardown(test_lookup_null_reg,  setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_lookup_null_id,   setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_lookup_found,     setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_lookup_not_found, setup_loaded, teardown),
  };

  const struct CMUnitTest stake_tests[] = {
    cmocka_unit_test_setup_teardown(test_stake_null_reg,     setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_stake_not_found,    setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_stake_insufficient, setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_stake_at_minimum,   setup_loaded, teardown),
    cmocka_unit_test_setup_teardown(test_stake_above_minimum,setup_loaded, teardown),
  };

  const struct CMUnitTest count_tests[] = {
    cmocka_unit_test_setup_teardown(test_count_null,           setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_count_after_register, setup_loaded, teardown),
  };

  const struct CMUnitTest total_stake_tests[] = {
    cmocka_unit_test_setup_teardown(test_total_stake_null, setup_empty,  teardown_empty),
    cmocka_unit_test_setup_teardown(test_total_stake_sum,  setup_loaded, teardown),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("registry/load",         load_tests,         NULL, NULL);
  failures += cmocka_run_group_tests_name("registry/register",     register_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("registry/lookup",       lookup_tests,       NULL, NULL);
  failures += cmocka_run_group_tests_name("registry/stake",        stake_tests,        NULL, NULL);
  failures += cmocka_run_group_tests_name("registry/count",        count_tests,        NULL, NULL);
  failures += cmocka_run_group_tests_name("registry/total_stake",  total_stake_tests,  NULL, NULL);
  return failures;
}
