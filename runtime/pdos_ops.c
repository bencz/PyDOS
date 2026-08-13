/*
 * pdos_ops.c - Polymorphic binary operators for PyDOS.
 *
 * Split from pdos_obj.c: that module's code segment sits at the 64 KB
 * large-model limit, so the whole binary-operator family lives here —
 * arithmetic, matmul, bitwise, shifts, augmented assignment and the
 * instance-dunder dispatcher they share.
 *
 * Dispatch order is uniform: type-specific fast paths, then instance
 * dunders (direct and reflected, NotImplemented falls through), then
 * TypeError.
 */

#include "pdos_ops.h"
#include "pdos_int.h"
#include "pdos_str.h"
#include "pdos_byt.h"
#include "pdos_bya.h"
#include "pdos_lst.h"
#include "pdos_cpx.h"
#include "pdos_exc.h"
#include "pdos_vtb.h"

typedef PyDosObj far * (PYDOS_API far *PyDosBinOp)(PyDosObj far *,
                                                   PyDosObj far *);

PyDosObj far * PYDOS_API pydos_obj_binary_dispatch(PyDosObj far *left,
                                                   PyDosObj far *right,
                                                   int left_slot,
                                                   int right_slot)
{
    PyDosObj far *result;
    PyDosBinOp operation;
    if ((PyDosType)left->type == PYDT_INSTANCE &&
        left->v.instance.vtable != (PyDosVTable far *)0) {
        operation = (PyDosBinOp)pydos_vtable_get_special(
            left->v.instance.vtable, (unsigned int)left_slot);
    } else {
        operation = (PyDosBinOp)0;
    }
    if (operation != (PyDosBinOp)0) {
        result = operation(left, right);
        if (result == (PyDosObj far *)0 && pydos_exc_pending())
            return (PyDosObj far *)0;
        if (result != (PyDosObj far *)0 &&
            (PyDosType)result->type != PYDT_NOTIMPLEMENTED)
            return result;
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
    }
    if ((PyDosType)right->type == PYDT_INSTANCE &&
        right->v.instance.vtable != (PyDosVTable far *)0) {
        operation = (PyDosBinOp)pydos_vtable_get_special(
            right->v.instance.vtable, (unsigned int)right_slot);
    } else {
        operation = (PyDosBinOp)0;
    }
    if (operation != (PyDosBinOp)0) {
        result = operation(right, left);
        if (result == (PyDosObj far *)0 && pydos_exc_pending())
            return (PyDosObj far *)0;
        if (result != (PyDosObj far *)0 &&
            (PyDosType)result->type != PYDT_NOTIMPLEMENTED)
            return result;
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
    }
    return (PyDosObj far *)0;
}

