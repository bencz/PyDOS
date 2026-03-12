/*
 * pydos_dict.h - Dictionary operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_DIC_H
#define PDOS_DIC_H

#include "pdos_obj.h"

/* Create empty dict with given initial hash table size */
PyDosObj far * PYDOS_API pydos_dict_new(unsigned int initial_size);

/* Set key-value pair. Overwrites if key exists */
void PYDOS_API pydos_dict_set(PyDosObj far *dict, PyDosObj far *key,
                    PyDosObj far *value);

/* Get value for key. Returns NULL if not found */
PyDosObj far * PYDOS_API pydos_dict_get(PyDosObj far *dict, PyDosObj far *key);

/* Delete key. Returns 1 on success, 0 if not found */
int PYDOS_API pydos_dict_delete(PyDosObj far *dict, PyDosObj far *key);

/* Check if key exists. Returns 1 or 0 */
int PYDOS_API pydos_dict_contains(PyDosObj far *dict, PyDosObj far *key);

/* Return number of entries */
long PYDOS_API pydos_dict_len(PyDosObj far *dict);

/* Remove all entries from dict */
void PYDOS_API pydos_dict_clear(PyDosObj far *dict);

/* Return list of keys (internal runtime utility, used by set ops and to_str) */
PyDosObj far * PYDOS_API pydos_dict_keys(PyDosObj far *dict);

/* Return list of values (internal runtime utility) */
PyDosObj far * PYDOS_API pydos_dict_values(PyDosObj far *dict);

/* Return list of (key, value) tuples (internal runtime utility) */
PyDosObj far * PYDOS_API pydos_dict_items(PyDosObj far *dict);

void PYDOS_API pydos_dict_init(void);
void PYDOS_API pydos_dict_shutdown(void);

/* ---- Set method wrappers ---- */
/* Sets are PYDT_SET backed by dict (hash table). These thin wrappers
   expose set mutation operations through the direct-call convention
   so the compiler can generate calls via @internal_implementation. */

void PYDOS_API pydos_set_add(PyDosObj far *self, PyDosObj far *item);
void PYDOS_API pydos_set_remove(PyDosObj far *self, PyDosObj far *item);
void PYDOS_API pydos_set_discard(PyDosObj far *self, PyDosObj far *item);
void PYDOS_API pydos_set_clear(PyDosObj far *self);
PyDosObj far * PYDOS_API pydos_set_pop(PyDosObj far *self);

#endif /* PDOS_DIC_H */
