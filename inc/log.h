/* log.h — Lightweight logging with compile-time level guards and no malloc.
 *
 * Design goals
 * ────────────
 * 1. No heisenbug risk: log_debug compiles away to ((void)0) in release —
 *    zero instructions emitted, heap layout and thread scheduling unchanged.
 * 2. Minimal latency: all formatting happens on the caller's stack (no malloc,
 *    no heap touch). The mutex is held only during the final fprintf, not
 *    during formatting, timestamp construction, or fflush.
 * 3. fflush on ERROR/WARN only: INFO and DEBUG are buffered for throughput.
 *    Critical messages reach the operator immediately; diagnostics do not
 *    add synchronisation overhead to the mining loop or chain operations.
 * 4. Runtime level gate: filtered messages are discarded in one comparison
 *    before any formatting or locking — the calling thread pays nothing.
 * 5. Security: source paths and function names are emitted only in DEBUG
 *    builds. Release logs never expose internal code structure.
 *
 * Level policy
 * ────────────
 *   LOG_LEVEL_ERROR (0)  Critical failures. Always compiled in.
 *                        Cannot be suppressed by log_set_level().
 *
 *   LOG_LEVEL_WARN  (1)  Recoverable anomalies. Always compiled in.
 *                        Cannot be suppressed by log_set_level().
 *                        (log_set_level clamps minimum to WARN.)
 *
 *   LOG_LEVEL_INFO  (2)  Chain state, mining milestones, lifecycle events.
 *                        Always compiled in. Default threshold.
 *                        Suppressible at runtime via log_set_level(WARN).
 *
 *   LOG_LEVEL_DEBUG (3)  Verbose detail, internal state, timing probes.
 *                        Compile-time guard only. Expands to ((void)0) in
 *                        release builds — zero overhead, no heisenbug.
 *                        Active only when compiled with -DDEBUG.
 *
 * Remote monitoring
 * ─────────────────
 * Use log_set_stream() to redirect output to a named pipe (FIFO):
 *   mkfifo /tmp/bloc.log
 *   // in code:  log_set_stream(fopen("/tmp/bloc.log", "w"));
 *   // shell:    cat /tmp/bloc.log | ssh user@monitor ...
 * A dedicated TCP/UDP logging port is NOT recommended: it exposes real-time
 * mining state (nonce trajectory, block solve timing) to unauthenticated
 * network observers, enabling timing attacks and selfish-mining intelligence.
 *
 * Security
 * ────────
 * NEVER pass private keys, VRF secrets, seed material, Dilithium secret keys,
 * or session tokens to any log macro. Log output may be written to shared
 * storage or forwarded to external systems — treat all log content as public.
 */

#ifndef LOG_H
#define LOG_H

#include <stdio.h>

/* ── Level constants ──────────────────────────────────────────────────── */

#define LOG_LEVEL_ERROR 0   /* critical failure          — always visible */
#define LOG_LEVEL_WARN  1   /* recoverable anomaly       — always visible */
#define LOG_LEVEL_INFO  2   /* operational milestone     — default level  */
#define LOG_LEVEL_DEBUG 3   /* verbose / internal detail — debug build only */

/*
 * Default runtime threshold.
 * INFO means ERROR + WARN + INFO are shown; DEBUG is gated at compile time.
 * Operators running a high-throughput node can lower this to LOG_LEVEL_WARN
 * via log_set_level() to reduce I/O without losing safety visibility.
 */
#define LOG_DEFAULT_LEVEL LOG_LEVEL_INFO

/* Maximum length of the formatted user message (truncated if longer). */
#define LOG_BUFFER_SIZE 512

/* ── Stream management ────────────────────────────────────────────────── */

extern FILE *log_stream;

/*
 * Redirect log output to stream. Falls back to stderr if stream is NULL.
 * Thread-safe. Call once at startup before any concurrent log activity.
 * Accepts a FILE* opened on a regular file, stderr, stdout, or a FIFO.
 */
void log_set_stream(FILE *stream);

/* ── Runtime level control ────────────────────────────────────────────── */

/*
 * Set the minimum severity threshold. Messages at higher levels (numerically)
 * are discarded before any formatting or I/O.
 *
 * ERROR and WARN cannot be suppressed: log_set_level clamps the minimum
 * to LOG_LEVEL_WARN. This ensures consensus failures and rejected blocks
 * are always visible regardless of operator configuration.
 *
 * Thread-safe.
 */
void log_set_level(int level);

/* ── Internal write function — do not call directly ───────────────────── */

/*
 * log_write(level, level_str, file, func, line, fmt, ...)
 *
 * Formats the message entirely on the caller's stack (no malloc), then
 * acquires the mutex only for the final write. fflush is called only for
 * ERROR and WARN. file/func/line are NULL/0 in release builds.
 */
void log_write(int level, const char *level_str,
               const char *file, const char *func, int line,
               const char *fmt, ...);

/* ── Public macros ────────────────────────────────────────────────────── */

/*
 * All macros use (...) / __VA_ARGS__ rather than (fmt, ...) / ##__VA_ARGS__.
 *
 * Why: the GNU ##__VA_ARGS__ extension (which removes the preceding comma
 * when the variadic list is empty) triggers -Wgnu-zero-variadic-macro-
 * arguments at every call site that passes only a format string and no
 * further arguments. Using plain __VA_ARGS__ avoids the extension entirely —
 * no pragma guards needed. All log calls always supply at least one argument
 * (the format string), so __VA_ARGS__ is never truly empty.
 */

#if defined(DEBUG)
/*
 * Debug build: all four levels active.
 * Source path, function name, and line number are included in every line.
 * Paths are normalised by -fmacro-prefix-map at compile time
 * (e.g. "/Users/.../src/chain.c" → "./src/chain.c").
 */
#define log_error(...) \
    log_write(LOG_LEVEL_ERROR, "ERROR", __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_warn(...)  \
    log_write(LOG_LEVEL_WARN,  "WARN",  __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_info(...)  \
    log_write(LOG_LEVEL_INFO,  "INFO",  __FILE__, __func__, __LINE__, __VA_ARGS__)
#define log_debug(...) \
    log_write(LOG_LEVEL_DEBUG, "DEBUG", __FILE__, __func__, __LINE__, __VA_ARGS__)

#else  /* Release build */

/*
 * Release build:
 *   log_error/warn/info — source location omitted. Release logs never expose
 *                         internal code structure (file paths, function names).
 *   log_debug           — expands to ((void)0). The compiler emits no
 *                         instructions at all: no stack frame, no register
 *                         spill, no heap touch, no thread scheduling impact.
 *                         This is the correct fix for debug-induced heisenbugs.
 *                         (...) captures all arguments so the call site never
 *                         passes zero args for '...' — no compiler warning.
 */
#define log_error(...) \
    log_write(LOG_LEVEL_ERROR, "ERROR", NULL, NULL, 0, __VA_ARGS__)
#define log_warn(...)  \
    log_write(LOG_LEVEL_WARN,  "WARN",  NULL, NULL, 0, __VA_ARGS__)
#define log_info(...)  \
    log_write(LOG_LEVEL_INFO,  "INFO",  NULL, NULL, 0, __VA_ARGS__)
#define log_debug(...) ((void)0)

#endif /* DEBUG */

#endif /* LOG_H */
