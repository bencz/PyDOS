/*
 * pdos_rng.c - Primitive storage and constant-time operations for range.
 *
 * Policy such as count() and index() belongs to stdlib/builtins/range.py.
 */

#include "pdos_rng.h"
#include "pdos_exc.h"
#include "pdos_slc.h"
#include <limits.h>

static unsigned long range_length_raw(long start, long stop, long step)
{
    unsigned long distance;
    unsigned long stride;

    if (step > 0L) {
        if (start >= stop) return 0UL;
        distance = (unsigned long)stop - (unsigned long)start - 1UL;
        return distance / (unsigned long)step + 1UL;
    }

    if (start <= stop) return 0UL;
    distance = (unsigned long)start - (unsigned long)stop - 1UL;
    stride = 0UL - (unsigned long)step;
    return distance / stride + 1UL;
}

static long range_value_at(PyDosObj far *range, unsigned long index)
{
    unsigned long value;

    value = (unsigned long)range->v.range.start;
    value += index * (unsigned long)range->v.range.step;
    return (long)value;
}

PyDosObj far * PYDOS_API pydos_range_new(long start, long stop, long step)
{
    PyDosObj far *obj;

    if (step == 0L) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"range() arg 3 must not be zero");
        return (PyDosObj far *)0;
    }

    obj = pydos_obj_alloc_type(PYDT_RANGE);
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    obj->v.range.start = start;
    obj->v.range.stop = stop;
    obj->v.range.step = step;
    obj->v.range.current = start;
    return obj;
}

PyDosObj far * PYDOS_API pydos_range_len(PyDosObj far *range)
{
    unsigned long length;

    if (range == (PyDosObj far *)0 ||
        (PyDosType)range->type != PYDT_RANGE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"expected a range object");
        return (PyDosObj far *)0;
    }

    length = range_length_raw(range->v.range.start, range->v.range.stop,
                              range->v.range.step);
    if (length > (unsigned long)LONG_MAX) {
        pydos_exc_raise(PYDOS_EXC_OVERFLOW,
                        (const char far *)"range object has too many items");
        return (PyDosObj far *)0;
    }
    return pydos_obj_new_int((long)length);
}

PyDosObj far * PYDOS_API pydos_range_getitem(PyDosObj far *range, long index)
{
    unsigned long length;

    if (range == (PyDosObj far *)0 ||
        (PyDosType)range->type != PYDT_RANGE) return (PyDosObj far *)0;

    length = range_length_raw(range->v.range.start, range->v.range.stop,
                              range->v.range.step);
    if (index < 0L) {
        unsigned long magnitude = 0UL - (unsigned long)index;
        if (magnitude > length) {
            pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                            (const char far *)"range object index out of range");
            return (PyDosObj far *)0;
        }
        index = (long)(length - magnitude);
    }
    if ((unsigned long)index >= length) {
        pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                        (const char far *)"range object index out of range");
        return (PyDosObj far *)0;
    }
    return pydos_obj_new_int(range_value_at(range, (unsigned long)index));
}

PyDosObj far * PYDOS_API pydos_range_slice(PyDosObj far *range,
                                           long start, long stop, long step)
{
    unsigned long raw_length;
    long length;
    long slice_length;
    long new_start;
    long new_stop;
    long new_step;

    if (range == (PyDosObj far *)0 ||
        (PyDosType)range->type != PYDT_RANGE) return (PyDosObj far *)0;
    if (step == 0L) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"slice step cannot be zero");
        return (PyDosObj far *)0;
    }

    raw_length = range_length_raw(range->v.range.start, range->v.range.stop,
                                  range->v.range.step);
    if (raw_length > (unsigned long)LONG_MAX) {
        pydos_exc_raise(PYDOS_EXC_OVERFLOW,
                        (const char far *)"range object has too many items");
        return (PyDosObj far *)0;
    }
    length = (long)raw_length;
    pydos_slice_normalize(length, &start, &stop, step);

    if (step > 0L) {
        slice_length = start < stop ? (stop - start - 1L) / step + 1L : 0L;
    } else {
        unsigned long stride = 0UL - (unsigned long)step;
        slice_length = start > stop
            ? (long)(((unsigned long)start - (unsigned long)stop - 1UL) /
                     stride + 1UL)
            : 0L;
    }

    new_step = (long)((unsigned long)range->v.range.step *
                      (unsigned long)step);
    if (slice_length == 0L) {
        new_start = range->v.range.start;
        new_stop = new_start;
    } else {
        new_start = range_value_at(range, (unsigned long)start);
        new_stop = (long)((unsigned long)new_start +
                          (unsigned long)slice_length *
                          (unsigned long)new_step);
    }
    return pydos_range_new(new_start, new_stop, new_step);
}

