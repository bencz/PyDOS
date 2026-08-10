/* pdos_rng.h - Primitive range representation operations. */

#ifndef PDOS_RNG_H
#define PDOS_RNG_H

#include "pdos_obj.h"

PyDosObj far * PYDOS_API pydos_range_new(long start, long stop, long step);
PyDosObj far * PYDOS_API pydos_range_len(PyDosObj far *range);
PyDosObj far * PYDOS_API pydos_range_getitem(PyDosObj far *range, long index);
PyDosObj far * PYDOS_API pydos_range_slice(PyDosObj far *range,
                                            long start, long stop, long step);
PyDosObj far * PYDOS_API pydos_range_slice_op(PyDosObj far *range,
                                               long start, long stop,
                                               long step);
int PYDOS_API pydos_range_contains(PyDosObj far *range, PyDosObj far *item);
int PYDOS_API pydos_range_equal(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_range_next(PyDosObj far *range_iter);

#endif /* PDOS_RNG_H */
