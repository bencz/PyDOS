/*
 * pydos_list.c - List operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Lists are dynamic arrays of PyDosObj far * pointers.
 * Growth factor is 1.5x (new_cap = old_cap + old_cap/2 + 1).
 */

#include "pdos_lst.h"
#include "pdos_obj.h"
#include "pdos_exc.h"
#include <string.h>

#include "pdos_mem.h"

/*
 * Ensure list has capacity for at least one more item.
 * Grows by factor 1.5x.
 */
static int list_ensure_capacity(PyDosObj far *list)
{
    unsigned int old_cap, new_cap;
    PyDosObj far * far *new_items;
    unsigned long alloc_size;

    if (list->v.list.len < list->v.list.cap) {
        return 0;
    }

    old_cap = list->v.list.cap;
    new_cap = old_cap + (old_cap >> 1) + 1; /* 1.5x + 1 */

    alloc_size = (unsigned long)new_cap * (unsigned long)sizeof(PyDosObj far *);

    if (list->v.list.items == (PyDosObj far * far *)0) {
        new_items = (PyDosObj far * far *)pydos_far_alloc(alloc_size);
    } else {
        new_items = (PyDosObj far * far *)pydos_far_realloc(
            list->v.list.items, alloc_size);
    }

    if (new_items == (PyDosObj far * far *)0) {
        return -1;
    }

    list->v.list.items = new_items;
    list->v.list.cap = new_cap;
    return 0;
}

/*
 * Normalize a Python-style index (handle negative indices).
 * Returns the normalized index, or -1 if out of bounds.
 */
static long normalize_index(long index, unsigned int len)
{
    if (index < 0) {
        index += (long)len;
    }
    if (index < 0 || index >= (long)len) {
        return -1L;
    }
    return index;
}

PyDosObj far * PYDOS_API pydos_list_new(unsigned int initial_cap)
{
    PyDosObj far *list;
    unsigned long alloc_size;

    if (initial_cap < 4) {
        initial_cap = 4;
    }

    list = pydos_obj_alloc();
    if (list == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    list->type = PYDT_LIST;
    list->flags = 0;
    list->refcount = 1;
    list->v.list.len = 0;
    list->v.list.cap = initial_cap;

    alloc_size = (unsigned long)initial_cap * (unsigned long)sizeof(PyDosObj far *);
    list->v.list.items = (PyDosObj far * far *)pydos_far_alloc(alloc_size);

    if (list->v.list.items == (PyDosObj far * far *)0) {
        list->v.list.cap = 0;
    } else {
        _fmemset(list->v.list.items, 0, (unsigned int)alloc_size);
    }

    return list;
}

void PYDOS_API pydos_list_append(PyDosObj far *list, PyDosObj far *item)
{
    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return;
    }

    if (list_ensure_capacity(list) != 0) {
        return;
    }

    list->v.list.items[list->v.list.len] = item;
    PYDOS_INCREF(item);
    list->v.list.len++;
}

PyDosObj far * PYDOS_API pydos_list_get(PyDosObj far *list, long index)
{
    long ni;

    if (list == (PyDosObj far *)0 ||
        (list->type != PYDT_LIST && list->type != PYDT_TUPLE)) {
        return (PyDosObj far *)0;
    }

    ni = normalize_index(index, list->v.list.len);
    if (ni < 0) {
        return (PyDosObj far *)0;
    }

    PYDOS_INCREF(list->v.list.items[(unsigned int)ni]);
    return list->v.list.items[(unsigned int)ni];
}

int PYDOS_API pydos_list_set(PyDosObj far *list, long index, PyDosObj far *item)
{
    long ni;
    PyDosObj far *old;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return -1;
    }

    ni = normalize_index(index, list->v.list.len);
    if (ni < 0) {
        return -1;
    }

    old = list->v.list.items[(unsigned int)ni];
    list->v.list.items[(unsigned int)ni] = item;
    PYDOS_INCREF(item);
    PYDOS_DECREF(old);
    return 0;
}

long PYDOS_API pydos_list_len(PyDosObj far *list)
{
    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return 0L;
    }
    return (long)list->v.list.len;
}

/*
 * Helper: clamp an index for slicing (different from item access).
 */
static long clamp_slice_index(long idx, long len)
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

