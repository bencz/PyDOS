/*
 * pdos_byt.h - Immutable bytes primitive for the PyDOS runtime
 */

#ifndef PDOS_BYT_H
#define PDOS_BYT_H

#include "pdos_obj.h"

PyDosObj far * PYDOS_API pydos_bytes_new(
    const unsigned char far *data, unsigned int len);
PyDosObj far * PYDOS_API pydos_bytes_new_zeroed(unsigned int len);
PyDosObj far * PYDOS_API pydos_bytes_concat(
    PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_bytes_repeat(PyDosObj far *value, long count);
PyDosObj far * PYDOS_API pydos_bytes_slice(
    PyDosObj far *value, long start, long stop, long step);
int PYDOS_API pydos_bytes_getitem(PyDosObj far *value, long index);
int PYDOS_API pydos_bytes_compare(PyDosObj far *a, PyDosObj far *b);

PyDosObj far * PYDOS_API pydos_builtin_bytes_conv(
    int argc, PyDosObj far * far *argv);

#endif /* PDOS_BYT_H */
