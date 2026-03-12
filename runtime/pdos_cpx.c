/*
 * pdos_cpx.c - Complex number type implementation for PyDOS runtime
 *
 * Complex numbers: (real, imag) pairs with full arithmetic.
 * No heap data beyond the PyDosObj itself (real/imag stored inline).
 */

#include "pdos_cpx.h"
#include "pdos_mem.h"
#include <math.h>

/* ------------------------------------------------------------------ */
/* Helper: extract a double value from a numeric PyDosObj              */
/* ------------------------------------------------------------------ */
static double extract_double(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return 0.0;
    switch ((PyDosType)obj->type) {
    case PYDT_INT:   return (double)obj->v.int_val;
    case PYDT_FLOAT: return obj->v.float_val;
    case PYDT_BOOL:  return (double)obj->v.bool_val;
    default:         return 0.0;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_complex_new — create a complex object                         */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_new(double real, double imag)
{
    PyDosObj far *obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    obj->type = (unsigned char)PYDT_COMPLEX;
    obj->v.complex_val.real = real;
    obj->v.complex_val.imag = imag;
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_complex_add                                                   */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_add(PyDosObj far *a, PyDosObj far *b)
{
    double ar, ai, br, bi;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return pydos_complex_new(0.0, 0.0);

    if ((PyDosType)a->type == PYDT_COMPLEX) {
        ar = a->v.complex_val.real;
        ai = a->v.complex_val.imag;
    } else {
        ar = extract_double(a);
        ai = 0.0;
    }
    if ((PyDosType)b->type == PYDT_COMPLEX) {
        br = b->v.complex_val.real;
        bi = b->v.complex_val.imag;
    } else {
        br = extract_double(b);
        bi = 0.0;
    }
    return pydos_complex_new(ar + br, ai + bi);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_sub                                                   */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_sub(PyDosObj far *a, PyDosObj far *b)
{
    double ar, ai, br, bi;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return pydos_complex_new(0.0, 0.0);

    if ((PyDosType)a->type == PYDT_COMPLEX) {
        ar = a->v.complex_val.real;
        ai = a->v.complex_val.imag;
    } else {
        ar = extract_double(a);
        ai = 0.0;
    }
    if ((PyDosType)b->type == PYDT_COMPLEX) {
        br = b->v.complex_val.real;
        bi = b->v.complex_val.imag;
    } else {
        br = extract_double(b);
        bi = 0.0;
    }
    return pydos_complex_new(ar - br, ai - bi);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_mul                                                   */
/* (a.r*b.r - a.i*b.i, a.r*b.i + a.i*b.r)                            */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_mul(PyDosObj far *a, PyDosObj far *b)
{
    double ar, ai, br, bi;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return pydos_complex_new(0.0, 0.0);

    if ((PyDosType)a->type == PYDT_COMPLEX) {
        ar = a->v.complex_val.real;
        ai = a->v.complex_val.imag;
    } else {
        ar = extract_double(a);
        ai = 0.0;
    }
    if ((PyDosType)b->type == PYDT_COMPLEX) {
        br = b->v.complex_val.real;
        bi = b->v.complex_val.imag;
    } else {
        br = extract_double(b);
        bi = 0.0;
    }
    return pydos_complex_new(ar * br - ai * bi, ar * bi + ai * br);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_div                                                   */
/* d = b.r^2 + b.i^2                                                  */
/* result = ((a.r*b.r + a.i*b.i)/d, (a.i*b.r - a.r*b.i)/d)           */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_div(PyDosObj far *a, PyDosObj far *b)
{
    double ar, ai, br, bi, d;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return pydos_complex_new(0.0, 0.0);

    if ((PyDosType)a->type == PYDT_COMPLEX) {
        ar = a->v.complex_val.real;
        ai = a->v.complex_val.imag;
    } else {
        ar = extract_double(a);
        ai = 0.0;
    }
    if ((PyDosType)b->type == PYDT_COMPLEX) {
        br = b->v.complex_val.real;
        bi = b->v.complex_val.imag;
    } else {
        br = extract_double(b);
        bi = 0.0;
    }

    d = br * br + bi * bi;
    if (d == 0.0) {
        /* ZeroDivisionError — return 0+0j as fallback */
        return pydos_complex_new(0.0, 0.0);
    }
    return pydos_complex_new((ar * br + ai * bi) / d,
                              (ai * br - ar * bi) / d);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_neg — unary negation                                  */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_neg(PyDosObj far *a)
{
    if (a == (PyDosObj far *)0) return pydos_complex_new(0.0, 0.0);
    return pydos_complex_new(-a->v.complex_val.real, -a->v.complex_val.imag);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_pos — unary positive (identity)                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_pos(PyDosObj far *a)
{
    if (a == (PyDosObj far *)0) return pydos_complex_new(0.0, 0.0);
    return pydos_complex_new(a->v.complex_val.real, a->v.complex_val.imag);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_abs — magnitude as float: sqrt(r^2 + i^2)            */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_abs(PyDosObj far *a)
{
    double r, i;
    if (a == (PyDosObj far *)0) return pydos_obj_new_float(0.0);
    r = a->v.complex_val.real;
    i = a->v.complex_val.imag;
    return pydos_obj_new_float(sqrt(r * r + i * i));
}

/* ------------------------------------------------------------------ */
/* pydos_complex_conjugate — (r, -i)                                   */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_complex_conjugate(PyDosObj far *a)
{
    if (a == (PyDosObj far *)0) return pydos_complex_new(0.0, 0.0);
    return pydos_complex_new(a->v.complex_val.real, -a->v.complex_val.imag);
}

/* ------------------------------------------------------------------ */
/* pydos_complex_extract_real/imag — helpers for .real/.imag access     */
/* ------------------------------------------------------------------ */
double PYDOS_API pydos_complex_extract_real(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return 0.0;
    if ((PyDosType)obj->type == PYDT_COMPLEX) return obj->v.complex_val.real;
    return extract_double(obj);
}

double PYDOS_API pydos_complex_extract_imag(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return 0.0;
    if ((PyDosType)obj->type == PYDT_COMPLEX) return obj->v.complex_val.imag;
    return 0.0;
}

/* ------------------------------------------------------------------ */
/* pydos_builtin_complex_conv — complex() constructor                  */
/* complex()       -> 0+0j                                             */
/* complex(real)   -> real+0j   (int/float/bool)                       */
/* complex(r, i)   -> r+ij                                             */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_builtin_complex_conv(int argc,
                                                     PyDosObj far * far *argv)
{
    double r, i;

    if (argc == 0) {
        return pydos_complex_new(0.0, 0.0);
    }
    if (argc == 1) {
        PyDosObj far *src = argv[0];
        if (src == (PyDosObj far *)0)
            return pydos_complex_new(0.0, 0.0);
        if ((PyDosType)src->type == PYDT_COMPLEX) {
            PYDOS_INCREF(src);
            return src;
        }
        r = extract_double(src);
        return pydos_complex_new(r, 0.0);
    }
    if (argc >= 2) {
        r = extract_double(argv[0]);
        i = extract_double(argv[1]);
        return pydos_complex_new(r, i);
    }

    return pydos_complex_new(0.0, 0.0);
}