PyDosObj far * PYDOS_API pydos_list_slice(PyDosObj far *list,
                               long start, long stop, long step)
{
    PyDosObj far *result;
    long slen, i, count;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return pydos_list_new(4);
    }

    slen = (long)list->v.list.len;

    if (step == 0) {
        return pydos_list_new(4);
    }

    /* Pre-clamp LONG_MAX sentinel to slen (workaround for Watcom 8086
     * optimizer bug with 32-bit comparison of LONG_MAX) */
    if (start == 0x7FFFFFFFL) start = slen;
    if (stop == 0x7FFFFFFFL) stop = slen;

    start = clamp_slice_index(start, slen);
    stop = clamp_slice_index(stop, slen);

    /* Count elements in slice */
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

    result = pydos_list_new((unsigned int)(count > 0 ? count : 4));
    if (result == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    if (step > 0) {
        for (i = start; i < stop; i += step) {
            pydos_list_append(result, list->v.list.items[(unsigned int)i]);
        }
    } else {
        for (i = start; i > stop; i += step) {
            pydos_list_append(result, list->v.list.items[(unsigned int)i]);
        }
    }

    return result;
}

PyDosObj far * PYDOS_API pydos_list_concat(PyDosObj far *a, PyDosObj far *b)
{
    PyDosObj far *result;
    unsigned int i, total;

    if (a == (PyDosObj far *)0 || a->type != PYDT_LIST) {
        if (b != (PyDosObj far *)0 && b->type == PYDT_LIST) {
            PYDOS_INCREF(b);
            return b;
        }
        return pydos_list_new(4);
    }
    if (b == (PyDosObj far *)0 || b->type != PYDT_LIST) {
        PYDOS_INCREF(a);
        return a;
    }

    total = a->v.list.len + b->v.list.len;
    result = pydos_list_new(total > 0 ? total : 4);
    if (result == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    for (i = 0; i < a->v.list.len; i++) {
        pydos_list_append(result, a->v.list.items[i]);
    }
    for (i = 0; i < b->v.list.len; i++) {
        pydos_list_append(result, b->v.list.items[i]);
    }

    return result;
}

int PYDOS_API pydos_list_contains(PyDosObj far *list, PyDosObj far *item)
{
    unsigned int i;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return 0;
    }

    for (i = 0; i < list->v.list.len; i++) {
        if (pydos_obj_equal(list->v.list.items[i], item)) {
            return 1;
        }
    }
    return 0;
}

PyDosObj far * PYDOS_API pydos_list_pop(PyDosObj far *list, long index)
{
    long ni;
    unsigned int ui;
    PyDosObj far *item;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return (PyDosObj far *)0;
    }

    if (list->v.list.len == 0) {
        return (PyDosObj far *)0;
    }

    ni = normalize_index(index, list->v.list.len);
    if (ni < 0) {
        return (PyDosObj far *)0;
    }

    ui = (unsigned int)ni;
    item = list->v.list.items[ui];

    /* Shift elements down */
    if (ui < list->v.list.len - 1) {
        _fmemmove(&list->v.list.items[ui],
                  &list->v.list.items[ui + 1],
                  (unsigned int)((list->v.list.len - ui - 1) *
                                 sizeof(PyDosObj far *)));
    }
    list->v.list.len--;
    /* Clear the now-unused slot to prevent stale pointer access */
    list->v.list.items[list->v.list.len] = (PyDosObj far *)0;

    /* Don't DECREF: we're returning the item, transferring ownership */
    return item;
}

void PYDOS_API pydos_list_insert(PyDosObj far *list, long index, PyDosObj far *item)
{
    unsigned int ui;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return;
    }

    /* Clamp index for insert */
    if (index < 0) {
        index += (long)list->v.list.len;
        if (index < 0) {
            index = 0;
        }
    }
    if (index > (long)list->v.list.len) {
        index = (long)list->v.list.len;
    }

    if (list_ensure_capacity(list) != 0) {
        return;
    }

    ui = (unsigned int)index;

    /* Shift elements up */
    if (ui < list->v.list.len) {
        _fmemmove(&list->v.list.items[ui + 1],
                  &list->v.list.items[ui],
                  (unsigned int)((list->v.list.len - ui) *
                                 sizeof(PyDosObj far *)));
    }

    list->v.list.items[ui] = item;
    PYDOS_INCREF(item);
    list->v.list.len++;
}

