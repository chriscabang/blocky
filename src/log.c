/**
 * @file log.h
 * @brief Implements a simple logging utility.
 *
 */

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "log.h"

FILE *log_stream = NULL;
static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

void log_set_stream(FILE *stream) {
  log_stream = (stream != NULL) ? stream : stderr;
}

/*static void __attribute__((used))*/
void log_write(const char *level, const char *file, const char *func, int line,
               const char *fmt, ...) {
  struct timespec ts;
  struct tm tm_info;
  char log_buffer[LOG_BUFFER_SIZE];
  char timestamp[32] = "";
  va_list args;

  pthread_mutex_lock(&log_mutex);

  if (!log_stream) {
    log_stream = stderr;
  }

  clock_gettime(CLOCK_REALTIME, &ts);
  localtime_r(&ts.tv_sec, &tm_info);

#if LOG_USE_DATE
  snprintf(timestamp, sizeof(timestamp),
           "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ", tm_info.tm_year + 1900,
           tm_info.tm_mon + 1, tm_info.tm_mday, tm_info.tm_hour, tm_info.tm_min,
           tm_info.tm_sec, ts.tv_nsec / 1000000);
#endif

  // Create a string with the source file and function name
  char *source_file = malloc(strlen(file) + strlen(func) + 1);
  snprintf(source_file, strlen(file) + strlen(func) + 2, "%s:%s", file, func);

  // Format the log message
  va_start(args, fmt);
  int log_len =
      snprintf(log_buffer, sizeof(log_buffer),
               "%s%-5s %-32s @ Ln.%d: ", timestamp, level, source_file, line);
  vsnprintf(log_buffer + log_len, sizeof(log_buffer) - log_len, fmt, args);
  va_end(args);

  // Print the log message to the log stream
  fprintf(log_stream, "%s\n", log_buffer);
  fflush(log_stream);

  // cleanup
  free(source_file);
  source_file = NULL;

  pthread_mutex_unlock(&log_mutex);
}
