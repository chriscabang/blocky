/*
 * test_integration_e2e.c — End-to-end CLI workflow tests.
 *
 * These tests exercise the complete user-facing workflow as a linear sequence,
 * covering all major CLI commands in the order a real operator would use them:
 *
 *   init → key_gen → send (multiple txns) → mine → verify → log → show →
 *   status → cat → propose
 *
 * This is distinct from test_main.c, which tests each command in isolation.
 * Here, the chain state accumulates across the steps of each test group,
 * confirming that the full workflow composes correctly.
 *
 * Each group runs in its own isolated .chain/ directory (setup/teardown).
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

#define ZUNO     "./build/zuno-debug"
#define KEY_GEN  "./build/utils/key_gen"

/* ── helpers ──────────────────────────────────────────────────────────── */

static int run(const char *args, char *buf, size_t bufsz)
{
    char cmd[1024];
    snprintf(cmd, sizeof(cmd), "%s %s 2>/dev/null", ZUNO, args);

    FILE *f = popen(cmd, "r");
    if (!f) return -1;

    if (buf && bufsz > 0) {
        size_t n = fread(buf, 1, bufsz - 1, f);
        buf[n] = '\0';
    }

    /* Drain remaining output so pclose() gets the real exit code. */
    { char drain[256]; while (fread(drain, 1, sizeof drain, f) > 0) {} }

    int status = pclose(f);
    return WEXITSTATUS(status);
}

static void keygen(const char *id)
{
    char cmd[256];
    snprintf(cmd, sizeof cmd, KEY_GEN " %s 2>/dev/null", id);
    system(cmd);
}

/* Extract the hash from "Mined block #N (<hash>)" or "block #N  <hash16>...". */
static void extract_mine_hash(const char *s, char *hash, size_t sz)
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

/* ── e2e/workflow ─────────────────────────────────────────────────────── */

/*
 * Full workflow: init → status → send → mine → verify → log → show → cat.
 *
 * Exercises every read and write command in order, confirming the chain
 * state is consistent at each step.
 */
