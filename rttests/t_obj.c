/*
 * t_obj.c - Unit tests for pdos_obj module
 *
 * Tests object allocation, constructors, truthiness, equality,
 * hashing, string conversion, type names, reference counting,
 * getitem, setitem, iterators, and call_method.
 */

#include "testfw.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_dic.h"
#include "../runtime/pdos_mem.h"

/* ------------------------------------------------------------------ */
/* Allocation                                                          */
/* ------------------------------------------------------------------ */

TEST(alloc_returns_nonnull)
{
    PyDosObj far *obj = pydos_obj_alloc();
    ASSERT_NOT_NULL(obj);
    pydos_obj_free(obj);
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

TEST(new_none)
{
    PyDosObj far *obj = pydos_obj_new_none();
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_NONE);
    ASSERT_EQ(obj->refcount, 1);
    PYDOS_DECREF(obj);
}

TEST(new_bool_true)
{
    PyDosObj far *obj = pydos_obj_new_bool(1);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_BOOL);
    ASSERT_EQ(obj->v.bool_val, 1);
    PYDOS_DECREF(obj);
}

TEST(new_bool_false)
{
    PyDosObj far *obj = pydos_obj_new_bool(0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_BOOL);
    ASSERT_EQ(obj->v.bool_val, 0);
    PYDOS_DECREF(obj);
}

TEST(new_int)
{
    PyDosObj far *obj = pydos_obj_new_int(42L);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_INT);
    ASSERT_EQ(obj->v.int_val, 42L);
    PYDOS_DECREF(obj);
}

TEST(new_int_negative)
{
    PyDosObj far *obj = pydos_obj_new_int(-100L);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->v.int_val, -100L);
    PYDOS_DECREF(obj);
}

TEST(new_int_zero)
{
    PyDosObj far *obj = pydos_obj_new_int(0L);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->v.int_val, 0L);
    PYDOS_DECREF(obj);
}

TEST(new_float)
{
    PyDosObj far *obj = pydos_obj_new_float(3.14);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_FLOAT);
    PYDOS_DECREF(obj);
}

TEST(new_str)
{
    PyDosObj far *obj = pydos_obj_new_str((const char far *)"hello", 5);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_STR);
    ASSERT_EQ(obj->v.str.len, 5);
    PYDOS_DECREF(obj);
}

TEST(new_str_empty)
{
    PyDosObj far *obj = pydos_obj_new_str((const char far *)"", 0);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->type, PYDT_STR);
    ASSERT_EQ(obj->v.str.len, 0);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Truthiness                                                          */
/* ------------------------------------------------------------------ */

