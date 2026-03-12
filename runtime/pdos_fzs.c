/*
 * pdos_fzs.c - Frozenset type for PyDOS runtime
 *
 * Immutable, hashable set with sorted-array storage and O(log n) lookup.
 * Elements are stored in a sorted, deduplicated array.
 * Sort order: type tag first, then value comparison within type.
 *
 * C89 only. Open Watcom compatible.
 */

#include "pdos_fzs.h"
#include "pdos_obj.h"
#include "pdos_mem.h"
#include "pdos_exc.h"
#include <string.h>
#include <stdlib.h>

/* --------------------------------------------------------------- */
/* Total ordering for qsort: type tag first, then value.            */
/* Returns -1, 0, +1.                                               */
/* --------------------------------------------------------------- */
static int fzs_compare_obj(PyDosObj far *a, PyDosObj far *b)
{
    int ta, tb;

    if (a == b) return 0;
    if (a == (PyDosObj far *)0) return -1;
    if (b == (PyDosObj far *)0) return 1;

    ta = (int)a->type;
    tb = (int)b->type;
    if (ta != tb) return (ta < tb) ? -1 : 1;

    switch ((PyDosType)a->type) {
    case PYDT_BOOL:
        if (a->v.int_val < b->v.int_val) return -1;
        if (a->v.int_val > b->v.int_val) return 1;
        return 0;
    case PYDT_INT:
        if (a->v.int_val < b->v.int_val) return -1;
        if (a->v.int_val > b->v.int_val) return 1;
        return 0;
    case PYDT_FLOAT:
        if (a->v.float_val < b->v.float_val) return -1;
        if (a->v.float_val > b->v.float_val) return 1;
        return 0;
    case PYDT_STR:
        if (a->v.str.len == 0 && b->v.str.len == 0) return 0;
        if (a->v.str.len == 0) return -1;
        if (b->v.str.len == 0) return 1;
        {
            unsigned int minlen;
            int cmp;
            minlen = a->v.str.len < b->v.str.len ? a->v.str.len : b->v.str.len;
            cmp = _fmemcmp(a->v.str.data, b->v.str.data, minlen);
            if (cmp != 0) return cmp < 0 ? -1 : 1;
            if (a->v.str.len < b->v.str.len) return -1;
            if (a->v.str.len > b->v.str.len) return 1;
            return 0;
        }
    case PYDT_NONE:
        return 0;
    default:
        /* For other types, compare by address as tiebreaker */
        if (a < b) return -1;
        if (a > b) return 1;
        return 0;
    }
}

/* qsort comparator wrapper (takes void far * for C89 qsort) */
static int fzs_qsort_cmp(const void *va, const void *vb)
{
    PyDosObj far *a;
    PyDosObj far *b;
    _fmemcpy((char far *)&a, (const char far *)va, sizeof(a));
    _fmemcpy((char far *)&b, (const char far *)vb, sizeof(b));
    return fzs_compare_obj(a, b);
}

/* --------------------------------------------------------------- */
/* Sort and deduplicate an array of PyDosObj pointers in-place.     */
/* Returns the new (deduplicated) count. DECREFs removed duplicates. */
/* --------------------------------------------------------------- */
static int fzs_sort_dedup(PyDosObj far * far *items, int count)
{
    int i, j;

    if (count <= 1) return count;

    /* Sort */
    qsort(items, (unsigned int)count, sizeof(PyDosObj far *), fzs_qsort_cmp);

    /* Deduplicate: keep first of each equal run */
    j = 0;
    for (i = 1; i < count; i++) {
        if (fzs_compare_obj(items[j], items[i]) == 0 &&
            pydos_obj_equal(items[j], items[i])) {
            /* Duplicate — release it */
            PYDOS_DECREF(items[i]);
        } else {
            j++;
            items[j] = items[i];
        }
    }

    return j + 1;
}

/* --------------------------------------------------------------- */
/* Compute hash for a frozenset: XOR of element hashes.             */
/* Order-independent since XOR is commutative.                      */
/* --------------------------------------------------------------- */
static unsigned int fzs_compute_hash(PyDosObj far * far *items, int len)
{
    unsigned int h;
    int i;

    h = 0;
    for (i = 0; i < len; i++) {
        h ^= (unsigned int)pydos_obj_hash(items[i]);
    }
    return h;
}

