/*
 * pdos_bya.h - Bytearray type for PyDOS runtime
 *
 * Mutable byte sequence. Each element is an unsigned char (0-255).
 * Similar to list but restricted to bytes.
 */

#ifndef PDOS_BYA_H
#define PDOS_BYA_H

#include "pdos_obj.h"

/* Create a new empty bytearray with given capacity */
PyDosObj far * PYDOS_API pydos_bytearray_new(unsigned int initial_cap);

/* Create a bytearray filled with zero bytes */
PyDosObj far * PYDOS_API pydos_bytearray_new_zeroed(unsigned int count);

/* Create a bytearray from raw byte data */
PyDosObj far * PYDOS_API pydos_bytearray_from_data(const unsigned char far *data,
                                                    unsigned int len);

/* Append a byte (0-255) to the bytearray */
void PYDOS_API pydos_bytearray_append(PyDosObj far *ba, unsigned char byte);

/* Extend with raw bytes */
void PYDOS_API pydos_bytearray_extend(PyDosObj far *ba,
                                       const unsigned char far *data,
                                       unsigned int len);

/* Get byte at index (returns -1 on error) */
int PYDOS_API pydos_bytearray_getitem(PyDosObj far *ba, int index);

/* Set byte at index */
void PYDOS_API pydos_bytearray_setitem(PyDosObj far *ba, int index,
                                        unsigned char byte);

/* Length */
unsigned int PYDOS_API pydos_bytearray_len(PyDosObj far *ba);

/* Pop last byte (returns -1 if empty) */
int PYDOS_API pydos_bytearray_pop(PyDosObj far *ba);

/* Clear all bytes */
void PYDOS_API pydos_bytearray_clear(PyDosObj far *ba);

/* Builtin bytearray() constructor */
PyDosObj far * PYDOS_API pydos_builtin_bytearray_conv(int argc,
                                                       PyDosObj far * far *argv);

#endif /* PDOS_BYA_H */