PyDosObj far * PYDOS_API pydos_range_slice_op(PyDosObj far *range,
                                               long start, long stop,
                                               long step)
{
    if (range == (PyDosObj far *)0 || range->type != PYDT_RANGE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"slicing requires a range");
        return (PyDosObj far *)0;
    }
    return pydos_range_slice(range, start, stop, step);
}

int PYDOS_API pydos_range_contains(PyDosObj far *range, PyDosObj far *item)
{
    long value;
    long start;
    long stop;
    long step;
    unsigned long delta;
    unsigned long stride;

    if (range == (PyDosObj far *)0 || item == (PyDosObj far *)0 ||
        (PyDosType)range->type != PYDT_RANGE) return 0;

    if ((PyDosType)item->type == PYDT_INT) {
        value = item->v.int_val;
    } else if ((PyDosType)item->type == PYDT_BOOL) {
        value = (long)item->v.bool_val;
    } else if ((PyDosType)item->type == PYDT_FLOAT) {
        double number = item->v.float_val;
        if (number < (double)LONG_MIN || number > (double)LONG_MAX) return 0;
        value = (long)number;
        if ((double)value != number) return 0;
    } else if ((PyDosType)item->type == PYDT_COMPLEX) {
        double number;
        if (item->v.complex_val.imag != 0.0) return 0;
        number = item->v.complex_val.real;
        if (number < (double)LONG_MIN || number > (double)LONG_MAX) return 0;
        value = (long)number;
        if ((double)value != number) return 0;
    } else {
        return 0;
    }

    start = range->v.range.start;
    stop = range->v.range.stop;
    step = range->v.range.step;
    if (step > 0L) {
        if (value < start || value >= stop) return 0;
        delta = (unsigned long)value - (unsigned long)start;
        stride = (unsigned long)step;
    } else {
        if (value > start || value <= stop) return 0;
        delta = (unsigned long)start - (unsigned long)value;
        stride = 0UL - (unsigned long)step;
    }
    return delta % stride == 0UL;
}

int PYDOS_API pydos_range_equal(PyDosObj far *a, PyDosObj far *b)
{
    unsigned long a_length;
    unsigned long b_length;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0 ||
        (PyDosType)a->type != PYDT_RANGE ||
        (PyDosType)b->type != PYDT_RANGE) return 0;
    a_length = range_length_raw(a->v.range.start, a->v.range.stop,
                                a->v.range.step);
    b_length = range_length_raw(b->v.range.start, b->v.range.stop,
                                b->v.range.step);
    if (a_length != b_length) return 0;
    if (a_length == 0UL) return 1;
    if (a->v.range.start != b->v.range.start) return 0;
    if (a_length == 1UL) return 1;
    return a->v.range.step == b->v.range.step;
}

PyDosObj far * PYDOS_API pydos_range_next(PyDosObj far *range_iter)
{
    long current;
    long stop;
    long step;

    if (range_iter == (PyDosObj far *)0 ||
        (PyDosType)range_iter->type != PYDT_RANGE) return (PyDosObj far *)0;
    current = range_iter->v.range.current;
    stop = range_iter->v.range.stop;
    step = range_iter->v.range.step;

    if (step > 0L && current < stop) {
        unsigned long remaining = (unsigned long)stop - (unsigned long)current;
        range_iter->v.range.current = remaining <= (unsigned long)step
            ? stop
            : current + step;
        return pydos_obj_new_int(current);
    }
    if (step < 0L && current > stop) {
        unsigned long remaining = (unsigned long)current - (unsigned long)stop;
        unsigned long stride = 0UL - (unsigned long)step;
        range_iter->v.range.current = remaining <= stride
            ? stop
            : current + step;
        return pydos_obj_new_int(current);
    }
    return (PyDosObj far *)0;
}
