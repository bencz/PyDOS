/*
 * t_dic.c - Unit tests for pdos_dic (dict) module
 *
 * Tests dict creation, set/get, overwrite, delete, contains,
 * len, keys/values/items (C API), mixed key types, growth,
 * dict.get via call_method, dict.clear, and iteration.
 *
 * PIR-backed methods (pop, setdefault, copy, update, popitem,
 * keys/values/items via Python) are tested via integration tests.
 */

#include "testfw.h"
#include "../runtime/pdos_dic.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_lst.h"

/* ------------------------------------------------------------------ */
/* Helper: create a string object (shorthand)                          */
/* ------------------------------------------------------------------ */
static PyDosObj far *mkstr(const char *s, unsigned int len)
{
    return pydos_obj_new_str((const char far *)s, len);
}

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

TEST(dict_new)
{
    PyDosObj far *d = pydos_dict_new(8);
    ASSERT_NOT_NULL(d);
    ASSERT_EQ(d->type, PYDT_DICT);
    ASSERT_EQ(pydos_dict_len(d), 0L);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Set / Get                                                           */
/* ------------------------------------------------------------------ */

TEST(dict_set_get)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key = mkstr("a", 1);
    PyDosObj far *val = pydos_obj_new_int(1L);
    PyDosObj far *lookup_key;
    PyDosObj far *got;

    pydos_dict_set(d, key, val);

    /* Use a NEW string object for lookup to test value-based matching */
    lookup_key = mkstr("a", 1);
    got = pydos_dict_get(d, lookup_key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 1L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(lookup_key);
    PYDOS_DECREF(key);
    PYDOS_DECREF(val);
    PYDOS_DECREF(d);
}

TEST(dict_set_overwrite)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key1 = mkstr("x", 1);
    PyDosObj far *val1 = pydos_obj_new_int(100L);
    PyDosObj far *key2 = mkstr("x", 1);
    PyDosObj far *val2 = pydos_obj_new_int(200L);
    PyDosObj far *lookup_key;
    PyDosObj far *got;

    pydos_dict_set(d, key1, val1);
    pydos_dict_set(d, key2, val2);

    ASSERT_EQ(pydos_dict_len(d), 1L);

    lookup_key = mkstr("x", 1);
    got = pydos_dict_get(d, lookup_key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 200L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(lookup_key);
    PYDOS_DECREF(key1);
    PYDOS_DECREF(key2);
    PYDOS_DECREF(val1);
    PYDOS_DECREF(val2);
    PYDOS_DECREF(d);
}

TEST(dict_get_missing)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key = mkstr("nope", 4);
    PyDosObj far *got;

    got = pydos_dict_get(d, key);
    ASSERT_NULL(got);

    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Delete                                                              */
/* ------------------------------------------------------------------ */

TEST(dict_delete_exists)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key = mkstr("del", 3);
    PyDosObj far *val = pydos_obj_new_int(42L);
    PyDosObj far *del_key;
    int rc;

    pydos_dict_set(d, key, val);
    ASSERT_EQ(pydos_dict_len(d), 1L);

    del_key = mkstr("del", 3);
    rc = pydos_dict_delete(d, del_key);
    ASSERT_EQ(rc, 1);
    ASSERT_EQ(pydos_dict_len(d), 0L);

    PYDOS_DECREF(del_key);
    PYDOS_DECREF(key);
    PYDOS_DECREF(val);
    PYDOS_DECREF(d);
}

TEST(dict_delete_missing)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key = mkstr("ghost", 5);
    int rc;

    rc = pydos_dict_delete(d, key);
    ASSERT_EQ(rc, 0);

    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Contains                                                            */
/* ------------------------------------------------------------------ */

TEST(dict_contains_true)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key = mkstr("here", 4);
    PyDosObj far *val = pydos_obj_new_int(1L);
    PyDosObj far *chk;

    pydos_dict_set(d, key, val);

    chk = mkstr("here", 4);
    ASSERT_EQ(pydos_dict_contains(d, chk), 1);

    PYDOS_DECREF(chk);
    PYDOS_DECREF(key);
    PYDOS_DECREF(val);
    PYDOS_DECREF(d);
}

TEST(dict_contains_false)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key = mkstr("absent", 6);

    ASSERT_EQ(pydos_dict_contains(d, key), 0);

    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Len                                                                 */
/* ------------------------------------------------------------------ */

