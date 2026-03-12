/*
 * pydos_obj.c - Universal object type implementation for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_obj.h"
#include "pdos_mem.h"
#include "pdos_dic.h"
#include "pdos_str.h"
#include "pdos_int.h"
#include "pdos_lst.h"
#include "pdos_vtb.h"
#include "pdos_exc.h"
#include "pdos_gen.h"
#include "pdos_fzs.h"
#include "pdos_cpx.h"
#include "pdos_bya.h"
#ifndef PYDOS_32BIT
#include <malloc.h>
#endif
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef PYDOS_DEBUG_CMP
#include "pdos_io.h"
/* Unbuffered debug print via INT 21h — bypasses C library stdio */
static void dbg_puts(const char *s)
{
    while (*s) {
        pydos_dos_putchar(*s);
        s++;
    }
}
static void dbg_putlong(long v)
{
    char buf[12];
    int i = 0;
    unsigned long uv;
    if (v < 0) {
        pydos_dos_putchar('-');
        uv = (unsigned long)(-(v + 1)) + 1UL;
    } else {
        uv = (unsigned long)v;
    }
    if (uv == 0) {
        pydos_dos_putchar('0');
        return;
    }
    while (uv > 0) {
        buf[i++] = (char)('0' + (int)(uv % 10));
        uv /= 10;
    }
    while (i > 0) {
        pydos_dos_putchar(buf[--i]);
    }
}
static void dbg_putint(int v)
{
    dbg_putlong((long)v);
}
#endif

/* ------------------------------------------------------------------ */
/* Small integer cache: values -1 .. 127  (129 entries)                */
/* ------------------------------------------------------------------ */
#define SMALL_INT_MIN   (-1)
#define SMALL_INT_MAX   127
#define SMALL_INT_COUNT 129  /* 127 - (-1) + 1 */

static PyDosObj small_ints[SMALL_INT_COUNT];
static int small_ints_ready = 0;

/* ------------------------------------------------------------------ */
/* Singletons: None, True, False                                       */
/* ------------------------------------------------------------------ */
static PyDosObj singleton_none;
static PyDosObj singleton_true;
static PyDosObj singleton_false;

/* ------------------------------------------------------------------ */
/* Free list for quick re-use of PyDosObj allocations                  */
/* ------------------------------------------------------------------ */
#define FREE_LIST_MAX 256

static PyDosObj far *free_list[FREE_LIST_MAX];
static unsigned int  free_count = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* DJB2 hash for far string data */
static unsigned int djb2_hash(const char far *data, unsigned int len)
{
    unsigned int h = 5381U;
    unsigned int i;
    for (i = 0; i < len; i++) {
        h = ((h << 5) + h) + (unsigned char)data[i];  /* h * 33 + c */
    }
    return h;
}

/* Long-to-string helper.  Writes into caller-supplied far buffer.
   Returns number of characters written (excluding NUL). */
static unsigned int ltoa_far(long val, char far *buf, unsigned int bufsz)
{
    char tmp[12];  /* enough for -2147483648\0 */
    unsigned int len;
    unsigned int i;

    ltoa(val, tmp, 10);
    len = (unsigned int)strlen(tmp);
    if (len >= bufsz) {
        len = bufsz - 1;
    }
    for (i = 0; i <= len; i++) {   /* copy including NUL */
        buf[i] = tmp[i];
    }
    return len;
}

/* Double-to-string helper. */
static unsigned int dtoa_far(double val, char far *buf, unsigned int bufsz)
{
    char tmp[32];
    unsigned int len;
    unsigned int i;

    sprintf(tmp, "%.12g", val);
    len = (unsigned int)strlen(tmp);
    if (len >= bufsz) {
        len = bufsz - 1;
    }
    for (i = 0; i <= len; i++) {
        buf[i] = tmp[i];
    }
    return len;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_alloc — allocate a blank object                           */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_alloc(void)
{
    PyDosObj far *obj;

    /* Try the free list first */
    if (free_count > 0) {
        free_count--;
        obj = free_list[free_count];
        _fmemset(obj, 0, sizeof(PyDosObj));
        obj->refcount = 1;
        return obj;
    }

    /* Allocate from far heap */
    obj = (PyDosObj far *)pydos_far_alloc((unsigned long)sizeof(PyDosObj));
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    _fmemset(obj, 0, sizeof(PyDosObj));
    obj->refcount = 1;
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_release_data — free only internal/child data, not obj     */
/* Called by pydos_obj_free and by the GC sweep (which frees the       */
/* GCHeader+PyDosObj block separately).                                */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_release_data(PyDosObj far *obj)
{
    unsigned int i;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    switch ((PyDosType)obj->type) {
    case PYDT_STR:
        if (obj->v.str.data != (char far *)0) {
            pydos_far_free(obj->v.str.data);
        }
        break;

    case PYDT_LIST:
        if (obj->v.list.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.list.len; i++) {
                PYDOS_DECREF(obj->v.list.items[i]);
            }
            pydos_far_free(obj->v.list.items);
        }
        break;

    case PYDT_DICT:
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    PYDOS_DECREF(obj->v.dict.entries[i].key);
                    PYDOS_DECREF(obj->v.dict.entries[i].value);
                }
            }
            pydos_far_free(obj->v.dict.entries);
        }
        break;

    case PYDT_TUPLE:
        if (obj->v.tuple.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.tuple.len; i++) {
                PYDOS_DECREF(obj->v.tuple.items[i]);
            }
            pydos_far_free(obj->v.tuple.items);
        }
        break;

    case PYDT_INSTANCE:
        if (obj->v.instance.attrs != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.instance.attrs);
        }
        if (obj->v.instance.cls != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.instance.cls);
        }
        break;

    case PYDT_FUNCTION:
        if (obj->v.func.defaults != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.defaults);
        }
        if (obj->v.func.closure != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.closure);
        }
        break;

    case PYDT_CELL:
        if (obj->v.cell.value != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.cell.value);
        }
        break;

    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
        if (obj->v.gen.state != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.gen.state);
        }
        if (obj->v.gen.locals != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.gen.locals);
        }
        break;

    case PYDT_EXCEPTION:
        if (obj->v.exc.message != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.message);
        }
        if (obj->v.exc.traceback != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.traceback);
        }
        if (obj->v.exc.cause != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.cause);
        }
        break;

    case PYDT_CLASS:
        if (obj->v.cls.bases != (PyDosObj far * far *)0) {
            for (i = 0; i < (unsigned int)obj->v.cls.num_bases; i++) {
                PYDOS_DECREF(obj->v.cls.bases[i]);
            }
            pydos_far_free(obj->v.cls.bases);
        }
        if (obj->v.cls.class_attrs != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.cls.class_attrs);
        }
        break;

    case PYDT_FILE:
        if (obj->v.file.buffer != (char far *)0) {
            pydos_far_free(obj->v.file.buffer);
        }
        break;

    case PYDT_EXC_GROUP:
        if (obj->v.excgroup.message != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.excgroup.message);
        }
        if (obj->v.excgroup.exceptions != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.excgroup.count; i++) {
                PYDOS_DECREF(obj->v.excgroup.exceptions[i]);
            }
            pydos_far_free(obj->v.excgroup.exceptions);
        }
        break;

    case PYDT_SET:
        /* Set uses same dict layout — free keys (values are None singletons) */
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    PYDOS_DECREF(obj->v.dict.entries[i].key);
                    PYDOS_DECREF(obj->v.dict.entries[i].value);
                }
            }
            pydos_far_free(obj->v.dict.entries);
        }
        break;

    case PYDT_FROZENSET:
        if (obj->v.frozenset.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.frozenset.len; i++) {
                PYDOS_DECREF(obj->v.frozenset.items[i]);
            }
            pydos_far_free(obj->v.frozenset.items);
        }
        break;

    case PYDT_BYTEARRAY:
        if (obj->v.bytearray.data != (unsigned char far *)0) {
            pydos_far_free(obj->v.bytearray.data);
        }
        break;

    case PYDT_RANGE:
    case PYDT_NONE:
    case PYDT_BOOL:
    case PYDT_INT:
    case PYDT_FLOAT:
    case PYDT_COMPLEX:
        /* No internal heap data to free */
        break;

    case PYDT_BYTES:
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_free — release an object and its internal data            */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_free(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return;
    }

    /* Never free immortal objects */
    if (obj->flags & OBJ_FLAG_IMMORTAL) {
        return;
    }

    /* Free type-specific internal data */
    pydos_obj_release_data(obj);

    /* Return to free list or release back to far heap */
    if (free_count < FREE_LIST_MAX) {
        free_list[free_count] = obj;
        free_count++;
    } else {
        pydos_far_free(obj);
    }
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

PyDosObj far * PYDOS_API pydos_obj_new_none(void)
{
    PYDOS_INCREF((PyDosObj far *)&singleton_none);
    return (PyDosObj far *)&singleton_none;
}

