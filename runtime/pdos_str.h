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
PyDosObj far * PYDOS_API pydos_str_slice_op(PyDosObj far *s,
                                             long start, long stop, long step);

/* Index a string, returning a single-character string */
PyDosObj far * PYDOS_API pydos_str_index(PyDosObj far *s, long idx);
PyDosObj far * PYDOS_API pydos_str_index_op(PyDosObj far *s, long idx);

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

#endif /* PDOS_STR_H */