TEST(is_truthy_none)
{
    PyDosObj far *obj = pydos_obj_new_none();
    ASSERT_FALSE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_bool_true)
{
    PyDosObj far *obj = pydos_obj_new_bool(1);
    ASSERT_TRUE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_bool_false)
{
    PyDosObj far *obj = pydos_obj_new_bool(0);
    ASSERT_FALSE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_int_nonzero)
{
    PyDosObj far *obj = pydos_obj_new_int(7L);
    ASSERT_TRUE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_int_zero)
{
    PyDosObj far *obj = pydos_obj_new_int(0L);
    ASSERT_FALSE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_str_nonempty)
{
    PyDosObj far *obj = pydos_obj_new_str((const char far *)"x", 1);
    ASSERT_TRUE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_str_empty)
{
    PyDosObj far *obj = pydos_obj_new_str((const char far *)"", 0);
    ASSERT_FALSE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Equality                                                            */
/* ------------------------------------------------------------------ */

TEST(equal_ints)
{
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(10L);
    ASSERT_TRUE(pydos_obj_equal(a, b));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(equal_ints_diff)
{
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    ASSERT_FALSE(pydos_obj_equal(a, b));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(equal_strs)
{
    PyDosObj far *a = pydos_obj_new_str((const char far *)"abc", 3);
    PyDosObj far *b = pydos_obj_new_str((const char far *)"abc", 3);
    ASSERT_TRUE(pydos_obj_equal(a, b));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(equal_none)
{
    PyDosObj far *a = pydos_obj_new_none();
    PyDosObj far *b = pydos_obj_new_none();
    ASSERT_TRUE(pydos_obj_equal(a, b));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* Hashing                                                             */
/* ------------------------------------------------------------------ */

TEST(hash_int)
{
    PyDosObj far *a = pydos_obj_new_int(42L);
    PyDosObj far *b = pydos_obj_new_int(42L);
    unsigned int h1 = pydos_obj_hash(a);
    unsigned int h2 = pydos_obj_hash(b);
    ASSERT_EQ(h1, h2);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(hash_str)
{
    PyDosObj far *a = pydos_obj_new_str((const char far *)"test", 4);
    PyDosObj far *b = pydos_obj_new_str((const char far *)"test", 4);
    unsigned int h1 = pydos_obj_hash(a);
    unsigned int h2 = pydos_obj_hash(b);
    ASSERT_EQ(h1, h2);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* String conversion                                                   */
/* ------------------------------------------------------------------ */

TEST(to_str_int)
{
    PyDosObj far *obj = pydos_obj_new_int(42L);
    PyDosObj far *s = pydos_obj_to_str(obj);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    ASSERT_STR_EQ(s->v.str.data, "42");
    PYDOS_DECREF(s);
    PYDOS_DECREF(obj);
}

TEST(to_str_none)
{
    PyDosObj far *obj = pydos_obj_new_none();
    PyDosObj far *s = pydos_obj_to_str(obj);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "None");
    PYDOS_DECREF(s);
    PYDOS_DECREF(obj);
}

TEST(to_str_bool_true)
{
    PyDosObj far *obj = pydos_obj_new_bool(1);
    PyDosObj far *s = pydos_obj_to_str(obj);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "True");
    PYDOS_DECREF(s);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Type names                                                          */
/* ------------------------------------------------------------------ */

TEST(type_name_int)
{
    PyDosObj far *obj = pydos_obj_new_int(1L);
    const char far *name = pydos_obj_type_name(obj);
    ASSERT_STR_EQ(name, "int");
    PYDOS_DECREF(obj);
}

TEST(type_name_str)
{
    PyDosObj far *obj = pydos_obj_new_str((const char far *)"a", 1);
    const char far *name = pydos_obj_type_name(obj);
    ASSERT_STR_EQ(name, "str");
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Reference counting                                                  */
/* ------------------------------------------------------------------ */

TEST(refcount_incref)
{
    PyDosObj far *obj = pydos_obj_new_int(999L);
    unsigned int rc;
    ASSERT_EQ(obj->refcount, 1);
    PYDOS_INCREF(obj);
    ASSERT_EQ(obj->refcount, 2);
    rc = obj->refcount;
    /* Decrement back to 1 so we can release cleanly */
    obj->refcount = 1;
    (void)rc;
    PYDOS_DECREF(obj);
}

TEST(refcount_decref)
{
    PyDosObj far *obj = pydos_obj_new_int(999L);
    PYDOS_INCREF(obj);
    ASSERT_EQ(obj->refcount, 2);
    PYDOS_DECREF(obj);
    ASSERT_EQ(obj->refcount, 1);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Helper: short string constructor                                    */
/* ------------------------------------------------------------------ */
static PyDosObj far *mkstr(const char *s, unsigned int len)
{
    return pydos_obj_new_str((const char far *)s, len);
}

/* ------------------------------------------------------------------ */
/* getitem                                                             */
/* ------------------------------------------------------------------ */

TEST(getitem_list_int)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *key;
    PyDosObj far *val;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_append(lst, pydos_obj_new_int(30L));
    key = pydos_obj_new_int(1L);
    val = pydos_obj_getitem(lst, key);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 20L);
    PYDOS_DECREF(val);
    PYDOS_DECREF(key);
    PYDOS_DECREF(lst);
}

TEST(getitem_list_negative)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *key;
    PyDosObj far *val;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_append(lst, pydos_obj_new_int(30L));
    key = pydos_obj_new_int(-1L);
    val = pydos_obj_getitem(lst, key);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 30L);
    PYDOS_DECREF(val);
    PYDOS_DECREF(key);
    PYDOS_DECREF(lst);
}

TEST(getitem_dict_str)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key;
    PyDosObj far *val;
    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(1L));
    key = mkstr("a", 1);
    val = pydos_obj_getitem(d, key);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 1L);
    PYDOS_DECREF(val);
    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

TEST(getitem_dict_missing)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key;
    PyDosObj far *val;
    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(1L));
    key = mkstr("z", 1);
    val = pydos_obj_getitem(d, key);
    ASSERT_NULL(val);
    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* setitem                                                             */
/* ------------------------------------------------------------------ */

TEST(setitem_list)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *key;
    PyDosObj far *newval;
    PyDosObj far *got;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    key = pydos_obj_new_int(0L);
    newval = pydos_obj_new_int(99L);
    pydos_obj_setitem(lst, key, newval);
    got = pydos_obj_getitem(lst, key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 99L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(newval);
    PYDOS_DECREF(key);
    PYDOS_DECREF(lst);
}

TEST(setitem_dict)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key;
    PyDosObj far *got;
    key = mkstr("x", 1);
    pydos_obj_setitem(d, key, pydos_obj_new_int(7L));
    got = pydos_obj_getitem(d, key);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 7L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

/* ------------------------------------------------------------------ */
/* delitem                                                             */
/* ------------------------------------------------------------------ */

TEST(delitem_dict)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *key;
    PyDosObj far *got;
    key = mkstr("a", 1);
    pydos_obj_setitem(d, key, pydos_obj_new_int(10L));
    ASSERT_EQ(pydos_dict_len(d), 1L);
    pydos_obj_delitem(d, key);
    ASSERT_EQ(pydos_dict_len(d), 0L);
    got = pydos_obj_getitem(d, key);
    ASSERT_NULL(got);
    PYDOS_DECREF(key);
    PYDOS_DECREF(d);
}

