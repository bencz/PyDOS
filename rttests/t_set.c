/*
 * t_set.c - Unit tests for set methods
 *
 * Tests set_add, set_remove, set_discard, set_clear, set_pop via
 * call_method (@internal_implementation C wrappers), plus direct
 * pydos_set_* wrapper API tests.
 *
 * PIR-backed methods (copy, union, intersection, difference,
 * symmetric_difference, issubset, issuperset, isdisjoint, update)
 * are tested via integration tests in tests/.
 *
 * Sets are PYDT_SET backed by dict (hash table).
 */

#include "testfw.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_dic.h"

/* ------------------------------------------------------------------ */
/* Helper: create an empty set                                         */
/* ------------------------------------------------------------------ */
static PyDosObj far *make_set(void)
{
    PyDosObj far *s = pydos_dict_new(8);
    s->type = PYDT_SET;
    return s;
}

/* ------------------------------------------------------------------ */
/* Helper: add an integer to a set                                     */
/* ------------------------------------------------------------------ */
static void set_add_int(PyDosObj far *s, long val)
{
    PyDosObj far *key = pydos_obj_new_int(val);
    PyDosObj far *none = pydos_obj_new_none();
    pydos_dict_set(s, key, none);
    PYDOS_DECREF(key);
    PYDOS_DECREF(none);
}

/* ------------------------------------------------------------------ */
/* set.add                                                             */
/* ------------------------------------------------------------------ */

TEST(set_add_item)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[2];
    PyDosObj far *ret;
    PyDosObj far *chk;

    argv[0] = s;
    argv[1] = pydos_obj_new_int(42L);
    ret = pydos_obj_call_method((const char far *)"add", 2, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->type, PYDT_NONE);
    PYDOS_DECREF(ret);

    /* Verify item is in the set */
    chk = pydos_obj_new_int(42L);
    ASSERT_EQ(pydos_dict_contains(s, chk), 1);
    PYDOS_DECREF(chk);

    ASSERT_EQ(pydos_dict_len(s), 1L);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(s);
}

TEST(set_add_duplicate)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[2];
    PyDosObj far *ret;

    set_add_int(s, 10L);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    /* Add same value again */
    argv[0] = s;
    argv[1] = pydos_obj_new_int(10L);
    ret = pydos_obj_call_method((const char far *)"add", 2, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    /* Size should still be 1 */
    ASSERT_EQ(pydos_dict_len(s), 1L);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* set.remove                                                          */
/* ------------------------------------------------------------------ */

TEST(set_remove_existing)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[2];
    PyDosObj far *ret;
    PyDosObj far *chk;

    set_add_int(s, 10L);
    set_add_int(s, 20L);
    ASSERT_EQ(pydos_dict_len(s), 2L);

    argv[0] = s;
    argv[1] = pydos_obj_new_int(10L);
    ret = pydos_obj_call_method((const char far *)"remove", 2, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->type, PYDT_NONE);
    PYDOS_DECREF(ret);

    ASSERT_EQ(pydos_dict_len(s), 1L);

    /* Verify 10 is gone */
    chk = pydos_obj_new_int(10L);
    ASSERT_EQ(pydos_dict_contains(s, chk), 0);
    PYDOS_DECREF(chk);

    /* Verify 20 is still there */
    chk = pydos_obj_new_int(20L);
    ASSERT_EQ(pydos_dict_contains(s, chk), 1);
    PYDOS_DECREF(chk);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* set.discard                                                         */
/* ------------------------------------------------------------------ */

TEST(set_discard_existing)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[2];
    PyDosObj far *ret;
    PyDosObj far *chk;

    set_add_int(s, 10L);
    set_add_int(s, 20L);

    argv[0] = s;
    argv[1] = pydos_obj_new_int(10L);
    ret = pydos_obj_call_method((const char far *)"discard", 2, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->type, PYDT_NONE);
    PYDOS_DECREF(ret);

    ASSERT_EQ(pydos_dict_len(s), 1L);
    chk = pydos_obj_new_int(10L);
    ASSERT_EQ(pydos_dict_contains(s, chk), 0);
    PYDOS_DECREF(chk);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(s);
}

TEST(set_discard_nonexisting)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[2];
    PyDosObj far *ret;

    set_add_int(s, 10L);

    /* Discard a value that is not in the set -- should not raise */
    argv[0] = s;
    argv[1] = pydos_obj_new_int(99L);
    ret = pydos_obj_call_method((const char far *)"discard", 2, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->type, PYDT_NONE);
    PYDOS_DECREF(ret);

    /* Size unchanged */
    ASSERT_EQ(pydos_dict_len(s), 1L);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* set.clear                                                           */
/* ------------------------------------------------------------------ */

