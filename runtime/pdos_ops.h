/*
 * pdos_ops.h - Polymorphic bitwise / shift operator dispatch.
 *
 * Lives in its own translation unit because the pdos_obj.c code segment
 * is at the 64 KB large-model limit.  The entry points mirror the
 * arithmetic family in pdos_obj.c: int/bool fast path, then
 * __and__/__rand__ etc. through the vtable, then TypeError.
 */

#ifndef PDOS_OPS_H
#define PDOS_OPS_H

#include "pdos_obj.h"

PyDosObj far * PYDOS_API pydos_obj_bitand(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_bitor(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_bitxor(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_lshift(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_rshift(PyDosObj far *a, PyDosObj far *b);

#endif /* PDOS_OPS_H */
