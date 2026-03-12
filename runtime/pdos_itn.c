/*
 * pydos_intern.c - String interning for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Uses open addressing hash table for interned strings.
 * Interned strings are marked immortal and never freed by refcount.
 */

#include "pdos_itn.h"
#include "pdos_str.h"
#include "pdos_obj.h"
#include <string.h>

#include "pdos_mem.h"

/* Initial and current intern table size */
#define INTERN_INITIAL_SIZE 256

/* Intern table: array of far pointers to interned string objects */
static PyDosObj far * far *intern_table = (PyDosObj far * far *)0;
static unsigned int intern_size = 0;
static unsigned int intern_used = 0;

/*
 * djb2_hash_data - DJB2 hash for raw far string data.
 */
static unsigned int djb2_hash_data(const char far *data, unsigned int len)
{
    unsigned int hash;
    unsigned int i;

    hash = 5381;
    for (i = 0; i < len; i++) {
        hash = ((hash << 5) + hash) + (unsigned char)data[i];
    }
    if (hash == 0) {
        hash = 1;
    }
    return hash;
}

/*
 * strings_equal - Compare far string data for equality.
 */
static int strings_equal(const char far *a, unsigned int alen,
                         const char far *b, unsigned int blen)
{
    if (alen != blen) {
        return 0;
    }
    if (alen == 0) {
        return 1;
    }
    return (_fmemcmp(a, b, alen) == 0) ? 1 : 0;
}

/*
 * intern_resize - Grow the intern table.
 */
static void intern_resize(unsigned int new_size)
{
    PyDosObj far * far *new_table;
    PyDosObj far *entry;
    unsigned int i, idx, old_size;
    PyDosObj far * far *old_table;

    new_table = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned long)new_size * (unsigned long)sizeof(PyDosObj far *));
    if (new_table == (PyDosObj far * far *)0) {
        return;
    }

    /* Zero out new table */
    _fmemset(new_table, 0,
             (unsigned int)((unsigned long)new_size * sizeof(PyDosObj far *)));

    /* Rehash existing entries */
    old_table = intern_table;
    old_size = intern_size;

    for (i = 0; i < old_size; i++) {
        entry = old_table[i];
        if (entry != (PyDosObj far *)0) {
            idx = entry->v.str.hash & (new_size - 1);
            while (new_table[idx] != (PyDosObj far *)0) {
                idx = (idx + 1) & (new_size - 1);
            }
            new_table[idx] = entry;
        }
    }

    /* Replace old table */
    intern_table = new_table;
    intern_size = new_size;

    if (old_table != (PyDosObj far * far *)0) {
        pydos_far_free(old_table);
    }
}

PyDosObj far * PYDOS_API pydos_intern(PyDosObj far *str)
{
    unsigned int hash, idx;
    PyDosObj far *existing;
    unsigned int load_threshold;

    if (str == (PyDosObj far *)0 || str->type != PYDT_STR) {
        return str;
    }

    if (intern_table == (PyDosObj far * far *)0) {
        return str;
    }

    /* Ensure hash is computed */
    hash = pydos_str_hash(str);

    /* Search for existing interned string */
    idx = hash & (intern_size - 1);
    for (;;) {
        existing = intern_table[idx];
        if (existing == (PyDosObj far *)0) {
            break;
        }
        if (existing->v.str.hash == hash &&
            strings_equal(existing->v.str.data, existing->v.str.len,
                         str->v.str.data, str->v.str.len)) {
            /* Found existing interned string */
            PYDOS_INCREF(existing);
            return existing;
        }
        idx = (idx + 1) & (intern_size - 1);
    }

    /* Not found - check if we need to resize */
    load_threshold = intern_size - (intern_size >> 2); /* 75% */
    if (intern_used >= load_threshold) {
        intern_resize(intern_size * 2);
        /* Recompute index after resize */
        idx = hash & (intern_size - 1);
        while (intern_table[idx] != (PyDosObj far *)0) {
            idx = (idx + 1) & (intern_size - 1);
        }
    }

    /* Add to table. Mark as immortal. */
    str->flags |= OBJ_FLAG_IMMORTAL;
    intern_table[idx] = str;
    intern_used++;

    PYDOS_INCREF(str);
    return str;
}

PyDosObj far * PYDOS_API pydos_intern_lookup(const char far *data, unsigned int len)
{
    unsigned int hash, idx;
    PyDosObj far *entry;

    if (intern_table == (PyDosObj far * far *)0 || data == (const char far *)0) {
        return (PyDosObj far *)0;
    }

    hash = djb2_hash_data(data, len);
    idx = hash & (intern_size - 1);

    for (;;) {
        entry = intern_table[idx];
        if (entry == (PyDosObj far *)0) {
            return (PyDosObj far *)0;
        }
        if (entry->v.str.hash == hash &&
            strings_equal(entry->v.str.data, entry->v.str.len,
                         data, len)) {
            PYDOS_INCREF(entry);
            return entry;
        }
        idx = (idx + 1) & (intern_size - 1);
    }
}

void PYDOS_API pydos_intern_init(void)
{
    intern_size = INTERN_INITIAL_SIZE;
    intern_used = 0;

    intern_table = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned long)intern_size * (unsigned long)sizeof(PyDosObj far *));
    if (intern_table != (PyDosObj far * far *)0) {
        _fmemset(intern_table, 0,
                 (unsigned int)((unsigned long)intern_size * sizeof(PyDosObj far *)));
    }
}

void PYDOS_API pydos_intern_shutdown(void)
{
    unsigned int i;
    PyDosObj far *entry;

    if (intern_table != (PyDosObj far * far *)0) {
        /* Clear immortal flag on all interned strings so they can be freed */
        for (i = 0; i < intern_size; i++) {
            entry = intern_table[i];
            if (entry != (PyDosObj far *)0) {
                entry->flags &= ~OBJ_FLAG_IMMORTAL;
                PYDOS_DECREF(entry);
            }
        }
        pydos_far_free(intern_table);
        intern_table = (PyDosObj far * far *)0;
    }
    intern_size = 0;
    intern_used = 0;
}
