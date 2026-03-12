/*
 * pydos_dict.c - Dictionary operations for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Open addressing hash table with linear probing.
 * Load factor threshold: 0.75 -> resize to 2x.
 * Deleted entries are marked with a sentinel key.
 */

#include "pdos_dic.h"
#include "pdos_lst.h"
#include "pdos_obj.h"
#include "pdos_exc.h"
#include <string.h>

#include "pdos_mem.h"

/* Sentinel value for deleted entries.
 * We use a special static object allocated at init time. */
static PyDosObj far *dict_deleted_sentinel = (PyDosObj far *)0;

/* Minimum initial table size */
#define DICT_MIN_SIZE 8

/*
 * Is this entry empty (never used)?
 */
static int entry_empty(PyDosDictEntry far *entry)
{
    return (entry->key == (PyDosObj far *)0);
}

/*
 * Is this entry a deleted tombstone?
 */
static int entry_deleted(PyDosDictEntry far *entry)
{
    return (entry->key == dict_deleted_sentinel);
}

/*
 * Is this entry active (has a live key-value pair)?
 */
static int entry_active(PyDosDictEntry far *entry)
{
    return (entry->key != (PyDosObj far *)0 &&
            entry->key != dict_deleted_sentinel);
}

/*
 * dict_resize - Resize the hash table to new_size.
 * Rehashes all active entries.
 */
static void dict_resize(PyDosObj far *dict, unsigned int new_size)
{
    PyDosDictEntry far *old_entries;
    PyDosDictEntry far *new_entries;
    unsigned int old_size, i, idx;
    unsigned long alloc_size;

    old_entries = dict->v.dict.entries;
    old_size = dict->v.dict.size;

    alloc_size = (unsigned long)new_size * (unsigned long)sizeof(PyDosDictEntry);
    new_entries = (PyDosDictEntry far *)pydos_far_alloc(alloc_size);
    if (new_entries == (PyDosDictEntry far *)0) {
        return;
    }

    /* Zero out new table */
    _fmemset(new_entries, 0, (unsigned int)alloc_size);

    /* Rehash active entries from old table */
    for (i = 0; i < old_size; i++) {
        if (entry_active(&old_entries[i])) {
            idx = old_entries[i].hash & (new_size - 1);
            while (new_entries[idx].key != (PyDosObj far *)0) {
                idx = (idx + 1) & (new_size - 1);
            }
            new_entries[idx].key = old_entries[i].key;
            new_entries[idx].value = old_entries[i].value;
            new_entries[idx].hash = old_entries[i].hash;
            new_entries[idx].insert_order = old_entries[i].insert_order;
        }
    }

    dict->v.dict.entries = new_entries;
    dict->v.dict.size = new_size;

    if (old_entries != (PyDosDictEntry far *)0) {
        pydos_far_free(old_entries);
    }
}