static PyDosObj far *unsupported_binary(const char far *message)
{
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR, message);
    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_add — polymorphic + operator                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_add(PyDosObj far *a, PyDosObj far *b)
{
    unsigned char ta, tb;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    ta = a->type;
    tb = b->type;

    /* Both int or bool: integer addition */
    if ((ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        return pydos_int_add(a, b);
    }

    /* Float arithmetic (including int+float promotion) */
    if ((ta == PYDT_FLOAT || ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_FLOAT || tb == PYDT_INT || tb == PYDT_BOOL) &&
        (ta == PYDT_FLOAT || tb == PYDT_FLOAT)) {
        double da = (ta == PYDT_FLOAT) ? a->v.float_val :
                    (ta == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
        double db = (tb == PYDT_FLOAT) ? b->v.float_val :
                    (tb == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
        return pydos_obj_new_float(da + db);
    }

    /* Complex arithmetic */
    if (ta == PYDT_COMPLEX || tb == PYDT_COMPLEX) {
        return pydos_complex_add(a, b);
    }

    /* Either is a string: concatenate (coerce non-str via to_str) */
    if (ta == PYDT_STR || tb == PYDT_STR) {
        PyDosObj far *sa;
        PyDosObj far *sb;
        PyDosObj far *result;

        sa = (ta == PYDT_STR) ? a : pydos_obj_to_str(a);
        sb = (tb == PYDT_STR) ? b : pydos_obj_to_str(b);
        result = pydos_str_concat(sa, sb);
        if (ta != PYDT_STR) { PYDOS_DECREF(sa); }
        if (tb != PYDT_STR) { PYDOS_DECREF(sb); }
        return result;
    }

    if (ta == PYDT_BYTES && tb == PYDT_BYTES) {
        return pydos_bytes_concat(a, b);
    }

    if (ta == PYDT_BYTEARRAY && tb == PYDT_BYTEARRAY) {
        return pydos_bytearray_concat(a, b);
    }

    if (ta == PYDT_TUPLE && tb == PYDT_TUPLE) {
        return pydos_tuple_concat(a, b);
    }

    if (ta == PYDT_LIST && tb == PYDT_LIST) {
        return pydos_list_concat(a, b);
    }

    {
        PyDosObj far *result = pydos_obj_binary_dispatch(
            a, b, VSLOT_ADD, VSLOT_RADD);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for +");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_sub — polymorphic - operator                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_sub(PyDosObj far *a, PyDosObj far *b)
{
    unsigned char ta, tb;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    ta = a->type;
    tb = b->type;

    if ((ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        return pydos_int_sub(a, b);
    }

    /* Float arithmetic (including int-float promotion) */
    if ((ta == PYDT_FLOAT || ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_FLOAT || tb == PYDT_INT || tb == PYDT_BOOL) &&
        (ta == PYDT_FLOAT || tb == PYDT_FLOAT)) {
        double da = (ta == PYDT_FLOAT) ? a->v.float_val :
                    (ta == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
        double db = (tb == PYDT_FLOAT) ? b->v.float_val :
                    (tb == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
        return pydos_obj_new_float(da - db);
    }

    /* Complex arithmetic */
    if (ta == PYDT_COMPLEX || tb == PYDT_COMPLEX) {
        return pydos_complex_sub(a, b);
    }

    {
        PyDosObj far *result = pydos_obj_binary_dispatch(
            a, b, VSLOT_SUB, VSLOT_RSUB);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for -");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_mul — polymorphic * operator                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_mul(PyDosObj far *a, PyDosObj far *b)
{
    unsigned char ta, tb;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    ta = a->type;
    tb = b->type;

    if ((ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        return pydos_int_mul(a, b);
    }

    /* Float arithmetic (including int*float promotion) */
    if ((ta == PYDT_FLOAT || ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_FLOAT || tb == PYDT_INT || tb == PYDT_BOOL) &&
        (ta == PYDT_FLOAT || tb == PYDT_FLOAT)) {
        double da = (ta == PYDT_FLOAT) ? a->v.float_val :
                    (ta == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
        double db = (tb == PYDT_FLOAT) ? b->v.float_val :
                    (tb == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
        return pydos_obj_new_float(da * db);
    }

    /* Complex arithmetic */
    if (ta == PYDT_COMPLEX || tb == PYDT_COMPLEX) {
        return pydos_complex_mul(a, b);
    }

    if (ta == PYDT_BYTES && (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_bytes_repeat(a, count);
    }
    if (tb == PYDT_BYTES && (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_bytes_repeat(b, count);
    }
    if (ta == PYDT_BYTEARRAY && (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_bytearray_repeat(a, count);
    }
    if (tb == PYDT_BYTEARRAY && (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_bytearray_repeat(b, count);
    }
    if (ta == PYDT_STR && (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_str_repeat(a, count);
    }
    if (tb == PYDT_STR && (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_str_repeat(b, count);
    }
    if ((ta == PYDT_LIST || ta == PYDT_TUPLE) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_seq_repeat(a, count);
    }
    if ((tb == PYDT_LIST || tb == PYDT_TUPLE) &&
        (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_seq_repeat(b, count);
    }

    {
        PyDosObj far *result = pydos_obj_binary_dispatch(
            a, b, VSLOT_MUL, VSLOT_RMUL);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for *");
}

PyDosObj far * PYDOS_API pydos_obj_floordiv(PyDosObj far *a,
                                             PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for //");
    result = pydos_obj_binary_dispatch(a, b, VSLOT_FLOORDIV, VSLOT_RFLOORDIV);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL ||
         (PyDosType)a->type == PYDT_FLOAT) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL ||
         (PyDosType)b->type == PYDT_FLOAT))
        return pydos_int_div(a, b);
    return unsupported_binary((const char far *)"unsupported operands for //");
}

PyDosObj far * PYDOS_API pydos_obj_truediv(PyDosObj far *a,
                                            PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for /");
    result = pydos_obj_binary_dispatch(a, b, VSLOT_TRUEDIV, VSLOT_RTRUEDIV);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if ((PyDosType)a->type == PYDT_COMPLEX ||
        (PyDosType)b->type == PYDT_COMPLEX)
        return pydos_complex_div(a, b);
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL ||
         (PyDosType)a->type == PYDT_FLOAT) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL ||
         (PyDosType)b->type == PYDT_FLOAT))
        return pydos_int_truediv(a, b);
    return unsupported_binary((const char far *)"unsupported operands for /");
}

PyDosObj far * PYDOS_API pydos_obj_mod(PyDosObj far *a, PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for %");
    result = pydos_obj_binary_dispatch(a, b, VSLOT_MOD, VSLOT_RMOD);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL ||
         (PyDosType)a->type == PYDT_FLOAT) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL ||
         (PyDosType)b->type == PYDT_FLOAT))
        return pydos_int_mod(a, b);
    return unsupported_binary((const char far *)"unsupported operands for %");
}

PyDosObj far * PYDOS_API pydos_obj_pow(PyDosObj far *a, PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for **");
    result = pydos_obj_binary_dispatch(a, b, VSLOT_POW, VSLOT_RPOW);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL))
        return pydos_int_pow(a, b);
    return unsupported_binary((const char far *)"unsupported operands for **");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_matmul — @ operator dispatch                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_matmul(PyDosObj far *a, PyDosObj far *b)
{
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    {
        PyDosObj far *result = pydos_obj_binary_dispatch(
            a, b, VSLOT_MATMUL, VSLOT_RMATMUL);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for @");
}

/* ------------------------------------------------------------------ */
/* Polymorphic bitwise / shift operators                               */
/* ------------------------------------------------------------------ */
static PyDosObj far *bitwise_dispatch(PyDosObj far *a, PyDosObj far *b,
                                      int left_slot, int right_slot,
                                      PyDosBinOp int_op,
                                      const char far *error_text)
{
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL)) {
        return int_op(a, b);
    }

    {
        PyDosObj far *result = pydos_obj_binary_dispatch(
            a, b, left_slot, right_slot);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary(error_text);
}

PyDosObj far * PYDOS_API pydos_obj_bitand(PyDosObj far *a, PyDosObj far *b)
{
    return bitwise_dispatch(a, b, VSLOT_AND, VSLOT_RAND, pydos_int_bitand,
                            (const char far *)"unsupported operands for &");
}

PyDosObj far * PYDOS_API pydos_obj_bitor(PyDosObj far *a, PyDosObj far *b)
{
    return bitwise_dispatch(a, b, VSLOT_OR, VSLOT_ROR, pydos_int_bitor,
                            (const char far *)"unsupported operands for |");
}

PyDosObj far * PYDOS_API pydos_obj_bitxor(PyDosObj far *a, PyDosObj far *b)
{
    return bitwise_dispatch(a, b, VSLOT_XOR, VSLOT_RXOR, pydos_int_bitxor,
                            (const char far *)"unsupported operands for ^");
}

PyDosObj far * PYDOS_API pydos_obj_lshift(PyDosObj far *a, PyDosObj far *b)
{
    return bitwise_dispatch(a, b, VSLOT_LSHIFT, VSLOT_RLSHIFT, pydos_int_shl,
                            (const char far *)"unsupported operands for <<");
}

PyDosObj far * PYDOS_API pydos_obj_rshift(PyDosObj far *a, PyDosObj far *b)
{
    return bitwise_dispatch(a, b, VSLOT_RSHIFT, VSLOT_RRSHIFT, pydos_int_shr,
                            (const char far *)"unsupported operands for >>");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_inplace — in-place operator dispatch                      */
/* op: 0=add,1=sub,2=mul,3=floordiv,4=truediv,5=mod,6=pow,            */
/*     7=and,8=or,9=xor,10=lshift,11=rshift,12=matmul                 */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_inplace(PyDosObj far *a, PyDosObj far *b,
                                            int op)
{
    static const int iplace_slots[] = {
        VSLOT_IADD, VSLOT_ISUB, VSLOT_IMUL, VSLOT_IFLOORDIV,
        VSLOT_ITRUEDIV, VSLOT_IMOD, VSLOT_IPOW,
        VSLOT_IAND, VSLOT_IOR, VSLOT_IXOR,
        VSLOT_ILSHIFT, VSLOT_IRSHIFT, VSLOT_IMATMUL
    };
    int slot_idx;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    if (op < 0 || op > 12) {
        return pydos_obj_new_int(0L);
    }

    /* Try a.__iadd__(b) etc via vtable */
    slot_idx = iplace_slots[op];
    if ((PyDosType)a->type == PYDT_INSTANCE &&
        a->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosBinOp operation = (PyDosBinOp)pydos_vtable_get_special(
            a->v.instance.vtable, (unsigned int)slot_idx);
        if (operation != (PyDosBinOp)0) return operation(a, b);
    }

    /* Fallback to the regular binary op, which handles the int fast path
     * and dispatches __add__/__and__ etc. (and reflected forms) when the
     * in-place dunder is absent — CPython's augmented-assign semantics. */
    switch (op) {
    case 0:  return pydos_obj_add(a, b);
    case 1:  return pydos_obj_sub(a, b);
    case 2:  return pydos_obj_mul(a, b);
    case 3:  return pydos_obj_floordiv(a, b);
    case 4:  return pydos_obj_truediv(a, b);
    case 5:  return pydos_obj_mod(a, b);
    case 6:  return pydos_obj_pow(a, b);
    case 7:  return pydos_obj_bitand(a, b);
    case 8:  return pydos_obj_bitor(a, b);
    case 9:  return pydos_obj_bitxor(a, b);
    case 10: return pydos_obj_lshift(a, b);
    case 11: return pydos_obj_rshift(a, b);
    case 12: return pydos_obj_matmul(a, b);
    default: break;
    }

    return pydos_obj_new_int(0L);
}
