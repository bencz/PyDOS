/*
 * pdos_bya.c - Bytearray type implementation for PyDOS runtime
 *
 * Mutable byte sequence with 2x growth strategy.
 * No PyDosObj child pointers (raw bytes only).
 */

#include "pdos_bya.h"
#include "pdos_mem.h"
#include <string.h>

#define BYA_MIN_CAP  8
#define BYA_MAX_CAP  65520U

/* ------------------------------------------------------------------ */
/* Internal: grow buffer to fit at least need_cap bytes                */
/* ------------------------------------------------------------------ */
static void bya_grow(PyDosObj far *ba, unsigned int need_cap)
{
    unsigned int new_cap;
    unsigned char far *new_data;

    if (ba->v.bytearray.cap >= need_cap) return;

    new_cap = ba->v.bytearray.cap;
    if (new_cap < BYA_MIN_CAP) new_cap = BYA_MIN_CAP;
    while (new_cap < need_cap) {
        if (new_cap > BYA_MAX_CAP / 2) {
            new_cap = BYA_MAX_CAP;
            break;
        }
        new_cap *= 2;
    }

    if (ba->v.bytearray.data == (unsigned char far *)0) {
        new_data = (unsigned char far *)pydos_far_alloc((unsigned long)new_cap);
    } else {
        new_data = (unsigned char far *)pydos_far_realloc(
            ba->v.bytearray.data, (unsigned long)new_cap);
    }
    if (new_data != (unsigned char far *)0) {
        ba->v.bytearray.data = new_data;
        ba->v.bytearray.cap = new_cap;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_new — create empty bytearray                        */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_bytearray_new(unsigned int initial_cap)
{
    PyDosObj far *obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    obj->type = (unsigned char)PYDT_BYTEARRAY;
    obj->v.bytearray.data = (unsigned char far *)0;
    obj->v.bytearray.len = 0;
    obj->v.bytearray.cap = 0;
    if (initial_cap > 0) {
        bya_grow(obj, initial_cap);
    }
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_new_zeroed — create bytearray filled with zeros     */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_bytearray_new_zeroed(unsigned int count)
{
    PyDosObj far *obj = pydos_bytearray_new(count);
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    if (count > 0 && obj->v.bytearray.data != (unsigned char far *)0) {
        _fmemset(obj->v.bytearray.data, 0, count);
        obj->v.bytearray.len = count;
    }
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_from_data — create from raw bytes                   */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_bytearray_from_data(
    const unsigned char far *data, unsigned int len)
{
    PyDosObj far *obj = pydos_bytearray_new(len);
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    if (len > 0 && obj->v.bytearray.data != (unsigned char far *)0 &&
        data != (const unsigned char far *)0) {
        _fmemcpy(obj->v.bytearray.data, data, len);
        obj->v.bytearray.len = len;
    }
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_append — append a single byte                       */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_bytearray_append(PyDosObj far *ba, unsigned char byte)
{
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return;
    bya_grow(ba, ba->v.bytearray.len + 1);
    if (ba->v.bytearray.len < ba->v.bytearray.cap) {
        ba->v.bytearray.data[ba->v.bytearray.len] = byte;
        ba->v.bytearray.len++;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_extend — extend with raw bytes                      */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_bytearray_extend(PyDosObj far *ba,
                                       const unsigned char far *data,
                                       unsigned int len)
{
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return;
    if (len == 0 || data == (const unsigned char far *)0) return;
    bya_grow(ba, ba->v.bytearray.len + len);
    if (ba->v.bytearray.len + len <= ba->v.bytearray.cap) {
        _fmemcpy(ba->v.bytearray.data + ba->v.bytearray.len, data, len);
        ba->v.bytearray.len += len;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_getitem                                             */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_bytearray_getitem(PyDosObj far *ba, int index)
{
    int actual;
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return -1;
    actual = index;
    if (actual < 0) actual += (int)ba->v.bytearray.len;
    if (actual < 0 || (unsigned int)actual >= ba->v.bytearray.len) return -1;
    return (int)ba->v.bytearray.data[actual];
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_setitem                                             */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_bytearray_setitem(PyDosObj far *ba, int index,
                                        unsigned char byte)
{
    int actual;
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return;
    actual = index;
    if (actual < 0) actual += (int)ba->v.bytearray.len;
    if (actual < 0 || (unsigned int)actual >= ba->v.bytearray.len) return;
    ba->v.bytearray.data[actual] = byte;
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_len                                                 */
/* ------------------------------------------------------------------ */
unsigned int PYDOS_API pydos_bytearray_len(PyDosObj far *ba)
{
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return 0;
    return ba->v.bytearray.len;
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_pop — remove and return last byte                   */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_bytearray_pop(PyDosObj far *ba)
{
    unsigned char val;
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return -1;
    if (ba->v.bytearray.len == 0) return -1;
    ba->v.bytearray.len--;
    val = ba->v.bytearray.data[ba->v.bytearray.len];
    return (int)val;
}

/* ------------------------------------------------------------------ */
/* pydos_bytearray_clear — remove all bytes                            */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_bytearray_clear(PyDosObj far *ba)
{
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return;
    ba->v.bytearray.len = 0;
}

/* ------------------------------------------------------------------ */
/* pydos_builtin_bytearray_conv — bytearray() constructor              */
/* bytearray()       -> empty                                          */
/* bytearray(int_n)  -> zero-filled n bytes                            */
/* bytearray(bytes)  -> copy from bytes object                         */
/* bytearray(list)   -> each element must be int 0-255                 */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_builtin_bytearray_conv(int argc,
                                                       PyDosObj far * far *argv)
{
    PyDosObj far *src;

    if (argc == 0) {
        return pydos_bytearray_new(0);
    }

    src = argv[0];
    if (src == (PyDosObj far *)0) {
        return pydos_bytearray_new(0);
    }

    /* bytearray(int) -> zero-filled */
    if ((PyDosType)src->type == PYDT_INT) {
        long n = src->v.int_val;
        if (n < 0) n = 0;
        if ((unsigned long)n > BYA_MAX_CAP) n = (long)BYA_MAX_CAP;
        return pydos_bytearray_new_zeroed((unsigned int)n);
    }

    /* bytearray(bytes) -> copy bytes data */
    if ((PyDosType)src->type == PYDT_BYTES || (PyDosType)src->type == PYDT_STR) {
        return pydos_bytearray_from_data(
            (const unsigned char far *)src->v.str.data, src->v.str.len);
    }

    /* bytearray(bytearray) -> copy */
    if ((PyDosType)src->type == PYDT_BYTEARRAY) {
        return pydos_bytearray_from_data(src->v.bytearray.data,
                                          src->v.bytearray.len);
    }

    /* bytearray(list) -> iterate, each must be int 0-255 */
    if ((PyDosType)src->type == PYDT_LIST) {
        PyDosObj far *result;
        unsigned int i;
        unsigned int n = src->v.list.len;
        result = pydos_bytearray_new(n);
        if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
        for (i = 0; i < n; i++) {
            PyDosObj far *item = src->v.list.items[i];
            long val;
            if (item == (PyDosObj far *)0) {
                val = 0;
            } else if ((PyDosType)item->type == PYDT_INT) {
                val = item->v.int_val;
            } else {
                val = 0;
            }
            if (val < 0) val = 0;
            if (val > 255) val = 255;
            pydos_bytearray_append(result, (unsigned char)val);
        }
        return result;
    }

    /* Fallback: empty bytearray */
    return pydos_bytearray_new(0);
}