PyDosObj far * PYDOS_API pydos_dict_new(unsigned int initial_size)
{
    PyDosObj far *dict;
    unsigned long alloc_size;

    if (initial_size < DICT_MIN_SIZE) {
        initial_size = DICT_MIN_SIZE;
    }

    /* Round up to power of 2 */
    {
        unsigned int s;
        s = 1;
        while (s < initial_size) {
            s <<= 1;
        }
        initial_size = s;
    }

    dict = pydos_obj_alloc();
    if (dict == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    dict->type = PYDT_DICT;
    dict->flags = 0;
    dict->refcount = 1;
    dict->v.dict.size = initial_size;
    dict->v.dict.used = 0;

    alloc_size = (unsigned long)initial_size * (unsigned long)sizeof(PyDosDictEntry);
    dict->v.dict.entries = (PyDosDictEntry far *)pydos_far_alloc(alloc_size);

    if (dict->v.dict.entries == (PyDosDictEntry far *)0) {
        dict->v.dict.size = 0;
    } else {
        _fmemset(dict->v.dict.entries, 0, (unsigned int)alloc_size);
    }

    return dict;
}

/*
 * Find the entry for a given key, or the slot where it should go.
 * Returns the index, or -1 if the table is full (should not happen
 * if load factor is maintained).
 */
static long dict_find_slot(PyDosObj far *dict, PyDosObj far *key,
                           unsigned int hash)
{
    unsigned int idx, start;
    long first_deleted;

    if (dict->v.dict.size == 0) {
        return -1L;
    }

    idx = hash & (dict->v.dict.size - 1);
    start = idx;
    first_deleted = -1L;

    do {
        if (entry_empty(&dict->v.dict.entries[idx])) {
            /* Empty slot - key not found */
            if (first_deleted >= 0L) {
                return first_deleted;
            }
            return (long)idx;
        }

        if (entry_deleted(&dict->v.dict.entries[idx])) {
            /* Deleted tombstone - remember first one */
            if (first_deleted < 0L) {
                first_deleted = (long)idx;
            }
        } else if (dict->v.dict.entries[idx].hash == hash) {
            /* Hash matches - check key equality */
            if (pydos_obj_equal(dict->v.dict.entries[idx].key, key)) {
                return (long)idx;
            }
        }

        idx = (idx + 1) & (dict->v.dict.size - 1);
    } while (idx != start);

    /* Table is full (should not happen) */
    if (first_deleted >= 0L) {
        return first_deleted;
    }
    return -1L;
}

void PYDOS_API pydos_dict_set(PyDosObj far *dict, PyDosObj far *key,
                    PyDosObj far *value)
{
    unsigned int hash, load_threshold;
    long slot;
    PyDosObj far *old_value;

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET) ||
        key == (PyDosObj far *)0) {
        return;
    }

    hash = pydos_obj_hash(key);

    /* Check if we need to resize (load factor > 0.75) */
    load_threshold = dict->v.dict.size - (dict->v.dict.size >> 2);
    if (dict->v.dict.used >= load_threshold) {
        dict_resize(dict, dict->v.dict.size * 2);
    }

    slot = dict_find_slot(dict, key, hash);
    if (slot < 0L) {
        return;
    }

    if (entry_active(&dict->v.dict.entries[(unsigned int)slot]) &&
        dict->v.dict.entries[(unsigned int)slot].hash == hash &&
        pydos_obj_equal(dict->v.dict.entries[(unsigned int)slot].key, key)) {
        /* Key already exists - update value */
        old_value = dict->v.dict.entries[(unsigned int)slot].value;
        dict->v.dict.entries[(unsigned int)slot].value = value;
        PYDOS_INCREF(value);
        PYDOS_DECREF(old_value);
    } else {
        /* New entry */
        dict->v.dict.entries[(unsigned int)slot].key = key;
        dict->v.dict.entries[(unsigned int)slot].value = value;
        dict->v.dict.entries[(unsigned int)slot].hash = hash;
        dict->v.dict.entries[(unsigned int)slot].insert_order = dict->v.dict.next_order++;
        PYDOS_INCREF(key);
        PYDOS_INCREF(value);
        dict->v.dict.used++;
    }
}

PyDosObj far * PYDOS_API pydos_dict_get(PyDosObj far *dict, PyDosObj far *key)
{
    unsigned int hash, idx, start;

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET) ||
        key == (PyDosObj far *)0 || dict->v.dict.size == 0) {
        return (PyDosObj far *)0;
    }

    hash = pydos_obj_hash(key);
    idx = hash & (dict->v.dict.size - 1);
    start = idx;

    do {
        if (entry_empty(&dict->v.dict.entries[idx])) {
            return (PyDosObj far *)0;
        }

        if (!entry_deleted(&dict->v.dict.entries[idx]) &&
            dict->v.dict.entries[idx].hash == hash &&
            pydos_obj_equal(dict->v.dict.entries[idx].key, key)) {
            PYDOS_INCREF(dict->v.dict.entries[idx].value);
            return dict->v.dict.entries[idx].value;
        }

        idx = (idx + 1) & (dict->v.dict.size - 1);
    } while (idx != start);

    return (PyDosObj far *)0;
}

int PYDOS_API pydos_dict_delete(PyDosObj far *dict, PyDosObj far *key)
{
    unsigned int hash, idx, start;

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET) ||
        key == (PyDosObj far *)0 || dict->v.dict.size == 0) {
        return 0;
    }

    hash = pydos_obj_hash(key);
    idx = hash & (dict->v.dict.size - 1);
    start = idx;

    do {
        if (entry_empty(&dict->v.dict.entries[idx])) {
            return 0;
        }

        if (!entry_deleted(&dict->v.dict.entries[idx]) &&
            dict->v.dict.entries[idx].hash == hash &&
            pydos_obj_equal(dict->v.dict.entries[idx].key, key)) {
            /* Found it - mark as deleted */
            PYDOS_DECREF(dict->v.dict.entries[idx].key);
            PYDOS_DECREF(dict->v.dict.entries[idx].value);
            dict->v.dict.entries[idx].key = dict_deleted_sentinel;
            dict->v.dict.entries[idx].value = (PyDosObj far *)0;
            dict->v.dict.entries[idx].hash = 0;
            dict->v.dict.used--;
            return 1;
        }

        idx = (idx + 1) & (dict->v.dict.size - 1);
    } while (idx != start);

    return 0;
}

