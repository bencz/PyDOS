/*
 * pydos_str.h - String operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_STR_H
#define PDOS_STR_H

#include "pdos_obj.h"

/* Create a new string object from far data */
PyDosObj far * PYDOS_API pydos_str_new(const char far *data, unsigned int len);

/* Create a new string object from a near C string */
PyDosObj far * PYDOS_API pydos_str_from_cstr(const char *s);

/* Concatenate two string objects, returning a new string */
PyDosObj far * PYDOS_API pydos_str_concat(PyDosObj far *a, PyDosObj far *b);

/* Repeat a string count times, returning a new string */
PyDosObj far * PYDOS_API pydos_str_repeat(PyDosObj far *s, long count);

/* Slice a string with start:stop:step, returning a new string */
PyDosObj far * PYDOS_API pydos_str_slice(PyDosObj far *s, long start, long stop, long step);

/* Index a string, returning a single-character string */
PyDosObj far * PYDOS_API pydos_str_index(PyDosObj far *s, long idx);

/* Find substring in string. Returns index or -1 */
long PYDOS_API pydos_str_find(PyDosObj far *s, PyDosObj far *sub);

/* Return string length */
long PYDOS_API pydos_str_len(PyDosObj far *s);

/* Test string equality. Returns 0 or 1 */
int PYDOS_API pydos_str_equal(PyDosObj far *a, PyDosObj far *b);

/* Lexicographic compare. Returns -1, 0, or 1 */
int PYDOS_API pydos_str_compare(PyDosObj far *a, PyDosObj far *b);

/* DJB2 hash of string data. Returns unsigned int */
unsigned int PYDOS_API pydos_str_hash(PyDosObj far *s);

/* Format a long integer as a string object */
PyDosObj far * PYDOS_API pydos_str_format_int(long val);

/* ---- String methods (return PyDosObj far *, all PYDOS_API) ---- */

/* Case transformations */
PyDosObj far * PYDOS_API pydos_str_upper(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_lower(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_title(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_capitalize(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_swapcase(PyDosObj far *s);

/* Whitespace trimming */
PyDosObj far * PYDOS_API pydos_str_strip(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_lstrip(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_rstrip(PyDosObj far *s);

/* Search */
PyDosObj far * PYDOS_API pydos_str_rfind_m(PyDosObj far *s, PyDosObj far *sub);
PyDosObj far * PYDOS_API pydos_str_find_m(PyDosObj far *s, PyDosObj far *sub);
PyDosObj far * PYDOS_API pydos_str_index_m(PyDosObj far *s, PyDosObj far *sub);
PyDosObj far * PYDOS_API pydos_str_rindex_m(PyDosObj far *s, PyDosObj far *sub);
PyDosObj far * PYDOS_API pydos_str_count_m(PyDosObj far *s, PyDosObj far *sub);

/* Predicates */
PyDosObj far * PYDOS_API pydos_str_startswith(PyDosObj far *s, PyDosObj far *prefix);
PyDosObj far * PYDOS_API pydos_str_endswith(PyDosObj far *s, PyDosObj far *suffix);
PyDosObj far * PYDOS_API pydos_str_isdigit(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_isalpha(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_isalnum(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_isspace(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_isupper(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_islower(PyDosObj far *s);

/* Split/join */
PyDosObj far * PYDOS_API pydos_str_split_m(PyDosObj far *s, PyDosObj far *sep);
PyDosObj far * PYDOS_API pydos_str_rsplit_m(PyDosObj far *s, PyDosObj far *sep);
PyDosObj far * PYDOS_API pydos_str_splitlines(PyDosObj far *s);
PyDosObj far * PYDOS_API pydos_str_join_m(PyDosObj far *sep, PyDosObj far *iterable);

/* Replace */
PyDosObj far * PYDOS_API pydos_str_replace_m(PyDosObj far *s, PyDosObj far *old_s,
                                              PyDosObj far *new_s);

/* Padding / alignment */
PyDosObj far * PYDOS_API pydos_str_center_m(PyDosObj far *s, PyDosObj far *width);
PyDosObj far * PYDOS_API pydos_str_ljust_m(PyDosObj far *s, PyDosObj far *width);
PyDosObj far * PYDOS_API pydos_str_rjust_m(PyDosObj far *s, PyDosObj far *width);
PyDosObj far * PYDOS_API pydos_str_zfill_m(PyDosObj far *s, PyDosObj far *width);

/* Encoding (stub) */
PyDosObj far * PYDOS_API pydos_str_encode(PyDosObj far *s);

/* Format (stub — f-strings are compiled, not runtime) */
PyDosObj far * PYDOS_API pydos_str_format_m(PyDosObj far *s);

void PYDOS_API pydos_str_init(void);
void PYDOS_API pydos_str_shutdown(void);

#endif /* PDOS_STR_H */
