/*
 * error.h - Error reporting for PyDOS Python-to-8086 compiler
 *
 * GCC-style diagnostics: file:line:col: level: message
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL - uses stdarg.h for variadic formatting.
 */

#ifndef ERROR_H
#define ERROR_H

/* Initialize error subsystem with the source filename for messages */
void error_init(const char *filename);

/* Shut down error subsystem */
void error_shutdown();

/* Report an error at a specific source location */
void error_at(int line, int col, const char *fmt, ...);

/* Report a warning at a specific source location */
void warning_at(int line, int col, const char *fmt, ...);

/* Report an informational note at a specific source location */
void note_at(int line, int col, const char *fmt, ...);

/* Report a fatal error (prints message and calls exit(1)) */
void error_fatal(const char *fmt, ...);

/* Return total number of errors reported so far */
int error_get_count();

/* Return total number of warnings reported so far */
int warning_get_count();

/* Set the maximum number of errors before aborting compilation.
 * Default is 20. When exceeded, prints "too many errors" and exits. */
void error_set_max(int max);

#endif /* ERROR_H */
