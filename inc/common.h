#ifndef COMMON_H
#define COMMON_H

#include "log.h"

/*
 * Structured error-handling macros for C functions.
 *
 * Usage pattern:
 *
 *   int my_func(void *ptr) {
 *       START
 *           CHECKNULL(ptr);
 *           // ... work ...
 *       RETURN
 *   }
 *
 * START opens a do-while(0) scope and declares int STATUS = EXIT_SUCCESS.
 * FAIL sets STATUS = EXIT_FAILURE and breaks out of the scope.
 * RETURN closes the scope and returns STATUS.
 *
 * WARNING: Do not use CHECKNULL / CHECKZERO / FAIL inside a nested for/while
 * loop within a START block — 'break' will exit the inner loop, not the
 * START scope.  Use an explicit 'if' + 'goto' pattern in those cases.
 */

#define START \
    do { \
        log_info("Start > "); \
        int STATUS = EXIT_SUCCESS;

#define FAIL(x) \
    STATUS = EXIT_FAILURE; break;

#define CHECKNULL(x) \
    if (!(x)) { log_warn("Null pointer: %s", #x); FAIL(x); }

#define CHECKZERO(x) \
    if ((x) == 0) { log_warn("Zero value: %s", #x); FAIL(x); }

#define END \
        log_info("< End"); \
    } while (0);

#define RETURN \
    END \
    return STATUS;

#endif /* COMMON_H */
