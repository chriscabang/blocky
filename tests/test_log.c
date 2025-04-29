#include <stdarg.h>
#include <stddef.h>

#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif
#include <setjmp.h>
#include <cmocka.h>
#if defined (__clang__)
#pragma clang diagnostic pop
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "log.h"

#define assert_string_contains(haystack, needle) \
  assert_true(strstr((haystack), (needle)) != NULL)
#define TEST_LOG_FILE "test_output.log"

/* Test logging to terminal */
static void test_log_to_terminal(void **state) {
  (void) state; // Suppress unused variable warning

  // Create a pipe to capture the output
  // pipefd[1] is the write end, pipefd[0] is the read end
  int pipefd[2];
  if (pipe(pipefd) == -1) {
    fail_msg("Failed to create pipe");
  }

  // Backup the original stdout
  int stdout_fd = dup(STDOUT_FILENO);
  if (stdout_fd == -1) {
    fail_msg("Failed to duplicate stdout");
  }

  // Redirect stdout to the write end of the pipe
  if (dup2(pipefd[1], STDOUT_FILENO) == -1) {
    fail_msg("Failed to redirect stdout");
  }
  close(pipefd[1]);

  // Set the log stream to stdout
  log_set_stream(stdout);

  // Log a message test to the terminal
  log_info("Test log message to terminal");

  printf("log_stream: %p\n", (void*) log_stream);

  // Read from the read end of the pipe
  char buffer[LOG_BUFFER_SIZE] = {0};
  ssize_t bytes_read = read(pipefd[0], buffer, sizeof(buffer) - 1);
  if (bytes_read < 0) {
    fail_msg("Failed to read from pipe");
  }
  buffer[bytes_read] = '\0';

  // Restore stdout
  if (dup2(stdout_fd, STDOUT_FILENO) == -1) {
    fail_msg("Failed to restore stdout");
  }
  close(stdout_fd);

  assert_string_contains(buffer, "INFO");
}

/* Test logging to file */
static void test_log_to_file(void **state) {
  (void)state; // Suppress unused variable warning

  // Create a mock stream to write to a file
  FILE *mock_stream = fopen(TEST_LOG_FILE, "w");
  if (!mock_stream) {
    fail_msg("Failed to open log file for writing");
  }

  // Set the log stream to the mock stream
  log_set_stream(mock_stream);

  // Log a message test to the file
  log_info("Test log message to file");

  fclose(mock_stream);

  // Reopen the file to read the contents
  FILE *file = fopen(TEST_LOG_FILE, "r");
  if (!file) {
    fail_msg("Failed to open log file for reading");
  }

  // Read the first line of the file
  char line[LOG_BUFFER_SIZE];
  fgets(line, sizeof(line), file);
  fclose(file);

  // Check if the line contains the expected log message
  assert_string_contains(line, "INFO");
  assert_string_contains(line, "Test log message to file");
}

/* Main function to run the tests */
int main(void) {
  // List of tests to run
  const struct CMUnitTest tests[] = {
      cmocka_unit_test(test_log_to_terminal),
      cmocka_unit_test(test_log_to_file),
  };

  // Run the tests as a group
  int result = cmocka_run_group_tests(tests, NULL, NULL);

  // Clean up the test log file
  remove(TEST_LOG_FILE);

  return result;
}


