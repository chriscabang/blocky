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
#include <unistd.h>

#include "log.h"

#define assert_string_contains(haystack, needle) \
    assert_true(strstr((haystack), (needle)) != NULL)

#define assert_string_not_contains(haystack, needle) \
    assert_true(strstr((haystack), (needle)) == NULL)

#define TEST_LOG_FILE "test_output.log"

/* ── helpers ──────────────────────────────────────────────────────────── */

/*
 * Open TEST_LOG_FILE for writing, redirect log output there, log one INFO
 * message, close the file, reopen it for reading and return the FILE*.
 * Caller must fclose() the returned handle and unlink TEST_LOG_FILE.
 */
static FILE *capture_log_to_file(void (*emit_fn)(void)) {
    FILE *w = fopen(TEST_LOG_FILE, "w");
    if (!w) fail_msg("cannot open test log file for writing");
    log_set_stream(w);
    emit_fn();
    fclose(w);
    log_set_stream(stderr); /* restore so later tests are not redirected */
    return fopen(TEST_LOG_FILE, "r");
}

static void emit_info(void)  { log_info("Test log message to file"); }
static void emit_error(void) { log_error("Test error message"); }

/* ── setup / teardown ─────────────────────────────────────────────────── */

static int setup(void **state) {
    (void)state;
    /* Reset to defaults before every test. */
    log_set_stream(stderr);
    log_set_level(LOG_DEFAULT_LEVEL);
    return 0;
}

static int teardown(void **state) {
    (void)state;
    log_set_stream(stderr);
    log_set_level(LOG_DEFAULT_LEVEL);
    remove(TEST_LOG_FILE);
    return 0;
}

/* ── log/stream ───────────────────────────────────────────────────────── */

/*
 * Redirect stdout to a pipe, call log_info, verify "INFO" appears.
 * Tests that log_set_stream(stdout) routes output correctly.
 */
static void test_log_to_terminal(void **state) {
    (void)state;

    int pipefd[2];
    if (pipe(pipefd) == -1) fail_msg("Failed to create pipe");

    int saved_stdout = dup(STDOUT_FILENO);
    if (saved_stdout == -1) fail_msg("Failed to dup stdout");

    if (dup2(pipefd[1], STDOUT_FILENO) == -1) fail_msg("Failed to redirect stdout");
    close(pipefd[1]);

    log_set_stream(stdout);
    log_info("Test log message to terminal");
    fflush(stdout); /* INFO is not auto-flushed; flush manually for test */

    char buffer[LOG_BUFFER_SIZE] = {0};
    ssize_t n = read(pipefd[0], buffer, sizeof(buffer) - 1);
    close(pipefd[0]);
    if (n < 0) fail_msg("Failed to read from pipe");
    buffer[n] = '\0';

    if (dup2(saved_stdout, STDOUT_FILENO) == -1) fail_msg("Failed to restore stdout");
    close(saved_stdout);

    assert_string_contains(buffer, "INFO");
    assert_string_contains(buffer, "Test log message to terminal");
}

/*
 * Log an INFO message to a file and verify the content is correct.
 */
static void test_log_to_file(void **state) {
    (void)state;

    FILE *f = capture_log_to_file(emit_info);
    if (!f) fail_msg("Failed to reopen test log file");

    char line[LOG_BUFFER_SIZE];
    fgets(line, sizeof(line), f);
    fclose(f);

    assert_string_contains(line, "INFO");
    assert_string_contains(line, "Test log message to file");
}

/* ── log/level_filtering ──────────────────────────────────────────────── */

/*
 * When the threshold is LOG_LEVEL_WARN, log_info must write nothing.
 * Confirms the runtime level gate fires before any I/O.
 */
static void test_info_suppressed_below_threshold(void **state) {
    (void)state;

    FILE *w = fopen(TEST_LOG_FILE, "w");
    if (!w) fail_msg("cannot open test log file for writing");
    log_set_stream(w);
    log_set_level(LOG_LEVEL_WARN);

    log_info("This INFO must not appear");

    fclose(w);
    log_set_stream(stderr);

    FILE *r = fopen(TEST_LOG_FILE, "r");
    if (!r) fail_msg("cannot reopen test log file");
    char line[LOG_BUFFER_SIZE] = {0};
    size_t n = fread(line, 1, sizeof(line) - 1, r);
    fclose(r);

    /* File must be empty — INFO was filtered before any write. */
    assert_int_equal((int)n, 0);
}

/*
 * ERROR is not suppressible: even when threshold is LOG_LEVEL_WARN,
 * log_error must still write to the stream.
 * Validates the LOG_LEVEL_WARN minimum-clamping rule.
 */