TEST(delitem_list)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *key;
    PyDosObj far *got;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_append(lst, pydos_obj_new_int(30L));
    key = pydos_obj_new_int(1L);
    pydos_obj_delitem(lst, key);
    ASSERT_EQ(pydos_list_len(lst), 2L);
    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 10L);
    PYDOS_DECREF(got);
    got = pydos_list_get(lst, 1L);
    ASSERT_EQ(got->v.int_val, 30L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(key);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* get_iter                                                            */
/* ------------------------------------------------------------------ */

TEST(get_iter_range)
{
    PyDosObj far *rng;
    PyDosObj far *iter;
    rng = pydos_obj_alloc();
    ASSERT_NOT_NULL(rng);
    rng->type = PYDT_RANGE;
    rng->refcount = 1;
    rng->flags = 0;
    rng->v.range.start = 0;
    rng->v.range.stop = 5;
    rng->v.range.step = 1;
    rng->v.range.current = 0;
    iter = pydos_obj_get_iter(rng);
    ASSERT_NOT_NULL(iter);
    ASSERT_EQ(iter->type, PYDT_RANGE);
    ASSERT_EQ(iter->v.range.current, 0L);
    ASSERT_EQ(iter->v.range.stop, 5L);
    PYDOS_DECREF(iter);
    PYDOS_DECREF(rng);
}

TEST(get_iter_list)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *iter;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    iter = pydos_obj_get_iter(lst);
    ASSERT_NOT_NULL(iter);
    ASSERT_EQ(iter->type, PYDT_GENERATOR);
    ASSERT_EQ(iter->v.gen.pc, 0);
    PYDOS_DECREF(iter);
    PYDOS_DECREF(lst);
}

TEST(get_iter_null)
{
    PyDosObj far *iter = pydos_obj_get_iter((PyDosObj far *)0);
    ASSERT_NULL(iter);
}

/* ------------------------------------------------------------------ */
/* iter_next                                                           */
/* ------------------------------------------------------------------ */

TEST(iter_next_range)
{
    PyDosObj far *rng;
    PyDosObj far *iter;
    PyDosObj far *val;

    rng = pydos_obj_alloc();
    ASSERT_NOT_NULL(rng);
    rng->type = PYDT_RANGE;
    rng->refcount = 1;
    rng->flags = 0;
    rng->v.range.start = 0;
    rng->v.range.stop = 3;
    rng->v.range.step = 1;
    rng->v.range.current = 0;

    iter = pydos_obj_get_iter(rng);
    ASSERT_NOT_NULL(iter);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 0L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 1L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 2L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NULL(val);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(rng);
}