/* --------------------------------------------------------------- */
/* pydos_frozenset_new — create from array of elements              */
/* --------------------------------------------------------------- */
PyDosObj far * PYDOS_API pydos_frozenset_new(PyDosObj far * far *items,
                                              int count)
{
    PyDosObj far *fs;
    PyDosObj far * far *buf;
    int deduped;
    int i;

    fs = pydos_obj_alloc();
    if (fs == (PyDosObj far *)0) return pydos_obj_new_none();

    fs->type = PYDT_FROZENSET;
    fs->flags = 0;

    if (count <= 0 || items == (PyDosObj far * far *)0) {
        fs->v.frozenset.items = (PyDosObj far * far *)0;
        fs->v.frozenset.len = 0;
        fs->v.frozenset.hash = 0;
        return fs;
    }

    /* Copy items into a working buffer for sort+dedup */
    buf = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned int)count * sizeof(PyDosObj far *));
    if (buf == (PyDosObj far * far *)0) {
        fs->v.frozenset.items = (PyDosObj far * far *)0;
        fs->v.frozenset.len = 0;
        fs->v.frozenset.hash = 0;
        return fs;
    }

    for (i = 0; i < count; i++) {
        buf[i] = items[i];
        PYDOS_INCREF(items[i]);
    }

    deduped = fzs_sort_dedup(buf, count);

    /* Shrink buffer if needed */
    if (deduped < count) {
        PyDosObj far * far *newbuf;
        newbuf = (PyDosObj far * far *)pydos_far_alloc(
            (unsigned int)deduped * sizeof(PyDosObj far *));
        if (newbuf != (PyDosObj far * far *)0) {
            _fmemcpy(newbuf, buf,
                     (unsigned int)deduped * sizeof(PyDosObj far *));
            pydos_far_free(buf);
            buf = newbuf;
        }
    }

    fs->v.frozenset.items = buf;
    fs->v.frozenset.len = (unsigned int)deduped;
    fs->v.frozenset.hash = fzs_compute_hash(buf, deduped);

    return fs;
}

/* --------------------------------------------------------------- */
/* pydos_frozenset_contains — binary search for item                */
/* --------------------------------------------------------------- */
int PYDOS_API pydos_frozenset_contains(PyDosObj far *fs, PyDosObj far *item)
{
    int lo, hi, mid, cmp;
    PyDosObj far * far *items;
    unsigned int len;

    if (fs == (PyDosObj far *)0 || (PyDosType)fs->type != PYDT_FROZENSET)
        return 0;

    items = fs->v.frozenset.items;
    len = fs->v.frozenset.len;
    if (len == 0 || items == (PyDosObj far * far *)0) return 0;

    lo = 0;
    hi = (int)len - 1;
    while (lo <= hi) {
        mid = lo + (hi - lo) / 2;
        cmp = fzs_compare_obj(item, items[mid]);
        if (cmp < 0) {
            hi = mid - 1;
        } else if (cmp > 0) {
            lo = mid + 1;
        } else {
            /* Sort order match — verify equality */
            if (pydos_obj_equal(item, items[mid])) return 1;
            /* Sort collision but not equal — scan neighbors */
            {
                int k;
                for (k = mid - 1; k >= lo; k--) {
                    if (fzs_compare_obj(item, items[k]) != 0) break;
                    if (pydos_obj_equal(item, items[k])) return 1;
                }
                for (k = mid + 1; k <= hi; k++) {
                    if (fzs_compare_obj(item, items[k]) != 0) break;
                    if (pydos_obj_equal(item, items[k])) return 1;
                }
            }
            return 0;
        }
    }
    return 0;
}

/* --------------------------------------------------------------- */
/* pydos_frozenset_len                                              */
/* --------------------------------------------------------------- */
int PYDOS_API pydos_frozenset_len(PyDosObj far *fs)
{
    if (fs == (PyDosObj far *)0 || (PyDosType)fs->type != PYDT_FROZENSET)
        return 0;
    return (int)fs->v.frozenset.len;
}

/* --------------------------------------------------------------- */
/* Set operations — produce new frozensets                          */
/* --------------------------------------------------------------- */