int PYDOS_API pydos_dict_contains(PyDosObj far *dict, PyDosObj far *key)
{
    unsigned int hash, idx, start;

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET) ||
        key == (PyDosObj far *)0 || dict->v.dict.size == 0) {
        return 0;
    }

    hash = pydos_obj_hash(key);
    idx = hash & (dict->v.dict.size - 1);
    start = idx;

    do {
        if (entry_empty(&dict->v.dict.entries[idx])) {
            return 0;
        }

        if (!entry_deleted(&dict->v.dict.entries[idx]) &&
            dict->v.dict.entries[idx].hash == hash &&
            pydos_obj_equal(dict->v.dict.entries[idx].key, key)) {
            return 1;
        }

        idx = (idx + 1) & (dict->v.dict.size - 1);
    } while (idx != start);

    return 0;
}

long PYDOS_API pydos_dict_len(PyDosObj far *dict)
{
    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET)) {
        return 0L;
    }
    return (long)dict->v.dict.used;
}

void PYDOS_API pydos_dict_clear(PyDosObj far *dict)
{
    unsigned int i;
    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET)) return;
    for (i = 0; i < dict->v.dict.size; i++) {
        if (entry_active(&dict->v.dict.entries[i])) {
            PYDOS_DECREF(dict->v.dict.entries[i].key);
            PYDOS_DECREF(dict->v.dict.entries[i].value);
            dict->v.dict.entries[i].key = (PyDosObj far *)0;
            dict->v.dict.entries[i].value = (PyDosObj far *)0;
            dict->v.dict.entries[i].hash = 0;
        }
    }
    dict->v.dict.used = 0;
    dict->v.dict.next_order = 0;
}

PyDosObj far * PYDOS_API pydos_dict_keys(PyDosObj far *dict)
{
    PyDosObj far *list;

    list = pydos_list_new(dict != (PyDosObj far *)0 ? dict->v.dict.used : 4);
    if (list == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET)) {
        return list;
    }

    /* Iterate in insertion order */
    {
        unsigned int next_min = 0;
        unsigned int emitted = 0;
        unsigned int j, best_j, best_ord;

        while (emitted < dict->v.dict.used) {
            best_ord = (unsigned int)-1;
            best_j = 0;
            for (j = 0; j < dict->v.dict.size; j++) {
                if (entry_active(&dict->v.dict.entries[j]) &&
                    dict->v.dict.entries[j].insert_order >= next_min &&
                    dict->v.dict.entries[j].insert_order < best_ord) {
                    best_ord = dict->v.dict.entries[j].insert_order;
                    best_j = j;
                }
            }
            if (best_ord == (unsigned int)-1) break;
            pydos_list_append(list, dict->v.dict.entries[best_j].key);
            next_min = best_ord + 1;
            emitted++;
        }
    }

    return list;
}

PyDosObj far * PYDOS_API pydos_dict_values(PyDosObj far *dict)
{
    PyDosObj far *list;

    list = pydos_list_new(dict != (PyDosObj far *)0 ? dict->v.dict.used : 4);
    if (list == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET)) {
        return list;
    }

    /* Iterate in insertion order */
    {
        unsigned int next_min = 0;
        unsigned int emitted = 0;
        unsigned int j, best_j, best_ord;

        while (emitted < dict->v.dict.used) {
            best_ord = (unsigned int)-1;
            best_j = 0;
            for (j = 0; j < dict->v.dict.size; j++) {
                if (entry_active(&dict->v.dict.entries[j]) &&
                    dict->v.dict.entries[j].insert_order >= next_min &&
                    dict->v.dict.entries[j].insert_order < best_ord) {
                    best_ord = dict->v.dict.entries[j].insert_order;
                    best_j = j;
                }
            }
            if (best_ord == (unsigned int)-1) break;
            pydos_list_append(list, dict->v.dict.entries[best_j].value);
            next_min = best_ord + 1;
            emitted++;
        }
    }

    return list;
}