TEST(iter_next_range_empty)
{
    PyDosObj far *rng;
    PyDosObj far *iter;
    PyDosObj far *val;

    rng = pydos_obj_alloc();
    ASSERT_NOT_NULL(rng);
    rng->type = PYDT_RANGE;
    rng->refcount = 1;
    rng->flags = 0;
    rng->v.range.start = 5;
    rng->v.range.stop = 5;
    rng->v.range.step = 1;
    rng->v.range.current = 5;

    iter = pydos_obj_get_iter(rng);
    ASSERT_NOT_NULL(iter);

    val = pydos_obj_iter_next(iter);
    ASSERT_NULL(val);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(rng);
}

TEST(iter_next_list)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *iter;
    PyDosObj far *val;

    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_append(lst, pydos_obj_new_int(30L));

    iter = pydos_obj_get_iter(lst);
    ASSERT_NOT_NULL(iter);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 10L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 20L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 30L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NULL(val);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* call_method                                                         */
/* ------------------------------------------------------------------ */

TEST(call_method_list_append)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[2];
    PyDosObj far *ret;

    argv[0] = lst;
    argv[1] = pydos_obj_new_int(42L);
    ret = pydos_obj_call_method((const char far *)"append", 2, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);
    ASSERT_EQ(pydos_list_len(lst), 1L);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(lst);
}

TEST(call_method_list_pop)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[1];
    PyDosObj far *ret;

    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    pydos_list_append(lst, pydos_obj_new_int(3L));

    argv[0] = lst;
    ret = pydos_obj_call_method((const char far *)"pop", 1, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->v.int_val, 3L);
    PYDOS_DECREF(ret);
    ASSERT_EQ(pydos_list_len(lst), 2L);

    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* obj_add: pydos_obj_add dispatches by type                           */
/* ------------------------------------------------------------------ */

