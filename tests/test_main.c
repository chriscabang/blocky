/**
 * @file test_main.c
 * @brief CLI integration tests for zuno.
 *
 * Each test spawns ./build/zuno-debug as a child process via popen(),
 * captures stdout, and asserts on output content and exit code.
 * stderr is suppressed so log_* noise does not pollute test output.
 *
 * Tests are grouped to mirror the command set:
 *   args · meta · init · status · send · mine · log · inspect
 *
 * Flow for send/mine tests:
 *   build/utils/key_gen <name>   (one-time key setup via utility)
 *   zuno send --from <name>      (sign + queue in mempool)
 *   zuno mine                    (verify sigs + PoW mine block)
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
#include <sys/wait.h>

#define BLOCKY   "./build/zuno-debug"
#define KEY_GEN  "./build/utils/key_gen"

/* Generate a keypair via the key_gen utility; used as test setup. */
static void keygen(const char *id)
{
  char cmd[256];
  snprintf(cmd, sizeof cmd, KEY_GEN " %s 2>/dev/null", id);
  system(cmd);
}

/* ── helpers ──────────────────────────────────────────────────────────── */

/**
 * Run zuno with the given argument string, capture stdout into buf
 * (stderr is suppressed), and return the exit code.
 */
static int run(const char *args, char *buf, size_t bufsz)
{
  char cmd[1024];
  snprintf(cmd, sizeof(cmd), "%s %s 2>/dev/null", BLOCKY, args);

  FILE *f = popen(cmd, "r");
  if (!f) return -1;

  if (buf && bufsz > 0) {
    size_t n = fread(buf, 1, bufsz - 1, f);
    buf[n] = '\0';
  }

  /*
   * Always drain any remaining pipe output before pclose().
   * Without this, a slow child (e.g. one doing OQS keygen) that hasn't
   * written yet when pclose() closes the read end will receive SIGPIPE,
   * causing pclose() to return 141 (128 + SIGPIPE=13) instead of the
   * real exit code.
   */
  { char drain[256]; while (fread(drain, 1, sizeof drain, f) > 0) {} }

  int status = pclose(f);
  return WEXITSTATUS(status);
}

/**
 * Parse the hash from "Mined block #N (<hash>)" output.
 * Writes an empty string if the pattern is not found.
 */
static void extract_hash(const char *s, char *hash, size_t sz)
{
  hash[0] = '\0';
  const char *p = strchr(s, '(');
  if (!p) return;
  p++;
  const char *end = strchr(p, ')');
  if (!end) return;
  size_t len = (size_t)(end - p);
  if (len >= sz) len = sz - 1;
  memcpy(hash, p, len);
  hash[len] = '\0';
}

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state)
{
  (void)state;
  system("rm -rf .chain");
  return 0;
}

static int teardown(void **state)
{
  (void)state;
  system("rm -rf .chain");
  return 0;
}

/* ── args: dispatch ───────────────────────────────────────────────────── */

static void test_no_args(void **state)
{
  (void)state;
  assert_int_equal(run("", NULL, 0), 1);
}

static void test_unknown_command(void **state)
{
  (void)state;
  assert_int_equal(run("foobar", NULL, 0), 1);
}

/* ── meta: version / help ─────────────────────────────────────────────── */

static void test_version(void **state)
{
  (void)state;
  char out[64];
  assert_int_equal(run("version", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "zuno"));
}

static void test_help(void **state)
{
  (void)state;
  char out[1024];
  assert_int_equal(run("help", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "Usage:"));
  assert_non_null(strstr(out, "send"));
  assert_non_null(strstr(out, "mine"));
}

/* ── propose ──────────────────────────────────────────────────────────── */

/* No .chain/peers file — "no peers configured" is reported on stdout. */
static void test_propose_no_peers(void **state)
{
  (void)state;
  run("init", NULL, 0);
  char out[256];
  assert_int_equal(run("propose", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "no peers configured"));
}

/* Empty peers file — also counts as "no peers configured". */
static void test_propose_empty_peers_file(void **state)
{
  (void)state;
  run("init", NULL, 0);
  system("touch .chain/peers");
  char out[256];
  assert_int_equal(run("propose", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "no peers configured"));
}

/* Unreachable peer: propose returns 0 (P2P silent-failure by design). */
static void test_propose_unreachable_peer(void **state)
{
  (void)state;
  run("init", NULL, 0);
  system("printf '127.0.0.1:9999\\n' > .chain/peers");
  char out[256];
  assert_int_equal(run("propose", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "Proposed block #0"));
}

/* Malformed peer entries are skipped; propose still succeeds. */
static void test_propose_malformed_entries_skipped(void **state)
{
  (void)state;
  run("init", NULL, 0);
  system("printf 'not-a-peer\\nbadformat\\n' > .chain/peers");
  char out[256];
  assert_int_equal(run("propose", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "Proposed block #0"));
}