void PYDOS_API pydos_list_reverse(PyDosObj far *list)
{
    unsigned int i, j;
    PyDosObj far *tmp;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return;
    }

    if (list->v.list.len < 2) {
        return;
    }

    i = 0;
    j = list->v.list.len - 1;
    while (i < j) {
        tmp = list->v.list.items[i];
        list->v.list.items[i] = list->v.list.items[j];
        list->v.list.items[j] = tmp;
        i++;
        j--;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_list_sort — in-place insertion sort (stable, ascending)       */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_list_sort(PyDosObj far *list)
{
    unsigned int i, j;
    PyDosObj far *key;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) {
        return;
    }

    if (list->v.list.len < 2) {
        return;
    }

    /* Insertion sort: stable, O(n^2), good for small lists on DOS */
    for (i = 1; i < list->v.list.len; i++) {
        key = list->v.list.items[i];
        j = i;
        while (j > 0 && pydos_obj_compare(list->v.list.items[j - 1], key) > 0) {
            list->v.list.items[j] = list->v.list.items[j - 1];
            j--;
        }
        list->v.list.items[j] = key;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_list_sort_key — in-place insertion sort with key function      */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_list_sort_key(PyDosObj far *list, PyDosObj far *key_fn, int reverse)
{
    unsigned int i, j, n;
    PyDosObj far * far *keys;
    PyDosObj far *tmp_item;
    PyDosObj far *tmp_key;
    typedef PyDosObj far * (PYDOS_API far *KeyFunc)(PyDosObj far *);
    KeyFunc kf;

    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) return;
    n = list->v.list.len;
    if (n < 2) return;
    if (key_fn == (PyDosObj far *)0 || key_fn->type != PYDT_FUNCTION) {
        pydos_list_sort(list);
        return;
    }

    kf = (KeyFunc)(key_fn->v.func.code);

    /* Allocate key array */
    keys = (PyDosObj far * far *)pydos_far_alloc((unsigned long)n * sizeof(PyDosObj far *));
    if (keys == (PyDosObj far * far *)0) {
        pydos_list_sort(list);
        return;
    }

    /* Compute keys */
    for (i = 0; i < n; i++) {
        keys[i] = kf(list->v.list.items[i]);
    }

    /* Insertion sort on keys, moving items in parallel.
     * When reverse is set, negate the comparison so we sort descending
     * while preserving stability (unlike sort-then-reverse). */
    for (i = 1; i < n; i++) {
        tmp_key = keys[i];
        tmp_item = list->v.list.items[i];
        j = i;
        while (j > 0) {
            int cmp = pydos_obj_compare(keys[j - 1], tmp_key);
            if (reverse) cmp = -cmp;
            if (cmp <= 0) break;
            keys[j] = keys[j - 1];
            list->v.list.items[j] = list->v.list.items[j - 1];
            j--;
        }
        keys[j] = tmp_key;
        list->v.list.items[j] = tmp_item;
    }

    /* Release keys */
    for (i = 0; i < n; i++) {
        if (keys[i] != (PyDosObj far *)0) {
            PYDOS_DECREF(keys[i]);
        }
    }
    pydos_far_free(keys);
}

int PYDOS_API pydos_list_remove(PyDosObj far *list, PyDosObj far *item)
{
    unsigned int i;
    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) return -1;
    for (i = 0; i < list->v.list.len; i++) {
        if (pydos_obj_equal(list->v.list.items[i], item)) {
            PyDosObj far *popped = pydos_list_pop(list, (long)i);
            if (popped != (PyDosObj far *)0) PYDOS_DECREF(popped);
            return 0;
        }
    }
    return -1;
}

void PYDOS_API pydos_list_clear(PyDosObj far *list)
{
    unsigned int i;
    if (list == (PyDosObj far *)0 || list->type != PYDT_LIST) return;
    for (i = 0; i < list->v.list.len; i++) {
        if (list->v.list.items[i] != (PyDosObj far *)0) {
            PYDOS_DECREF(list->v.list.items[i]);
            list->v.list.items[i] = (PyDosObj far *)0;
        }
    }
    list->v.list.len = 0;
}

PyDosObj far * PYDOS_API pydos_list_copy(PyDosObj far *src)
{
    PyDosObj far *dst;
    unsigned int i;
    if (src == (PyDosObj far *)0) return pydos_list_new(4);
    if (src->type != PYDT_LIST && src->type != PYDT_TUPLE)
        return pydos_list_new(4);
    dst = pydos_list_new(src->v.list.len > 0 ? src->v.list.len : 4);
    if (dst == (PyDosObj far *)0) return (PyDosObj far *)0;
    for (i = 0; i < src->v.list.len; i++) {
        pydos_list_append(dst, src->v.list.items[i]);
    }
    return dst;
}

PyDosObj far * PYDOS_API pydos_list_from_iter(PyDosObj far *iterable)
{
    PyDosObj far *result;
    PyDosObj far *iter;
    PyDosObj far *item;

    if (iterable == (PyDosObj far *)0) return pydos_list_new(4);

    /* Fast path: if already a list or tuple, just copy */
    if (iterable->type == PYDT_LIST || iterable->type == PYDT_TUPLE) {
        return pydos_list_copy(iterable);
    }

    /* Slow path: iterate and collect */
    result = pydos_list_new(8);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;

    iter = pydos_obj_get_iter(iterable);
    if (iter == (PyDosObj far *)0) return result;

    for (;;) {
        item = pydos_obj_iter_next(iter);
        if (item == (PyDosObj far *)0) break;
        pydos_list_append(result, item);
        PYDOS_DECREF(item);
    }
    PYDOS_DECREF(iter);
    return result;
}

