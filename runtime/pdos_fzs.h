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

/* Primitive construction bridge used by Python-backed set algorithms. */
PyDosObj far * PYDOS_API pydos_frozenset_from_list(PyDosObj far *self,
                                                    PyDosObj far *items);

/* Builtin constructor: frozenset() / frozenset(iterable) */
PyDosObj far * PYDOS_API pydos_builtin_frozenset_conv(int argc,
                                                       PyDosObj far * far *argv);

void PYDOS_API pydos_frozenset_init(void);
void PYDOS_API pydos_frozenset_shutdown(void);

#endif /* PDOS_FZS_H */
