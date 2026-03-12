/*
 * pydos_int.c - Integer operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * All operations extract long values, perform C arithmetic, and
 * return new int objects. Python-style floor division is used.
 */

#include "pdos_int.h"
#include "pdos_str.h"
#include "pdos_obj.h"
#include "pdos_exc.h"
#include <stdlib.h>

#include "pdos_mem.h"

/*
 * Helper: extract int_val from a PyDosObj, with type check.
 * Returns 0 if obj is null or not an int.
 */
static long get_int(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return 0L;
    if (obj->type == PYDT_INT) return obj->v.int_val;
    if (obj->type == PYDT_BOOL) return (long)obj->v.bool_val;
    return 0L;
}

PyDosObj far * PYDOS_API pydos_int_add(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_obj_new_int(get_int(a) + get_int(b));
}

PyDosObj far * PYDOS_API pydos_int_sub(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_obj_new_int(get_int(a) - get_int(b));
}

PyDosObj far * PYDOS_API pydos_int_mul(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_obj_new_int(get_int(a) * get_int(b));
}

/*
 * Python-style floor division: rounds toward negative infinity.
 * if ((a % b != 0) && ((a ^ b) < 0)) result = a/b - 1; else result = a/b;
 */
PyDosObj far * PYDOS_API pydos_int_div(PyDosObj far *a, PyDosObj far *b)
{
    long va, vb, result, remainder;

    /* Float division (true division for Python / operator) */
    if ((a != (PyDosObj far *)0 && a->type == PYDT_FLOAT) ||
        (b != (PyDosObj far *)0 && b->type == PYDT_FLOAT)) {
        double da, db;
        da = (a != (PyDosObj far *)0 && a->type == PYDT_FLOAT) ? a->v.float_val :
             (double)get_int(a);
        db = (b != (PyDosObj far *)0 && b->type == PYDT_FLOAT) ? b->v.float_val :
             (double)get_int(b);
        if (db == 0.0) {
            pydos_exc_raise(PYDOS_EXC_ZERO_DIVISION, "float division by zero");
            return pydos_obj_new_float(0.0);
        }
        return pydos_obj_new_float(da / db);
    }

    va = get_int(a);
    vb = get_int(b);

    if (vb == 0L) {
        pydos_exc_raise(PYDOS_EXC_ZERO_DIVISION, "division by zero");
        return pydos_obj_new_int(0L);  /* unreachable, but satisfy compiler */
    }

    result = va / vb;
    remainder = va % vb;

    /* Adjust for Python floor division semantics */
    if (remainder != 0L && ((va ^ vb) < 0L)) {
        result--;
    }

    return pydos_obj_new_int(result);
}

/*
 * Python 3 true division (/): always returns float.
 * int / int -> float, float / float -> float, etc.
 */
PyDosObj far * PYDOS_API pydos_int_truediv(PyDosObj far *a, PyDosObj far *b)
{
    double da, db;

    if (a != (PyDosObj far *)0 && a->type == PYDT_FLOAT)
        da = a->v.float_val;
    else
        da = (double)get_int(a);

    if (b != (PyDosObj far *)0 && b->type == PYDT_FLOAT)
        db = b->v.float_val;
    else
        db = (double)get_int(b);

    if (db == 0.0) {
        pydos_exc_raise(PYDOS_EXC_ZERO_DIVISION, "division by zero");
        return pydos_obj_new_float(0.0);
    }
    return pydos_obj_new_float(da / db);
}

/*
 * Python-style modulo: result has same sign as divisor.
 * if ((a % b != 0) && ((a ^ b) < 0)) result = a%b + b; else result = a%b;
 */
PyDosObj far * PYDOS_API pydos_int_mod(PyDosObj far *a, PyDosObj far *b)
{
    long va, vb, result;

    va = get_int(a);
    vb = get_int(b);

    if (vb == 0L) {
        pydos_exc_raise(PYDOS_EXC_ZERO_DIVISION, "integer modulo by zero");
        return pydos_obj_new_int(0L);  /* unreachable */
    }

    result = va % vb;

    /* Adjust for Python modulo semantics */
    if (result != 0L && ((va ^ vb) < 0L)) {
        result += vb;
    }

    return pydos_obj_new_int(result);
}

PyDosObj far * PYDOS_API pydos_int_neg(PyDosObj far *a)
{
    if (a != (PyDosObj far *)0 && a->type == PYDT_FLOAT) {
        return pydos_obj_new_float(-a->v.float_val);
    }
    return pydos_obj_new_int(-get_int(a));
}

