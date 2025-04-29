#ifndef LOG_H
#define LOG_H

/**
 * @file log.h
 * @brief Implements a simple logging utility.
 *
 * The log module provides functions to log messages to the console or a file.
 * The log module provides functions to log messages with different log levels.
 * The log module provides functions to log messages with timestamps.
 * The log module provides functions to log messages with the source file, function, and line number.
 * The log module provides functions to log messages with variable arguments.
 * The log module provides functions to log messages with a custom log stream.
 * The log module provides functions to log messages with a custom log level.
 * The log module provides functions to log messages with a custom log format.
 */

#undef ENABLED
#if defined (DEBUG)
  #define ENABLED 1
#else
  #define ENABLED 0
#endif

#include <stdio.h>

#define LOG_INFO  "INFO"
#define LOG_WARN  "WARN"
#define LOG_ERROR "ERROR"
#define LOG_DEBUG "DEBUG"

#define LOG_USE_DATE 1
#define LOG_BUFFER_SIZE 512

extern FILE* log_stream;

void log_set_stream(FILE* stream);
void log_write(const char* level, const char* file, const char* func, int line,
           const char* fmt, ...);

#if defined (__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wgnu-zero-variadic-macro-arguments"
#endif

#define log_info(fmt, ...) \
    log_write(LOG_INFO, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define log_warn(fmt, ...) \
    log_write(LOG_WARN, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define log_error(fmt, ...) \
    log_write(LOG_ERROR, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)
#define log_debug(fmt, ...) \
    log_write(LOG_DEBUG, __FILE__, __func__, __LINE__, fmt, ##__VA_ARGS__)

#if defined (__clang__)
#pragma clang diagnostic pop
#endif

#endif//LOG_H
