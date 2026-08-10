/*
 * pdos_bya.c - Bytearray type implementation for PyDOS runtime
 *
 * Mutable byte sequence with 2x growth strategy.
 * No PyDosObj child pointers (raw bytes only).
 */

#include "pdos_bya.h"
#include "pdos_mem.h"
#include "pdos_exc.h"
#include "pdos_slc.h"
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
        new_data = (unsigned char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)new_cap);
    } else {
        new_data = (unsigned char far *)pydos_mem_realloc(
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
    PyDosObj far *obj = pydos_obj_alloc_type(PYDT_BYTEARRAY);
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
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

PyDosObj far * PYDOS_API pydos_bytearray_slice(PyDosObj far *ba,
                                                long start, long stop,
                                                long step)
{
    PyDosObj far *result;
    long len;
    long i;

    if (ba == (PyDosObj far *)0 ||
        (PyDosType)ba->type != PYDT_BYTEARRAY) {
        return (PyDosObj far *)0;
    }
    len = (long)ba->v.bytearray.len;
    if (!pydos_slice_normalize(len, &start, &stop, step)) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"slice step cannot be zero");
        return (PyDosObj far *)0;
    }

    result = pydos_bytearray_new(0);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    if (step > 0) {
        for (i = start; i < stop; i += step) {
            pydos_bytearray_append(result, ba->v.bytearray.data[i]);
        }
    } else {
        for (i = start; i > stop; i += step) {
            pydos_bytearray_append(result, ba->v.bytearray.data[i]);
        }
    }
    return result;
}

PyDosObj far * PYDOS_API pydos_bytearray_concat(PyDosObj far *a,
                                                 PyDosObj far *b)
{
    PyDosObj far *result;
    unsigned long total;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0 ||
        (PyDosType)a->type != PYDT_BYTEARRAY ||
        (PyDosType)b->type != PYDT_BYTEARRAY) return (PyDosObj far *)0;
    total = (unsigned long)a->v.bytearray.len +
            (unsigned long)b->v.bytearray.len;
    if (total > BYA_MAX_CAP) return (PyDosObj far *)0;
    result = pydos_bytearray_new((unsigned int)total);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    pydos_bytearray_extend(result, a->v.bytearray.data,
                           a->v.bytearray.len);
    pydos_bytearray_extend(result, b->v.bytearray.data,
                           b->v.bytearray.len);
    return result;
}

PyDosObj far * PYDOS_API pydos_bytearray_repeat(PyDosObj far *ba,
                                                 long count)
{
    PyDosObj far *result;
    unsigned long total;
    long i;

    if (ba == (PyDosObj far *)0 ||
        (PyDosType)ba->type != PYDT_BYTEARRAY || count <= 0) {
        return pydos_bytearray_new(0);
    }
    total = (unsigned long)ba->v.bytearray.len * (unsigned long)count;
    if (total > BYA_MAX_CAP) return (PyDosObj far *)0;
    result = pydos_bytearray_new((unsigned int)total);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    for (i = 0; i < count; i++) {
        pydos_bytearray_extend(result, ba->v.bytearray.data,
                               ba->v.bytearray.len);
    }
    return result;
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

void PYDOS_API pydos_bytearray_insert(PyDosObj far *ba, long index,
                                      unsigned char byte)
{
    unsigned int i;
    long len;

    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return;
    len = (long)ba->v.bytearray.len;
    if (index < 0) {
        index += len;
        if (index < 0) index = 0;
    }
    if (index > len) index = len;

    bya_grow(ba, ba->v.bytearray.len + 1);
    if (ba->v.bytearray.len >= ba->v.bytearray.cap) return;
    i = ba->v.bytearray.len;
    while (i > (unsigned int)index) {
        ba->v.bytearray.data[i] = ba->v.bytearray.data[i - 1];
        i--;
    }
    ba->v.bytearray.data[(unsigned int)index] = byte;
    ba->v.bytearray.len++;
}

int PYDOS_API pydos_bytearray_pop_at(PyDosObj far *ba, long index)
{
    unsigned int i;
    unsigned char value;
    long len;

    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY)
        return -1;
    len = (long)ba->v.bytearray.len;
    if (index < 0) index += len;
    if (index < 0 || index >= len) return -1;

    value = ba->v.bytearray.data[(unsigned int)index];
    for (i = (unsigned int)index; i + 1 < ba->v.bytearray.len; i++) {
        ba->v.bytearray.data[i] = ba->v.bytearray.data[i + 1];
    }
    ba->v.bytearray.len--;
    return (int)value;
}

