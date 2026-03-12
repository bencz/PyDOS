/*
 * pydos_str.c - String operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_str.h"
#include "pdos_obj.h"
#include "pdos_lst.h"
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

    buf = (char far *)pydos_far_alloc((unsigned long)total + 1);
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
    buf = (char far *)pydos_far_alloc((unsigned long)total + 1);
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

/*
 * clamp_index - Adjust a Python-style slice index to be within [0, len].
 */
static long clamp_index(long idx, long len)
{
    if (idx < 0) {
        idx += len;
        if (idx < 0) {
            idx = 0;
        }
    }
    if (idx > len) {
        idx = len;
    }
    return idx;
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

    if (step == 0) {
        /* step of 0 is an error; return empty string */
        return pydos_obj_new_str((const char far *)"", 0);
    }

    /* Pre-clamp LONG_MAX sentinel to slen (workaround for Watcom 8086
     * optimizer bug with 32-bit comparison of LONG_MAX) */
    if (start == 0x7FFFFFFFL) start = slen;
    if (stop == 0x7FFFFFFFL) stop = slen;

    /* Clamp start and stop */
    start = clamp_index(start, slen);
    stop = clamp_index(stop, slen);

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

    buf = (char far *)pydos_far_alloc((unsigned long)count + 1);
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

/* ------------------------------------------------------------------ */
/* String methods                                                      */
/* ------------------------------------------------------------------ */

static int is_whitespace(char c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
}

PyDosObj far * PYDOS_API pydos_str_upper(PyDosObj far *s)
{
    unsigned int i, len;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    buf = (char far *)pydos_far_alloc((unsigned long)len + 1);
    if (!buf) return pydos_obj_new_str((const char far *)"", 0);
    for (i = 0; i < len; i++) {
        char c = s->v.str.data[i];
        buf[i] = (c >= 'a' && c <= 'z') ? (char)(c - 32) : c;
    }
    buf[len] = '\0';
    result = pydos_obj_new_str(buf, len);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_lower(PyDosObj far *s)
{
    unsigned int i, len;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    buf = (char far *)pydos_far_alloc((unsigned long)len + 1);
    if (!buf) return pydos_obj_new_str((const char far *)"", 0);
    for (i = 0; i < len; i++) {
        char c = s->v.str.data[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    buf[len] = '\0';
    result = pydos_obj_new_str(buf, len);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_title(PyDosObj far *s)
{
    unsigned int i, len;
    int after_space;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    buf = (char far *)pydos_far_alloc((unsigned long)len + 1);
    if (!buf) return pydos_obj_new_str((const char far *)"", 0);
    after_space = 1;
    for (i = 0; i < len; i++) {
        char c = s->v.str.data[i];
        if (after_space && c >= 'a' && c <= 'z') {
            buf[i] = (char)(c - 32);
        } else if (!after_space && c >= 'A' && c <= 'Z') {
            buf[i] = (char)(c + 32);
        } else {
            buf[i] = c;
        }
        after_space = !((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'));
    }
    buf[len] = '\0';
    result = pydos_obj_new_str(buf, len);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_capitalize(PyDosObj far *s)
{
    unsigned int i, len;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    buf = (char far *)pydos_far_alloc((unsigned long)len + 1);
    if (!buf) return pydos_obj_new_str((const char far *)"", 0);
    buf[0] = (s->v.str.data[0] >= 'a' && s->v.str.data[0] <= 'z')
             ? (char)(s->v.str.data[0] - 32) : s->v.str.data[0];
    for (i = 1; i < len; i++) {
        char c = s->v.str.data[i];
        buf[i] = (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c;
    }
    buf[len] = '\0';
    result = pydos_obj_new_str(buf, len);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_swapcase(PyDosObj far *s)
{
    unsigned int i, len;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    buf = (char far *)pydos_far_alloc((unsigned long)len + 1);
    if (!buf) return pydos_obj_new_str((const char far *)"", 0);
    for (i = 0; i < len; i++) {
        char c = s->v.str.data[i];
        if (c >= 'a' && c <= 'z') buf[i] = (char)(c - 32);
        else if (c >= 'A' && c <= 'Z') buf[i] = (char)(c + 32);
        else buf[i] = c;
    }
    buf[len] = '\0';
    result = pydos_obj_new_str(buf, len);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_strip(PyDosObj far *s)
{
    unsigned int len, start, end;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    start = 0;
    while (start < len && is_whitespace(s->v.str.data[start])) start++;
    end = len;
    while (end > start && is_whitespace(s->v.str.data[end - 1])) end--;
    return pydos_obj_new_str(s->v.str.data + start, end - start);
}

PyDosObj far * PYDOS_API pydos_str_lstrip(PyDosObj far *s)
{
    unsigned int len, start;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    start = 0;
    while (start < len && is_whitespace(s->v.str.data[start])) start++;
    return pydos_obj_new_str(s->v.str.data + start, len - start);
}

PyDosObj far * PYDOS_API pydos_str_rstrip(PyDosObj far *s)
{
    unsigned int len, end;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    len = s->v.str.len;
    if (len == 0) return pydos_obj_new_str((const char far *)"", 0);
    end = len;
    while (end > 0 && is_whitespace(s->v.str.data[end - 1])) end--;
    return pydos_obj_new_str(s->v.str.data, end);
}

PyDosObj far * PYDOS_API pydos_str_find_m(PyDosObj far *s, PyDosObj far *sub)
{
    return pydos_obj_new_int(pydos_str_find(s, sub));
}

PyDosObj far * PYDOS_API pydos_str_rfind_m(PyDosObj far *s, PyDosObj far *sub)
{
    unsigned int slen, sublen, i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        sub == (PyDosObj far *)0 || sub->type != PYDT_STR) return pydos_obj_new_int(-1L);
    slen = s->v.str.len;
    sublen = sub->v.str.len;
    if (sublen == 0) return pydos_obj_new_int((long)slen);
    if (sublen > slen) return pydos_obj_new_int(-1L);
    for (i = slen - sublen + 1; i > 0; i--) {
        if (_fmemcmp(s->v.str.data + i - 1, sub->v.str.data, sublen) == 0)
            return pydos_obj_new_int((long)(i - 1));
    }
    return pydos_obj_new_int(-1L);
}

PyDosObj far * PYDOS_API pydos_str_index_m(PyDosObj far *s, PyDosObj far *sub)
{
    long idx = pydos_str_find(s, sub);
    /* Python raises ValueError on not found; we return -1 for now */
    return pydos_obj_new_int(idx);
}

PyDosObj far * PYDOS_API pydos_str_rindex_m(PyDosObj far *s, PyDosObj far *sub)
{
    return pydos_str_rfind_m(s, sub);
}

PyDosObj far * PYDOS_API pydos_str_count_m(PyDosObj far *s, PyDosObj far *sub)
{
    unsigned int slen, sublen, i;
    long count;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        sub == (PyDosObj far *)0 || sub->type != PYDT_STR) return pydos_obj_new_int(0L);
    slen = s->v.str.len;
    sublen = sub->v.str.len;
    if (sublen == 0) return pydos_obj_new_int((long)slen + 1);
    if (sublen > slen) return pydos_obj_new_int(0L);
    count = 0;
    for (i = 0; i <= slen - sublen; i++) {
        if (_fmemcmp(s->v.str.data + i, sub->v.str.data, sublen) == 0) {
            count++;
            i += sublen - 1; /* non-overlapping */
        }
    }
    return pydos_obj_new_int(count);
}

PyDosObj far * PYDOS_API pydos_str_startswith(PyDosObj far *s, PyDosObj far *prefix)
{
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        prefix == (PyDosObj far *)0 || prefix->type != PYDT_STR) return pydos_obj_new_bool(0);
    if (prefix->v.str.len > s->v.str.len) return pydos_obj_new_bool(0);
    if (prefix->v.str.len == 0) return pydos_obj_new_bool(1);
    return pydos_obj_new_bool(_fmemcmp(s->v.str.data, prefix->v.str.data, prefix->v.str.len) == 0 ? 1 : 0);
}

PyDosObj far * PYDOS_API pydos_str_endswith(PyDosObj far *s, PyDosObj far *suffix)
{
    unsigned int off;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        suffix == (PyDosObj far *)0 || suffix->type != PYDT_STR) return pydos_obj_new_bool(0);
    if (suffix->v.str.len > s->v.str.len) return pydos_obj_new_bool(0);
    if (suffix->v.str.len == 0) return pydos_obj_new_bool(1);
    off = s->v.str.len - suffix->v.str.len;
    return pydos_obj_new_bool(_fmemcmp(s->v.str.data + off, suffix->v.str.data, suffix->v.str.len) == 0 ? 1 : 0);
}

PyDosObj far * PYDOS_API pydos_str_isdigit(PyDosObj far *s)
{
    unsigned int i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || s->v.str.len == 0) return pydos_obj_new_bool(0);
    for (i = 0; i < s->v.str.len; i++) {
        char c = s->v.str.data[i];
        if (c < '0' || c > '9') return pydos_obj_new_bool(0);
    }
    return pydos_obj_new_bool(1);
}

PyDosObj far * PYDOS_API pydos_str_isalpha(PyDosObj far *s)
{
    unsigned int i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || s->v.str.len == 0) return pydos_obj_new_bool(0);
    for (i = 0; i < s->v.str.len; i++) {
        char c = s->v.str.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'))) return pydos_obj_new_bool(0);
    }
    return pydos_obj_new_bool(1);
}

PyDosObj far * PYDOS_API pydos_str_isalnum(PyDosObj far *s)
{
    unsigned int i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || s->v.str.len == 0) return pydos_obj_new_bool(0);
    for (i = 0; i < s->v.str.len; i++) {
        char c = s->v.str.data[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')))
            return pydos_obj_new_bool(0);
    }
    return pydos_obj_new_bool(1);
}

PyDosObj far * PYDOS_API pydos_str_isspace(PyDosObj far *s)
{
    unsigned int i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || s->v.str.len == 0) return pydos_obj_new_bool(0);
    for (i = 0; i < s->v.str.len; i++) {
        if (!is_whitespace(s->v.str.data[i])) return pydos_obj_new_bool(0);
    }
    return pydos_obj_new_bool(1);
}

PyDosObj far * PYDOS_API pydos_str_isupper(PyDosObj far *s)
{
    unsigned int i;
    int has_alpha;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || s->v.str.len == 0) return pydos_obj_new_bool(0);
    has_alpha = 0;
    for (i = 0; i < s->v.str.len; i++) {
        char c = s->v.str.data[i];
        if (c >= 'a' && c <= 'z') return pydos_obj_new_bool(0);
        if (c >= 'A' && c <= 'Z') has_alpha = 1;
    }
    return pydos_obj_new_bool(has_alpha);
}

PyDosObj far * PYDOS_API pydos_str_islower(PyDosObj far *s)
{
    unsigned int i;
    int has_alpha;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR || s->v.str.len == 0) return pydos_obj_new_bool(0);
    has_alpha = 0;
    for (i = 0; i < s->v.str.len; i++) {
        char c = s->v.str.data[i];
        if (c >= 'A' && c <= 'Z') return pydos_obj_new_bool(0);
        if (c >= 'a' && c <= 'z') has_alpha = 1;
    }
    return pydos_obj_new_bool(has_alpha);
}

PyDosObj far * PYDOS_API pydos_str_split_m(PyDosObj far *s, PyDosObj far *sep)
{
    PyDosObj far *result;
    unsigned int slen, start, i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_list_new(0);
    slen = s->v.str.len;
    result = pydos_list_new(4);

    if (sep == (PyDosObj far *)0 || sep->type == PYDT_NONE) {
        /* Split on whitespace */
        start = 0;
        while (start < slen && is_whitespace(s->v.str.data[start])) start++;
        while (start < slen) {
            i = start;
            while (i < slen && !is_whitespace(s->v.str.data[i])) i++;
            pydos_list_append(result, pydos_obj_new_str(s->v.str.data + start, i - start));
            start = i;
            while (start < slen && is_whitespace(s->v.str.data[start])) start++;
        }
    } else if (sep->type == PYDT_STR && sep->v.str.len > 0) {
        unsigned int seplen = sep->v.str.len;
        start = 0;
        for (i = 0; i <= slen - seplen; i++) {
            if (_fmemcmp(s->v.str.data + i, sep->v.str.data, seplen) == 0) {
                pydos_list_append(result, pydos_obj_new_str(s->v.str.data + start, i - start));
                start = i + seplen;
                i = start - 1;
            }
        }
        pydos_list_append(result, pydos_obj_new_str(s->v.str.data + start, slen - start));
    } else {
        pydos_list_append(result, pydos_obj_new_str(s->v.str.data, slen));
    }
    return result;
}

PyDosObj far * PYDOS_API pydos_str_rsplit_m(PyDosObj far *s, PyDosObj far *sep)
{
    /* Simplified: same as split for now */
    return pydos_str_split_m(s, sep);
}

PyDosObj far * PYDOS_API pydos_str_splitlines(PyDosObj far *s)
{
    PyDosObj far *result;
    unsigned int slen, start, i;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR) return pydos_list_new(0);
    slen = s->v.str.len;
    result = pydos_list_new(4);
    start = 0;
    for (i = 0; i < slen; i++) {
        if (s->v.str.data[i] == '\n') {
            pydos_list_append(result, pydos_obj_new_str(s->v.str.data + start, i - start));
            start = i + 1;
        } else if (s->v.str.data[i] == '\r') {
            pydos_list_append(result, pydos_obj_new_str(s->v.str.data + start, i - start));
            if (i + 1 < slen && s->v.str.data[i + 1] == '\n') i++;
            start = i + 1;
        }
    }
    if (start < slen) {
        pydos_list_append(result, pydos_obj_new_str(s->v.str.data + start, slen - start));
    }
    return result;
}

PyDosObj far * PYDOS_API pydos_str_join_m(PyDosObj far *sep, PyDosObj far *iterable)
{
    unsigned int total, sep_len, i, list_len, pos;
    char far *buf;
    PyDosObj far *item, *result;
    if (sep == (PyDosObj far *)0 || sep->type != PYDT_STR) return pydos_obj_new_str((const char far *)"", 0);
    if (iterable == (PyDosObj far *)0 || iterable->type != PYDT_LIST) return pydos_obj_new_str((const char far *)"", 0);
    sep_len = sep->v.str.len;
    list_len = iterable->v.list.len;
    if (list_len == 0) return pydos_obj_new_str((const char far *)"", 0);
    /* Calculate total length */
    total = 0;
    for (i = 0; i < list_len; i++) {
        item = iterable->v.list.items[i];
        if (item != (PyDosObj far *)0 && item->type == PYDT_STR) total += item->v.str.len;
        if (i > 0) total += sep_len;
    }
    buf = (char far *)pydos_far_alloc((unsigned long)total + 1);
    if (!buf) return pydos_obj_new_str((const char far *)"", 0);
    pos = 0;
    for (i = 0; i < list_len; i++) {
        if (i > 0 && sep_len > 0) {
            _fmemcpy(buf + pos, sep->v.str.data, sep_len);
            pos += sep_len;
        }
        item = iterable->v.list.items[i];
        if (item != (PyDosObj far *)0 && item->type == PYDT_STR && item->v.str.len > 0) {
            _fmemcpy(buf + pos, item->v.str.data, item->v.str.len);
            pos += item->v.str.len;
        }
    }
    buf[pos] = '\0';
    result = pydos_obj_new_str(buf, pos);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_replace_m(PyDosObj far *s, PyDosObj far *old_s,
                                              PyDosObj far *new_s)
{
    unsigned int slen, olen, nlen, i, count, pos, total;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        old_s == (PyDosObj far *)0 || old_s->type != PYDT_STR ||
        new_s == (PyDosObj far *)0 || new_s->type != PYDT_STR)
        return (s != (PyDosObj far *)0) ? s : pydos_obj_new_str((const char far *)"", 0);
    slen = s->v.str.len;
    olen = old_s->v.str.len;
    nlen = new_s->v.str.len;
    if (olen == 0) return s;
    /* Count occurrences */
    count = 0;
    for (i = 0; i <= slen - olen; i++) {
        if (_fmemcmp(s->v.str.data + i, old_s->v.str.data, olen) == 0) {
            count++;
            i += olen - 1;
        }
    }
    if (count == 0) return s;
    total = slen - count * olen + count * nlen;
    buf = (char far *)pydos_far_alloc((unsigned long)total + 1);
    if (!buf) return s;
    pos = 0;
    for (i = 0; i < slen; ) {
        if (i <= slen - olen && _fmemcmp(s->v.str.data + i, old_s->v.str.data, olen) == 0) {
            if (nlen > 0) { _fmemcpy(buf + pos, new_s->v.str.data, nlen); pos += nlen; }
            i += olen;
        } else {
            buf[pos++] = s->v.str.data[i++];
        }
    }
    buf[pos] = '\0';
    result = pydos_obj_new_str(buf, pos);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_center_m(PyDosObj far *s, PyDosObj far *width)
{
    unsigned int slen, w, pad, left;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        width == (PyDosObj far *)0 || width->type != PYDT_INT) return s ? s : pydos_obj_new_str((const char far *)"", 0);
    slen = s->v.str.len;
    w = (unsigned int)width->v.int_val;
    if (w <= slen) return s;
    pad = w - slen;
    left = pad / 2;
    buf = (char far *)pydos_far_alloc((unsigned long)w + 1);
    if (!buf) return s;
    _fmemset(buf, ' ', w);
    _fmemcpy(buf + left, s->v.str.data, slen);
    buf[w] = '\0';
    result = pydos_obj_new_str(buf, w);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_ljust_m(PyDosObj far *s, PyDosObj far *width)
{
    unsigned int slen, w;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        width == (PyDosObj far *)0 || width->type != PYDT_INT) return s ? s : pydos_obj_new_str((const char far *)"", 0);
    slen = s->v.str.len;
    w = (unsigned int)width->v.int_val;
    if (w <= slen) return s;
    buf = (char far *)pydos_far_alloc((unsigned long)w + 1);
    if (!buf) return s;
    _fmemcpy(buf, s->v.str.data, slen);
    _fmemset(buf + slen, ' ', w - slen);
    buf[w] = '\0';
    result = pydos_obj_new_str(buf, w);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_rjust_m(PyDosObj far *s, PyDosObj far *width)
{
    unsigned int slen, w, pad;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        width == (PyDosObj far *)0 || width->type != PYDT_INT) return s ? s : pydos_obj_new_str((const char far *)"", 0);
    slen = s->v.str.len;
    w = (unsigned int)width->v.int_val;
    if (w <= slen) return s;
    pad = w - slen;
    buf = (char far *)pydos_far_alloc((unsigned long)w + 1);
    if (!buf) return s;
    _fmemset(buf, ' ', pad);
    _fmemcpy(buf + pad, s->v.str.data, slen);
    buf[w] = '\0';
    result = pydos_obj_new_str(buf, w);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_zfill_m(PyDosObj far *s, PyDosObj far *width)
{
    unsigned int slen, w, pad, start;
    char far *buf;
    PyDosObj far *result;
    if (s == (PyDosObj far *)0 || s->type != PYDT_STR ||
        width == (PyDosObj far *)0 || width->type != PYDT_INT) return s ? s : pydos_obj_new_str((const char far *)"", 0);
    slen = s->v.str.len;
    w = (unsigned int)width->v.int_val;
    if (w <= slen) return s;
    pad = w - slen;
    buf = (char far *)pydos_far_alloc((unsigned long)w + 1);
    if (!buf) return s;
    start = 0;
    if (slen > 0 && (s->v.str.data[0] == '-' || s->v.str.data[0] == '+')) {
        buf[0] = s->v.str.data[0];
        start = 1;
        _fmemset(buf + 1, '0', pad);
    } else {
        _fmemset(buf, '0', pad);
    }
    _fmemcpy(buf + pad + start, s->v.str.data + start, slen - start);
    buf[w] = '\0';
    result = pydos_obj_new_str(buf, w);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_str_encode(PyDosObj far *s)
{
    /* Stub: return the string itself (ASCII/bytes identity) */
    if (s != (PyDosObj far *)0) return s;
    return pydos_obj_new_str((const char far *)"", 0);
}

PyDosObj far * PYDOS_API pydos_str_format_m(PyDosObj far *s)
{
    /* Stub: return the string itself (no format args supported at runtime) */
    if (s != (PyDosObj far *)0) return s;
    return pydos_obj_new_str((const char far *)"", 0);
}

void PYDOS_API pydos_str_init(void)
{
    /* No global state to initialize for Phase 1 */
}

void PYDOS_API pydos_str_shutdown(void)
{
    /* No global state to clean up for Phase 1 */
}
