/*
 * pdos_cpx.h - Complex number type for PyDOS runtime
 *
 * Provides complex number creation and arithmetic operations.
 * Complex numbers store real and imaginary parts as doubles.
 */

#ifndef PDOS_CPX_H
#define PDOS_CPX_H

#include "pdos_obj.h"

/* Create a new complex object with given real and imaginary parts */
PyDosObj far * PYDOS_API pydos_complex_new(double real, double imag);

/* Arithmetic operations — return new complex objects */
PyDosObj far * PYDOS_API pydos_complex_add(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_complex_sub(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_complex_mul(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_complex_div(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_complex_neg(PyDosObj far *a);
PyDosObj far * PYDOS_API pydos_complex_pos(PyDosObj far *a);

/* abs(complex) returns float (the magnitude) */
PyDosObj far * PYDOS_API pydos_complex_abs(PyDosObj far *a);

/* conjugate() returns new complex */
PyDosObj far * PYDOS_API pydos_complex_conjugate(PyDosObj far *a);

/* Builtin complex() constructor */
PyDosObj far * PYDOS_API pydos_builtin_complex_conv(int argc,
                                                     PyDosObj far * far *argv);

/* Extract real/imag as double from any numeric object */
double PYDOS_API pydos_complex_extract_real(PyDosObj far *obj);
double PYDOS_API pydos_complex_extract_imag(PyDosObj far *obj);

#endif /* PDOS_CPX_H */