/* ── init ─────────────────────────────────────────────────────────────── */

static void test_init_creates_genesis(void **state)
{
  (void)state;
  char out[256];
  assert_int_equal(run("init", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "Initialised chain"));
  assert_non_null(strstr(out, "block #0"));
}

static void test_init_idempotent(void **state)
{
  (void)state;
  run("init", NULL, 0);
  char out[256];
  assert_int_equal(run("init", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "block #0"));
}

/* ── status ───────────────────────────────────────────────────────────── */

static void test_status_empty_mempool(void **state)
{
  (void)state;
  run("init", NULL, 0);
  char out[256];
  assert_int_equal(run("status", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "block #0"));
  assert_non_null(strstr(out, "empty"));
}

static void test_status_shows_pending(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  char out[256];
  assert_int_equal(run("status", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "1 transaction(s) pending"));
}

/* ── send ─────────────────────────────────────────────────────────────── */

static void test_send_basic(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  char out[256];
  assert_int_equal(
    run("send --from alice --to bob --amount 10", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "Sent:"));
  assert_non_null(strstr(out, "alice"));
  assert_non_null(strstr(out, "bob"));
  assert_non_null(strstr(out, "mempool"));
}

static void test_send_missing_args(void **state)
{
  (void)state;
  run("init", NULL, 0);
  assert_int_equal(run("send --from alice", NULL, 0), 1);
}

static void test_send_negative_amount(void **state)
{
  (void)state;
  run("init", NULL, 0);
  assert_int_equal(run("send --from a --to b --amount -5", NULL, 0), 1);
}

static void test_send_zero_amount(void **state)
{
  (void)state;
  run("init", NULL, 0);
  assert_int_equal(run("send --from a --to b --amount 0", NULL, 0), 1);
}

/* send without a keypair must fail with a helpful message. */
static void test_send_no_key(void **state)
{
  (void)state;
  run("init", NULL, 0);
  /* alice has no keypair — must fail */
  assert_int_equal(
    run("send --from alice --to bob --amount 10", NULL, 0), 1);
}

/* ── mine ─────────────────────────────────────────────────────────────── */

static void test_mine_empty_mempool(void **state)
{
  (void)state;
  run("init", NULL, 0);
  char out[128];
  assert_int_equal(run("mine", out, sizeof(out)), 1);
  assert_non_null(strstr(out, "nothing to mine"));
}

static void test_mine_basic(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  char out[256];
  assert_int_equal(run("mine", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "Mined block #1"));
}

static void test_mine_clears_mempool(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  run("mine", NULL, 0);
  char out[256];
  run("status", out, sizeof(out));
  assert_non_null(strstr(out, "empty"));
}

/* ── log ──────────────────────────────────────────────────────────────── */

static void test_log_shows_all_blocks(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  run("mine", NULL, 0);
  char out[512];
  assert_int_equal(run("log", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "block #1"));
  assert_non_null(strstr(out, "block #0"));
}

static void test_log_limit_respected(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  run("mine", NULL, 0);
  char out[512];
  assert_int_equal(run("log --limit 1", out, sizeof(out)), 0);
  assert_non_null(strstr(out, "block #1"));
  assert_null(strstr(out, "block #0"));
}

/* ── inspect: show / cat / verify ────────────────────────────────────── */

static void test_show_valid_block(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  char mine_out[256];
  run("mine", mine_out, sizeof(mine_out));

  char hash[65];
  extract_hash(mine_out, hash, sizeof(hash));
  assert_int_equal((int)strlen(hash), 64);

  char args[128];
  snprintf(args, sizeof(args), "show %s", hash);
  char out[512];
  assert_int_equal(run(args, out, sizeof(out)), 0);
  assert_non_null(strstr(out, "block #1"));
  assert_non_null(strstr(out, "hash:"));
  assert_non_null(strstr(out, "alice"));
}

static void test_show_bad_hash(void **state)
{
  (void)state;
  run("init", NULL, 0);
  assert_int_equal(run("show deadbeef", NULL, 0), 2);
}

static void test_show_missing_arg(void **state)
{
  (void)state;
  run("init", NULL, 0);
  assert_int_equal(run("show", NULL, 0), 1);
}

static void test_cat_valid_block(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  char mine_out[256];
  run("mine", mine_out, sizeof(mine_out));

  char hash[65];
  extract_hash(mine_out, hash, sizeof(hash));
  assert_int_equal((int)strlen(hash), 64);

  char args[128];
  snprintf(args, sizeof(args), "cat %s", hash);
  char out[512];
  assert_int_equal(run(args, out, sizeof(out)), 0);
  assert_non_null(strstr(out, "index:"));
  assert_non_null(strstr(out, "hash:"));
  assert_non_null(strstr(out, "prev:"));
  assert_non_null(strstr(out, "merkle:"));
  assert_non_null(strstr(out, "txns:"));
  assert_non_null(strstr(out, "alice"));
}