TEST(dict_len)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("a", 1);
    PyDosObj far *k2 = mkstr("b", 1);
    PyDosObj far *k3 = mkstr("c", 1);
    PyDosObj far *v1 = pydos_obj_new_int(1L);
    PyDosObj far *v2 = pydos_obj_new_int(2L);
    PyDosObj far *v3 = pydos_obj_new_int(3L);

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);
    pydos_dict_set(d, k3, v3);

    ASSERT_EQ(pydos_dict_len(d), 3L);

    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(k3);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(v3);
    PYDOS_DECREF(d);
}

TEST(dict_len_after_delete)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("a", 1);
    PyDosObj far *k2 = mkstr("b", 1);
    PyDosObj far *k3 = mkstr("c", 1);
    PyDosObj far *v1 = pydos_obj_new_int(1L);
    PyDosObj far *v2 = pydos_obj_new_int(2L);
    PyDosObj far *v3 = pydos_obj_new_int(3L);
    PyDosObj far *del_key;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);
    pydos_dict_set(d, k3, v3);

    del_key = mkstr("b", 1);
    pydos_dict_delete(d, del_key);
    ASSERT_EQ(pydos_dict_len(d), 2L);

    PYDOS_DECREF(del_key);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(k3);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(v3);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Keys / Values / Items (test C runtime internal utility functions)    */
/* ------------------------------------------------------------------ */

TEST(dict_keys)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("a", 1);
    PyDosObj far *k2 = mkstr("b", 1);
    PyDosObj far *v1 = pydos_obj_new_int(1L);
    PyDosObj far *v2 = pydos_obj_new_int(2L);
    PyDosObj far *keys;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);

    keys = pydos_dict_keys(d);
    ASSERT_NOT_NULL(keys);
    ASSERT_EQ(keys->type, PYDT_LIST);
    ASSERT_EQ(keys->v.list.len, 2);

    PYDOS_DECREF(keys);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(d);
}

TEST(dict_values)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("a", 1);
    PyDosObj far *k2 = mkstr("b", 1);
    PyDosObj far *v1 = pydos_obj_new_int(1L);
    PyDosObj far *v2 = pydos_obj_new_int(2L);
    PyDosObj far *vals;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);

    vals = pydos_dict_values(d);
    ASSERT_NOT_NULL(vals);
    ASSERT_EQ(vals->type, PYDT_LIST);
    ASSERT_EQ(vals->v.list.len, 2);

    PYDOS_DECREF(vals);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(d);
}

TEST(dict_items)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("a", 1);
    PyDosObj far *k2 = mkstr("b", 1);
    PyDosObj far *v1 = pydos_obj_new_int(1L);
    PyDosObj far *v2 = pydos_obj_new_int(2L);
    PyDosObj far *items;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);

    items = pydos_dict_items(d);
    ASSERT_NOT_NULL(items);
    ASSERT_EQ(items->type, PYDT_LIST);
    ASSERT_EQ(items->v.list.len, 2);

    PYDOS_DECREF(items);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Mixed key types                                                     */
/* ------------------------------------------------------------------ */

TEST(dict_multiple_types)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *str_key = mkstr("name", 4);
    PyDosObj far *int_key = pydos_obj_new_int(42L);
    PyDosObj far *val1 = pydos_obj_new_int(100L);
    PyDosObj far *val2 = pydos_obj_new_int(200L);
    PyDosObj far *lookup_str;
    PyDosObj far *lookup_int;
    PyDosObj far *got;

    pydos_dict_set(d, str_key, val1);
    pydos_dict_set(d, int_key, val2);

    ASSERT_EQ(pydos_dict_len(d), 2L);

    lookup_str = mkstr("name", 4);
    got = pydos_dict_get(d, lookup_str);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 100L);
    PYDOS_DECREF(got);

    lookup_int = pydos_obj_new_int(42L);
    got = pydos_dict_get(d, lookup_int);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 200L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lookup_str);
    PYDOS_DECREF(lookup_int);
    PYDOS_DECREF(str_key);
    PYDOS_DECREF(int_key);
    PYDOS_DECREF(val1);
    PYDOS_DECREF(val2);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Growth / Rehash                                                     */
/* ------------------------------------------------------------------ */