TEST(obj_add_ints)
{
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *r = pydos_obj_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 30L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(obj_add_strs)
{
    PyDosObj far *a = mkstr("hello", 5);
    PyDosObj far *b = mkstr(" world", 6);
    PyDosObj far *r = pydos_obj_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_STR_EQ(r->v.str.data, "hello world");
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* obj_sub: pydos_obj_sub dispatches by type                           */
/* ------------------------------------------------------------------ */

TEST(obj_sub_ints)
{
    PyDosObj far *a = pydos_obj_new_int(50L);
    PyDosObj far *b = pydos_obj_new_int(30L);
    PyDosObj far *r = pydos_obj_sub(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 20L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* obj_mul: pydos_obj_mul dispatches by type                           */
/* ------------------------------------------------------------------ */

TEST(obj_mul_ints)
{
    PyDosObj far *a = pydos_obj_new_int(6L);
    PyDosObj far *b = pydos_obj_new_int(7L);
    PyDosObj far *r = pydos_obj_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 42L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* call_method: list.insert                                            */
/* ------------------------------------------------------------------ */

TEST(call_method_list_insert)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[3];
    PyDosObj far *ret;
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(3L));

    argv[0] = lst;
    argv[1] = pydos_obj_new_int(1L);
    argv[2] = pydos_obj_new_int(2L);
    ret = pydos_obj_call_method((const char far *)"insert", 3, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    ASSERT_EQ(pydos_list_len(lst), 3L);
    got = pydos_list_get(lst, 1L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 2L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(argv[2]);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* call_method: list.reverse                                           */
/* ------------------------------------------------------------------ */

TEST(call_method_list_reverse)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[1];
    PyDosObj far *ret;
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    pydos_list_append(lst, pydos_obj_new_int(3L));

    argv[0] = lst;
    ret = pydos_obj_call_method((const char far *)"reverse", 1, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    got = pydos_list_get(lst, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* call_method: dict.values                                            */
/* ------------------------------------------------------------------ */

/* ------------------------------------------------------------------ */
/* Float arithmetic                                                    */
/* ------------------------------------------------------------------ */

TEST(obj_add_floats)
{
    PyDosObj far *a = pydos_obj_new_float(1.5);
    PyDosObj far *b = pydos_obj_new_float(2.5);
    PyDosObj far *r = pydos_obj_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_FLOAT);
    /* 1.5 + 2.5 = 4.0 */
    ASSERT_TRUE(r->v.float_val > 3.9 && r->v.float_val < 4.1);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(obj_sub_floats)
{
    PyDosObj far *a = pydos_obj_new_float(5.0);
    PyDosObj far *b = pydos_obj_new_float(1.5);
    PyDosObj far *r = pydos_obj_sub(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_FLOAT);
    /* 5.0 - 1.5 = 3.5 */
    ASSERT_TRUE(r->v.float_val > 3.4 && r->v.float_val < 3.6);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(obj_mul_floats)
{
    PyDosObj far *a = pydos_obj_new_float(2.5);
    PyDosObj far *b = pydos_obj_new_float(4.0);
    PyDosObj far *r = pydos_obj_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_FLOAT);
    /* 2.5 * 4.0 = 10.0 */
    ASSERT_TRUE(r->v.float_val > 9.9 && r->v.float_val < 10.1);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(obj_add_int_float)
{
    PyDosObj far *a = pydos_obj_new_int(3L);
    PyDosObj far *b = pydos_obj_new_float(4.5);
    PyDosObj far *r = pydos_obj_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_FLOAT);
    /* 3 + 4.5 = 7.5 */
    ASSERT_TRUE(r->v.float_val > 7.4 && r->v.float_val < 7.6);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(obj_add_float_int)
{
    PyDosObj far *a = pydos_obj_new_float(4.5);
    PyDosObj far *b = pydos_obj_new_int(3L);
    PyDosObj far *r = pydos_obj_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_FLOAT);
    /* 4.5 + 3 = 7.5 */
    ASSERT_TRUE(r->v.float_val > 7.4 && r->v.float_val < 7.6);
    PYDOS_DECREF(r);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* Float truthiness                                                    */
/* ------------------------------------------------------------------ */

TEST(is_truthy_float_nonzero)
{
    PyDosObj far *obj = pydos_obj_new_float(3.14);
    ASSERT_TRUE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

TEST(is_truthy_float_zero)
{
    PyDosObj far *obj = pydos_obj_new_float(0.0);
    ASSERT_FALSE(pydos_obj_is_truthy(obj));
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Float to_str                                                        */
/* ------------------------------------------------------------------ */

TEST(to_str_float)
{
    PyDosObj far *obj = pydos_obj_new_float(3.14);
    PyDosObj far *s = pydos_obj_to_str(obj);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    /* Should start with "3.14" */
    ASSERT_TRUE(s->v.str.len >= 4);
    ASSERT_TRUE(s->v.str.data[0] == '3');
    ASSERT_TRUE(s->v.str.data[1] == '.');
    PYDOS_DECREF(s);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_compare                                                   */
/* ------------------------------------------------------------------ */

TEST(compare_ints_lt)
{
    PyDosObj far *a = pydos_obj_new_int(3L);
    PyDosObj far *b = pydos_obj_new_int(7L);
    ASSERT_TRUE(pydos_obj_compare(a, b) < 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_ints_gt)
{
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(2L);
    ASSERT_TRUE(pydos_obj_compare(a, b) > 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_ints_eq)
{
    PyDosObj far *a = pydos_obj_new_int(5L);
    PyDosObj far *b = pydos_obj_new_int(5L);
    ASSERT_EQ(pydos_obj_compare(a, b), 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_floats)
{
    PyDosObj far *a = pydos_obj_new_float(3.14);
    PyDosObj far *b = pydos_obj_new_float(2.71);
    ASSERT_TRUE(pydos_obj_compare(a, b) > 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_int_float)
{
    PyDosObj far *a = pydos_obj_new_int(3L);
    PyDosObj far *b = pydos_obj_new_float(3.5);
    ASSERT_TRUE(pydos_obj_compare(a, b) < 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_strs)
{
    PyDosObj far *a = mkstr("apple", 5);
    PyDosObj far *b = mkstr("banana", 6);
    ASSERT_TRUE(pydos_obj_compare(a, b) < 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_contains                                                  */
/* ------------------------------------------------------------------ */

TEST(contains_list_found)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *item;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    item = pydos_obj_new_int(20L);
    ASSERT_EQ(pydos_obj_contains(lst, item), 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(lst);
}

TEST(contains_list_missing)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *item;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    item = pydos_obj_new_int(99L);
    ASSERT_EQ(pydos_obj_contains(lst, item), 0);
    PYDOS_DECREF(item);
    PYDOS_DECREF(lst);
}

TEST(contains_dict_found)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *item;
    pydos_dict_set(d, mkstr("a", 1), pydos_obj_new_int(1L));
    item = mkstr("a", 1);
    ASSERT_EQ(pydos_obj_contains(d, item), 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(d);
}

TEST(contains_str_found)
{
    PyDosObj far *s = mkstr("hello world", 11);
    PyDosObj far *item = mkstr("world", 5);
    ASSERT_EQ(pydos_obj_contains(s, item), 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(contains_str_missing)
{
    PyDosObj far *s = mkstr("hello", 5);
    PyDosObj far *item = mkstr("xyz", 3);
    ASSERT_EQ(pydos_obj_contains(s, item), 0);
    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* Tuple operations                                                    */
/* ------------------------------------------------------------------ */

/* Helper: create a tuple with 3 int elements */
static PyDosObj far *make_tuple3(long a, long b, long c)
{
    PyDosObj far *t = pydos_obj_alloc();
    t->type = PYDT_TUPLE;
    t->refcount = 1;
    t->flags = 0;
    t->v.tuple.len = 3;
    t->v.tuple.items = (PyDosObj far * far *)pydos_far_alloc(
        3 * sizeof(PyDosObj far *));
    t->v.tuple.items[0] = pydos_obj_new_int(a);
    t->v.tuple.items[1] = pydos_obj_new_int(b);
    t->v.tuple.items[2] = pydos_obj_new_int(c);
    return t;
}

TEST(tuple_truthiness)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    ASSERT_TRUE(pydos_obj_is_truthy(t));
    PYDOS_DECREF(t);
}

TEST(tuple_truthiness_empty)
{
    PyDosObj far *t = pydos_obj_alloc();
    t->type = PYDT_TUPLE;
    t->refcount = 1;
    t->flags = 0;
    t->v.tuple.len = 0;
    t->v.tuple.items = (PyDosObj far * far *)0;
    ASSERT_FALSE(pydos_obj_is_truthy(t));
    PYDOS_DECREF(t);
}

TEST(tuple_getitem)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    PyDosObj far *key = pydos_obj_new_int(1L);
    PyDosObj far *val = pydos_obj_getitem(t, key);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 20L);
    PYDOS_DECREF(val);
    PYDOS_DECREF(key);
    PYDOS_DECREF(t);
}

TEST(tuple_getitem_negative)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    PyDosObj far *key = pydos_obj_new_int(-1L);
    PyDosObj far *val = pydos_obj_getitem(t, key);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 30L);
    PYDOS_DECREF(val);
    PYDOS_DECREF(key);
    PYDOS_DECREF(t);
}

TEST(tuple_contains_found)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    PyDosObj far *item = pydos_obj_new_int(20L);
    ASSERT_EQ(pydos_obj_contains(t, item), 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(t);
}

TEST(tuple_contains_missing)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    PyDosObj far *item = pydos_obj_new_int(99L);
    ASSERT_EQ(pydos_obj_contains(t, item), 0);
    PYDOS_DECREF(item);
    PYDOS_DECREF(t);
}

TEST(tuple_to_str)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    PyDosObj far *s = pydos_obj_to_str(t);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    ASSERT_STR_EQ(s->v.str.data, "(10, 20, 30)");
    PYDOS_DECREF(s);
    PYDOS_DECREF(t);
}

TEST(tuple_iter)
{
    PyDosObj far *t = make_tuple3(10L, 20L, 30L);
    PyDosObj far *iter = pydos_obj_get_iter(t);
    PyDosObj far *val;
    ASSERT_NOT_NULL(iter);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 10L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 20L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->v.int_val, 30L);
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NULL(val);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(t);
}

/* ------------------------------------------------------------------ */
/* Set operations                                                      */
/* ------------------------------------------------------------------ */

/* Helper: create a set with int keys using dict layout */
static PyDosObj far *make_set3(long a, long b, long c)
{
    PyDosObj far *s = pydos_dict_new(8);
    PyDosObj far *none1 = pydos_obj_new_none();
    PyDosObj far *none2 = pydos_obj_new_none();
    PyDosObj far *none3 = pydos_obj_new_none();
    pydos_dict_set(s, pydos_obj_new_int(a), none1);
    pydos_dict_set(s, pydos_obj_new_int(b), none2);
    pydos_dict_set(s, pydos_obj_new_int(c), none3);
    s->type = PYDT_SET;
    PYDOS_DECREF(none1);
    PYDOS_DECREF(none2);
    PYDOS_DECREF(none3);
    return s;
}

TEST(set_truthiness)
{
    PyDosObj far *s = make_set3(10L, 20L, 30L);
    ASSERT_TRUE(pydos_obj_is_truthy(s));
    PYDOS_DECREF(s);
}

TEST(set_contains_found)
{
    PyDosObj far *s = make_set3(10L, 20L, 30L);
    PyDosObj far *item = pydos_obj_new_int(20L);
    ASSERT_EQ(pydos_obj_contains(s, item), 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(set_contains_missing)
{
    PyDosObj far *s = make_set3(10L, 20L, 30L);
    PyDosObj far *item = pydos_obj_new_int(99L);
    ASSERT_EQ(pydos_obj_contains(s, item), 0);
    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(set_to_str)
{
    /* Set to_str should produce something like {10, 20, 30}
     * but order is hash-dependent; just verify it starts with '{' */
    PyDosObj far *s = make_set3(10L, 20L, 30L);
    PyDosObj far *str = pydos_obj_to_str(s);
    ASSERT_NOT_NULL(str);
    ASSERT_EQ(str->type, PYDT_STR);
    ASSERT_TRUE(str->v.str.data[0] == '{');
    ASSERT_TRUE(str->v.str.data[str->v.str.len - 1] == '}');
    PYDOS_DECREF(str);
    PYDOS_DECREF(s);
}

TEST(set_iter)
{
    PyDosObj far *s = make_set3(10L, 20L, 30L);
    PyDosObj far *iter = pydos_obj_get_iter(s);
    PyDosObj far *val;
    long sum = 0;
    int count = 0;
    ASSERT_NOT_NULL(iter);

    while (1) {
        val = pydos_obj_iter_next(iter);
        if (val == (PyDosObj far *)0) break;
        ASSERT_EQ(val->type, PYDT_INT);
        sum += val->v.int_val;
        count++;
        PYDOS_DECREF(val);
    }
    ASSERT_EQ(count, 3);
    ASSERT_EQ(sum, 60L);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* Dict/string iteration                                               */
/* ------------------------------------------------------------------ */

TEST(iter_dict_keys)
{
    /* Iterating a dict yields its keys */
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *iter;
    PyDosObj far *val;
    int count = 0;
    pydos_dict_set(d, mkstr("x", 1), pydos_obj_new_int(1L));
    pydos_dict_set(d, mkstr("y", 1), pydos_obj_new_int(2L));

    iter = pydos_obj_get_iter(d);
    ASSERT_NOT_NULL(iter);

    for (;;) {
        val = pydos_obj_iter_next(iter);
        if (val == (PyDosObj far *)0) break;
        ASSERT_EQ(val->type, PYDT_STR);
        count++;
        PYDOS_DECREF(val);
    }
    ASSERT_EQ(count, 2);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(d);
}

TEST(iter_string_chars)
{
    /* Iterating a string yields single-char strings */
    PyDosObj far *s = mkstr("hi", 2);
    PyDosObj far *iter;
    PyDosObj far *val;

    iter = pydos_obj_get_iter(s);
    ASSERT_NOT_NULL(iter);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->type, PYDT_STR);
    ASSERT_EQ(val->v.str.len, 1);
    ASSERT_STR_EQ(val->v.str.data, "h");
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(val);
    ASSERT_STR_EQ(val->v.str.data, "i");
    PYDOS_DECREF(val);

    val = pydos_obj_iter_next(iter);
    ASSERT_NULL(val);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* del_attr tests                                                      */
/* ------------------------------------------------------------------ */

TEST(del_attr_basic)
{
    /* Set an attribute, verify it exists, delete it, verify it's gone */
    PyDosObj far *obj = pydos_obj_alloc();
    PyDosObj far *val = pydos_obj_new_int(42L);
    PyDosObj far *got;

    pydos_obj_set_attr(obj, "x", val);
    got = pydos_obj_get_attr(obj, "x");
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 42L);
    PYDOS_DECREF(got);

    pydos_obj_del_attr(obj, "x");
    got = pydos_obj_get_attr(obj, "x");
    /* get_attr returns None (not NULL) when attr is missing */
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->type, PYDT_NONE);
    PYDOS_DECREF(got);

    PYDOS_DECREF(val);
    PYDOS_DECREF(obj);
}

TEST(del_attr_nonexistent)
{
    /* Deleting a non-existent attribute should not crash */
    PyDosObj far *obj = pydos_obj_alloc();
    PyDosObj far *val = pydos_obj_new_int(1L);

    /* Promote to INSTANCE by setting an attribute */
    pydos_obj_set_attr(obj, "y", val);

    /* Delete non-existent attribute — should be a no-op */
    pydos_obj_del_attr(obj, "nonexistent");

    /* Original attribute should still exist */
    {
        PyDosObj far *got = pydos_obj_get_attr(obj, "y");
        ASSERT_NOT_NULL(got);
        PYDOS_DECREF(got);
    }

    PYDOS_DECREF(val);
    PYDOS_DECREF(obj);
}

TEST(del_attr_null_obj)
{
    /* Deleting an attribute on NULL should not crash */
    pydos_obj_del_attr((PyDosObj far *)0, "x");
    ASSERT_TRUE(1);
}

TEST(del_attr_non_instance)
{
    /* Deleting an attribute on an int should be a no-op, no crash */
    PyDosObj far *obj = pydos_obj_new_int(99L);
    pydos_obj_del_attr(obj, "x");
    ASSERT_EQ(obj->v.int_val, 99L);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_obj_tests(void)
{
    SUITE("pdos_obj");

    RUN(alloc_returns_nonnull);
    RUN(new_none);
    RUN(new_bool_true);
    RUN(new_bool_false);
    RUN(new_int);
    RUN(new_int_negative);
    RUN(new_int_zero);
    RUN(new_float);
    RUN(new_str);
    RUN(new_str_empty);
    RUN(is_truthy_none);
    RUN(is_truthy_bool_true);
    RUN(is_truthy_bool_false);
    RUN(is_truthy_int_nonzero);
    RUN(is_truthy_int_zero);
    RUN(is_truthy_str_nonempty);
    RUN(is_truthy_str_empty);
    RUN(equal_ints);
    RUN(equal_ints_diff);
    RUN(equal_strs);
    RUN(equal_none);
    RUN(hash_int);
    RUN(hash_str);
    RUN(to_str_int);
    RUN(to_str_none);
    RUN(to_str_bool_true);
    RUN(type_name_int);
    RUN(type_name_str);
    RUN(refcount_incref);
    RUN(refcount_decref);
    RUN(getitem_list_int);
    RUN(getitem_list_negative);
    RUN(getitem_dict_str);
    RUN(getitem_dict_missing);
    RUN(setitem_list);
    RUN(setitem_dict);
    RUN(delitem_dict);
    RUN(delitem_list);
    RUN(get_iter_range);
    RUN(get_iter_list);
    RUN(get_iter_null);
    RUN(iter_next_range);
    RUN(iter_next_range_empty);
    RUN(iter_next_list);
    RUN(call_method_list_append);
    RUN(call_method_list_pop);
    RUN(obj_add_ints);
    RUN(obj_add_strs);
    RUN(obj_sub_ints);
    RUN(obj_mul_ints);
    RUN(call_method_list_insert);
    RUN(call_method_list_reverse);
    RUN(obj_add_floats);
    RUN(obj_sub_floats);
    RUN(obj_mul_floats);
    RUN(obj_add_int_float);
    RUN(obj_add_float_int);
    RUN(is_truthy_float_nonzero);
    RUN(is_truthy_float_zero);
    RUN(to_str_float);
    RUN(compare_ints_lt);
    RUN(compare_ints_gt);
    RUN(compare_ints_eq);
    RUN(compare_floats);
    RUN(compare_int_float);
    RUN(compare_strs);
    RUN(contains_list_found);
    RUN(contains_list_missing);
    RUN(contains_dict_found);
    RUN(contains_str_found);
    RUN(contains_str_missing);
    RUN(tuple_truthiness);
    RUN(tuple_truthiness_empty);
    RUN(tuple_getitem);
    RUN(tuple_getitem_negative);
    RUN(tuple_contains_found);
    RUN(tuple_contains_missing);
    RUN(tuple_to_str);
    RUN(tuple_iter);
    RUN(set_truthiness);
    RUN(set_contains_found);
    RUN(set_contains_missing);
    RUN(set_to_str);
    RUN(set_iter);
    RUN(iter_dict_keys);
    RUN(iter_string_chars);
    RUN(del_attr_basic);
    RUN(del_attr_nonexistent);
    RUN(del_attr_null_obj);
    RUN(del_attr_non_instance);
}