static void test_verify_valid_block(void **state)
{
  (void)state;
  run("init", NULL, 0);
  keygen("alice");
  run("send --from alice --to bob --amount 10", NULL, 0);
  char mine_out[256];
  run("mine", mine_out, sizeof(mine_out));

  char hash[65];
  extract_hash(mine_out, hash, sizeof(hash));
  assert_int_equal((int)strlen(hash), 64);

  char args[128];
  snprintf(args, sizeof(args), "verify %s", hash);
  char out[128];
  assert_int_equal(run(args, out, sizeof(out)), 0);
  assert_non_null(strstr(out, "OK"));
}

static void test_verify_bad_hash(void **state)
{
  (void)state;
  run("init", NULL, 0);
  assert_int_equal(run("verify deadbeef", NULL, 0), 2);
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
  const struct CMUnitTest args_tests[] = {
    cmocka_unit_test_setup_teardown(test_no_args,         setup, teardown),
    cmocka_unit_test_setup_teardown(test_unknown_command, setup, teardown),
  };

  const struct CMUnitTest meta_tests[] = {
    cmocka_unit_test_setup_teardown(test_version, setup, teardown),
    cmocka_unit_test_setup_teardown(test_help,    setup, teardown),
  };

  const struct CMUnitTest propose_tests[] = {
    cmocka_unit_test_setup_teardown(test_propose_no_peers,              setup, teardown),
    cmocka_unit_test_setup_teardown(test_propose_empty_peers_file,      setup, teardown),
    cmocka_unit_test_setup_teardown(test_propose_unreachable_peer,      setup, teardown),
    cmocka_unit_test_setup_teardown(test_propose_malformed_entries_skipped, setup, teardown),
  };

  const struct CMUnitTest init_tests[] = {
    cmocka_unit_test_setup_teardown(test_init_creates_genesis, setup, teardown),
    cmocka_unit_test_setup_teardown(test_init_idempotent,      setup, teardown),
  };

  const struct CMUnitTest status_tests[] = {
    cmocka_unit_test_setup_teardown(test_status_empty_mempool, setup, teardown),
    cmocka_unit_test_setup_teardown(test_status_shows_pending, setup, teardown),
  };

  const struct CMUnitTest send_tests[] = {
    cmocka_unit_test_setup_teardown(test_send_basic,           setup, teardown),
    cmocka_unit_test_setup_teardown(test_send_missing_args,    setup, teardown),
    cmocka_unit_test_setup_teardown(test_send_negative_amount, setup, teardown),
    cmocka_unit_test_setup_teardown(test_send_zero_amount,     setup, teardown),
    cmocka_unit_test_setup_teardown(test_send_no_key,          setup, teardown),
  };

  const struct CMUnitTest mine_tests[] = {
    cmocka_unit_test_setup_teardown(test_mine_empty_mempool, setup, teardown),
    cmocka_unit_test_setup_teardown(test_mine_basic,         setup, teardown),
    cmocka_unit_test_setup_teardown(test_mine_clears_mempool, setup, teardown),
  };

  const struct CMUnitTest log_tests[] = {
    cmocka_unit_test_setup_teardown(test_log_shows_all_blocks, setup, teardown),
    cmocka_unit_test_setup_teardown(test_log_limit_respected,  setup, teardown),
  };

  const struct CMUnitTest inspect_tests[] = {
    cmocka_unit_test_setup_teardown(test_show_valid_block,   setup, teardown),
    cmocka_unit_test_setup_teardown(test_show_bad_hash,      setup, teardown),
    cmocka_unit_test_setup_teardown(test_show_missing_arg,   setup, teardown),
    cmocka_unit_test_setup_teardown(test_cat_valid_block,    setup, teardown),
    cmocka_unit_test_setup_teardown(test_verify_valid_block, setup, teardown),
    cmocka_unit_test_setup_teardown(test_verify_bad_hash,    setup, teardown),
  };

  int failures = 0;
  failures += cmocka_run_group_tests_name("args",    args_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("meta",    meta_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("propose", propose_tests, NULL, NULL);
  failures += cmocka_run_group_tests_name("init",    init_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("status",  status_tests,  NULL, NULL);
  failures += cmocka_run_group_tests_name("send",    send_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("mine",    mine_tests,    NULL, NULL);
  failures += cmocka_run_group_tests_name("log",     log_tests,     NULL, NULL);
  failures += cmocka_run_group_tests_name("inspect", inspect_tests, NULL, NULL);
  return failures;
}