TEST(dict_grow)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *keys[20];
    PyDosObj far *vals[20];
    int i;

    /* Insert 20 entries to trigger multiple rehashes */
    for (i = 0; i < 20; i++) {
        char buf[8];
        unsigned int slen;
        buf[0] = 'k';
        buf[1] = (char)('A' + (i / 10));
        buf[2] = (char)('0' + (i % 10));
        buf[3] = '\0';
        slen = 3;
        keys[i] = mkstr(buf, slen);
        vals[i] = pydos_obj_new_int((long)(i * 10));
        pydos_dict_set(d, keys[i], vals[i]);
    }

    ASSERT_EQ(pydos_dict_len(d), 20L);

    /* Verify all entries retrievable with fresh key objects */
    for (i = 0; i < 20; i++) {
        char buf[8];
        unsigned int slen;
        PyDosObj far *lk;
        PyDosObj far *got;
        buf[0] = 'k';
        buf[1] = (char)('A' + (i / 10));
        buf[2] = (char)('0' + (i % 10));
        buf[3] = '\0';
        slen = 3;
        lk = mkstr(buf, slen);
        got = pydos_dict_get(d, lk);
        ASSERT_NOT_NULL(got);
        ASSERT_EQ(got->v.int_val, (long)(i * 10));
        PYDOS_DECREF(got);
        PYDOS_DECREF(lk);
    }

    for (i = 0; i < 20; i++) {
        PYDOS_DECREF(keys[i]);
        PYDOS_DECREF(vals[i]);
    }
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Integer keys                                                        */
/* ------------------------------------------------------------------ */

TEST(dict_int_keys)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = pydos_obj_new_int(10L);
    PyDosObj far *k2 = pydos_obj_new_int(20L);
    PyDosObj far *k3 = pydos_obj_new_int(30L);
    PyDosObj far *v1 = pydos_obj_new_int(100L);
    PyDosObj far *v2 = pydos_obj_new_int(200L);
    PyDosObj far *v3 = pydos_obj_new_int(300L);
    PyDosObj far *lookup;
    PyDosObj far *got;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);
    pydos_dict_set(d, k3, v3);

    ASSERT_EQ(pydos_dict_len(d), 3L);

    /* Lookup with fresh int key objects */
    lookup = pydos_obj_new_int(20L);
    got = pydos_dict_get(d, lookup);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 200L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(lookup);

    lookup = pydos_obj_new_int(10L);
    got = pydos_dict_get(d, lookup);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 100L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(lookup);

    lookup = pydos_obj_new_int(30L);
    got = pydos_dict_get(d, lookup);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 300L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(lookup);

    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(k3);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(v3);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* dict_keys_content: verify actual key values in returned list        */
/* ------------------------------------------------------------------ */

TEST(dict_keys_content)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("a", 1);
    PyDosObj far *k2 = mkstr("b", 1);
    PyDosObj far *v1 = pydos_obj_new_int(1L);
    PyDosObj far *v2 = pydos_obj_new_int(2L);
    PyDosObj far *keys;
    PyDosObj far *item;
    int found_a;
    int found_b;
    long i;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);

    keys = pydos_dict_keys(d);
    ASSERT_NOT_NULL(keys);
    ASSERT_EQ(keys->v.list.len, 2);

    found_a = 0;
    found_b = 0;
    for (i = 0; i < 2; i++) {
        item = pydos_list_get(keys, i);
        ASSERT_NOT_NULL(item);
        if (item->type == PYDT_STR) {
            if (item->v.str.len == 1 && item->v.str.data[0] == 'a')
                found_a = 1;
            if (item->v.str.len == 1 && item->v.str.data[0] == 'b')
                found_b = 1;
        }
        PYDOS_DECREF(item);
    }
    ASSERT_TRUE(found_a);
    ASSERT_TRUE(found_b);

    PYDOS_DECREF(keys);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* dict_values_content: verify actual values in returned list           */
/* ------------------------------------------------------------------ */

TEST(dict_values_content)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("x", 1);
    PyDosObj far *k2 = mkstr("y", 1);
    PyDosObj far *v1 = pydos_obj_new_int(10L);
    PyDosObj far *v2 = pydos_obj_new_int(20L);
    PyDosObj far *vals;
    PyDosObj far *item;
    int found_10;
    int found_20;
    long i;

    pydos_dict_set(d, k1, v1);
    pydos_dict_set(d, k2, v2);

    vals = pydos_dict_values(d);
    ASSERT_NOT_NULL(vals);
    ASSERT_EQ(vals->v.list.len, 2);

    found_10 = 0;
    found_20 = 0;
    for (i = 0; i < 2; i++) {
        item = pydos_list_get(vals, i);
        ASSERT_NOT_NULL(item);
        if (item->type == PYDT_INT) {
            if (item->v.int_val == 10L) found_10 = 1;
            if (item->v.int_val == 20L) found_20 = 1;
        }
        PYDOS_DECREF(item);
    }
    ASSERT_TRUE(found_10);
    ASSERT_TRUE(found_20);

    PYDOS_DECREF(vals);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* dict_items_content: verify (key, value) pairs in items list         */
