/*
 * error.cpp - Error reporting for PyDOS Python-to-8086 compiler
 *
 * GCC-style diagnostics: file:line:col: level: message
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL - uses stdarg.h for variadic formatting.
 */

#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* Module-level state                                               */
/* --------------------------------------------------------------- */
static const char *error_filename = "";
static int error_count = 0;
static int warning_count = 0;
static int max_errors = 20;

/* --------------------------------------------------------------- */
/* error_init / error_shutdown                                      */
/* --------------------------------------------------------------- */

void error_init(const char *filename)
{
    error_filename = filename ? filename : "<unknown>";
    error_count = 0;
    warning_count = 0;
    max_errors = 20;
}

void error_shutdown()
{
    error_filename = "";
    error_count = 0;
    warning_count = 0;
}

/* --------------------------------------------------------------- */
/* Internal: print a diagnostic with level string                   */
/* --------------------------------------------------------------- */

static void emit_diagnostic(const char *level, int line, int col,
                            const char *fmt, va_list ap)
{
    /* file:line:col: level: message */
    fprintf(stderr, "%s:%d:%d: %s: ", error_filename, line, col, level);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
}

/* --------------------------------------------------------------- */
/* error_at                                                         */
/* --------------------------------------------------------------- */

void error_at(int line, int col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit_diagnostic("error", line, col, fmt, ap);
    va_end(ap);

    error_count++;

    if (error_count >= max_errors) {
        fprintf(stderr, "%s: too many errors (%d), stopping\n",
                error_filename, error_count);
        exit(1);
    }
}

/* --------------------------------------------------------------- */
/* warning_at                                                       */
/* --------------------------------------------------------------- */

void warning_at(int line, int col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit_diagnostic("warning", line, col, fmt, ap);
    va_end(ap);

    warning_count++;
}

/* --------------------------------------------------------------- */
/* note_at                                                          */
/* --------------------------------------------------------------- */

void note_at(int line, int col, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    emit_diagnostic("note", line, col, fmt, ap);
    va_end(ap);
}

/* --------------------------------------------------------------- */
/* error_fatal                                                      */
/* --------------------------------------------------------------- */

void error_fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "%s: fatal error: ", error_filename);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    va_end(ap);

    exit(1);
}

/* --------------------------------------------------------------- */
/* Queries                                                          */
/* --------------------------------------------------------------- */

int error_get_count()
{
    return error_count;
}

int warning_get_count()
{
    return warning_count;
}

void error_set_max(int max)
{
    if (max > 0) {
        max_errors = max;
    }
}
