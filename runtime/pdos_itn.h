/*
 * pydos_intern.h - String interning for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_ITN_H
#define PDOS_ITN_H

#include "pdos_obj.h"

/* Intern a string object. Returns the interned version (may be same or different).
 * The interned string has OBJ_FLAG_IMMORTAL set. */
PyDosObj far * PYDOS_API pydos_intern(PyDosObj far *str);

/* Look up an existing interned string by raw data. Returns NULL if not found */
PyDosObj far * PYDOS_API pydos_intern_lookup(const char far *data, unsigned int len);

void PYDOS_API pydos_intern_init(void);
void PYDOS_API pydos_intern_shutdown(void);

#endif /* PDOS_ITN_H */