/* ------------------------------------------------------------------ */

TEST(dict_items_content)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *k1 = mkstr("m", 1);
    PyDosObj far *v1 = pydos_obj_new_int(99L);
    PyDosObj far *items;
    PyDosObj far *pair;

    pydos_dict_set(d, k1, v1);

    items = pydos_dict_items(d);
    ASSERT_NOT_NULL(items);
    ASSERT_EQ(items->v.list.len, 1);

    pair = pydos_list_get(items, 0L);
    ASSERT_NOT_NULL(pair);
    ASSERT_EQ(pair->type, PYDT_TUPLE);
    ASSERT_EQ(pair->v.tuple.len, 2);

    ASSERT_NOT_NULL(pair->v.tuple.items[0]);
    ASSERT_EQ(pair->v.tuple.items[0]->type, PYDT_STR);
    ASSERT_STR_EQ(pair->v.tuple.items[0]->v.str.data, "m");

    ASSERT_NOT_NULL(pair->v.tuple.items[1]);
    ASSERT_EQ(pair->v.tuple.items[1]->type, PYDT_INT);
    ASSERT_EQ(pair->v.tuple.items[1]->v.int_val, 99L);

    PYDOS_DECREF(pair);
    PYDOS_DECREF(items);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* dict.get via call_method (with default)                             */
/* ------------------------------------------------------------------ */

TEST(dict_get_present)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *argv[3];
    PyDosObj far *result;

    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(10L));

    argv[0] = d;
    argv[1] = mkstr("a", 1);
    result = pydos_obj_call_method((const char far *)"get", 2, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 10L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(d);
}

TEST(dict_get_missing_none)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *argv[2];
    PyDosObj far *result;

    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(10L));

    argv[0] = d;
    argv[1] = mkstr("z", 1);
    result = pydos_obj_call_method((const char far *)"get", 2, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(d);
}

TEST(dict_get_missing_default)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *argv[3];
    PyDosObj far *result;

    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(10L));

    argv[0] = d;
    argv[1] = mkstr("z", 1);
    argv[2] = pydos_obj_new_int(99L);
    result = pydos_obj_call_method((const char far *)"get", 3, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 99L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(argv[2]);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* dict iteration via get_iter                                         */
/* ------------------------------------------------------------------ */

TEST(dict_iter)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *iter;
    PyDosObj far *val;
    int count = 0;
    int found_a = 0;
    int found_b = 0;

    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(1L));
    pydos_dict_set(d, mkstr("b", 1), pydos_obj_new_int(2L));

    iter = pydos_obj_get_iter(d);
    ASSERT_NOT_NULL(iter);

    while (1) {
        val = pydos_obj_iter_next(iter);
        if (val == (PyDosObj far *)0) break;
        ASSERT_EQ(val->type, PYDT_STR);
        if (val->v.str.len == 1 && val->v.str.data[0] == 'a') found_a = 1;
        if (val->v.str.len == 1 && val->v.str.data[0] == 'b') found_b = 1;
        count++;
        PYDOS_DECREF(val);
    }
    ASSERT_EQ(count, 2);
    ASSERT_TRUE(found_a);
    ASSERT_TRUE(found_b);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Dict method tests: clear, pop, setdefault, copy, update             */
/* ------------------------------------------------------------------ */

TEST(dict_clear_method)
{
    PyDosObj far *d = pydos_dict_new(8);
    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(1L));
    pydos_dict_set(d, mkstr("b", 1), pydos_obj_new_int(2L));
    ASSERT_EQ(pydos_dict_len(d), 2L);
    pydos_dict_clear(d);
    ASSERT_EQ(pydos_dict_len(d), 0L);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_dic_tests(void)
{
    SUITE("pdos_dic");

    RUN(dict_new);
    RUN(dict_set_get);
    RUN(dict_set_overwrite);
    RUN(dict_get_missing);
    RUN(dict_delete_exists);
    RUN(dict_delete_missing);
    RUN(dict_contains_true);
    RUN(dict_contains_false);
    RUN(dict_len);
    RUN(dict_len_after_delete);
    RUN(dict_keys);
    RUN(dict_values);
    RUN(dict_items);
    RUN(dict_multiple_types);
    RUN(dict_grow);
    RUN(dict_int_keys);
    RUN(dict_keys_content);
    RUN(dict_values_content);
    RUN(dict_items_content);
    RUN(dict_get_present);
    RUN(dict_get_missing_none);
    RUN(dict_get_missing_default);
    RUN(dict_iter);

    /* Dict method tests */
    RUN(dict_clear_method);
}