PyDosObj far * PYDOS_API pydos_dict_items(PyDosObj far *dict)
{
    PyDosObj far *list;
    PyDosObj far *tuple;

    list = pydos_list_new(dict != (PyDosObj far *)0 ? dict->v.dict.used : 4);
    if (list == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    if (dict == (PyDosObj far *)0 ||
        (dict->type != PYDT_DICT && dict->type != PYDT_SET)) {
        return list;
    }

    /* Iterate in insertion order */
    {
        unsigned int next_min = 0;
        unsigned int emitted = 0;
        unsigned int j, best_j, best_ord;

        while (emitted < dict->v.dict.used) {
            best_ord = (unsigned int)-1;
            best_j = 0;
            for (j = 0; j < dict->v.dict.size; j++) {
                if (entry_active(&dict->v.dict.entries[j]) &&
                    dict->v.dict.entries[j].insert_order >= next_min &&
                    dict->v.dict.entries[j].insert_order < best_ord) {
                    best_ord = dict->v.dict.entries[j].insert_order;
                    best_j = j;
                }
            }
            if (best_ord == (unsigned int)-1) break;

            /* Create a 2-tuple for (key, value) */
            tuple = pydos_obj_alloc();
            if (tuple != (PyDosObj far *)0) {
                tuple->type = PYDT_TUPLE;
                tuple->flags = 0;
                tuple->refcount = 1;
                tuple->v.tuple.len = 2;
                tuple->v.tuple.items = (PyDosObj far * far *)pydos_far_alloc(
                    2UL * (unsigned long)sizeof(PyDosObj far *));

                if (tuple->v.tuple.items != (PyDosObj far * far *)0) {
                    tuple->v.tuple.items[0] = dict->v.dict.entries[best_j].key;
                    PYDOS_INCREF(dict->v.dict.entries[best_j].key);
                    tuple->v.tuple.items[1] = dict->v.dict.entries[best_j].value;
                    PYDOS_INCREF(dict->v.dict.entries[best_j].value);
                    pydos_list_append(list, tuple);
                    PYDOS_DECREF(tuple);
                } else {
                    tuple->v.tuple.len = 0;
                    PYDOS_DECREF(tuple);
                }
            }

            next_min = best_ord + 1;
            emitted++;
        }
    }

    return list;
}

/* ---- Set method wrappers ---- */

void PYDOS_API pydos_set_add(PyDosObj far *self, PyDosObj far *item)
{
    PyDosObj far *none_val = pydos_obj_new_none();
    pydos_dict_set(self, item, none_val);
    PYDOS_DECREF(none_val);
}

void PYDOS_API pydos_set_remove(PyDosObj far *self, PyDosObj far *item)
{
    if (!pydos_dict_contains(self, item)) {
        pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                         (const char far *)"set.remove(x): x not in set");
        return;
    }
    pydos_dict_delete(self, item);
}

void PYDOS_API pydos_set_discard(PyDosObj far *self, PyDosObj far *item)
{
    pydos_dict_delete(self, item);
}

void PYDOS_API pydos_set_clear(PyDosObj far *self)
{
    pydos_dict_clear(self);
}

PyDosObj far * PYDOS_API pydos_set_pop(PyDosObj far *self)
{
    PyDosObj far *keys;
    PyDosObj far *item;

    keys = pydos_dict_keys(self);
    if (keys == (PyDosObj far *)0 || keys->v.list.len == 0) {
        if (keys != (PyDosObj far *)0) PYDOS_DECREF(keys);
        pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                         (const char far *)"pop from an empty set");
        return pydos_obj_new_none();
    }
    item = pydos_list_get(keys, 0L);
    pydos_dict_delete(self, item);
    PYDOS_DECREF(keys);
    return item;
}

void PYDOS_API pydos_dict_init(void)
{
    /* Create the deleted sentinel object.
     * This is a special object that marks deleted hash table entries.
     * It is immortal and never freed. */
    dict_deleted_sentinel = pydos_obj_alloc();
    if (dict_deleted_sentinel != (PyDosObj far *)0) {
        dict_deleted_sentinel->type = PYDT_NONE;
        dict_deleted_sentinel->flags = OBJ_FLAG_IMMORTAL;
        dict_deleted_sentinel->refcount = 1;
    }
}

void PYDOS_API pydos_dict_shutdown(void)
{
    if (dict_deleted_sentinel != (PyDosObj far *)0) {
        dict_deleted_sentinel->flags &= ~OBJ_FLAG_IMMORTAL;
        PYDOS_DECREF(dict_deleted_sentinel);
        dict_deleted_sentinel = (PyDosObj far *)0;
    }
}