PyDosObj far * PYDOS_API pydos_int_abs(PyDosObj far *a)
{
    long v;

    v = get_int(a);
    if (v < 0L) {
        v = -v;
    }
    return pydos_obj_new_int(v);
}

PyDosObj far * PYDOS_API pydos_int_pow(PyDosObj far *base, PyDosObj far *exp)
{
    long b, e, result;

    b = get_int(base);
    e = get_int(exp);

    if (e < 0L) {
        /* Negative exponent with integers: Python returns float,
         * but for Phase 1 we return 0 (truncation to int) */
        return pydos_obj_new_int(0L);
    }

    result = 1L;
    while (e > 0L) {
        if (e & 1L) {
            result *= b;
        }
        b *= b;
        e >>= 1;
    }

    return pydos_obj_new_int(result);
}

int PYDOS_API pydos_int_compare(PyDosObj far *a, PyDosObj far *b)
{
    long va, vb;

    va = get_int(a);
    vb = get_int(b);

    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

PyDosObj far * PYDOS_API pydos_int_bitand(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_obj_new_int(get_int(a) & get_int(b));
}

PyDosObj far * PYDOS_API pydos_int_bitor(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_obj_new_int(get_int(a) | get_int(b));
}

PyDosObj far * PYDOS_API pydos_int_bitxor(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_obj_new_int(get_int(a) ^ get_int(b));
}

PyDosObj far * PYDOS_API pydos_int_bitnot(PyDosObj far *a)
{
    return pydos_obj_new_int(~get_int(a));
}

PyDosObj far * PYDOS_API pydos_int_shl(PyDosObj far *a, PyDosObj far *b)
{
    long shift;

    shift = get_int(b);
    if (shift < 0L || shift > 31L) {
        return pydos_obj_new_int(0L);
    }
    return pydos_obj_new_int(get_int(a) << (int)shift);
}

PyDosObj far * PYDOS_API pydos_int_shr(PyDosObj far *a, PyDosObj far *b)
{
    long shift;

    shift = get_int(b);
    if (shift < 0L || shift > 31L) {
        return pydos_obj_new_int(0L);
    }
    return pydos_obj_new_int(get_int(a) >> (int)shift);
}

PyDosObj far * PYDOS_API pydos_int_to_str(PyDosObj far *obj)
{
    long val;

    if (obj == (PyDosObj far *)0) {
        return pydos_str_from_cstr("0");
    }

    val = get_int(obj);
    return pydos_str_format_int(val);
}

PyDosObj far * PYDOS_API pydos_int_from_str(PyDosObj far *str_obj)
{
    const char far *data;
    unsigned int len;
    unsigned int i;
    int neg;
    long result;

    if (str_obj == (PyDosObj far *)0 || str_obj->type != PYDT_STR) {
        return (PyDosObj far *)0;
    }

    data = str_obj->v.str.data;
    len = str_obj->v.str.len;
    i = 0;

    /* Skip leading whitespace */
    while (i < len && (data[i] == ' ' || data[i] == '\t' ||
                       data[i] == '\r' || data[i] == '\n')) {
        i++;
    }

    if (i >= len) {
        return (PyDosObj far *)0;
    }

    /* Handle sign */
    neg = 0;
    if (data[i] == '-') {
        neg = 1;
        i++;
    } else if (data[i] == '+') {
        i++;
    }

    if (i >= len || data[i] < '0' || data[i] > '9') {
        return (PyDosObj far *)0;
    }

    /* Accumulate digits */
    result = 0L;
    while (i < len && data[i] >= '0' && data[i] <= '9') {
        result = result * 10L + (long)(data[i] - '0');
        i++;
    }

    /* Skip trailing whitespace */
    while (i < len && (data[i] == ' ' || data[i] == '\t' ||
                       data[i] == '\r' || data[i] == '\n')) {
        i++;
    }

    /* If there are remaining non-whitespace chars, it's invalid */
    if (i < len) {
        return (PyDosObj far *)0;
    }

    if (neg) {
        result = -result;
    }

    return pydos_obj_new_int(result);
}

PyDosObj far * PYDOS_API pydos_int_bit_length(PyDosObj far *self)
{
    unsigned long v;
    long bits;

    if (self == (PyDosObj far *)0 || self->type != PYDT_INT) {
        return pydos_obj_new_int(0L);
    }
    v = (unsigned long)(self->v.int_val < 0 ? -self->v.int_val : self->v.int_val);
    bits = 0;
    while (v > 0) {
        bits++;
        v >>= 1;
    }
    return pydos_obj_new_int(bits);
}

void PYDOS_API pydos_int_init(void)
{
    /* No global state to initialize */
}

void PYDOS_API pydos_int_shutdown(void)
{
    /* No global state to clean up */
}