PyDosObj far * PYDOS_API pydos_frozenset_union(PyDosObj far *a,
                                                PyDosObj far *b)
{
    unsigned int alen, blen, total;
    PyDosObj far * far *buf;
    PyDosObj far *result;
    unsigned int i;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_FROZENSET)
        return pydos_obj_new_none();
    if (b == (PyDosObj far *)0 || (PyDosType)b->type != PYDT_FROZENSET)
        return pydos_obj_new_none();

    alen = a->v.frozenset.len;
    blen = b->v.frozenset.len;
    total = alen + blen;
    if (total == 0) return pydos_frozenset_new((PyDosObj far * far *)0, 0);

    buf = (PyDosObj far * far *)pydos_far_alloc(
        total * sizeof(PyDosObj far *));
    if (buf == (PyDosObj far * far *)0) return pydos_obj_new_none();

    for (i = 0; i < alen; i++) buf[i] = a->v.frozenset.items[i];
    for (i = 0; i < blen; i++) buf[alen + i] = b->v.frozenset.items[i];

    /* pydos_frozenset_new will INCREF + sort + dedup */
    result = pydos_frozenset_new(buf, (int)total);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_frozenset_intersection(PyDosObj far *a,
                                                       PyDosObj far *b)
{
    unsigned int alen, i, count;
    PyDosObj far * far *buf;
    PyDosObj far *result;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_FROZENSET)
        return pydos_obj_new_none();
    if (b == (PyDosObj far *)0 || (PyDosType)b->type != PYDT_FROZENSET)
        return pydos_obj_new_none();

    alen = a->v.frozenset.len;
    if (alen == 0) return pydos_frozenset_new((PyDosObj far * far *)0, 0);

    buf = (PyDosObj far * far *)pydos_far_alloc(
        alen * sizeof(PyDosObj far *));
    if (buf == (PyDosObj far * far *)0) return pydos_obj_new_none();

    count = 0;
    for (i = 0; i < alen; i++) {
        if (pydos_frozenset_contains(b, a->v.frozenset.items[i])) {
            buf[count++] = a->v.frozenset.items[i];
        }
    }

    result = pydos_frozenset_new(buf, (int)count);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_frozenset_difference(PyDosObj far *a,
                                                     PyDosObj far *b)
{
    unsigned int alen, i, count;
    PyDosObj far * far *buf;
    PyDosObj far *result;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_FROZENSET)
        return pydos_obj_new_none();
    if (b == (PyDosObj far *)0 || (PyDosType)b->type != PYDT_FROZENSET)
        return pydos_obj_new_none();

    alen = a->v.frozenset.len;
    if (alen == 0) return pydos_frozenset_new((PyDosObj far * far *)0, 0);

    buf = (PyDosObj far * far *)pydos_far_alloc(
        alen * sizeof(PyDosObj far *));
    if (buf == (PyDosObj far * far *)0) return pydos_obj_new_none();

    count = 0;
    for (i = 0; i < alen; i++) {
        if (!pydos_frozenset_contains(b, a->v.frozenset.items[i])) {
            buf[count++] = a->v.frozenset.items[i];
        }
    }

    result = pydos_frozenset_new(buf, (int)count);
    pydos_far_free(buf);
    return result;
}

PyDosObj far * PYDOS_API pydos_frozenset_symmetric_difference(
                                                PyDosObj far *a,
                                                PyDosObj far *b)
{
    unsigned int alen, blen, i, count;
    PyDosObj far * far *buf;
    PyDosObj far *result;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_FROZENSET)
        return pydos_obj_new_none();
    if (b == (PyDosObj far *)0 || (PyDosType)b->type != PYDT_FROZENSET)
        return pydos_obj_new_none();

    alen = a->v.frozenset.len;
    blen = b->v.frozenset.len;

    buf = (PyDosObj far * far *)pydos_far_alloc(
        (alen + blen) * sizeof(PyDosObj far *));
    if (buf == (PyDosObj far * far *)0) return pydos_obj_new_none();

    count = 0;
    for (i = 0; i < alen; i++) {
        if (!pydos_frozenset_contains(b, a->v.frozenset.items[i])) {
            buf[count++] = a->v.frozenset.items[i];
        }
    }
    for (i = 0; i < blen; i++) {
        if (!pydos_frozenset_contains(a, b->v.frozenset.items[i])) {
            buf[count++] = b->v.frozenset.items[i];
        }
    }

    result = pydos_frozenset_new(buf, (int)count);
    pydos_far_free(buf);
    return result;
}

/* --------------------------------------------------------------- */
/* Subset/superset/disjoint                                         */
/* --------------------------------------------------------------- */

int PYDOS_API pydos_frozenset_issubset(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int i;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_FROZENSET)
        return 0;
    if (b == (PyDosObj far *)0 || (PyDosType)b->type != PYDT_FROZENSET)
        return 0;
    if (a->v.frozenset.len > b->v.frozenset.len) return 0;

    for (i = 0; i < a->v.frozenset.len; i++) {
        if (!pydos_frozenset_contains(b, a->v.frozenset.items[i]))
            return 0;
    }
    return 1;
}

int PYDOS_API pydos_frozenset_issuperset(PyDosObj far *a, PyDosObj far *b)
{
    return pydos_frozenset_issubset(b, a);
}

