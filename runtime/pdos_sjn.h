/*
 * pdos_sjn.h - String join for PyDOS runtime
 *
 * Single-allocation join of multiple string parts, used by f-string
 * compilation to eliminate N-1 intermediate allocations.
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_SJN_H
#define PDOS_SJN_H

#include "pdos_obj.h"

/* Join 'count' string parts into a single string.
 * Parts are passed as variadic far pointers (PyDosObj far *).
 * Non-string or NULL parts are silently skipped.
 * Max 16 parts (excess ignored). */
PyDosObj far * PYDOS_API pydos_str_join_n(unsigned int count, ...);

#endif /* PDOS_SJN_H */
