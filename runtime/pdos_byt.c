/*
 * pdos_byt.c - Immutable bytes primitive for the PyDOS runtime
 *
 * Bytes reuse the string payload layout (data, length, cached hash), but
 * carry PYDT_BYTES and therefore remain distinct from text strings.
 */

#include "pdos_byt.h"
#include "pdos_mem.h"
#include "pdos_exc.h"
#include "pdos_slc.h"
#include <string.h>

#define BYTES_MAX_LEN 65520U

static unsigned int bytes_hash(const unsigned char far *data,
                               unsigned int len)
{
    unsigned int hash;
    unsigned int i;

    hash = 5381U;
    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + data[i];
    }
    return hash == 0 ? 1U : hash;
}

PyDosObj far * PYDOS_API pydos_bytes_new(
    const unsigned char far *data, unsigned int len)
{
    PyDosObj far *obj;
    char far *buffer;

    obj = pydos_obj_alloc_type(PYDT_BYTES);
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;

    buffer = (char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)len + 1UL);
    if (buffer == (char far *)0) {
        pydos_obj_free(obj);
        return (PyDosObj far *)0;
    }
    if (data != (const unsigned char far *)0 && len > 0) {
        _fmemcpy(buffer, data, len);
    }
    buffer[len] = '\0';

    obj->v.str.data = buffer;
    obj->v.str.len = len;
    obj->v.str.hash = bytes_hash((const unsigned char far *)buffer, len);
    return obj;
}

PyDosObj far * PYDOS_API pydos_bytes_new_zeroed(unsigned int len)
{
    PyDosObj far *result;

    result = pydos_bytes_new((const unsigned char far *)0, len);
    if (result != (PyDosObj far *)0 && len > 0) {
        _fmemset(result->v.str.data, 0, len);
        result->v.str.hash = bytes_hash(
            (const unsigned char far *)result->v.str.data, len);
    }
    return result;
}

PyDosObj far * PYDOS_API pydos_bytes_concat(
    PyDosObj far *a, PyDosObj far *b)
{
    unsigned int alen;
    unsigned int blen;
    unsigned int total;
    unsigned char far *buffer;
    PyDosObj far *result;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0 ||
        (PyDosType)a->type != PYDT_BYTES ||
        (PyDosType)b->type != PYDT_BYTES) return (PyDosObj far *)0;
    alen = a->v.str.len;
    blen = b->v.str.len;
    if (alen > BYTES_MAX_LEN - blen) return (PyDosObj far *)0;
    total = alen + blen;
    buffer = (unsigned char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)total);
    if (buffer == (unsigned char far *)0 && total > 0) return (PyDosObj far *)0;
    if (alen > 0) _fmemcpy(buffer, a->v.str.data, alen);
    if (blen > 0) _fmemcpy(buffer + alen, b->v.str.data, blen);
    result = pydos_bytes_new(buffer, total);
    if (buffer != (unsigned char far *)0) pydos_far_free(buffer);
    return result;
}

PyDosObj far * PYDOS_API pydos_bytes_repeat(PyDosObj far *value, long count)
{
    unsigned int len;
    unsigned int total;
    unsigned int i;
    unsigned char far *buffer;
    PyDosObj far *result;

    if (value == (PyDosObj far *)0 ||
        (PyDosType)value->type != PYDT_BYTES || count <= 0) {
        return pydos_bytes_new((const unsigned char far *)0, 0);
    }
    len = value->v.str.len;
    if (len == 0) return pydos_bytes_new((const unsigned char far *)0, 0);
    if (count > (long)(BYTES_MAX_LEN / len)) return (PyDosObj far *)0;
    total = (unsigned int)(len * (unsigned int)count);
    buffer = (unsigned char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)total);
    if (buffer == (unsigned char far *)0) return (PyDosObj far *)0;
    for (i = 0; i < (unsigned int)count; i++) {
        _fmemcpy(buffer + i * len, value->v.str.data, len);
    }
    result = pydos_bytes_new(buffer, total);
    pydos_far_free(buffer);
    return result;
}

PyDosObj far * PYDOS_API pydos_bytes_slice(
    PyDosObj far *value, long start, long stop, long step)
{
    long len;
    long count;
    long i;
    long pos;
    unsigned char far *buffer;
    PyDosObj far *result;

    if (value == (PyDosObj far *)0 ||
        (PyDosType)value->type != PYDT_BYTES) {
        return (PyDosObj far *)0;
    }
    len = (long)value->v.str.len;
    if (!pydos_slice_normalize(len, &start, &stop, step)) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"slice step cannot be zero");
        return (PyDosObj far *)0;
    }
    count = 0;
    if (step > 0) {
        for (i = start; i < stop; i += step) count++;
    } else {
        for (i = start; i > stop; i += step) count++;
    }
    if (count == 0) return pydos_bytes_new((const unsigned char far *)0, 0);
    buffer = (unsigned char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)count);
    if (buffer == (unsigned char far *)0) return (PyDosObj far *)0;
    pos = 0;
    if (step > 0) {
        for (i = start; i < stop; i += step) buffer[pos++] =
            (unsigned char)value->v.str.data[i];
    } else {
        for (i = start; i > stop; i += step) buffer[pos++] =
            (unsigned char)value->v.str.data[i];
    }
    result = pydos_bytes_new(buffer, (unsigned int)count);
    pydos_far_free(buffer);
    return result;
}