PyDosObj far * PYDOS_API pydos_obj_new_bool(int val)
{
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[BOOL ");
    dbg_putint(val);
    dbg_puts("]\r\n");
#endif
    if (val) {
        PYDOS_INCREF((PyDosObj far *)&singleton_true);
        return (PyDosObj far *)&singleton_true;
    }
    PYDOS_INCREF((PyDosObj far *)&singleton_false);
    return (PyDosObj far *)&singleton_false;
}

PyDosObj far * PYDOS_API pydos_obj_new_int(long val)
{
    PyDosObj far *obj;
    int idx;

    /* Check small integer cache */
    if (small_ints_ready && val >= SMALL_INT_MIN && val <= SMALL_INT_MAX) {
        idx = (int)(val - SMALL_INT_MIN);
        obj = (PyDosObj far *)&small_ints[idx];
        PYDOS_INCREF(obj);
        return obj;
    }

    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    obj->type = PYDT_INT;
    obj->v.int_val = val;
    return obj;
}

PyDosObj far * PYDOS_API pydos_obj_new_float(double val)
{
    PyDosObj far *obj;

    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    obj->type = PYDT_FLOAT;
    obj->v.float_val = val;
    return obj;
}

PyDosObj far * PYDOS_API pydos_obj_new_str(const char far *data, unsigned int len)
{
    PyDosObj far *obj;
    char far *buf;

    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    obj->type = PYDT_STR;

    /* Allocate buffer for string data + NUL terminator */
    buf = (char far *)pydos_far_alloc((unsigned long)(len + 1));
    if (buf == (char far *)0) {
        pydos_obj_free(obj);
        return (PyDosObj far *)0;
    }

    if (data != (const char far *)0 && len > 0) {
        _fmemcpy(buf, data, len);
    }
    buf[len] = '\0';

    obj->v.str.data = buf;
    obj->v.str.len = len;
    obj->v.str.hash = djb2_hash(buf, len);
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_is_truthy                                                 */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_is_truthy(PyDosObj far *obj)
{
    int result;

    if (obj == (PyDosObj far *)0) {
        result = 0;
        goto done;
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:
        result = 0; break;
    case PYDT_BOOL:
        result = obj->v.bool_val != 0; break;
    case PYDT_INT:
        result = obj->v.int_val != 0L; break;
    case PYDT_FLOAT:
        result = obj->v.float_val != 0.0; break;
    case PYDT_COMPLEX:
        result = (obj->v.complex_val.real != 0.0 || obj->v.complex_val.imag != 0.0); break;
    case PYDT_STR:
        result = obj->v.str.len > 0; break;
    case PYDT_LIST:
        result = obj->v.list.len > 0; break;
    case PYDT_DICT:
        result = obj->v.dict.used > 0; break;
    case PYDT_SET:
        result = obj->v.dict.used > 0; break;
    case PYDT_TUPLE:
        result = obj->v.tuple.len > 0; break;
    case PYDT_FROZENSET:
        result = obj->v.frozenset.len > 0; break;
    case PYDT_BYTEARRAY:
        result = obj->v.bytearray.len > 0; break;
    case PYDT_RANGE: {
        /* A range is truthy if it contains at least one element */
        long s = obj->v.range.start;
        long e = obj->v.range.stop;
        long st = obj->v.range.step;
        if (st > 0 && s < e) { result = 1; break; }
        if (st < 0 && s > e) { result = 1; break; }
        result = 0; break;
    }
    case PYDT_INSTANCE:
        /* Check for __bool__ via vtable slot */
        if (obj->v.instance.vtable != (struct PyDosVTable far *)0 &&
            obj->v.instance.vtable->slots[VSLOT_BOOL] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BoolFn)(PyDosObj far *);
            PyDosObj far *res = ((BoolFn)obj->v.instance.vtable->slots[VSLOT_BOOL])(obj);
            if (res != (PyDosObj far *)0) {
                if ((PyDosType)res->type == PYDT_BOOL) {
                    result = res->v.bool_val != 0;
                } else {
                    result = pydos_obj_is_truthy(res);
                }
            } else {
                result = 0;
            }
            break;
        }
        /* Check for __len__ fallback (Python: empty = falsy) */
        if (obj->v.instance.vtable != (struct PyDosVTable far *)0 &&
            obj->v.instance.vtable->slots[VSLOT_LEN] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *LenFn)(PyDosObj far *);
            PyDosObj far *len_res = ((LenFn)obj->v.instance.vtable->slots[VSLOT_LEN])(obj);
            if (len_res != (PyDosObj far *)0 &&
                (PyDosType)len_res->type == PYDT_INT) {
                result = len_res->v.int_val != 0L;
            } else {
                result = 1;
            }
            break;
        }
        result = 1; break;  /* no __bool__ or __len__: truthy by default */
    default:
        result = 1; break;  /* functions, classes, etc. are truthy */
    }

done:
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[TRUTHY ");
    dbg_putint(result);
    dbg_puts("]\r\n");
#endif
    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_equal — deep equality comparison                          */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_equal(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int i;

    if (a == b) {
        return 1;
    }
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return 0;
    }

    /* None equality */
    if (a->type == PYDT_NONE && b->type == PYDT_NONE) {
        return 1;
    }

    /* Bool-int interop: Python treats True==1, False==0 */
    if ((a->type == PYDT_BOOL || a->type == PYDT_INT) &&
        (b->type == PYDT_BOOL || b->type == PYDT_INT)) {
        long va, vb;
        va = (a->type == PYDT_BOOL) ? (long)a->v.bool_val : a->v.int_val;
        vb = (b->type == PYDT_BOOL) ? (long)b->v.bool_val : b->v.int_val;
        return va == vb;
    }

    /* Int-float interop */
    if ((a->type == PYDT_INT || a->type == PYDT_FLOAT) &&
        (b->type == PYDT_INT || b->type == PYDT_FLOAT)) {
        double da, db;
        da = (a->type == PYDT_INT) ? (double)a->v.int_val : a->v.float_val;
        db = (b->type == PYDT_INT) ? (double)b->v.int_val : b->v.float_val;
        return da == db;
    }

    /* Different types beyond numeric are never equal */
    if (a->type != b->type) {
        return 0;
    }

    switch ((PyDosType)a->type) {
    case PYDT_STR:
        if (a->v.str.len != b->v.str.len) {
            return 0;
        }
        if (a->v.str.hash != b->v.str.hash) {
            return 0;
        }
        return _fmemcmp(a->v.str.data, b->v.str.data, a->v.str.len) == 0;

    case PYDT_TUPLE:
        if (a->v.tuple.len != b->v.tuple.len) {
            return 0;
        }
        for (i = 0; i < a->v.tuple.len; i++) {
            if (!pydos_obj_equal(a->v.tuple.items[i], b->v.tuple.items[i])) {
                return 0;
            }
        }
        return 1;

    case PYDT_LIST:
        if (a->v.list.len != b->v.list.len) {
            return 0;
        }
        for (i = 0; i < a->v.list.len; i++) {
            if (!pydos_obj_equal(a->v.list.items[i], b->v.list.items[i])) {
                return 0;
            }
        }
        return 1;

    case PYDT_FROZENSET:
        if (a->v.frozenset.len != b->v.frozenset.len) {
            return 0;
        }
        if (a->v.frozenset.hash != b->v.frozenset.hash) {
            return 0;
        }
        for (i = 0; i < a->v.frozenset.len; i++) {
            if (!pydos_obj_equal(a->v.frozenset.items[i],
                                 b->v.frozenset.items[i])) {
                return 0;
            }
        }
        return 1;

    case PYDT_RANGE:
        return (a->v.range.start == b->v.range.start &&
                a->v.range.stop == b->v.range.stop &&
                a->v.range.step == b->v.range.step);

    case PYDT_COMPLEX:
        if ((PyDosType)b->type != PYDT_COMPLEX) return 0;
        return a->v.complex_val.real == b->v.complex_val.real &&
               a->v.complex_val.imag == b->v.complex_val.imag;

    case PYDT_BYTEARRAY:
        if ((PyDosType)b->type != PYDT_BYTEARRAY) return 0;
        if (a->v.bytearray.len != b->v.bytearray.len) return 0;
        if (a->v.bytearray.len == 0) return 1;
        return _fmemcmp(a->v.bytearray.data, b->v.bytearray.data, a->v.bytearray.len) == 0;

    case PYDT_INSTANCE:
        /* Check for __eq__ via vtable */
        if (a->v.instance.vtable != (PyDosVTable far *)0) {
            PyDosVTable far *vt = a->v.instance.vtable;
            if (vt->slots[VSLOT_EQ] != (void (far *)(void))0) {
                typedef PyDosObj far * (PYDOS_API far *EqFn)(PyDosObj far *, PyDosObj far *);
                PyDosObj far *res = ((EqFn)vt->slots[VSLOT_EQ])(a, b);
                if (res != (PyDosObj far *)0) {
                    int truthy = pydos_obj_is_truthy(res);
                    PYDOS_DECREF(res);
                    return truthy;
                }
            }
        }
        return (a == b);

    default:
        /* Identity comparison for other types */
        return (a == b);
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_hash                                                      */
/* ------------------------------------------------------------------ */
unsigned int PYDOS_API pydos_obj_hash(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return 0;
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:
        return (unsigned int)0x6E65U;  /* "None" hash, 16-bit */
    case PYDT_BOOL:
        return (unsigned int)obj->v.bool_val;
    case PYDT_INT: {
        /* Spread 32-bit long into 16-bit hash */
        unsigned int lo = (unsigned int)(obj->v.int_val & 0xFFFF);
        unsigned int hi = (unsigned int)((obj->v.int_val >> 16) & 0xFFFF);
        return lo ^ (hi * 31);
    }
    case PYDT_FLOAT: {
        /* Hash double by treating its bytes as data */
        unsigned int h;
        h = djb2_hash((const char far *)&obj->v.float_val,
                       (unsigned int)sizeof(double));
        return h;
    }
    case PYDT_STR:
        return obj->v.str.hash;
    case PYDT_TUPLE: {
        unsigned int h = 0x5678U;
        unsigned int i;
        for (i = 0; i < obj->v.tuple.len; i++) {
            unsigned int ih = pydos_obj_hash(obj->v.tuple.items[i]);
            h = (h ^ ih) * 1000003U;
            h ^= obj->v.tuple.len - i;
        }
        return h;
    }
    case PYDT_FROZENSET:
        return obj->v.frozenset.hash;
    case PYDT_COMPLEX: {
        unsigned int hr, hi;
        union { double d; unsigned int u[2]; } conv;
        conv.d = obj->v.complex_val.real;
        hr = conv.u[0] ^ conv.u[1];
        conv.d = obj->v.complex_val.imag;
        hi = conv.u[0] ^ conv.u[1];
        return hr ^ (hi * 31);
    }
    case PYDT_INSTANCE:
        if (obj->v.instance.vtable != (PyDosVTable far *)0 &&
            obj->v.instance.vtable->slots[VSLOT_HASH] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *HashFn)(PyDosObj far *);
            HashFn hfn = (HashFn)obj->v.instance.vtable->slots[VSLOT_HASH];
            PyDosObj far *hobj = hfn(obj);
            if (hobj != (PyDosObj far *)0 &&
                (PyDosType)hobj->type == PYDT_INT) {
                unsigned int h = (unsigned int)(hobj->v.int_val & 0xFFFFUL);
                PYDOS_DECREF(hobj);
                return h;
            }
            if (hobj != (PyDosObj far *)0) {
                PYDOS_DECREF(hobj);
            }
        }
        /* fallthrough to address-based hash */
        return (unsigned int)((unsigned long)obj & 0xFFFFUL);
    default:
        /* Unhashable types - use address as fallback */
        return (unsigned int)((unsigned long)obj & 0xFFFFUL);
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_to_str — produce a string representation                  */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_to_str(PyDosObj far *obj)
{
    char far *buf;
    unsigned int len;

    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_str((const char far *)"None", 4);
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:
        return pydos_obj_new_str((const char far *)"None", 4);

    case PYDT_BOOL:
        if (obj->v.bool_val) {
            return pydos_obj_new_str((const char far *)"True", 4);
        }
        return pydos_obj_new_str((const char far *)"False", 5);

    case PYDT_INT:
        buf = (char far *)pydos_far_alloc(16UL);
        if (buf == (char far *)0) {
            return (PyDosObj far *)0;
        }
        len = ltoa_far(obj->v.int_val, buf, 16);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(buf, len);
            pydos_far_free(buf);
            return result;
        }

    case PYDT_FLOAT:
        buf = (char far *)pydos_far_alloc(32UL);
        if (buf == (char far *)0) {
            return (PyDosObj far *)0;
        }
        len = dtoa_far(obj->v.float_val, buf, 32);
        /* Python always prints whole-number floats with .0 suffix */
        {
            int has_dot = 0;
            unsigned int fi;
            for (fi = 0; fi < len; fi++) {
                if (buf[fi] == '.' || buf[fi] == 'e' || buf[fi] == 'E' ||
                    buf[fi] == 'i' || buf[fi] == 'n') {
                    has_dot = 1;
                    break;
                }
            }
            if (!has_dot && len + 2 < 32) {
                buf[len++] = '.';
                buf[len++] = '0';
                buf[len] = '\0';
            }
        }
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(buf, len);
            pydos_far_free(buf);
            return result;
        }

    case PYDT_COMPLEX: {
        double r = obj->v.complex_val.real;
        double i = obj->v.complex_val.imag;
        char rbuf[32], ibuf[32];
        unsigned int rlen, ilen, tlen;
        char far *out;
        unsigned int pos;

        if (r == 0.0 && i >= 0.0) {
            /* Pure imaginary, positive: "3j" or "0j" */
            buf = (char far *)pydos_far_alloc(34UL);
            if (buf == (char far *)0) return (PyDosObj far *)0;
            len = dtoa_far(i, buf, 32);
            buf[len++] = 'j';
            buf[len] = '\0';
            {
                PyDosObj far *result = pydos_obj_new_str(buf, len);
                pydos_far_free(buf);
                return result;
            }
        }
        if (r == 0.0 && i < 0.0) {
            /* Pure imaginary, negative: "-3j" */
            buf = (char far *)pydos_far_alloc(34UL);
            if (buf == (char far *)0) return (PyDosObj far *)0;
            len = dtoa_far(i, buf, 32);
            buf[len++] = 'j';
            buf[len] = '\0';
            {
                PyDosObj far *result = pydos_obj_new_str(buf, len);
                pydos_far_free(buf);
                return result;
            }
        }
        /* General case: "(r+ij)" or "(r-ij)" */
        rlen = (unsigned int)sprintf(rbuf, "%g", r);
        if (i >= 0.0) {
            ilen = (unsigned int)sprintf(ibuf, "%g", i);
            /* "(r+ij)" */
            tlen = 1 + rlen + 1 + ilen + 1 + 1 + 1; /* ( r + i j ) NUL */
            out = (char far *)pydos_far_alloc((unsigned long)tlen);
            if (out == (char far *)0) return (PyDosObj far *)0;
            pos = 0;
            out[pos++] = '(';
            _fmemcpy(out + pos, (const char far *)rbuf, rlen); pos += rlen;
            out[pos++] = '+';
            _fmemcpy(out + pos, (const char far *)ibuf, ilen); pos += ilen;
            out[pos++] = 'j';
            out[pos++] = ')';
            out[pos] = '\0';
        } else {
            double ai = -i;
            ilen = (unsigned int)sprintf(ibuf, "%g", ai);
            /* "(r-ij)" */
            tlen = 1 + rlen + 1 + ilen + 1 + 1 + 1;
            out = (char far *)pydos_far_alloc((unsigned long)tlen);
            if (out == (char far *)0) return (PyDosObj far *)0;
            pos = 0;
            out[pos++] = '(';
            _fmemcpy(out + pos, (const char far *)rbuf, rlen); pos += rlen;
            out[pos++] = '-';
            _fmemcpy(out + pos, (const char far *)ibuf, ilen); pos += ilen;
            out[pos++] = 'j';
            out[pos++] = ')';
            out[pos] = '\0';
        }
        {
            PyDosObj far *result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_STR:
        /* Return a new reference to the same string content */
        return pydos_obj_new_str(obj->v.str.data, obj->v.str.len);

    case PYDT_LIST: {
        /* Build "[item1, item2, ...]" representation.
         * Two-pass: first convert items + compute exact size,
         * then format into correctly sized buffer. */
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int i;
        unsigned int n = obj->v.list.len;
        PyDosObj far * far *strs;

        if (n == 0) {
            return pydos_obj_new_str((const char far *)"[]", 2);
        }

        /* Pass 1: convert all items to strings and compute exact size */
        strs = (PyDosObj far * far *)pydos_far_alloc(
            (unsigned long)n * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            return pydos_obj_new_str((const char far *)"[...]", 5);
        }

        alloc_sz = 3; /* '[' + ']' + NUL */
        for (i = 0; i < n; i++) {
            strs[i] = pydos_obj_to_str(obj->v.list.items[i]);
            if (strs[i] != (PyDosObj far *)0) {
                int quote_it = (obj->v.list.items[i] != (PyDosObj far *)0 &&
                                obj->v.list.items[i]->type == PYDT_STR);
                alloc_sz += strs[i]->v.str.len + (quote_it ? 2 : 0);
            }
            if (i > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_far_alloc((unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (i = 0; i < n; i++) {
                if (strs[i]) PYDOS_DECREF(strs[i]);
            }
            pydos_far_free(strs);
            return (PyDosObj far *)0;
        }

        /* Pass 2: format into buffer */
        out[0] = '[';
        pos = 1;
        for (i = 0; i < n; i++) {
            if (i > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (strs[i] != (PyDosObj far *)0) {
                int quote_it = (obj->v.list.items[i] != (PyDosObj far *)0 &&
                                obj->v.list.items[i]->type == PYDT_STR);
                unsigned int slen = strs[i]->v.str.len;
                if (quote_it) out[pos++] = '\'';
                _fmemcpy(out + pos, strs[i]->v.str.data, slen);
                pos += slen;
                if (quote_it) out[pos++] = '\'';
                PYDOS_DECREF(strs[i]);
            }
        }
        out[pos++] = ']';
        out[pos] = '\0';

        pydos_far_free(strs);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_TUPLE: {
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int i;
        unsigned int n = obj->v.tuple.len;
        PyDosObj far * far *strs;

        if (n == 0) {
            return pydos_obj_new_str((const char far *)"()", 2);
        }

        /* Pass 1: convert all items to strings and compute exact size */
        strs = (PyDosObj far * far *)pydos_far_alloc(
            (unsigned long)n * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            return pydos_obj_new_str((const char far *)"(...)", 4);
        }

        alloc_sz = 3; /* '(' + ')' + NUL */
        if (n == 1) alloc_sz++; /* trailing comma */
        for (i = 0; i < n; i++) {
            strs[i] = pydos_obj_to_str(obj->v.tuple.items[i]);
            if (strs[i] != (PyDosObj far *)0) {
                int quote_it = (obj->v.tuple.items[i] != (PyDosObj far *)0 &&
                                obj->v.tuple.items[i]->type == PYDT_STR);
                alloc_sz += strs[i]->v.str.len + (quote_it ? 2 : 0);
            }
            if (i > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_far_alloc((unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (i = 0; i < n; i++) {
                if (strs[i]) PYDOS_DECREF(strs[i]);
            }
            pydos_far_free(strs);
            return (PyDosObj far *)0;
        }

        /* Pass 2: format into buffer */
        out[0] = '(';
        pos = 1;
        for (i = 0; i < n; i++) {
            if (i > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (strs[i] != (PyDosObj far *)0) {
                int quote_it = (obj->v.tuple.items[i] != (PyDosObj far *)0 &&
                                obj->v.tuple.items[i]->type == PYDT_STR);
                unsigned int slen = strs[i]->v.str.len;
                if (quote_it) out[pos++] = '\'';
                _fmemcpy(out + pos, strs[i]->v.str.data, slen);
                pos += slen;
                if (quote_it) out[pos++] = '\'';
                PYDOS_DECREF(strs[i]);
            }
        }
        if (n == 1) out[pos++] = ',';
        out[pos++] = ')';
        out[pos] = '\0';

        pydos_far_free(strs);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_DICT:
        /* Simplified dict repr */
        return pydos_obj_new_str((const char far *)"{...}", 5);

    case PYDT_SET: {
        /* Build "{item1, item2, ...}" representation */
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int si;
        unsigned int first;

        alloc_sz = 3;
        for (si = 0; si < obj->v.dict.size; si++) {
            if (obj->v.dict.entries[si].key != (PyDosObj far *)0) {
                alloc_sz += 20;
            }
        }
        out = (char far *)pydos_far_alloc((unsigned long)alloc_sz);
        if (out == (char far *)0) {
            return (PyDosObj far *)0;
        }
        out[0] = '{';
        pos = 1;
        first = 1;
        for (si = 0; si < obj->v.dict.size && pos < alloc_sz - 5; si++) {
            if (obj->v.dict.entries[si].key != (PyDosObj far *)0) {
                PyDosObj far *s;
                unsigned int slen;
                if (!first) {
                    out[pos++] = ',';
                    out[pos++] = ' ';
                }
                first = 0;
                s = pydos_obj_to_str(obj->v.dict.entries[si].key);
                if (s != (PyDosObj far *)0) {
                    slen = s->v.str.len;
                    if (pos + slen >= alloc_sz - 2) {
                        slen = alloc_sz - 2 - pos;
                    }
                    _fmemcpy(out + pos, s->v.str.data, slen);
                    pos += slen;
                    PYDOS_DECREF(s);
                }
            }
        }
        out[pos++] = '}';
        out[pos] = '\0';
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_FROZENSET: {
        /* Build "frozenset({elem1, elem2})" or "frozenset()" representation.
         * Two-pass: first convert items + compute exact size,
         * then format into correctly sized buffer. */
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int fi;
        unsigned int n = obj->v.frozenset.len;
        PyDosObj far * far *strs;

        if (n == 0) {
            return pydos_obj_new_str((const char far *)"frozenset()", 11);
        }

        /* Pass 1: convert all items to strings and compute exact size */
        strs = (PyDosObj far * far *)pydos_far_alloc(
            (unsigned long)n * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            return pydos_obj_new_str((const char far *)"frozenset({...})", 16);
        }

        /* "frozenset({" = 11, "})" = 2, NUL = 1 => 14 base */
        alloc_sz = 14;
        for (fi = 0; fi < n; fi++) {
            strs[fi] = pydos_obj_to_str(obj->v.frozenset.items[fi]);
            if (strs[fi] != (PyDosObj far *)0) {
                int quote_it = (obj->v.frozenset.items[fi] != (PyDosObj far *)0 &&
                                obj->v.frozenset.items[fi]->type == PYDT_STR);
                alloc_sz += strs[fi]->v.str.len + (quote_it ? 2 : 0);
            }
            if (fi > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_far_alloc((unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (fi = 0; fi < n; fi++) {
                if (strs[fi]) PYDOS_DECREF(strs[fi]);
            }
            pydos_far_free(strs);
            return (PyDosObj far *)0;
        }

        /* Pass 2: format into buffer */
        _fmemcpy(out, (const char far *)"frozenset({", 11);
        pos = 11;
        for (fi = 0; fi < n; fi++) {
            if (fi > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (strs[fi] != (PyDosObj far *)0) {
                int quote_it = (obj->v.frozenset.items[fi] != (PyDosObj far *)0 &&
                                obj->v.frozenset.items[fi]->type == PYDT_STR);
                unsigned int slen = strs[fi]->v.str.len;
                if (quote_it) out[pos++] = '\'';
                _fmemcpy(out + pos, strs[fi]->v.str.data, slen);
                pos += slen;
                if (quote_it) out[pos++] = '\'';
                PYDOS_DECREF(strs[fi]);
            }
        }
        out[pos++] = '}';
        out[pos++] = ')';
        out[pos] = '\0';

        pydos_far_free(strs);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_FUNCTION:
        if (obj->v.func.name != (const char far *)0) {
            /* Build "<function name>" */
            char far *out;
            unsigned int nlen;
            unsigned int pos;
            const char far *p;

            p = obj->v.func.name;
            nlen = 0;
            while (p[nlen] != '\0') nlen++;

            out = (char far *)pydos_far_alloc((unsigned long)(nlen + 12));
            if (out == (char far *)0) {
                return (PyDosObj far *)0;
            }
            _fmemcpy(out, (const char far *)"<function ", 10);
            _fmemcpy(out + 10, obj->v.func.name, nlen);
            pos = 10 + nlen;
            out[pos++] = '>';
            out[pos] = '\0';
            {
                PyDosObj far *result;
                result = pydos_obj_new_str(out, pos);
                pydos_far_free(out);
                return result;
            }
        }
        return pydos_obj_new_str((const char far *)"<function>", 10);

    case PYDT_CLASS:
        if (obj->v.cls.name != (const char far *)0) {
            char far *out;
            unsigned int nlen;
            unsigned int pos;
            const char far *p;

            p = obj->v.cls.name;
            nlen = 0;
            while (p[nlen] != '\0') nlen++;

            out = (char far *)pydos_far_alloc((unsigned long)(nlen + 10));
            if (out == (char far *)0) {
                return (PyDosObj far *)0;
            }
            _fmemcpy(out, (const char far *)"<class '", 8);
            _fmemcpy(out + 8, obj->v.cls.name, nlen);
            pos = 8 + nlen;
            out[pos++] = '\'';
            out[pos++] = '>';
            out[pos] = '\0';
            {
                PyDosObj far *result;
                result = pydos_obj_new_str(out, pos);
                pydos_far_free(out);
                return result;
            }
        }
        return pydos_obj_new_str((const char far *)"<class>", 7);

    case PYDT_RANGE: {
        char far *out;
        unsigned int pos;
        unsigned int slen;

        out = (char far *)pydos_far_alloc(48UL);
        if (out == (char far *)0) {
            return (PyDosObj far *)0;
        }
        _fmemcpy(out, (const char far *)"range(", 6);
        pos = 6;
        pos += ltoa_far(obj->v.range.start, out + pos, 48 - pos);
        out[pos++] = ',';
        out[pos++] = ' ';
        pos += ltoa_far(obj->v.range.stop, out + pos, 48 - pos);
        if (obj->v.range.step != 1L) {
            out[pos++] = ',';
            out[pos++] = ' ';
            pos += ltoa_far(obj->v.range.step, out + pos, 48 - pos);
        }
        out[pos++] = ')';
        out[pos] = '\0';
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_INSTANCE:
        /* Check for __str__ in vtable */
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            PyDosVTable far *vt = obj->v.instance.vtable;
            if (vt->slots[VSLOT_STR] != (void (far *)(void))0) {
                typedef PyDosObj far * (PYDOS_API far *StrFn)(PyDosObj far *);
                return ((StrFn)vt->slots[VSLOT_STR])(obj);
            }
            /* Fallback to __repr__ if no __str__ */
            if (vt->slots[VSLOT_REPR] != (void (far *)(void))0) {
                typedef PyDosObj far * (PYDOS_API far *ReprFn)(PyDosObj far *);
                return ((ReprFn)vt->slots[VSLOT_REPR])(obj);
            }
        }
        /* Build "<__main__.ClassName object>" if class_name is available */
        if (obj->v.instance.vtable != (PyDosVTable far *)0 &&
            obj->v.instance.vtable->class_name != (const char far *)0) {
            const char far *cn;
            char buf[128];
            int pos;
            /* prefix: "<__main__." */
            buf[0] = '<';
            buf[1] = '_'; buf[2] = '_'; buf[3] = 'm'; buf[4] = 'a';
            buf[5] = 'i'; buf[6] = 'n'; buf[7] = '_'; buf[8] = '_';
            buf[9] = '.';
            pos = 10;
            cn = obj->v.instance.vtable->class_name;
            while (*cn && pos < 118) {
                buf[pos++] = *cn++;
            }
            /* suffix: " object>" */
            buf[pos++] = ' ';
            buf[pos++] = 'o'; buf[pos++] = 'b'; buf[pos++] = 'j';
            buf[pos++] = 'e'; buf[pos++] = 'c'; buf[pos++] = 't';
            buf[pos++] = '>';
            buf[pos] = '\0';
            return pydos_obj_new_str((const char far *)buf, pos);
        }
        return pydos_obj_new_str((const char far *)"<instance>", 10);

    case PYDT_GENERATOR:
        return pydos_obj_new_str((const char far *)"<generator>", 11);

    case PYDT_COROUTINE:
        return pydos_obj_new_str((const char far *)"<coroutine object>", 18);

    case PYDT_EXCEPTION:
        if (obj->v.exc.message != (PyDosObj far *)0 &&
            obj->v.exc.message->type == PYDT_STR) {
            return pydos_obj_new_str(obj->v.exc.message->v.str.data,
                                     obj->v.exc.message->v.str.len);
        }
        return pydos_obj_new_str((const char far *)"Exception", 9);

    case PYDT_FILE:
        return pydos_obj_new_str((const char far *)"<file>", 6);

    case PYDT_CELL:
        return pydos_obj_new_str((const char far *)"<cell>", 6);

    case PYDT_EXC_GROUP: {
        /* Format: ExceptionGroup('msg', [exc1, exc2, ...]) */
        char buf[128];
        unsigned int pos;
        unsigned int i;
        unsigned int msg_len;

        pos = 0;
        _fmemcpy((char far *)(buf + pos), (const char far *)"ExceptionGroup('", 16);
        pos += 16;

        if (obj->v.excgroup.message != (PyDosObj far *)0 &&
            obj->v.excgroup.message->type == PYDT_STR) {
            msg_len = obj->v.excgroup.message->v.str.len;
            if (pos + msg_len < 100) {
                _fmemcpy((char far *)(buf + pos),
                         obj->v.excgroup.message->v.str.data, msg_len);
                pos += msg_len;
            }
        }

        buf[pos++] = '\'';
        buf[pos++] = ',';
        buf[pos++] = ' ';
        buf[pos++] = '[';

        for (i = 0; i < obj->v.excgroup.count && pos < 120; i++) {
            PyDosObj far *child;
            if (i > 0 && pos < 118) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            child = obj->v.excgroup.exceptions[i];
            if (child != (PyDosObj far *)0 &&
                (PyDosType)child->type == PYDT_EXCEPTION) {
                const char far *tn;
                unsigned int tn_len;
                int tc = child->v.exc.type_code;
                tn = pydos_exc_type_name(tc);
                tn_len = (unsigned int)_fstrlen(tn);
                if (pos + tn_len + 2 < 125) {
                    _fmemcpy((char far *)(buf + pos), tn, tn_len);
                    pos += tn_len;
                    buf[pos++] = '(';
                    buf[pos++] = ')';
                }
            }
        }

        buf[pos++] = ']';
        buf[pos++] = ')';
        buf[pos] = '\0';

        return pydos_obj_new_str((const char far *)buf, pos);
    }

    case PYDT_BYTEARRAY: {
        unsigned int i, n;
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;

        n = obj->v.bytearray.len;
        /* "bytearray(b'" = 13 chars, "')" = 2 chars, NUL = 1 */
        alloc_sz = 16 + n * 4; /* worst case: each byte as \xNN */
        out = (char far *)pydos_far_alloc((unsigned long)alloc_sz);
        if (out == (char far *)0) return (PyDosObj far *)0;

        _fmemcpy(out, (const char far *)"bytearray(b'", 13);
        pos = 13;
        for (i = 0; i < n; i++) {
            unsigned char ch = obj->v.bytearray.data[i];
            if (ch >= 32 && ch < 127 && ch != '\'' && ch != '\\') {
                out[pos++] = (char)ch;
            } else {
                out[pos++] = '\\';
                out[pos++] = 'x';
                out[pos++] = "0123456789abcdef"[ch >> 4];
                out[pos++] = "0123456789abcdef"[ch & 0x0f];
            }
        }
        out[pos++] = '\'';
        out[pos++] = ')';
        out[pos] = '\0';
        {
            PyDosObj far *result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    default:
        return pydos_obj_new_str((const char far *)"<object>", 8);
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_type_name — return a human-readable type name string      */
/* ------------------------------------------------------------------ */
const char far * PYDOS_API pydos_obj_type_name(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return (const char far *)"NoneType";
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:       return (const char far *)"NoneType";
    case PYDT_BOOL:       return (const char far *)"bool";
    case PYDT_INT:        return (const char far *)"int";
    case PYDT_FLOAT:      return (const char far *)"float";
    case PYDT_COMPLEX:    return (const char far *)"complex";
    case PYDT_STR:        return (const char far *)"str";
    case PYDT_LIST:       return (const char far *)"list";
    case PYDT_DICT:       return (const char far *)"dict";
    case PYDT_TUPLE:      return (const char far *)"tuple";
    case PYDT_SET:        return (const char far *)"set";
    case PYDT_BYTES:      return (const char far *)"bytes";
    case PYDT_INSTANCE:   return (const char far *)"instance";
    case PYDT_FUNCTION:   return (const char far *)"function";
    case PYDT_GENERATOR:  return (const char far *)"generator";
    case PYDT_EXCEPTION:  return (const char far *)"Exception";
    case PYDT_CLASS:      return (const char far *)"type";
    case PYDT_RANGE:      return (const char far *)"range";
    case PYDT_FILE:       return (const char far *)"file";
    case PYDT_CELL:       return (const char far *)"cell";
    case PYDT_COROUTINE:  return (const char far *)"coroutine";
    case PYDT_EXC_GROUP:  return (const char far *)"ExceptionGroup";
    case PYDT_FROZENSET:  return (const char far *)"frozenset";
    case PYDT_BYTEARRAY:  return (const char far *)"bytearray";
    default:              return (const char far *)"<unknown>";
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_init / pydos_obj_shutdown                                 */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_obj_init(void)
{
    int i;

    /* Initialize None singleton */
    _fmemset(&singleton_none, 0, sizeof(PyDosObj));
    singleton_none.type = PYDT_NONE;
    singleton_none.flags = OBJ_FLAG_IMMORTAL;
    singleton_none.refcount = 1;

    /* Initialize True singleton */
    _fmemset(&singleton_true, 0, sizeof(PyDosObj));
    singleton_true.type = PYDT_BOOL;
    singleton_true.flags = OBJ_FLAG_IMMORTAL;
    singleton_true.refcount = 1;
    singleton_true.v.bool_val = 1;

    /* Initialize False singleton */
    _fmemset(&singleton_false, 0, sizeof(PyDosObj));
    singleton_false.type = PYDT_BOOL;
    singleton_false.flags = OBJ_FLAG_IMMORTAL;
    singleton_false.refcount = 1;
    singleton_false.v.bool_val = 0;

    /* Initialize small integer cache */
    for (i = 0; i < SMALL_INT_COUNT; i++) {
        _fmemset(&small_ints[i], 0, sizeof(PyDosObj));
        small_ints[i].type = PYDT_INT;
        small_ints[i].flags = OBJ_FLAG_IMMORTAL;
        small_ints[i].refcount = 1;
        small_ints[i].v.int_val = (long)(i + SMALL_INT_MIN);
    }
    small_ints_ready = 1;

    /* Clear free list */
    free_count = 0;
}

void PYDOS_API pydos_obj_shutdown(void)
{
    unsigned int i;

    /* Release free list entries */
    for (i = 0; i < free_count; i++) {
        pydos_far_free(free_list[i]);
        free_list[i] = (PyDosObj far *)0;
    }
    free_count = 0;
    small_ints_ready = 0;
}

void PYDOS_API pydos_incref(PyDosObj far *obj)
{
    PYDOS_INCREF(obj);
}

void PYDOS_API pydos_decref(PyDosObj far *obj)
{
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_set_attr — set an instance attribute by name              */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_set_attr(PyDosObj far *obj,
                                   const char far *attr_name,
                                   PyDosObj far *value)
{
    unsigned int len;
    const char far *p;
    PyDosObj far *key;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    /* Promote freshly-allocated NONE object to INSTANCE */
    if ((PyDosType)obj->type == PYDT_NONE) {
        obj->type = PYDT_INSTANCE;
        obj->v.instance.attrs  = (PyDosObj far *)0;
        obj->v.instance.vtable = (struct PyDosVTable far *)0;
        obj->v.instance.cls    = (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type != PYDT_INSTANCE) {
        return;
    }

    /* Lazily create the attrs dict */
    if (obj->v.instance.attrs == (PyDosObj far *)0) {
        obj->v.instance.attrs = pydos_dict_new(8);
        if (obj->v.instance.attrs == (PyDosObj far *)0) {
            return;
        }
    }

    /* Compute length of attr_name (far pointer) */
    p = attr_name;
    len = 0;
    while (p[len] != '\0') len++;

    /* Wrap raw C string in a PyDosObj string for dict key */
    key = pydos_obj_new_str(attr_name, len);
    if (key == (PyDosObj far *)0) {
        return;
    }

    pydos_dict_set(obj->v.instance.attrs, key, value);
    PYDOS_DECREF(key);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_del_attr — delete an attribute from an instance object     */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_del_attr(PyDosObj far *obj,
                                   const char far *attr_name)
{
    unsigned int len;
    const char far *p;
    PyDosObj far *key;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    if ((PyDosType)obj->type != PYDT_INSTANCE) {
        return;
    }

    /* Check VSLOT_DELATTR vtable slot for custom __delattr__ */
    if (obj->v.instance.vtable != (struct PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_DELATTR] != (void (far *)(void))0) {
        /* Custom __delattr__: build str arg, call it */
        typedef PyDosObj far * (PYDOS_API *DelattrFn)(int, PyDosObj far * far *);
        PyDosObj far *name_obj;
        PyDosObj far *args[2];
        DelattrFn delattr_fn;

        p = attr_name;
        len = 0;
        while (p[len] != '\0') len++;
        name_obj = pydos_obj_new_str(attr_name, len);
        if (name_obj == (PyDosObj far *)0) return;

        args[0] = obj;
        args[1] = name_obj;
        delattr_fn = (DelattrFn)obj->v.instance.vtable->slots[VSLOT_DELATTR];
        delattr_fn(2, args);
        PYDOS_DECREF(name_obj);
        return;
    }

    /* No attrs dict => nothing to delete */
    if (obj->v.instance.attrs == (PyDosObj far *)0) {
        return;
    }

    /* Compute length of attr_name (far pointer) */
    p = attr_name;
    len = 0;
    while (p[len] != '\0') len++;

    /* Wrap raw C string in a PyDosObj string for dict key */
    key = pydos_obj_new_str(attr_name, len);
    if (key == (PyDosObj far *)0) {
        return;
    }

    pydos_dict_delete(obj->v.instance.attrs, key);
    PYDOS_DECREF(key);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_set_vtable — assign a vtable to an instance object        */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_set_vtable(PyDosObj far *obj,
                                     struct PyDosVTable far *vt)
{
    if (obj == (PyDosObj far *)0) return;

    /* Promote to INSTANCE if still NONE (e.g. empty __init__) */
    if ((PyDosType)obj->type == PYDT_NONE) {
        obj->type = PYDT_INSTANCE;
        obj->v.instance.attrs  = (PyDosObj far *)0;
        obj->v.instance.cls    = (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        obj->v.instance.vtable = vt;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_isinstance_vtable — check if obj has a matching vtable    */
/* Checks direct vtable match, then walks MRO for inheritance.         */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_isinstance_vtable(PyDosObj far *obj,
                                           struct PyDosVTable far *target_vt)
{
    PyDosVTable far *vt;
    unsigned char i;

    if (obj == (PyDosObj far *)0) return 0;
    if ((PyDosType)obj->type != PYDT_INSTANCE) return 0;

    vt = obj->v.instance.vtable;
    if (vt == (PyDosVTable far *)0) return 0;

    /* Direct match */
    if (vt == target_vt) return 1;

    /* Check MRO chain (parent vtables) */
    for (i = 0; i < vt->mro_len; i++) {
        if (vt->mro[i] == target_vt) return 1;
    }

    return 0;
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

    /* Instance with __add__ via vtable */
    if (ta == PYDT_INSTANCE && a->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = a->v.instance.vtable;
        if (vt->slots[VSLOT_ADD] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
            return ((BinOp)vt->slots[VSLOT_ADD])(a, b);
        }
    }

    /* Reflected: try b.__radd__(a) */
    if (tb == PYDT_INSTANCE && b->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = b->v.instance.vtable;
        if (vt->slots[VSLOT_RADD] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
            return ((BinOp)vt->slots[VSLOT_RADD])(b, a);
        }
    }

    /* Fallback */
    return pydos_obj_new_int(0L);
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

    /* Instance with __sub__ via vtable */
    if (ta == PYDT_INSTANCE && a->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = a->v.instance.vtable;
        if (vt->slots[VSLOT_SUB] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
            return ((BinOp)vt->slots[VSLOT_SUB])(a, b);
        }
    }

    /* Reflected: try b.__rsub__(a) */
    if (tb == PYDT_INSTANCE && b->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = b->v.instance.vtable;
        if (vt->slots[VSLOT_RSUB] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
            return ((BinOp)vt->slots[VSLOT_RSUB])(b, a);
        }
    }

    return pydos_obj_new_int(0L);
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

    /* Instance with __mul__ via vtable */
    if (ta == PYDT_INSTANCE && a->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = a->v.instance.vtable;
        if (vt->slots[VSLOT_MUL] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
            return ((BinOp)vt->slots[VSLOT_MUL])(a, b);
        }
    }

    /* Reflected: try b.__rmul__(a) */
    if (tb == PYDT_INSTANCE && b->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = b->v.instance.vtable;
        if (vt->slots[VSLOT_RMUL] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
            return ((BinOp)vt->slots[VSLOT_RMUL])(b, a);
        }
    }

    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_matmul — @ operator dispatch                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_matmul(PyDosObj far *a, PyDosObj far *b)
{
    typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    /* Try a.__matmul__(b) */
    if ((PyDosType)a->type == PYDT_INSTANCE &&
        a->v.instance.vtable != (PyDosVTable far *)0 &&
        a->v.instance.vtable->slots[VSLOT_MATMUL] != (void (far *)(void))0) {
        return ((BinOp)a->v.instance.vtable->slots[VSLOT_MATMUL])(a, b);
    }

    /* Reflected: try b.__rmatmul__(a) */
    if ((PyDosType)b->type == PYDT_INSTANCE &&
        b->v.instance.vtable != (PyDosVTable far *)0 &&
        b->v.instance.vtable->slots[VSLOT_RMATMUL] != (void (far *)(void))0) {
        return ((BinOp)b->v.instance.vtable->slots[VSLOT_RMATMUL])(b, a);
    }

    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_inplace — in-place operator dispatch                      */
/* op: 0=add,1=sub,2=mul,3=floordiv,4=truediv,5=mod,6=pow,            */
/*     7=and,8=or,9=xor,10=lshift,11=rshift,12=matmul                 */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_inplace(PyDosObj far *a, PyDosObj far *b,
                                            int op)
{
    typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
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
        a->v.instance.vtable != (PyDosVTable far *)0 &&
        a->v.instance.vtable->slots[slot_idx] != (void (far *)(void))0) {
        return ((BinOp)a->v.instance.vtable->slots[slot_idx])(a, b);
    }

    /* Fallback to regular binary op */
    switch (op) {
    case 0:  return pydos_obj_add(a, b);
    case 1:  return pydos_obj_sub(a, b);
    case 2:  return pydos_obj_mul(a, b);
    case 3:  return pydos_int_div(a, b);
    case 4:  return pydos_int_truediv(a, b);
    case 5:  return pydos_int_mod(a, b);
    case 6:  return pydos_int_pow(a, b);
    case 7:  return pydos_int_bitand(a, b);
    case 8:  return pydos_int_bitor(a, b);
    case 9:  return pydos_int_bitxor(a, b);
    case 10: return pydos_int_shl(a, b);
    case 11: return pydos_int_shr(a, b);
    case 12: return pydos_obj_matmul(a, b);
    default: break;
    }

    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_get_attr — get an instance attribute by name              */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* pydos_obj_getitem — polymorphic subscript operator                   */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_getitem(PyDosObj far *obj,
                                            PyDosObj far *key)
{
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        long idx = 0;
        if (key != (PyDosObj far *)0) {
            if (key->type == PYDT_INT)  idx = key->v.int_val;
            else if (key->type == PYDT_BOOL) idx = (long)key->v.bool_val;
        }
        return pydos_list_get(obj, idx);  /* already increfs */
    }

    if ((PyDosType)obj->type == PYDT_DICT) {
        return pydos_dict_get(obj, key);  /* already increfs */
    }

    if ((PyDosType)obj->type == PYDT_TUPLE) {
        long idx = 0;
        if (key != (PyDosObj far *)0) {
            if (key->type == PYDT_INT)  idx = key->v.int_val;
            else if (key->type == PYDT_BOOL) idx = (long)key->v.bool_val;
        }
        /* Normalize negative index */
        if (idx < 0) idx += (long)obj->v.tuple.len;
        if (idx >= 0 && (unsigned int)idx < obj->v.tuple.len) {
            PYDOS_INCREF(obj->v.tuple.items[(unsigned int)idx]);
            return obj->v.tuple.items[(unsigned int)idx];
        }
        return pydos_obj_new_none();
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        int idx;
        if (key == (PyDosObj far *)0) return pydos_obj_new_int(0L);
        idx = pydos_bytearray_getitem(obj, (int)key->v.int_val);
        if (idx < 0) return pydos_obj_new_int(0L);
        return pydos_obj_new_int((long)idx);
    }

    /* Instance with __getitem__: dispatch via vtable */
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_GETITEM] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *GetItemFn)(PyDosObj far *, PyDosObj far *);
        return ((GetItemFn)obj->v.instance.vtable->slots[VSLOT_GETITEM])(obj, key);
    }

    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_setitem — polymorphic subscript assignment                 */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_setitem(PyDosObj far *obj,
                                  PyDosObj far *key,
                                  PyDosObj far *value)
{
    if (obj == (PyDosObj far *)0) {
        return;
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        long idx = 0;
        if (key != (PyDosObj far *)0) {
            if (key->type == PYDT_INT)  idx = key->v.int_val;
            else if (key->type == PYDT_BOOL) idx = (long)key->v.bool_val;
        }
        pydos_list_set(obj, idx, value);
        return;
    }

    if ((PyDosType)obj->type == PYDT_DICT) {
        pydos_dict_set(obj, key, value);
        return;
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        if (key != (PyDosObj far *)0 && value != (PyDosObj far *)0 &&
            ((PyDosType)key->type == PYDT_INT || (PyDosType)key->type == PYDT_BOOL) &&
            ((PyDosType)value->type == PYDT_INT || (PyDosType)value->type == PYDT_BOOL)) {
            long v = ((PyDosType)value->type == PYDT_INT) ? value->v.int_val : (long)value->v.bool_val;
            long k = ((PyDosType)key->type == PYDT_INT) ? key->v.int_val : (long)key->v.bool_val;
            if (v >= 0 && v <= 255) {
                pydos_bytearray_setitem(obj, (int)k, (unsigned char)v);
            }
        }
        return;
    }

    /* Instance with __setitem__: dispatch via vtable */
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_SETITEM] != (void (far *)(void))0) {
        typedef void (PYDOS_API far *SetItemFn)(PyDosObj far *, PyDosObj far *, PyDosObj far *);
        ((SetItemFn)obj->v.instance.vtable->slots[VSLOT_SETITEM])(obj, key, value);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_delitem — polymorphic subscript deletion                  */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_delitem(PyDosObj far *obj, PyDosObj far *key)
{
    if (obj == (PyDosObj far *)0) return;
    if ((PyDosType)obj->type == PYDT_DICT) {
        pydos_dict_delete(obj, key);
        return;
    }
    if ((PyDosType)obj->type == PYDT_LIST) {
        long idx = 0;
        if (key != (PyDosObj far *)0) {
            if (key->type == PYDT_INT)  idx = key->v.int_val;
            else if (key->type == PYDT_BOOL) idx = (long)key->v.bool_val;
        }
        pydos_list_pop(obj, idx);
        return;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_get_iter — get an iterator for an iterable object         */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_get_iter(PyDosObj far *obj)
{
    PyDosObj far *iter;

    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[ITER_NEW type=");
    dbg_putint((int)obj->type);
    dbg_puts("]\r\n");
#endif

    /* True generator objects (with resume function) are their own iterator */
    if ((PyDosType)obj->type == PYDT_GENERATOR &&
        obj->v.gen.resume != (void (far *)(void))0) {
        PYDOS_INCREF(obj);
        return obj;
    }

    /* Coroutines are NOT iterable — TypeError per Python spec */
    if ((PyDosType)obj->type == PYDT_COROUTINE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"coroutine object is not iterable");
        return (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_RANGE) {
        /* Clone the range, set current = start */
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->type = PYDT_RANGE;
        iter->v.range.start   = obj->v.range.start;
        iter->v.range.stop    = obj->v.range.stop;
        iter->v.range.step    = obj->v.range.step;
        iter->v.range.current = obj->v.range.start;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        /* Use a GENERATOR object as a list iterator:
         * state = the list, pc = current index */
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->type = PYDT_GENERATOR;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[ITER_OK]\r\n");
#endif
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_DICT || (PyDosType)obj->type == PYDT_SET) {
        /* For dict/set iteration, get list of keys, then iterate over that.
         * Python semantics: `for k in d` iterates over keys.
         * pydos_dict_keys() doesn't check the type tag, works on sets too. */
        PyDosObj far *keys = pydos_dict_keys(obj);
        if (keys == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) {
            PYDOS_DECREF(keys);
            return (PyDosObj far *)0;
        }
        iter->type = PYDT_GENERATOR;
        iter->v.gen.state  = keys;   /* takes ownership of keys list */
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_STR) {
        /* String iteration: iterate over individual characters */
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->type = PYDT_GENERATOR;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_TUPLE) {
        /* Tuple iteration: use GENERATOR with state=tuple, pc=index */
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->type = PYDT_GENERATOR;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_FROZENSET) {
        /* Frozenset iteration: use GENERATOR with state=frozenset, pc=index */
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->type = PYDT_GENERATOR;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        /* Bytearray iteration: build a list of int objects, then iterate */
        unsigned int bi;
        PyDosObj far *lst = pydos_list_new(obj->v.bytearray.len > 0 ? obj->v.bytearray.len : 4);
        if (lst == (PyDosObj far *)0) return (PyDosObj far *)0;
        for (bi = 0; bi < obj->v.bytearray.len; bi++) {
            PyDosObj far *byte_obj = pydos_obj_new_int((long)obj->v.bytearray.data[bi]);
            pydos_list_append(lst, byte_obj);
            PYDOS_DECREF(byte_obj);
        }
        iter = pydos_obj_alloc();
        if (iter == (PyDosObj far *)0) {
            PYDOS_DECREF(lst);
            return (PyDosObj far *)0;
        }
        iter->type = PYDT_GENERATOR;
        iter->v.gen.state  = lst;  /* takes ownership */
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        /* Instance with __iter__: call it and return result */
        if (obj->v.instance.vtable != (PyDosVTable far *)0 &&
            obj->v.instance.vtable->slots[VSLOT_ITER] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *IterFn)(PyDosObj far *);
            PyDosObj far *result = ((IterFn)obj->v.instance.vtable->slots[VSLOT_ITER])(obj);
            if (result == obj) {
                /* __iter__ returned self — this instance IS the iterator.
                 * Return it directly (iter_next will call __next__). */
                return result;
            }
            if (result != (PyDosObj far *)0 &&
                (PyDosType)result->type == PYDT_INSTANCE) {
                /* __iter__ returned another instance — it is the iterator */
                return result;
            }
            if (result != (PyDosObj far *)0 &&
                (PyDosType)result->type != PYDT_GENERATOR) {
                /* __iter__ returned a builtin iterable, wrap it */
                PyDosObj far *wrapped = pydos_obj_get_iter(result);
                PYDOS_DECREF(result);
                return wrapped;
            }
            return result;
        }
    }

    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_iter_next — return next element or NULL (StopIteration)    */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_iter_next(PyDosObj far *iter)
{
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[ITNX]\r\n");
#endif
    if (iter == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[ITNX t=");
    dbg_putint((int)iter->type);
    dbg_puts("]\r\n");
#endif

    if ((PyDosType)iter->type == PYDT_RANGE) {
        long cur  = iter->v.range.current;
        long stop = iter->v.range.stop;
        long step = iter->v.range.step;

        if (step > 0 && cur < stop) {
            iter->v.range.current = cur + step;
            return pydos_obj_new_int(cur);
        }
        if (step < 0 && cur > stop) {
            iter->v.range.current = cur + step;
            return pydos_obj_new_int(cur);
        }
        return (PyDosObj far *)0;
    }

    /* True generator (has a resume function) — call it */
#ifdef PYDOS_DEBUG_CMP
    if ((PyDosType)iter->type == PYDT_GENERATOR) {
        int has_resume = (iter->v.gen.resume != (void (far *)(void))0);
        dbg_puts("[ITNX r=");
        dbg_putint(has_resume);
        dbg_puts("]\r\n");
    }
#endif
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.resume != (void (far *)(void))0) {
        return pydos_gen_next(iter);
    }

    /* List iterator (stored as GENERATOR with state=list, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_LIST) {
        PyDosObj far *list = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[NEXT pc=");
        dbg_putint((int)idx);
        dbg_puts(" len=");
        dbg_putint((int)list->v.list.len);
        dbg_puts("]\r\n");
#endif
        if (idx < list->v.list.len) {
            PyDosObj far *item = list->v.list.items[idx];
            iter->v.gen.pc = (int)(idx + 1);
            PYDOS_INCREF(item);
            return item;
        }
        return (PyDosObj far *)0;
    }

    /* String iterator (stored as GENERATOR with state=str, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_STR) {
        PyDosObj far *str = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < str->v.str.len) {
            char c = str->v.str.data[idx];
            iter->v.gen.pc = (int)(idx + 1);
            return pydos_obj_new_str((const char far *)&c, 1);
        }
        return (PyDosObj far *)0;
    }

    /* Tuple iterator (stored as GENERATOR with state=tuple, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_TUPLE) {
        PyDosObj far *tup = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < tup->v.tuple.len) {
            PyDosObj far *item = tup->v.tuple.items[idx];
            iter->v.gen.pc = (int)(idx + 1);
            PYDOS_INCREF(item);
            return item;
        }
        return (PyDosObj far *)0;
    }

    /* Frozenset iterator (stored as GENERATOR with state=frozenset, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_FROZENSET) {
        PyDosObj far *fs = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < fs->v.frozenset.len) {
            PyDosObj far *item = fs->v.frozenset.items[idx];
            iter->v.gen.pc = (int)(idx + 1);
            PYDOS_INCREF(item);
            return item;
        }
        return (PyDosObj far *)0;
    }

    /* Instance iterator — call __next__ via vtable, catch StopIteration.
     * We use pydos_exc_alloc_frame() + direct setjmp() here instead of
     * pydos_try_enter() because setjmp must be called in a function whose
     * frame persists while longjmp may fire (C89 7.6.1.1). */
    if ((PyDosType)iter->type == PYDT_INSTANCE &&
        iter->v.instance.vtable != (PyDosVTable far *)0 &&
        iter->v.instance.vtable->slots[VSLOT_NEXT] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *NextFn)(PyDosObj far *);
        NextFn next_fn;
        void far *exc_buf;

        next_fn = (NextFn)iter->v.instance.vtable->slots[VSLOT_NEXT];
        exc_buf = pydos_exc_alloc_frame();

        if (setjmp(*(jmp_buf far *)exc_buf) == 0) {
            PyDosObj far *result;
            result = next_fn(iter);
            pydos_exc_pop();
            return result;
        } else {
            /* Exception caught — check if it's StopIteration */
            PyDosObj far *exc;
            int is_stop;
            exc = pydos_exc_current();
            is_stop = (exc != (PyDosObj far *)0 &&
                       (PyDosType)exc->type == PYDT_EXCEPTION &&
                       pydos_exc_matches(exc, PYDOS_EXC_STOP_ITERATION));
            pydos_exc_pop();
            if (is_stop) {
                pydos_exc_clear();
                return (PyDosObj far *)0;
            }
            /* Re-raise non-StopIteration exceptions */
            pydos_exc_raise_obj(exc);
            return (PyDosObj far *)0;
        }
    }

    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_call_method — generic method dispatch                     */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_call_method(
    const char far *method_name,
    unsigned int argc,
    PyDosObj far * far *argv)
{
    PyDosObj far *self;
    unsigned char stype;

    if (argc < 1 || argv == (PyDosObj far * far *)0) {
        return pydos_obj_new_none();
    }

    self = argv[0];
    if (self == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }

    stype = self->type;

    /* ---- List methods ---- */
    if (stype == PYDT_LIST) {
        if (_fstrcmp(method_name, (const char far *)"append") == 0) {
            if (argc > 1) {
                pydos_list_append(self, argv[1]);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"pop") == 0) {
            if (argc > 1 && argv[1] != (PyDosObj far *)0) {
                return pydos_list_pop(self, argv[1]->v.int_val);
            }
            return pydos_list_pop(self, -1L);
        }
        if (_fstrcmp(method_name, (const char far *)"insert") == 0) {
            if (argc > 2 && argv[1] != (PyDosObj far *)0) {
                pydos_list_insert(self, argv[1]->v.int_val, argv[2]);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"reverse") == 0) {
            pydos_list_reverse(self);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"sort") == 0) {
            pydos_list_sort(self);
            /* reverse=True passed as positional arg after self */
            if (argc > 1 && argv[1] != (PyDosObj far *)0 &&
                pydos_obj_is_truthy(argv[1])) {
                pydos_list_reverse(self);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            pydos_list_clear(self);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"remove") == 0) {
            if (argc > 1) {
                if (pydos_list_remove(self, argv[1]) != 0) {
                    pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"list.remove(x): x not in list");
                }
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"copy") == 0) {
            return pydos_list_copy(self);
        }
    }

    /* ---- String methods ---- */
    if (stype == PYDT_STR) {
        if (_fstrcmp(method_name, (const char far *)"upper") == 0) return pydos_str_upper(self);
        if (_fstrcmp(method_name, (const char far *)"lower") == 0) return pydos_str_lower(self);
        if (_fstrcmp(method_name, (const char far *)"strip") == 0) return pydos_str_strip(self);
        if (_fstrcmp(method_name, (const char far *)"lstrip") == 0) return pydos_str_lstrip(self);
        if (_fstrcmp(method_name, (const char far *)"rstrip") == 0) return pydos_str_rstrip(self);
        if (_fstrcmp(method_name, (const char far *)"title") == 0) return pydos_str_title(self);
        if (_fstrcmp(method_name, (const char far *)"capitalize") == 0) return pydos_str_capitalize(self);
        if (_fstrcmp(method_name, (const char far *)"swapcase") == 0) return pydos_str_swapcase(self);
        if (_fstrcmp(method_name, (const char far *)"find") == 0) {
            if (argc > 1) return pydos_str_find_m(self, argv[1]);
            return pydos_obj_new_int(-1L);
        }
        if (_fstrcmp(method_name, (const char far *)"rfind") == 0) {
            if (argc > 1) return pydos_str_rfind_m(self, argv[1]);
            return pydos_obj_new_int(-1L);
        }
        if (_fstrcmp(method_name, (const char far *)"index") == 0) {
            if (argc > 1) return pydos_str_index_m(self, argv[1]);
            return pydos_obj_new_int(-1L);
        }
        if (_fstrcmp(method_name, (const char far *)"rindex") == 0) {
            if (argc > 1) return pydos_str_rindex_m(self, argv[1]);
            return pydos_obj_new_int(-1L);
        }
        if (_fstrcmp(method_name, (const char far *)"count") == 0) {
            if (argc > 1) return pydos_str_count_m(self, argv[1]);
            return pydos_obj_new_int(0L);
        }
        if (_fstrcmp(method_name, (const char far *)"startswith") == 0) {
            if (argc > 1) return pydos_str_startswith(self, argv[1]);
            return pydos_obj_new_bool(0);
        }
        if (_fstrcmp(method_name, (const char far *)"endswith") == 0) {
            if (argc > 1) return pydos_str_endswith(self, argv[1]);
            return pydos_obj_new_bool(0);
        }
        if (_fstrcmp(method_name, (const char far *)"isdigit") == 0) return pydos_str_isdigit(self);
        if (_fstrcmp(method_name, (const char far *)"isalpha") == 0) return pydos_str_isalpha(self);
        if (_fstrcmp(method_name, (const char far *)"isalnum") == 0) return pydos_str_isalnum(self);
        if (_fstrcmp(method_name, (const char far *)"isspace") == 0) return pydos_str_isspace(self);
        if (_fstrcmp(method_name, (const char far *)"isupper") == 0) return pydos_str_isupper(self);
        if (_fstrcmp(method_name, (const char far *)"islower") == 0) return pydos_str_islower(self);
        if (_fstrcmp(method_name, (const char far *)"split") == 0) {
            return pydos_str_split_m(self, (argc > 1) ? argv[1] : (PyDosObj far *)0);
        }
        if (_fstrcmp(method_name, (const char far *)"rsplit") == 0) {
            return pydos_str_rsplit_m(self, (argc > 1) ? argv[1] : (PyDosObj far *)0);
        }
        if (_fstrcmp(method_name, (const char far *)"splitlines") == 0) return pydos_str_splitlines(self);
        if (_fstrcmp(method_name, (const char far *)"join") == 0) {
            if (argc > 1) return pydos_str_join_m(self, argv[1]);
            return pydos_obj_new_str((const char far *)"", 0);
        }
        if (_fstrcmp(method_name, (const char far *)"replace") == 0) {
            if (argc > 2) return pydos_str_replace_m(self, argv[1], argv[2]);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"center") == 0) {
            if (argc > 1) return pydos_str_center_m(self, argv[1]);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"ljust") == 0) {
            if (argc > 1) return pydos_str_ljust_m(self, argv[1]);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"rjust") == 0) {
            if (argc > 1) return pydos_str_rjust_m(self, argv[1]);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"zfill") == 0) {
            if (argc > 1) return pydos_str_zfill_m(self, argv[1]);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"encode") == 0) return pydos_str_encode(self);
        if (_fstrcmp(method_name, (const char far *)"format") == 0) return pydos_str_format_m(self);
    }

    /* ---- Dict methods ---- */
    if (stype == PYDT_DICT) {
        if (_fstrcmp(method_name, (const char far *)"get") == 0) {
            PyDosObj far *result;
            if (argc < 2) return pydos_obj_new_none();
            result = pydos_dict_get(self, argv[1]);
            if (result != (PyDosObj far *)0) return result;
            return (argc > 2) ? argv[2] : pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            pydos_dict_clear(self);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"items") == 0) {
            return pydos_dict_items(self);
        }
        if (_fstrcmp(method_name, (const char far *)"keys") == 0) {
            return pydos_dict_keys(self);
        }
        if (_fstrcmp(method_name, (const char far *)"values") == 0) {
            return pydos_dict_values(self);
        }
    }

    /* ---- Set methods ---- */
    if (stype == PYDT_SET) {
        if (_fstrcmp(method_name, (const char far *)"add") == 0) {
            if (argc > 1) {
                pydos_dict_set(self, argv[1], pydos_obj_new_none());
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"remove") == 0) {
            if (argc > 1) {
                if (!pydos_dict_contains(self, argv[1])) {
                    pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                                     (const char far *)"set.remove(x): x not in set");
                }
                pydos_dict_delete(self, argv[1]);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"discard") == 0) {
            if (argc > 1) {
                pydos_dict_delete(self, argv[1]);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            pydos_dict_clear(self);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"pop") == 0) {
            PyDosObj far *keys = pydos_dict_keys(self);
            if (keys != (PyDosObj far *)0 && keys->v.list.len > 0) {
                PyDosObj far *item = pydos_list_get(keys, 0L);
                pydos_dict_delete(self, item);
                PYDOS_DECREF(keys);
                return item;
            }
            if (keys != (PyDosObj far *)0) PYDOS_DECREF(keys);
            pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                             (const char far *)"pop from an empty set");
            return pydos_obj_new_none();
        }
    }

    /* ---- Frozenset methods ---- */
    if (stype == PYDT_FROZENSET) {
        if (_fstrcmp(method_name, (const char far *)"copy") == 0) {
            /* Frozenset is immutable — copy returns self with INCREF */
            PYDOS_INCREF(self);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"union") == 0) {
            if (argc > 1) {
                return pydos_frozenset_union(self, argv[1]);
            }
            PYDOS_INCREF(self);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"intersection") == 0) {
            if (argc > 1) {
                return pydos_frozenset_intersection(self, argv[1]);
            }
            PYDOS_INCREF(self);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"difference") == 0) {
            if (argc > 1) {
                return pydos_frozenset_difference(self, argv[1]);
            }
            PYDOS_INCREF(self);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"symmetric_difference") == 0) {
            if (argc > 1) {
                return pydos_frozenset_symmetric_difference(self, argv[1]);
            }
            PYDOS_INCREF(self);
            return self;
        }
        if (_fstrcmp(method_name, (const char far *)"issubset") == 0) {
            if (argc > 1) {
                return pydos_obj_new_bool(pydos_frozenset_issubset(self, argv[1]));
            }
            return pydos_obj_new_bool(0);
        }
        if (_fstrcmp(method_name, (const char far *)"issuperset") == 0) {
            if (argc > 1) {
                return pydos_obj_new_bool(pydos_frozenset_issuperset(self, argv[1]));
            }
            return pydos_obj_new_bool(0);
        }
        if (_fstrcmp(method_name, (const char far *)"isdisjoint") == 0) {
            if (argc > 1) {
                return pydos_obj_new_bool(pydos_frozenset_isdisjoint(self, argv[1]));
            }
            return pydos_obj_new_bool(1);
        }
    }

    /* ---- Complex methods ---- */
    if ((PyDosType)self->type == PYDT_COMPLEX) {
        if (_fstrcmp(method_name, (const char far *)"conjugate") == 0) {
            return pydos_complex_conjugate(self);
        }
        return pydos_obj_new_none();
    }

    /* ---- Bytearray methods ---- */
    if ((PyDosType)self->type == PYDT_BYTEARRAY) {
        if (_fstrcmp(method_name, (const char far *)"append") == 0) {
            if (argc >= 2) {
                PyDosObj far *val = argv[1];
                if (val != (PyDosObj far *)0 && (PyDosType)val->type == PYDT_INT) {
                    long v = val->v.int_val;
                    if (v >= 0 && v <= 255) {
                        pydos_bytearray_append(self, (unsigned char)v);
                    }
                }
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"pop") == 0) {
            int val = pydos_bytearray_pop(self);
            if (val < 0) return pydos_obj_new_int(0L);
            return pydos_obj_new_int((long)val);
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            pydos_bytearray_clear(self);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"copy") == 0) {
            return pydos_bytearray_from_data(self->v.bytearray.data, self->v.bytearray.len);
        }
        return pydos_obj_new_none();
    }

    /* ---- Generator / Coroutine methods ---- */
    if (stype == PYDT_GENERATOR || stype == PYDT_COROUTINE) {
        if (_fstrcmp(method_name, (const char far *)"send") == 0) {
            PyDosObj far *val = (argc > 1) ? argv[1] : pydos_obj_new_none();
            return pydos_gen_send(self, val);
        }
        if (_fstrcmp(method_name, (const char far *)"throw") == 0) {
            /* g.throw(exc_instance) — extract type code and message */
            if (argc > 1 && argv[1] != (PyDosObj far *)0 &&
                (PyDosType)argv[1]->type == PYDT_EXCEPTION) {
                int tcode = argv[1]->v.exc.type_code;
                const char far *msg = (const char far *)"";
                if (argv[1]->v.exc.message != (PyDosObj far *)0 &&
                    (PyDosType)argv[1]->v.exc.message->type == PYDT_STR) {
                    msg = argv[1]->v.exc.message->v.str.data;
                }
                return pydos_gen_throw(self, tcode, msg);
            }
            /* g.throw() with no valid exception — raise TypeError */
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                (const char far *)"throw() argument must be an exception instance");
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"close") == 0) {
            pydos_gen_close(self);
            return pydos_obj_new_none();
        }
    }

    /* ---- Int methods ---- */
    if (stype == PYDT_INT) {
        if (_fstrcmp(method_name, (const char far *)"bit_length") == 0) {
            return pydos_int_bit_length(self);
        }
    }

    /* ---- Instance methods (vtable dispatch) ---- */
    if (stype == PYDT_INSTANCE) {
        PyDosVTable far *vt = self->v.instance.vtable;
        if (vt != (PyDosVTable far *)0) {
            unsigned int mhash;
            void (far *mfunc)(void);

            /* Compute hash of method name */
            mhash = 5381U;
            {
                const char far *p = method_name;
                while (*p != '\0') {
                    mhash = ((mhash << 5) + mhash) + (unsigned char)*p;
                    p++;
                }
            }

            mfunc = pydos_vtable_lookup(vt, mhash);
            if (mfunc != (void (far *)(void))0) {
                /* Codegen-generated methods use cdecl with individual
                 * far-pointer args on the stack: func(self, arg1, arg2, ...).
                 * argv[0]=self, argv[1]=arg1, etc.
                 * Always pass 4 args, zero-padding omitted ones with NULL.
                 * This handles methods with default parameters correctly. */
                typedef PyDosObj far * (PYDOS_API far *MFn0)(void);
                typedef PyDosObj far * (PYDOS_API far *MFn1)(PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn2)(PyDosObj far *, PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn3)(PyDosObj far *, PyDosObj far *, PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn4)(PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn5)(PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn6)(PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn7)(PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
                typedef PyDosObj far * (PYDOS_API far *MFn8)(PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
                /* Always pass 8 args, padding missing ones with None.
                 * This ensures methods with default parameters (e.g.
                 * first(self, predicate=None)) receive the None singleton
                 * instead of stack garbage.  __cdecl: caller cleans up. */
                {
                    PyDosObj far *a[8];
                    PyDosObj far *none_val = pydos_obj_new_none();
                    unsigned int mi;
                    for (mi = 0; mi < 8; mi++) {
                        a[mi] = (mi < (unsigned int)argc) ? argv[mi] : none_val;
                    }
                    return ((MFn8)mfunc)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
                }
            }
        }
    }

    return pydos_obj_new_none();
}

/* ------------------------------------------------------------------ */
/* pydos_obj_get_attr — get an instance attribute by name              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_get_attr(PyDosObj far *obj,
                                             const char far *attr_name)
{
    unsigned int len;
    unsigned int hash;
    unsigned int idx, start;
    const char far *p;
    PyDosObj far *dict;
    PyDosDictEntry far *entries;
    unsigned int dsize;

    /* Complex attribute access: .real, .imag */
    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_COMPLEX) {
        if (_fstrcmp(attr_name, (const char far *)"real") == 0) {
            return pydos_obj_new_float(obj->v.complex_val.real);
        }
        if (_fstrcmp(attr_name, (const char far *)"imag") == 0) {
            return pydos_obj_new_float(obj->v.complex_val.imag);
        }
        return pydos_obj_new_none();
    }

    if (obj == (PyDosObj far *)0 ||
        (PyDosType)obj->type != PYDT_INSTANCE ||
        obj->v.instance.attrs == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }

    /* Compute length of attr_name up front (needed for __getattr__ too) */
    p = attr_name;
    len = 0;
    while (p[len] != '\0') len++;

    dict = obj->v.instance.attrs;
    if (dict->type == PYDT_DICT && dict->v.dict.size > 0) {
        hash = 5381U;
        {
            unsigned int i;
            for (i = 0; i < len; i++) {
                hash = ((hash << 5) + hash) + (unsigned char)attr_name[i];
            }
        }

        /* Direct dict lookup without creating a temporary key string */
        entries = dict->v.dict.entries;
        dsize = dict->v.dict.size;
        idx = hash & (dsize - 1);
        start = idx;

        do {
            if (entries[idx].key == (PyDosObj far *)0) {
                /* Empty slot — not found, fall through to __getattr__ */
                break;
            }
            if (entries[idx].hash == hash &&
                entries[idx].key != (PyDosObj far *)0 &&
                entries[idx].key->type == PYDT_STR &&
                entries[idx].key->v.str.len == len &&
                _fmemcmp(entries[idx].key->v.str.data, attr_name, len) == 0) {
                /* Found it */
                PYDOS_INCREF(entries[idx].value);
                return entries[idx].value;
            }
            idx = (idx + 1) & (dsize - 1);
        } while (idx != start);
    }

    /* Attribute not found — check __getattr__ vtable slot */
    if (obj->v.instance.vtable != (struct PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_GETATTR] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *GAFn)(PyDosObj far *, PyDosObj far *);
        GAFn ga_fn = (GAFn)obj->v.instance.vtable->slots[VSLOT_GETATTR];
        PyDosObj far *name_obj = pydos_obj_new_str(attr_name, len);
        PyDosObj far *result = ga_fn(obj, name_obj);
        PYDOS_DECREF(name_obj);
        return result;
    }

    return pydos_obj_new_none();
}

/* ------------------------------------------------------------------ */
/* pydos_obj_contains — polymorphic 'in' operator                      */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_contains(PyDosObj far *container, PyDosObj far *item)
{
    if (container == (PyDosObj far *)0) {
        return 0;
    }

    switch ((PyDosType)container->type) {
    case PYDT_LIST:
        return pydos_list_contains(container, item);
    case PYDT_DICT:
        return pydos_dict_contains(container, item);
    case PYDT_SET:
        return pydos_dict_contains(container, item);
    case PYDT_STR:
        /* 'x' in 'hello' — substring search */
        if (item != (PyDosObj far *)0 && item->type == PYDT_STR) {
            return pydos_str_find(container, item) >= 0 ? 1 : 0;
        }
        return 0;
    case PYDT_TUPLE: {
        unsigned int i;
        for (i = 0; i < container->v.tuple.len; i++) {
            if (pydos_obj_equal(container->v.tuple.items[i], item)) {
                return 1;
            }
        }
        return 0;
    }
    case PYDT_FROZENSET:
        return pydos_frozenset_contains(container, item);
    case PYDT_BYTEARRAY: {
        if (item != (PyDosObj far *)0 &&
            ((PyDosType)item->type == PYDT_INT || (PyDosType)item->type == PYDT_BOOL)) {
            long val = ((PyDosType)item->type == PYDT_INT) ? item->v.int_val : (long)item->v.bool_val;
            unsigned int i;
            if (val < 0 || val > 255) return 0;
            for (i = 0; i < container->v.bytearray.len; i++) {
                if (container->v.bytearray.data[i] == (unsigned char)val) return 1;
            }
        }
        return 0;
    }
    case PYDT_INSTANCE:
        /* Instance with __contains__: dispatch via vtable */
        if (container->v.instance.vtable != (PyDosVTable far *)0 &&
            container->v.instance.vtable->slots[VSLOT_CONTAINS] != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *ContainsFn)(PyDosObj far *, PyDosObj far *);
            PyDosObj far *res = ((ContainsFn)container->v.instance.vtable->slots[VSLOT_CONTAINS])(container, item);
            if (res != (PyDosObj far *)0 && pydos_obj_is_truthy(res)) {
                return 1;
            }
            return 0;
        }
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_compare — polymorphic ordering comparison                  */
/* Returns negative if a<b, 0 if a==b, positive if a>b                */
/*                                                                      */
/* Split into type-specific helpers so that Watcom does not allocate    */
/* stack space for ALL paths (doubles, vtable vars, hash loops) in a   */
/* single frame.  The monolithic version caused 8086 crashes under -od */
/* due to excessive frame size interacting with FP emulation.          */
/* ------------------------------------------------------------------ */

/* Fast-path: INT/BOOL comparison (no FP, no vtable, no strings) */
static int compare_int(long va, long vb)
{
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* Instance comparison via vtable __lt__/__gt__ (O(1) slot dispatch) */
static int compare_instance(PyDosObj far *a, PyDosObj far *b)
{
    PyDosVTable far *vt = a->v.instance.vtable;
    typedef PyDosObj far * (PYDOS_API far *CmpFn)(PyDosObj far *, PyDosObj far *);

    /* __lt__ via slot */
    if (vt->slots[VSLOT_LT] != (void (far *)(void))0) {
        PyDosObj far *res = ((CmpFn)vt->slots[VSLOT_LT])(a, b);
        if (res != (PyDosObj far *)0 && pydos_obj_is_truthy(res)) {
            PYDOS_DECREF(res);
            return -1;
        }
        if (res != (PyDosObj far *)0) PYDOS_DECREF(res);
    }
    /* __gt__ via slot */
    if (vt->slots[VSLOT_GT] != (void (far *)(void))0) {
        PyDosObj far *res = ((CmpFn)vt->slots[VSLOT_GT])(a, b);
        if (res != (PyDosObj far *)0 && pydos_obj_is_truthy(res)) {
            PYDOS_DECREF(res);
            return 1;
        }
        if (res != (PyDosObj far *)0) PYDOS_DECREF(res);
    }
    /* Try __eq__ for equality */
    if (pydos_obj_equal(a, b)) return 0;
    return 0;
}

/* Float comparison with int/bool promotion */
static int compare_float(PyDosObj far *a, PyDosObj far *b)
{
    double da = (a->type == PYDT_FLOAT) ? a->v.float_val :
                (a->type == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
    double db = (b->type == PYDT_FLOAT) ? b->v.float_val :
                (b->type == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Main dispatcher — thin, small frame */
int PYDOS_API pydos_obj_compare(PyDosObj far *a, PyDosObj far *b)
{
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[CMP ENTER a.type=");
    dbg_putint(a ? (int)a->type : -1);
    dbg_puts(" b.type=");
    dbg_putint(b ? (int)b->type : -1);
    dbg_puts("]\r\n");
#endif
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[CMP NULL => 0]\r\n");
#endif
        return 0;
    }

    /* Instance with vtable */
    if (a->type == PYDT_INSTANCE && a->v.instance.vtable != (PyDosVTable far *)0) {
        return compare_instance(a, b);
    }

    /* Int/Bool comparison */
    if ((a->type == PYDT_INT || a->type == PYDT_BOOL) &&
        (b->type == PYDT_INT || b->type == PYDT_BOOL)) {
        long va = (a->type == PYDT_BOOL) ? (long)a->v.bool_val : a->v.int_val;
        long vb = (b->type == PYDT_BOOL) ? (long)b->v.bool_val : b->v.int_val;
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[CMP va=");
        dbg_putlong(va);
        dbg_puts(" vb=");
        dbg_putlong(vb);
        if (va < vb) dbg_puts(" => -1]\r\n");
        else if (va > vb) dbg_puts(" => 1]\r\n");
        else dbg_puts(" => 0]\r\n");
#endif
        return compare_int(va, vb);
    }

    /* Float (including int-float promotion) */
    if ((a->type == PYDT_FLOAT || a->type == PYDT_INT || a->type == PYDT_BOOL) &&
        (b->type == PYDT_FLOAT || b->type == PYDT_INT || b->type == PYDT_BOOL) &&
        (a->type == PYDT_FLOAT || b->type == PYDT_FLOAT)) {
        return compare_float(a, b);
    }

    /* String comparison */
    if (a->type == PYDT_STR && b->type == PYDT_STR) {
        return pydos_str_compare(a, b);
    }

    /* Fallback: compare by type tag */
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[CMP FALLBACK a.type=");
    dbg_putint((int)a->type);
    dbg_puts(" b.type=");
    dbg_putint((int)b->type);
    dbg_puts("]\r\n");
#endif
    if (a->type != b->type) {
        return ((int)a->type < (int)b->type) ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_neg — polymorphic unary negation                          */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_neg(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }
    if ((PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_BOOL) {
        return pydos_int_neg(obj);
    }
    if ((PyDosType)obj->type == PYDT_FLOAT) {
        return pydos_obj_new_float(-obj->v.float_val);
    }
    if ((PyDosType)obj->type == PYDT_COMPLEX) {
        return pydos_complex_neg(obj);
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_NEG] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *UnaryFn)(PyDosObj far *);
        UnaryFn fn = (UnaryFn)obj->v.instance.vtable->slots[VSLOT_NEG];
        return fn(obj);
    }
    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_pos — polymorphic unary positive                          */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_pos(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }
    if ((PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_BOOL ||
        (PyDosType)obj->type == PYDT_FLOAT ||
        (PyDosType)obj->type == PYDT_COMPLEX) {
        PYDOS_INCREF(obj);
        return obj;
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_POS] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *UnaryFn)(PyDosObj far *);
        UnaryFn fn = (UnaryFn)obj->v.instance.vtable->slots[VSLOT_POS];
        return fn(obj);
    }
    PYDOS_INCREF(obj);
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_invert — polymorphic bitwise invert                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_invert(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }
    if ((PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_BOOL) {
        return pydos_int_bitnot(obj);
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_INVERT] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *UnaryFn)(PyDosObj far *);
        UnaryFn fn = (UnaryFn)obj->v.instance.vtable->slots[VSLOT_INVERT];
        return fn(obj);
    }
    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_func_new — create a first-class function object               */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_func_new(void (far *code)(void),
                                         const char far *name)
{
    PyDosObj far *obj;
    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    obj->type = PYDT_FUNCTION;
    obj->flags = 0;
    obj->refcount = 1;
    obj->v.func.code = code;
    obj->v.func.name = name;
    obj->v.func.defaults = (PyDosObj far *)0;
    obj->v.func.closure = (PyDosObj far *)0;
    return obj;
}