TEST(set_clear_items)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[1];
    PyDosObj far *ret;

    set_add_int(s, 1L);
    set_add_int(s, 2L);
    set_add_int(s, 3L);
    ASSERT_EQ(pydos_dict_len(s), 3L);

    argv[0] = s;
    ret = pydos_obj_call_method((const char far *)"clear", 1, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->type, PYDT_NONE);
    PYDOS_DECREF(ret);

    ASSERT_EQ(pydos_dict_len(s), 0L);

    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* set.pop                                                             */
/* ------------------------------------------------------------------ */

TEST(set_pop_item)
{
    PyDosObj far *s = make_set();
    PyDosObj far *argv[1];
    PyDosObj far *ret;
    long val;

    set_add_int(s, 10L);
    set_add_int(s, 20L);
    set_add_int(s, 30L);
    ASSERT_EQ(pydos_dict_len(s), 3L);

    argv[0] = s;
    ret = pydos_obj_call_method((const char far *)"pop", 1, argv);
    ASSERT_NOT_NULL(ret);
    ASSERT_EQ(ret->type, PYDT_INT);

    /* The popped value must be one of {10, 20, 30} */
    val = ret->v.int_val;
    ASSERT_TRUE(val == 10L || val == 20L || val == 30L);
    PYDOS_DECREF(ret);

    /* Size should have decreased by one */
    ASSERT_EQ(pydos_dict_len(s), 2L);

    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* Direct wrapper tests (pydos_set_* functions)                        */
/* ------------------------------------------------------------------ */

TEST(set_add_direct)
{
    PyDosObj far *s = make_set();
    PyDosObj far *item = pydos_obj_new_int(42L);
    PyDosObj far *chk;

    pydos_set_add(s, item);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    chk = pydos_obj_new_int(42L);
    ASSERT_EQ(pydos_dict_contains(s, chk), 1);
    PYDOS_DECREF(chk);

    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(set_add_direct_dup)
{
    PyDosObj far *s = make_set();
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(10L);

    pydos_set_add(s, a);
    pydos_set_add(s, b);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    PYDOS_DECREF(b);
    PYDOS_DECREF(a);
    PYDOS_DECREF(s);
}

TEST(set_remove_direct)
{
    PyDosObj far *s = make_set();
    PyDosObj far *item = pydos_obj_new_int(10L);
    PyDosObj far *chk;

    set_add_int(s, 10L);
    set_add_int(s, 20L);
    ASSERT_EQ(pydos_dict_len(s), 2L);

    pydos_set_remove(s, item);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    chk = pydos_obj_new_int(10L);
    ASSERT_EQ(pydos_dict_contains(s, chk), 0);
    PYDOS_DECREF(chk);

    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(set_discard_direct)
{
    PyDosObj far *s = make_set();
    PyDosObj far *item = pydos_obj_new_int(10L);

    set_add_int(s, 10L);
    set_add_int(s, 20L);
    ASSERT_EQ(pydos_dict_len(s), 2L);

    pydos_set_discard(s, item);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(set_discard_direct_missing)
{
    PyDosObj far *s = make_set();
    PyDosObj far *item = pydos_obj_new_int(99L);

    set_add_int(s, 10L);
    pydos_set_discard(s, item);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

TEST(set_clear_direct)
{
    PyDosObj far *s = make_set();

    set_add_int(s, 1L);
    set_add_int(s, 2L);
    set_add_int(s, 3L);
    ASSERT_EQ(pydos_dict_len(s), 3L);

    pydos_set_clear(s);
    ASSERT_EQ(pydos_dict_len(s), 0L);

    PYDOS_DECREF(s);
}

TEST(set_pop_direct)
{
    PyDosObj far *s = make_set();
    PyDosObj far *item;

    set_add_int(s, 10L);
    ASSERT_EQ(pydos_dict_len(s), 1L);

    item = pydos_set_pop(s);
    ASSERT_NOT_NULL(item);
    ASSERT_EQ(item->type, PYDT_INT);
    ASSERT_EQ(item->v.int_val, 10L);
    ASSERT_EQ(pydos_dict_len(s), 0L);

    PYDOS_DECREF(item);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_set_tests(void)
{
    SUITE("pdos_set");

    RUN(set_add_item);
    RUN(set_add_duplicate);
    RUN(set_remove_existing);
    RUN(set_discard_existing);
    RUN(set_discard_nonexisting);
    RUN(set_clear_items);
    RUN(set_pop_item);
    RUN(set_add_direct);
    RUN(set_add_direct_dup);
    RUN(set_remove_direct);
    RUN(set_discard_direct);
    RUN(set_discard_direct_missing);
    RUN(set_clear_direct);
    RUN(set_pop_direct);
}