int PYDOS_API pydos_bytes_getitem(PyDosObj far *value, long index)
{
    long len;

    if (value == (PyDosObj far *)0 ||
        (PyDosType)value->type != PYDT_BYTES) return -1;
    len = (long)value->v.str.len;
    if (index < 0) index += len;
    if (index < 0 || index >= len) return -1;
    return (int)(unsigned char)value->v.str.data[index];
}

int PYDOS_API pydos_bytes_compare(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int min_len;
    int cmp;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0 ||
        (PyDosType)a->type != PYDT_BYTES ||
        (PyDosType)b->type != PYDT_BYTES) return 0;
    min_len = a->v.str.len < b->v.str.len ? a->v.str.len : b->v.str.len;
    if (min_len > 0) {
        cmp = _fmemcmp(a->v.str.data, b->v.str.data, min_len);
        if (cmp < 0) return -1;
        if (cmp > 0) return 1;
    }
    if (a->v.str.len < b->v.str.len) return -1;
    if (a->v.str.len > b->v.str.len) return 1;
    return 0;
}

PyDosObj far * PYDOS_API pydos_builtin_bytes_conv(
    int argc, PyDosObj far * far *argv)
{
    PyDosObj far *source;
    PyDosObj far *result;
    unsigned int i;

    if (argc == 0 || argv == (PyDosObj far * far *)0 ||
        argv[0] == (PyDosObj far *)0) {
        return pydos_bytes_new((const unsigned char far *)0, 0);
    }
    source = argv[0];
    if ((PyDosType)source->type == PYDT_NONE) {
        return pydos_bytes_new((const unsigned char far *)0, 0);
    }
    if ((PyDosType)source->type == PYDT_INT) {
        long len = source->v.int_val;
        if (len < 0 || (unsigned long)len > BYTES_MAX_LEN) {
            pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                            (const char far *)"invalid bytes length");
            return (PyDosObj far *)0;
        }
        return pydos_bytes_new_zeroed((unsigned int)len);
    }
    if ((PyDosType)source->type == PYDT_BYTES) {
        return pydos_bytes_new((const unsigned char far *)source->v.str.data,
                               source->v.str.len);
    }
    if ((PyDosType)source->type == PYDT_BYTEARRAY) {
        return pydos_bytes_new(source->v.bytearray.data,
                               source->v.bytearray.len);
    }
    if ((PyDosType)source->type == PYDT_INSTANCE &&
        pydos_obj_has_attr(source, (const char far *)"__bytes__")) {
        PyDosObj far *method_args[1];
        method_args[0] = source;
        result = pydos_obj_call_method(
            (const char far *)"__bytes__", 1U, method_args);
        if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
        if ((PyDosType)result->type != PYDT_BYTES) {
            PYDOS_DECREF(result);
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"__bytes__ returned non-bytes");
            return (PyDosObj far *)0;
        }
        return result;
    }
    if ((PyDosType)source->type != PYDT_LIST &&
        (PyDosType)source->type != PYDT_TUPLE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"cannot convert object to bytes");
        return (PyDosObj far *)0;
    }

    result = pydos_bytes_new_zeroed(
        (PyDosType)source->type == PYDT_LIST ? source->v.list.len :
                                               source->v.tuple.len);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    for (i = 0; i < result->v.str.len; i++) {
        PyDosObj far *item = (PyDosType)source->type == PYDT_LIST ?
            source->v.list.items[i] : source->v.tuple.items[i];
        long value;
        if (item == (PyDosObj far *)0 ||
            ((PyDosType)item->type != PYDT_INT &&
             (PyDosType)item->type != PYDT_BOOL)) {
            PYDOS_DECREF(result);
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"bytes elements must be integers");
            return (PyDosObj far *)0;
        }
        value = (PyDosType)item->type == PYDT_INT ? item->v.int_val :
                                                    (long)item->v.bool_val;
        if (value < 0 || value > 255) {
            PYDOS_DECREF(result);
            pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                            (const char far *)"bytes must be in range(0, 256)");
            return (PyDosObj far *)0;
        }
        result->v.str.data[i] = (char)(unsigned char)value;
    }
    result->v.str.hash = bytes_hash(
        (const unsigned char far *)result->v.str.data, result->v.str.len);
    return result;
}
