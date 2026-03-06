/* log.c — Logging implementation: stack-only, level-gated, brief mutex. */
#include "log.h"

#include <pthread.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ── Module state ─────────────────────────────────────────────────────── */

FILE *log_stream = NULL;

/*
 * log_level: runtime severity threshold.
 *
 * volatile: ensures the compiler reloads the value on every read across
 * threads without recompilation. On all target platforms (x86, ARM/RPi),
 * aligned 32-bit reads/writes are atomic, so a torn read during a level
 * change would at worst let one extra message through or drop one — both
 * acceptable. The mutex in log_set_level serialises writes.
 */
static volatile int log_level = LOG_DEFAULT_LEVEL;

static pthread_mutex_t log_mutex = PTHREAD_MUTEX_INITIALIZER;

/* ── Public API ───────────────────────────────────────────────────────── */

void log_set_stream(FILE *stream) {
    pthread_mutex_lock(&log_mutex);
    log_stream = (stream != NULL) ? stream : stderr;
    pthread_mutex_unlock(&log_mutex);
}

void log_set_level(int level) {
    /*
     * ERROR and WARN are never suppressed: clamp minimum to LOG_LEVEL_WARN.
     * This guarantees consensus failures and rejected blocks always reach
     * the operator regardless of configuration.
     */
    if (level < LOG_LEVEL_WARN)  level = LOG_LEVEL_WARN;
    if (level > LOG_LEVEL_DEBUG) level = LOG_LEVEL_DEBUG;
    pthread_mutex_lock(&log_mutex);
    log_level = level;
    pthread_mutex_unlock(&log_mutex);
}

/* ── Internal write ───────────────────────────────────────────────────── */

void log_write(int level, const char *level_str,
               const char *file, const char *func, int line,
               const char *fmt, ...) {
    /*
     * ① Level gate — fast path, no lock, no allocation.
     *   Filtered messages cost one comparison on the calling thread.
     *   This is the primary performance guard for high-frequency paths
     *   (chain_validate, storage operations).
     */
    if (level > log_level) return;

    /*
     * ② Format everything on the caller's stack — no malloc, no heap touch.
     *    All work below happens BEFORE acquiring the mutex, so other threads
     *    are not blocked while this thread formats timestamps or messages.
     */

    /* Timestamp. */
    char timestamp[32] = "";
    struct timespec ts;
    struct tm       tm_info;
    clock_gettime(CLOCK_REALTIME, &ts);
    localtime_r(&ts.tv_sec, &tm_info);
    snprintf(timestamp, sizeof(timestamp),
             "[%04d-%02d-%02d %02d:%02d:%02d.%03ld] ",
             tm_info.tm_year + 1900, tm_info.tm_mon + 1, tm_info.tm_mday,
             tm_info.tm_hour,        tm_info.tm_min,      tm_info.tm_sec,
             ts.tv_nsec / 1000000L);

    /*
     * Source location — present only in debug builds (file/func are NULL in
     * release macros). Truncated to 48 chars to keep lines readable.
     * Stack buffer: no malloc, heap layout unchanged.
     */
    char source[64] = "";
    if (file && func)
        snprintf(source, sizeof(source), "%s:%s", file, func);

    /* User message — truncated to LOG_BUFFER_SIZE if necessary. */
    char msg[LOG_BUFFER_SIZE];
    va_list args;
    va_start(args, fmt);
    vsnprintf(msg, sizeof(msg), fmt, args);
    va_end(args);

    /*
     * Assembled line.
     * Debug format:   [timestamp] LEVEL  source:func              @ Ln.N: msg
     * Release format: [timestamp] LEVEL  msg
     *
     * Sizing: timestamp(27) + level(5) + source(40) + " @ Ln.NNN: "(12) +
     *         msg(512) = 596 < LOG_BUFFER_SIZE + 128 (640). No truncation for
     *         typical messages.
     */
    char line_buf[LOG_BUFFER_SIZE + 128];
    if (source[0] != '\0') {
        snprintf(line_buf, sizeof(line_buf),
                 "%s%-5s %-40s @ Ln.%d: %s",
                 timestamp, level_str, source, line, msg);
    } else {
        snprintf(line_buf, sizeof(line_buf),
                 "%s%-5s %s",
                 timestamp, level_str, msg);
    }

    /*
     * ③ Write — mutex held only for fprintf + conditional fflush.
     *    This is the minimal critical section: no formatting, no syscalls
     *    other than the write itself.
     *
     *    fflush on ERROR and WARN only:
     *      Critical messages must reach the operator immediately even if the
     *      process crashes shortly after. INFO and DEBUG are left in the OS
     *      write buffer for throughput — they will be flushed on process exit
     *      or when the buffer fills. This eliminates fflush as a latency
     *      source for the mining loop and chain operations.
     */
    pthread_mutex_lock(&log_mutex);
    if (!log_stream) log_stream = stderr;
    fprintf(log_stream, "%s\n", line_buf);
    if (level <= LOG_LEVEL_WARN) fflush(log_stream);
    pthread_mutex_unlock(&log_mutex);
}
