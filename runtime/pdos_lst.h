/*
 * pydos_list.h - List operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_LST_H
#define PDOS_LST_H

#include "pdos_obj.h"

/* Create empty list with given initial capacity */
PyDosObj far * PYDOS_API pydos_list_new(unsigned int initial_cap);

/* Append item to list */
PyDosObj far * PYDOS_API pydos_list_append(PyDosObj far *list,
                                            PyDosObj far *item);

/* Get item by index (negative wraps). Returns NULL on out of bounds */
PyDosObj far * PYDOS_API pydos_list_get(PyDosObj far *list, long index);

/* Set item at index (negative wraps). Returns 0 on success, -1 on error */
int PYDOS_API pydos_list_set(PyDosObj far *list, long index, PyDosObj far *item);

/* Return list length */
long PYDOS_API pydos_list_len(PyDosObj far *list);

/* Return new list from slice */
PyDosObj far * PYDOS_API pydos_list_slice(PyDosObj far *list,
                               long start, long stop, long step);
PyDosObj far * PYDOS_API pydos_list_get_op(PyDosObj far *list, long index);
void PYDOS_API pydos_list_set_op(PyDosObj far *list, long index,
                                  PyDosObj far *item);
PyDosObj far * PYDOS_API pydos_list_slice_op(PyDosObj far *list,
                                  long start, long stop, long step);
PyDosObj far * PYDOS_API pydos_tuple_slice(PyDosObj far *tuple,
                                long start, long stop, long step);

/* Concatenate two lists, return new list */
PyDosObj far * PYDOS_API pydos_list_concat(PyDosObj far *a, PyDosObj far *b);

/* Repeat a list or tuple count times. The result keeps the input type tag */
PyDosObj far * PYDOS_API pydos_seq_repeat(PyDosObj far *seq, long count);

/* Concatenate two tuples, return new tuple */
PyDosObj far * PYDOS_API pydos_tuple_concat(PyDosObj far *a, PyDosObj far *b);

/* Retag a freshly built list as a tuple */
PyDosObj far * PYDOS_API pydos_list_to_tuple(PyDosObj far *list);

/* Test membership. Returns 1 if item found, 0 otherwise */
int PYDOS_API pydos_list_contains(PyDosObj far *list, PyDosObj far *item);

/* Remove and return item at index. Returns NULL on error */
PyDosObj far * PYDOS_API pydos_list_pop(PyDosObj far *list, long index);

/* Insert item at index */
void PYDOS_API pydos_list_insert(PyDosObj far *list, long index, PyDosObj far *item);

/* Reverse list in place */
void PYDOS_API pydos_list_reverse(PyDosObj far *list);

/* Sort list in place (ascending, stable insertion sort) */
void PYDOS_API pydos_list_sort(PyDosObj far *list);

/* Sort list in place with key function (PYDT_FUNCTION object).
 * If reverse is nonzero, sort descending (stable). */
void PYDOS_API pydos_list_sort_key(PyDosObj far *list, PyDosObj far *key_fn, int reverse);

/* Remove first occurrence of item by value. Returns 0 on success, -1 if not found */
int PYDOS_API pydos_list_remove(PyDosObj far *list, PyDosObj far *item);

/* Method wrapper: list.pop(index_obj) - unboxes int from PyDosObj* */
PyDosObj far * PYDOS_API pydos_list_pop_m(PyDosObj far *self, PyDosObj far *idx_obj);

/* Method wrapper: list.insert(index_obj, item) - unboxes int from PyDosObj* */
PyDosObj far * PYDOS_API pydos_list_insert_m(PyDosObj far *self,
                                              PyDosObj far *idx_obj,
                                              PyDosObj far *item);
PyDosObj far * PYDOS_API pydos_list_reverse_m(PyDosObj far *self);
PyDosObj far * PYDOS_API pydos_list_clear_m(PyDosObj far *self);

/* Remove all items from list */
void PYDOS_API pydos_list_clear(PyDosObj far *list);

/* Create a new list by copying all items from source list */
PyDosObj far * PYDOS_API pydos_list_copy(PyDosObj far *src);

/* Create a new list by iterating any iterable (list, tuple, range, dict, str, generator).
 * Fast path for list/tuple (copies directly), slow path iterates via get_iter/iter_next. */
PyDosObj far * PYDOS_API pydos_list_from_iter(PyDosObj far *iterable);

/* Star unpacking helper: a, *b, c = seq
 * Builtin calling convention: argc=3, argv=[seq, before_int, after_int].
 * Returns list of (before + 1 + after) elements.
 * Elements 0..before-1 are individual items from front,
 * element 'before' is a list of middle items,
 * elements before+1..end are individual items from back.
 * Raises ValueError if len(seq) < before + after. */
PyDosObj far * PYDOS_API pydos_unpack_ex(int argc,
                              PyDosObj far * far *argv);

void PYDOS_API pydos_list_init(void);
void PYDOS_API pydos_list_shutdown(void);

#endif /* PDOS_LST_H */
