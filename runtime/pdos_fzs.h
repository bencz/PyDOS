/*
 * pdos_fzs.h - Frozenset type for PyDOS runtime
 *
 * Immutable, hashable set with sorted-array storage and O(log n) lookup.
 * Python-to-8086 DOS compiler runtime.
 */

#ifndef PDOS_FZS_H
#define PDOS_FZS_H

#include "pdos_obj.h"

/* Create a frozenset from an array of elements.
 * Elements are sorted and deduplicated. Duplicates are DECREFed.
 * Takes ownership of element references (caller must have INCREFed). */
PyDosObj far * PYDOS_API pydos_frozenset_new(PyDosObj far * far *items,
                                              int count);

/* Check if frozenset contains an item. Returns 1 if found, 0 otherwise. */
int PYDOS_API pydos_frozenset_contains(PyDosObj far *fs, PyDosObj far *item);

/* Return the number of elements in the frozenset. */
int PYDOS_API pydos_frozenset_len(PyDosObj far *fs);

/* Set operations — all return new frozenset objects */
PyDosObj far * PYDOS_API pydos_frozenset_union(PyDosObj far *a,
                                                PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_frozenset_intersection(PyDosObj far *a,
                                                       PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_frozenset_difference(PyDosObj far *a,
                                                     PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_frozenset_symmetric_difference(
                                                PyDosObj far *a,
                                                PyDosObj far *b);

/* Subset/superset/disjoint tests */
int PYDOS_API pydos_frozenset_issubset(PyDosObj far *a, PyDosObj far *b);
int PYDOS_API pydos_frozenset_issuperset(PyDosObj far *a, PyDosObj far *b);
int PYDOS_API pydos_frozenset_isdisjoint(PyDosObj far *a, PyDosObj far *b);

/* Builtin constructor: frozenset() / frozenset(iterable) */
PyDosObj far * PYDOS_API pydos_builtin_frozenset_conv(int argc,
                                                       PyDosObj far * far *argv);

void PYDOS_API pydos_frozenset_init(void);
void PYDOS_API pydos_frozenset_shutdown(void);

#endif /* PDOS_FZS_H */