PyDosObj far * PYDOS_API pydos_bytearray_append_m(PyDosObj far *ba,
                                                   PyDosObj far *byte)
{
    long value;

    if (byte == (PyDosObj far *)0 ||
        ((PyDosType)byte->type != PYDT_INT &&
         (PyDosType)byte->type != PYDT_BOOL)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"an integer is required");
        return (PyDosObj far *)0;
    }
    value = (PyDosType)byte->type == PYDT_INT ? byte->v.int_val :
                                                (long)byte->v.bool_val;
    if (value < 0 || value > 255) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"byte must be in range(0, 256)");
        return (PyDosObj far *)0;
    }
    pydos_bytearray_append(ba, (unsigned char)value);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_bytearray_insert_m(PyDosObj far *ba,
                                                   PyDosObj far *index,
                                                   PyDosObj far *byte)
{
    long position;
    long value;

    if (index == (PyDosObj far *)0 ||
        ((PyDosType)index->type != PYDT_INT &&
         (PyDosType)index->type != PYDT_BOOL)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"index must be an integer");
        return (PyDosObj far *)0;
    }
    if (byte == (PyDosObj far *)0 ||
        ((PyDosType)byte->type != PYDT_INT &&
         (PyDosType)byte->type != PYDT_BOOL)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"an integer is required");
        return (PyDosObj far *)0;
    }
    position = (PyDosType)index->type == PYDT_INT ? index->v.int_val :
                                                    (long)index->v.bool_val;
    value = (PyDosType)byte->type == PYDT_INT ? byte->v.int_val :
                                                (long)byte->v.bool_val;
    if (value < 0 || value > 255) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"byte must be in range(0, 256)");
        return (PyDosObj far *)0;
    }
    pydos_bytearray_insert(ba, position, (unsigned char)value);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_bytearray_pop_m(PyDosObj far *ba,
                                                PyDosObj far *index)
{
    long position;
    int value;

    position = -1L;
    if (index != (PyDosObj far *)0 && (PyDosType)index->type != PYDT_NONE) {
        if ((PyDosType)index->type != PYDT_INT &&
            (PyDosType)index->type != PYDT_BOOL) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"index must be an integer");
            return (PyDosObj far *)0;
        }
        position = (PyDosType)index->type == PYDT_INT ? index->v.int_val :
                                                       (long)index->v.bool_val;
    }
    value = pydos_bytearray_pop_at(ba, position);
    if (value < 0) {
        pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                        (const char far *)"pop index out of range");
        return (PyDosObj far *)0;
    }
    return pydos_obj_new_int((long)value);
}

PyDosObj far * PYDOS_API pydos_bytearray_clear_m(PyDosObj far *ba)
{
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"clear requires a bytearray");
        return (PyDosObj far *)0;
    }
    pydos_bytearray_clear(ba);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_bytearray_len_m(PyDosObj far *ba)
{
    if (ba == (PyDosObj far *)0 || (PyDosType)ba->type != PYDT_BYTEARRAY) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__len__ requires a bytearray");
        return (PyDosObj far *)0;
    }
    return pydos_obj_new_int((long)ba->v.bytearray.len);
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

    if (argc == 0 || argv == (PyDosObj far * far *)0) {
        return pydos_bytearray_new(0);
    }

    src = argv[0];
    if (src == (PyDosObj far *)0 || (PyDosType)src->type == PYDT_NONE) {
        return pydos_bytearray_new(0);
    }

    /* bytearray(int) -> zero-filled */
    if ((PyDosType)src->type == PYDT_INT) {
        long n = src->v.int_val;
        if (n < 0 || (unsigned long)n > BYA_MAX_CAP) {
            pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                            (const char far *)"invalid bytearray length");
            return (PyDosObj far *)0;
        }
        return pydos_bytearray_new_zeroed((unsigned int)n);
    }

    /* bytearray(bytes) -> copy bytes data */
    if ((PyDosType)src->type == PYDT_BYTES) {
        return pydos_bytearray_from_data(
            (const unsigned char far *)src->v.str.data, src->v.str.len);
    }

    /* bytearray(bytearray) -> copy */
    if ((PyDosType)src->type == PYDT_BYTEARRAY) {
        return pydos_bytearray_from_data(src->v.bytearray.data,
                                          src->v.bytearray.len);
    }

    /* bytearray(list/tuple) -> each element must be int 0-255 */
    if ((PyDosType)src->type == PYDT_LIST ||
        (PyDosType)src->type == PYDT_TUPLE) {
        PyDosObj far *result;
        unsigned int i;
        unsigned int n = (PyDosType)src->type == PYDT_LIST ?
                         src->v.list.len : src->v.tuple.len;
        result = pydos_bytearray_new(n);
        if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
        for (i = 0; i < n; i++) {
            PyDosObj far *item = (PyDosType)src->type == PYDT_LIST ?
                                 src->v.list.items[i] : src->v.tuple.items[i];
            long val;
            if (item == (PyDosObj far *)0 ||
                ((PyDosType)item->type != PYDT_INT &&
                 (PyDosType)item->type != PYDT_BOOL)) {
                PYDOS_DECREF(result);
                pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"bytearray elements must be integers");
                return (PyDosObj far *)0;
            }
            val = (PyDosType)item->type == PYDT_INT ? item->v.int_val :
                                                      (long)item->v.bool_val;
            if (val < 0 || val > 255) {
                PYDOS_DECREF(result);
                pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                    (const char far *)"byte must be in range(0, 256)");
                return (PyDosObj far *)0;
            }
            pydos_bytearray_append(result, (unsigned char)val);
        }
        return result;
    }

    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"cannot convert object to bytearray");
    return (PyDosObj far *)0;
}
