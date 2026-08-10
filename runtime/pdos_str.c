/*
 * pydos_str.c - String operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_str.h"
#include "pdos_obj.h"
#include "pdos_exc.h"
#include "pdos_slc.h"
#include <string.h>
#include <stdlib.h>

#include "pdos_mem.h"

/*
 * djb2_hash_far - Compute DJB2 hash of far string data.
 * hash = 5381; for each byte: hash = hash * 33 + c
 * We use ((hash << 5) + hash) + c to avoid 32-bit multiply.
 */
static unsigned int djb2_hash_far(const char far *data, unsigned int len)
{
    unsigned int hash;
    unsigned int i;

    hash = 5381;
    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)data[i];
    }
    return hash;
}

PyDosObj far * PYDOS_API pydos_str_new(const char far *data, unsigned int len)
{
    return pydos_obj_new_str(data, len);
}

PyDosObj far * PYDOS_API pydos_str_from_cstr(const char *s)
{
    unsigned int len;

    if (s == (const char *)0) {
        return pydos_obj_new_str((const char far *)0, 0);
    }

    len = (unsigned int)strlen(s);
    return pydos_obj_new_str((const char far *)s, len);
}

PyDosObj far * PYDOS_API pydos_str_concat(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int alen, blen, total;
    char far *buf;
    PyDosObj far *result;

    if (a == (PyDosObj far *)0 || a->type != PYDT_STR ||
        b == (PyDosObj far *)0 || b->type != PYDT_STR) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    alen = a->v.str.len;
    blen = b->v.str.len;
    total = alen + blen;

    if (total == 0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    buf = (char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)total + 1UL);
    if (buf == (char far *)0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    if (alen > 0) {
        _fmemcpy(buf, a->v.str.data, alen);
    }
    if (blen > 0) {
        _fmemcpy(buf + alen, b->v.str.data, blen);
    }
    buf[total] = '\0';

    result = pydos_obj_new_str(buf, total);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_repeat(PyDosObj far *s, long count)
{
    unsigned int slen, total;
    long i;
    char far *buf;
    PyDosObj far *result;

    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || count <= 0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    slen = s->v.str.len;
    if (slen == 0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    /* Check for overflow */
    if (count > 65535L / (long)slen) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    total = (unsigned int)(slen * (unsigned int)count);
    buf = (char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)total + 1UL);
    if (buf == (char far *)0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    for (i = 0; i < count; i++) {
        _fmemcpy(buf + (unsigned int)(i * slen), s->v.str.data, slen);
    }
    buf[total] = '\0';

    result = pydos_obj_new_str(buf, total);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_slice(PyDosObj far *s, long start, long stop, long step)
{
    long slen, i, count, pos;
    char far *buf;
    PyDosObj far *result;

    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    slen = (long)s->v.str.len;

    if (!pydos_slice_normalize(slen, &start, &stop, step)) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"slice step cannot be zero");
        return (PyDosObj far *)0;
    }

    /* Count characters in the slice */
    count = 0;
    if (step > 0) {
        for (i = start; i < stop; i += step) {
            count++;
        }
    } else {
        for (i = start; i > stop; i += step) {
            count++;
        }
    }

    if (count <= 0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    buf = (char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)count + 1UL);
    if (buf == (char far *)0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    pos = 0;
    if (step > 0) {
        for (i = start; i < stop; i += step) {
            buf[pos] = s->v.str.data[i];
            pos++;
        }
    } else {
        for (i = start; i > stop; i += step) {
            buf[pos] = s->v.str.data[i];
            pos++;
        }
    }
    buf[count] = '\0';

    result = pydos_obj_new_str(buf, (unsigned int)count);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_slice_op(PyDosObj far *s,
                                             long start, long stop, long step)
{
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"slicing requires a string");
        return (PyDosObj far *)0;
    }
    return pydos_str_slice(s, start, stop, step);
}

PyDosObj far * PYDOS_API pydos_str_index(PyDosObj far *s, long idx)
{
    long slen;
    char c;

    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) {
        return (PyDosObj far *)0;
    }

    slen = (long)s->v.str.len;
    if (idx < 0) {
        idx += slen;
    }
    if (idx < 0 || idx >= slen) {
        return (PyDosObj far *)0;
    }

    c = s->v.str.data[idx];
    return pydos_obj_new_str((const char far *)&c, 1);
}

PyDosObj far * PYDOS_API pydos_str_index_op(PyDosObj far *s, long idx)
{
    PyDosObj far *result = pydos_str_index(s, idx);
    if (result == (PyDosObj far *)0 && !pydos_exc_pending())
        pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                        (const char far *)"string index out of range");
    return result;
}