/*
 * Star unpacking helper for assignment targets with *.
 * Example: a, *b, c = [1,2,3,4,5]
 *   pydos_unpack_ex(3, [seq, int(1), int(1)])
 *   → list [1, [2,3,4], 5]  (before=1 item, starred list, after=1 item)
 *
 * Builtin calling convention: argc=3, argv=[seq, before_int, after_int].
 * Returns a list with (before + 1 + after) elements where the element
 * at index 'before' is a list containing the starred portion.
 * Raises ValueError if len(seq) < before + after.
 */
PyDosObj far * PYDOS_API pydos_unpack_ex(int argc,
                              PyDosObj far * far *argv)
{
    PyDosObj far *seq;
    PyDosObj far *result;
    PyDosObj far *starred_list;
    PyDosObj far *item;
    PyDosObj far *idx_obj;
    long seq_len;
    long starred_len;
    int before;
    int after;
    int i;

    (void)argc;
    seq = argv[0];
    before = (int)argv[1]->v.int_val;
    after = (int)argv[2]->v.int_val;

    if (seq == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
            (const char far *)"cannot unpack None");
        return pydos_obj_new_none();
    }

    /* Get sequence length */
    if ((PyDosType)seq->type == PYDT_LIST) {
        seq_len = (long)seq->v.list.len;
    } else if ((PyDosType)seq->type == PYDT_TUPLE) {
        seq_len = (long)seq->v.tuple.len;
    } else if ((PyDosType)seq->type == PYDT_STR) {
        seq_len = (long)seq->v.str.len;
    } else {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
            (const char far *)"cannot unpack non-sequence");
        return pydos_obj_new_none();
    }

    /* Validate minimum length */
    if (seq_len < (long)(before + after)) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
            (const char far *)"not enough values to unpack");
        return pydos_obj_new_none();
    }

    starred_len = seq_len - (long)before - (long)after;

    /* Build result list with (before + 1 + after) elements */
    result = pydos_list_new((unsigned int)(before + 1 + after));
    if (result == (PyDosObj far *)0) return pydos_obj_new_none();

    /* Extract 'before' elements from front */
    for (i = 0; i < before; i++) {
        idx_obj = pydos_obj_new_int((long)i);
        item = pydos_obj_getitem(seq, idx_obj);
        pydos_list_append(result, item);
        PYDOS_DECREF(item);
        PYDOS_DECREF(idx_obj);
    }

    /* Build starred list from middle portion */
    starred_list = pydos_list_new(starred_len > 0 ? (unsigned int)starred_len : 1);
    if (starred_list != (PyDosObj far *)0) {
        for (i = before; i < before + (int)starred_len; i++) {
            idx_obj = pydos_obj_new_int((long)i);
            item = pydos_obj_getitem(seq, idx_obj);
            pydos_list_append(starred_list, item);
            PYDOS_DECREF(item);
            PYDOS_DECREF(idx_obj);
        }
    }
    pydos_list_append(result, starred_list);
    PYDOS_DECREF(starred_list);

    /* Extract 'after' elements from back */
    for (i = 0; i < after; i++) {
        long back_idx = seq_len - (long)after + (long)i;
        idx_obj = pydos_obj_new_int(back_idx);
        item = pydos_obj_getitem(seq, idx_obj);
        pydos_list_append(result, item);
        PYDOS_DECREF(item);
        PYDOS_DECREF(idx_obj);
    }

    return result;
}

PyDosObj far * PYDOS_API pydos_list_pop_m(PyDosObj far *self, PyDosObj far *idx_obj)
{
    long idx;
    idx = (idx_obj != (PyDosObj far *)0 && idx_obj->type == PYDT_INT)
           ? idx_obj->v.int_val : -1L;
    return pydos_list_pop(self, idx);
}

void PYDOS_API pydos_list_insert_m(PyDosObj far *self, PyDosObj far *idx_obj,
                          PyDosObj far *item)
{
    long idx;
    idx = (idx_obj != (PyDosObj far *)0 && idx_obj->type == PYDT_INT)
           ? idx_obj->v.int_val : 0L;
    pydos_list_insert(self, idx, item);
}

void PYDOS_API pydos_list_init(void)
{
    /* No global state to initialize */
}

void PYDOS_API pydos_list_shutdown(void)
{
    /* No global state to clean up */
}
