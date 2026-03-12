/*
 * pydos_int.h - Integer operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_INT_H
#define PDOS_INT_H

#include "pdos_obj.h"

/* Arithmetic operations - all return new PyDosObj far * (int type) */
PyDosObj far * PYDOS_API pydos_int_add(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_sub(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_mul(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_div(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_truediv(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_mod(PyDosObj far *a, PyDosObj far *b);

/* Unary operations */
PyDosObj far * PYDOS_API pydos_int_neg(PyDosObj far *a);
PyDosObj far * PYDOS_API pydos_int_abs(PyDosObj far *a);

/* Power */
PyDosObj far * PYDOS_API pydos_int_pow(PyDosObj far *base, PyDosObj far *exp);

/* Comparison: returns -1, 0, or 1 */
int PYDOS_API pydos_int_compare(PyDosObj far *a, PyDosObj far *b);

/* Bitwise operations */
PyDosObj far * PYDOS_API pydos_int_bitand(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_bitor(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_bitxor(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_bitnot(PyDosObj far *a);

/* Shift operations */
PyDosObj far * PYDOS_API pydos_int_shl(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_int_shr(PyDosObj far *a, PyDosObj far *b);

/* Convert int object to string object */
PyDosObj far * PYDOS_API pydos_int_to_str(PyDosObj far *obj);

/* Parse string object to int object. Returns NULL on error */
PyDosObj far * PYDOS_API pydos_int_from_str(PyDosObj far *str_obj);

/* Methods */
PyDosObj far * PYDOS_API pydos_int_bit_length(PyDosObj far *self);

void PYDOS_API pydos_int_init(void);
void PYDOS_API pydos_int_shutdown(void);

#endif /* PDOS_INT_H */