long PYDOS_API pydos_str_find(PyDosObj far *s, PyDosObj far *sub)
{
    unsigned int slen, sublen;
    unsigned int i, j;
    int found;

    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        sub == (PyDosObj far *)0 || sub->type != PYDT_STR) {
        return -1L;
    }

    slen = s->v.str.len;
    sublen = sub->v.str.len;

    if (sublen == 0) {
        return 0L;
    }
    if (sublen > slen) {
        return -1L;
    }

    for (i = 0; i <= slen - sublen; i++) {
        found = 1;
        for (j = 0; j < sublen; j++) {
            if (s->v.str.data[i + j] != sub->v.str.data[j]) {
                found = 0;
                break;
            }
        }
        if (found) {
            return (long)i;
        }
    }
    return -1L;
}

long PYDOS_API pydos_str_len(PyDosObj far *s)
{
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) {
        return 0L;
    }
    return (long)s->v.str.len;
}

int PYDOS_API pydos_str_equal(PyDosObj far *a, PyDosObj far *b)
{
    if (a == (PyDosObj far *)0 || a->type != PYDT_STR ||
        b == (PyDosObj far *)0 || b->type != PYDT_STR) {
        return 0;
    }

    if (a->v.str.len != b->v.str.len) {
        return 0;
    }

    /* Quick hash check */
    if (a->v.str.hash != 0 && b->v.str.hash != 0) {
        if (a->v.str.hash != b->v.str.hash) {
            return 0;
        }
    }

    if (a->v.str.len == 0) {
        return 1;
    }

    return (_fmemcmp(a->v.str.data, b->v.str.data, a->v.str.len) == 0) ? 1 : 0;
}

int PYDOS_API pydos_str_compare(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int minlen;
    int cmp;

    if (a == (PyDosObj far *)0 || a->type != PYDT_STR ||
        b == (PyDosObj far *)0 || b->type != PYDT_STR) {
        return 0;
    }

    minlen = (a->v.str.len < b->v.str.len) ? a->v.str.len : b->v.str.len;

    if (minlen > 0) {
        cmp = _fmemcmp(a->v.str.data, b->v.str.data, minlen);
        if (cmp < 0) return -1;
        if (cmp > 0) return 1;
    }

    /* Equal up to minlen; shorter string is less */
    if (a->v.str.len < b->v.str.len) return -1;
    if (a->v.str.len > b->v.str.len) return 1;
    return 0;
}

unsigned int PYDOS_API pydos_str_hash(PyDosObj far *s)
{
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) {
        return 0;
    }

    /* If hash is already cached, return it */
    if (s->v.str.hash != 0) {
        return s->v.str.hash;
    }

    s->v.str.hash = djb2_hash_far(s->v.str.data, s->v.str.len);
    /* Avoid hash value of 0, which means "not computed" */
    if (s->v.str.hash == 0) {
        s->v.str.hash = 1;
    }
    return s->v.str.hash;
}

PyDosObj far * PYDOS_API pydos_str_format_int(long val)
{
    char buf[12]; /* "-2147483648" is 11 chars + null */
    int pos, neg, i, j;
    unsigned long uval;
    char tmp;

    pos = 0;
    neg = 0;

    if (val < 0) {
        neg = 1;
        /* Handle LONG_MIN carefully */
        uval = (unsigned long)(-(val + 1)) + 1UL;
    } else {
        uval = (unsigned long)val;
    }

    /* Generate digits in reverse order */
    if (uval == 0) {
        buf[pos] = '0';
        pos++;
    } else {
        while (uval > 0) {
            buf[pos] = (char)('0' + (int)(uval % 10UL));
            pos++;
            uval /= 10UL;
        }
    }

    if (neg) {
        buf[pos] = '-';
        pos++;
    }

    /* Reverse the string */
    for (i = 0, j = pos - 1; i < j; i++, j--) {
        tmp = buf[i];
        buf[i] = buf[j];
        buf[j] = tmp;
    }

    buf[pos] = '\0';
    return pydos_obj_new_str((const char far *)buf, (unsigned int)pos);
}