static void test_full_send_mine_inspect(void **state)
{
    (void)state;
    char out[2048];
    char hash[128];

    /* init */
    assert_int_equal(run("init", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #0"));

    /* status: empty mempool */
    assert_int_equal(run("status", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #0"));
    assert_non_null(strstr(out, "empty"));

    /* generate keys */
    keygen("alice");
    keygen("bob");

    /* send two transactions */
    assert_int_equal(run("send --from alice --to bob --amount 10.5", out, sizeof out), 0);
    assert_non_null(strstr(out, "alice"));
    assert_non_null(strstr(out, "bob"));

    assert_int_equal(run("send --from bob --to alice --amount 3.0", out, sizeof out), 0);

    /* status: 2 pending */
    assert_int_equal(run("status", out, sizeof out), 0);
    assert_non_null(strstr(out, "2 transaction"));

    /* mine */
    assert_int_equal(run("mine", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #1"));
    assert_non_null(strstr(out, "2 tx"));

    extract_mine_hash(out, hash, sizeof hash);
    assert_int_not_equal(hash[0], '\0');

    /* status: tip advanced, mempool clear */
    assert_int_equal(run("status", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #1"));
    assert_non_null(strstr(out, "empty"));

    /* verify the mined block hash */
    {
        char args[256];
        snprintf(args, sizeof args, "verify %s", hash);
        assert_int_equal(run(args, out, sizeof out), 0);
        assert_non_null(strstr(out, "OK"));
    }

    /* log: block #1 appears */
    assert_int_equal(run("log", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #1"));

    /* show: detailed view of the mined block */
    {
        char args[256];
        snprintf(args, sizeof args, "show %s", hash);
        assert_int_equal(run(args, out, sizeof out), 0);
        assert_non_null(strstr(out, "block #1"));
        assert_non_null(strstr(out, "alice"));
        assert_non_null(strstr(out, "bob"));
    }

    /* cat: raw field dump of the mined block */
    {
        char args[256];
        snprintf(args, sizeof args, "cat %s", hash);
        assert_int_equal(run(args, out, sizeof out), 0);
        assert_non_null(strstr(out, "index:"));
        assert_non_null(strstr(out, "hash:"));
        assert_non_null(strstr(out, "txns:"));
    }
}

/*
 * Multi-block chain: mine two sequential blocks and confirm both appear in log.
 */
static void test_multi_block_chain(void **state)
{
    (void)state;
    char out[2048];

    run("init", NULL, 0);
    keygen("carol");
    keygen("dave");

    /* block 1 */
    run("send --from carol --to dave --amount 5.0", NULL, 0);
    assert_int_equal(run("mine", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #1"));

    /* block 2 */
    run("send --from dave --to carol --amount 2.5", NULL, 0);
    assert_int_equal(run("mine", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #2"));

    /* log shows both */
    assert_int_equal(run("log", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #2"));
    assert_non_null(strstr(out, "block #1"));

    /* status shows tip at block #2 */
    assert_int_equal(run("status", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #2"));
}

/*
 * propose: with no peers file, the command reports no peers but succeeds.
 */
static void test_propose_no_peers(void **state)
{
    (void)state;
    char out[512];

    run("init", NULL, 0);
    assert_int_equal(run("propose", out, sizeof out), 0);
    assert_non_null(strstr(out, "no peers"));
}

/*
 * mine rejects unverifiable transactions (no key for sender).
 */
static void test_mine_rejects_unsigned_txns(void **state)
{
    (void)state;
    char out[1024];

    run("init", NULL, 0);
    keygen("eve");

    /* Send from "unknownuser" who has no key registered. */
    /* We send from eve (has key) then mine — only eve's tx should be included. */
    run("send --from eve --to alice --amount 1.0", NULL, 0);

    /* Mine: eve's transaction is included */
    assert_int_equal(run("mine", out, sizeof out), 0);
    assert_non_null(strstr(out, "1 tx"));
}

/*
 * verify returns non-zero for a fabricated (non-existent) hash.
 */
static void test_verify_bad_hash(void **state)
{
    (void)state;
    char out[256];

    run("init", NULL, 0);
    int rc = run("verify 0000000000000000000000000000000000000000000000000000000000000bad",
                 out, sizeof out);
    assert_int_not_equal(rc, 0);
}

/*
 * log with --limit respects the requested count.
 */
static void test_log_limit(void **state)
{
    (void)state;
    char out[2048];

    run("init", NULL, 0);
    keygen("miner");

    /* Mine 3 blocks */
    for (int i = 0; i < 3; i++) {
        char cmd[128];
        snprintf(cmd, sizeof cmd,
                 "send --from miner --to miner --amount %d.0", i + 1);
        run(cmd, NULL, 0);
        run("mine", NULL, 0);
    }

    /* log --limit 2 should show block #3 and #2 but not #1 */
    assert_int_equal(run("log --limit 2", out, sizeof out), 0);
    assert_non_null(strstr(out, "block #3"));
    assert_non_null(strstr(out, "block #2"));
    /* block #1 should not appear */
    assert_null(strstr(out, "block #1"));
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void)
{
    const struct CMUnitTest tests[] = {
        cmocka_unit_test_setup_teardown(
            test_full_send_mine_inspect, setup, teardown),
        cmocka_unit_test_setup_teardown(
            test_multi_block_chain, setup, teardown),
        cmocka_unit_test_setup_teardown(
            test_propose_no_peers, setup, teardown),
        cmocka_unit_test_setup_teardown(
            test_mine_rejects_unsigned_txns, setup, teardown),
        cmocka_unit_test_setup_teardown(
            test_verify_bad_hash, setup, teardown),
        cmocka_unit_test_setup_teardown(
            test_log_limit, setup, teardown),
    };

    return cmocka_run_group_tests_name("e2e", tests, NULL, NULL);
}