int PYDOS_API pydos_frozenset_isdisjoint(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int i;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_FROZENSET)
        return 1;
    if (b == (PyDosObj far *)0 || (PyDosType)b->type != PYDT_FROZENSET)
        return 1;

    for (i = 0; i < a->v.frozenset.len; i++) {
        if (pydos_frozenset_contains(b, a->v.frozenset.items[i]))
            return 0;
    }
    return 1;
}

/* --------------------------------------------------------------- */
/* Builtin constructor: frozenset() / frozenset(iterable)           */
/* --------------------------------------------------------------- */
PyDosObj far * PYDOS_API pydos_builtin_frozenset_conv(int argc,
                                                       PyDosObj far * far *argv)
{
    PyDosObj far *src;
    PyDosObj far *iter_obj;
    PyDosObj far *item;
    PyDosObj far * far *buf;
    int count, cap;
    PyDosObj far *result;

    /* frozenset() — empty */
    if (argc == 0) {
        return pydos_frozenset_new((PyDosObj far * far *)0, 0);
    }

    src = argv[0];
    if (src == (PyDosObj far *)0) {
        return pydos_frozenset_new((PyDosObj far * far *)0, 0);
    }

    /* frozenset(frozenset) — copy */
    if ((PyDosType)src->type == PYDT_FROZENSET) {
        PYDOS_INCREF(src);
        return src;
    }

    /* frozenset(set) — convert set (dict-based) to frozenset */
    if ((PyDosType)src->type == PYDT_SET) {
        unsigned int i;
        count = 0;
        for (i = 0; i < src->v.dict.size; i++) {
            if (src->v.dict.entries[i].key != (PyDosObj far *)0 &&
                src->v.dict.entries[i].key != (PyDosObj far *)1) {
                count++;
            }
        }
        if (count == 0) {
            return pydos_frozenset_new((PyDosObj far * far *)0, 0);
        }
        buf = (PyDosObj far * far *)pydos_far_alloc(
            (unsigned int)count * sizeof(PyDosObj far *));
        if (buf == (PyDosObj far * far *)0) return pydos_obj_new_none();
        {
            int j;
            j = 0;
            for (i = 0; i < src->v.dict.size; i++) {
                if (src->v.dict.entries[i].key != (PyDosObj far *)0 &&
                    src->v.dict.entries[i].key != (PyDosObj far *)1) {
                    buf[j++] = src->v.dict.entries[i].key;
                }
            }
        }
        result = pydos_frozenset_new(buf, count);
        pydos_far_free(buf);
        return result;
    }

    /* frozenset(list) — fast path */
    if ((PyDosType)src->type == PYDT_LIST) {
        result = pydos_frozenset_new(src->v.list.items,
                                     (int)src->v.list.len);
        return result;
    }

    /* frozenset(tuple) — fast path */
    if ((PyDosType)src->type == PYDT_TUPLE) {
        result = pydos_frozenset_new(src->v.tuple.items,
                                     (int)src->v.tuple.len);
        return result;
    }

    /* Generic iterable — collect into buffer then create frozenset */
    iter_obj = pydos_obj_get_iter(src);
    if (iter_obj == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"frozenset() argument is not iterable");
        return pydos_obj_new_none();
    }

    cap = 8;
    buf = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned int)cap * sizeof(PyDosObj far *));
    if (buf == (PyDosObj far * far *)0) {
        PYDOS_DECREF(iter_obj);
        return pydos_obj_new_none();
    }
    count = 0;

    for (;;) {
        item = pydos_obj_iter_next(iter_obj);
        if (item == (PyDosObj far *)0) break;

        if (count >= cap) {
            PyDosObj far * far *newbuf;
            cap *= 2;
            newbuf = (PyDosObj far * far *)pydos_far_alloc(
                (unsigned int)cap * sizeof(PyDosObj far *));
            if (newbuf == (PyDosObj far * far *)0) {
                PYDOS_DECREF(item);
                break;
            }
            _fmemcpy(newbuf, buf,
                     (unsigned int)count * sizeof(PyDosObj far *));
            pydos_far_free(buf);
            buf = newbuf;
        }
        buf[count++] = item;
    }

    PYDOS_DECREF(iter_obj);

    result = pydos_frozenset_new(buf, count);

    /* DECREF the collected items since frozenset_new INCREFs them */
    {
        int i;
        for (i = 0; i < count; i++) {
            PYDOS_DECREF(buf[i]);
        }
    }
    pydos_far_free(buf);

    return result;
}

/* --------------------------------------------------------------- */
void PYDOS_API pydos_frozenset_init(void) { }
void PYDOS_API pydos_frozenset_shutdown(void) { }