static void test_error_never_suppressed(void **state) {
    (void)state;

    FILE *f = fopen(TEST_LOG_FILE, "w");
    if (!f) fail_msg("cannot open test log file");
    log_set_stream(f);
    log_set_level(LOG_LEVEL_WARN); /* minimum threshold: only ERROR + WARN */

    log_error("Critical failure that must appear");

    fclose(f);
    log_set_stream(stderr);

    FILE *r = fopen(TEST_LOG_FILE, "r");
    if (!r) fail_msg("cannot reopen log file");
    char line[LOG_BUFFER_SIZE] = {0};
    fgets(line, sizeof(line), r);
    fclose(r);

    assert_string_contains(line, "ERROR");
    assert_string_contains(line, "Critical failure that must appear");
}

/*
 * WARN appears when threshold is LOG_LEVEL_WARN.
 * Validates that WARN (level 1) passes the gate when log_level == 1.
 */
static void test_warn_visible_at_warn_threshold(void **state) {
    (void)state;

    FILE *f = fopen(TEST_LOG_FILE, "w");
    if (!f) fail_msg("cannot open test log file");
    log_set_stream(f);
    log_set_level(LOG_LEVEL_WARN);

    log_warn("Anomaly detected");

    fclose(f);
    log_set_stream(stderr);

    FILE *r = fopen(TEST_LOG_FILE, "r");
    if (!r) fail_msg("cannot reopen log file");
    char line[LOG_BUFFER_SIZE] = {0};
    fgets(line, sizeof(line), r);
    fclose(r);

    assert_string_contains(line, "WARN");
    assert_string_contains(line, "Anomaly detected");
}

/*
 * The runtime threshold cannot be set below LOG_LEVEL_WARN.
 * log_set_level(LOG_LEVEL_ERROR) must be clamped to LOG_LEVEL_WARN,
 * so WARN messages still appear.
 */
static void test_level_clamped_at_warn_minimum(void **state) {
    (void)state;

    FILE *f = fopen(TEST_LOG_FILE, "w");
    if (!f) fail_msg("cannot open test log file");
    log_set_stream(f);
    log_set_level(LOG_LEVEL_ERROR); /* below minimum — must be clamped to WARN */

    log_warn("This WARN must still appear after clamping");

    fclose(f);
    log_set_stream(stderr);

    FILE *r = fopen(TEST_LOG_FILE, "r");
    if (!r) fail_msg("cannot reopen log file");
    char line[LOG_BUFFER_SIZE] = {0};
    fgets(line, sizeof(line), r);
    fclose(r);

    assert_string_contains(line, "WARN");
}

/* ── log/format ───────────────────────────────────────────────────────── */

/*
 * In a DEBUG build, log lines must include the source file and function name.
 * In release they are omitted (this test runs under the DEBUG build used by
 * the test suite, so we always verify source location is present here).
 */
static void test_debug_build_includes_source_location(void **state) {
    (void)state;

    FILE *f = capture_log_to_file(emit_info);
    if (!f) fail_msg("cannot reopen test log file");

    char line[LOG_BUFFER_SIZE] = {0};
    fgets(line, sizeof(line), f);
    fclose(f);

    /* Tests compile with -DDEBUG: source location must be present. */
    assert_string_contains(line, "test_log.c");
    assert_string_contains(line, "emit_info");
}

/*
 * The timestamp must appear in log output (LOG_USE_DATE path in log.c).
 * Format: [YYYY-MM-DD HH:MM:SS.mmm]
 */
static void test_timestamp_present(void **state) {
    (void)state;

    FILE *f = capture_log_to_file(emit_info);
    if (!f) fail_msg("cannot reopen test log file");

    char line[LOG_BUFFER_SIZE] = {0};
    fgets(line, sizeof(line), f);
    fclose(f);

    /* Leading bracket from timestamp. */
    assert_string_contains(line, "[20"); /* year starts with "20" */
}

/* ── main ─────────────────────────────────────────────────────────────── */

int main(void) {
    log_set_stream(stderr);

    const struct CMUnitTest stream_tests[] = {
        cmocka_unit_test_setup_teardown(test_log_to_terminal, setup, teardown),
        cmocka_unit_test_setup_teardown(test_log_to_file,     setup, teardown),
    };

    const struct CMUnitTest level_tests[] = {
        cmocka_unit_test_setup_teardown(test_info_suppressed_below_threshold, setup, teardown),
        cmocka_unit_test_setup_teardown(test_error_never_suppressed,          setup, teardown),
        cmocka_unit_test_setup_teardown(test_warn_visible_at_warn_threshold,  setup, teardown),
        cmocka_unit_test_setup_teardown(test_level_clamped_at_warn_minimum,   setup, teardown),
    };

    const struct CMUnitTest format_tests[] = {
        cmocka_unit_test_setup_teardown(test_debug_build_includes_source_location, setup, teardown),
        cmocka_unit_test_setup_teardown(test_timestamp_present,                    setup, teardown),
    };

    int failures = 0;
    failures += cmocka_run_group_tests_name("log/stream", stream_tests, NULL, NULL);
    failures += cmocka_run_group_tests_name("log/level",  level_tests,  NULL, NULL);
    failures += cmocka_run_group_tests_name("log/format", format_tests, NULL, NULL);
    return failures;
}
