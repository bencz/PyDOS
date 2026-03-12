/*
 * t_fzs.c - Unit tests for frozenset type (pdos_fzs module)
 *
 * Tests frozenset creation, contains, set operations, hash, equality,
 * and the builtin constructor.
 */

#include "testfw.h"
#include "../runtime/pdos_fzs.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_mem.h"

/* ------------------------------------------------------------------ */
/* fzs_empty: empty frozenset has len 0                                */
/* ------------------------------------------------------------------ */
TEST(fzs_empty)
{
    PyDosObj far *fs;
    fs = pydos_frozenset_new((PyDosObj far * far *)0, 0);
    ASSERT_NOT_NULL(fs);
    ASSERT_EQ(fs->type, PYDT_FROZENSET);
    ASSERT_EQ(pydos_frozenset_len(fs), 0);
    ASSERT_EQ(fs->v.frozenset.hash, 0);
    PYDOS_DECREF(fs);
}

/* ------------------------------------------------------------------ */
/* fzs_single: frozenset with one element                              */
/* ------------------------------------------------------------------ */
TEST(fzs_single)
{
    PyDosObj far *items[1];
    PyDosObj far *fs;

    items[0] = pydos_obj_new_int(42);
    fs = pydos_frozenset_new(items, 1);
    ASSERT_NOT_NULL(fs);
    ASSERT_EQ(pydos_frozenset_len(fs), 1);
    ASSERT_TRUE(pydos_frozenset_contains(fs, items[0]));
    PYDOS_DECREF(fs);
    PYDOS_DECREF(items[0]);
}

/* ------------------------------------------------------------------ */
/* fzs_dedup: duplicate elements removed                               */
/* ------------------------------------------------------------------ */
TEST(fzs_dedup)
{
    PyDosObj far *items[3];
    PyDosObj far *fs;

    items[0] = pydos_obj_new_int(1);
    items[1] = pydos_obj_new_int(1);
    items[2] = pydos_obj_new_int(2);
    fs = pydos_frozenset_new(items, 3);
    ASSERT_NOT_NULL(fs);
    ASSERT_EQ(pydos_frozenset_len(fs), 2);
    PYDOS_DECREF(fs);
    PYDOS_DECREF(items[0]);
    PYDOS_DECREF(items[1]);
    PYDOS_DECREF(items[2]);
}

/* ------------------------------------------------------------------ */
/* fzs_contains_yes: element found                                     */
/* ------------------------------------------------------------------ */
TEST(fzs_contains_yes)
{
    PyDosObj far *items[3];
    PyDosObj far *fs;
    PyDosObj far *needle;

    items[0] = pydos_obj_new_int(10);
    items[1] = pydos_obj_new_int(20);
    items[2] = pydos_obj_new_int(30);
    fs = pydos_frozenset_new(items, 3);

    needle = pydos_obj_new_int(20);
    ASSERT_TRUE(pydos_frozenset_contains(fs, needle));
    PYDOS_DECREF(needle);
    PYDOS_DECREF(fs);
    PYDOS_DECREF(items[0]);
    PYDOS_DECREF(items[1]);
    PYDOS_DECREF(items[2]);
}

/* ------------------------------------------------------------------ */
/* fzs_contains_no: element not found                                  */
/* ------------------------------------------------------------------ */
TEST(fzs_contains_no)
{
    PyDosObj far *items[2];
    PyDosObj far *fs;
    PyDosObj far *needle;

    items[0] = pydos_obj_new_int(10);
    items[1] = pydos_obj_new_int(20);
    fs = pydos_frozenset_new(items, 2);

    needle = pydos_obj_new_int(99);
    ASSERT_FALSE(pydos_frozenset_contains(fs, needle));
    PYDOS_DECREF(needle);
    PYDOS_DECREF(fs);
    PYDOS_DECREF(items[0]);
    PYDOS_DECREF(items[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_hash_cached: hash is precomputed and consistent                 */
/* ------------------------------------------------------------------ */
TEST(fzs_hash_cached)
{
    PyDosObj far *items[2];
    PyDosObj far *fs;
    long h;

    items[0] = pydos_obj_new_int(1);
    items[1] = pydos_obj_new_int(2);
    fs = pydos_frozenset_new(items, 2);
    ASSERT_NOT_NULL(fs);
    h = pydos_obj_hash(fs);
    ASSERT_EQ(h, (long)fs->v.frozenset.hash);
    PYDOS_DECREF(fs);
    PYDOS_DECREF(items[0]);
    PYDOS_DECREF(items[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_equal: two frozensets with same elements are equal              */
/* ------------------------------------------------------------------ */
TEST(fzs_equal)
{
    PyDosObj far *items1[2];
    PyDosObj far *items2[2];
    PyDosObj far *fs1;
    PyDosObj far *fs2;

    items1[0] = pydos_obj_new_int(3);
    items1[1] = pydos_obj_new_int(5);
    items2[0] = pydos_obj_new_int(5);
    items2[1] = pydos_obj_new_int(3);
    fs1 = pydos_frozenset_new(items1, 2);
    fs2 = pydos_frozenset_new(items2, 2);
    ASSERT_TRUE(pydos_obj_equal(fs1, fs2));
    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items2[0]);
    PYDOS_DECREF(items2[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_not_equal: different elements                                   */
/* ------------------------------------------------------------------ */
TEST(fzs_not_equal)
{
    PyDosObj far *items1[2];
    PyDosObj far *items2[2];
    PyDosObj far *fs1;
    PyDosObj far *fs2;

    items1[0] = pydos_obj_new_int(1);
    items1[1] = pydos_obj_new_int(2);
    items2[0] = pydos_obj_new_int(1);
    items2[1] = pydos_obj_new_int(3);
    fs1 = pydos_frozenset_new(items1, 2);
    fs2 = pydos_frozenset_new(items2, 2);
    ASSERT_FALSE(pydos_obj_equal(fs1, fs2));
    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items2[0]);
    PYDOS_DECREF(items2[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_union: union of two frozensets                                  */
/* ------------------------------------------------------------------ */
TEST(fzs_union)
{
    PyDosObj far *items1[2];
    PyDosObj far *items2[2];
    PyDosObj far *fs1;
    PyDosObj far *fs2;
    PyDosObj far *result;
    PyDosObj far *needle;

    items1[0] = pydos_obj_new_int(1);
    items1[1] = pydos_obj_new_int(2);
    items2[0] = pydos_obj_new_int(2);
    items2[1] = pydos_obj_new_int(3);
    fs1 = pydos_frozenset_new(items1, 2);
    fs2 = pydos_frozenset_new(items2, 2);

    result = pydos_frozenset_union(fs1, fs2);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(pydos_frozenset_len(result), 3);

    needle = pydos_obj_new_int(3);
    ASSERT_TRUE(pydos_frozenset_contains(result, needle));
    PYDOS_DECREF(needle);

    PYDOS_DECREF(result);
    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items2[0]);
    PYDOS_DECREF(items2[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_intersection: intersection of two frozensets                    */
/* ------------------------------------------------------------------ */
TEST(fzs_intersection)
{
    PyDosObj far *items1[3];
    PyDosObj far *items2[2];
    PyDosObj far *fs1;
    PyDosObj far *fs2;
    PyDosObj far *result;

    items1[0] = pydos_obj_new_int(1);
    items1[1] = pydos_obj_new_int(2);
    items1[2] = pydos_obj_new_int(3);
    items2[0] = pydos_obj_new_int(2);
    items2[1] = pydos_obj_new_int(3);
    fs1 = pydos_frozenset_new(items1, 3);
    fs2 = pydos_frozenset_new(items2, 2);

    result = pydos_frozenset_intersection(fs1, fs2);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(pydos_frozenset_len(result), 2);

    PYDOS_DECREF(result);
    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items1[2]);
    PYDOS_DECREF(items2[0]);
    PYDOS_DECREF(items2[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_difference: a - b                                               */
/* ------------------------------------------------------------------ */
TEST(fzs_difference)
{
    PyDosObj far *items1[3];
    PyDosObj far *items2[1];
    PyDosObj far *fs1;
    PyDosObj far *fs2;
    PyDosObj far *result;

    items1[0] = pydos_obj_new_int(1);
    items1[1] = pydos_obj_new_int(2);
    items1[2] = pydos_obj_new_int(3);
    items2[0] = pydos_obj_new_int(2);
    fs1 = pydos_frozenset_new(items1, 3);
    fs2 = pydos_frozenset_new(items2, 1);

    result = pydos_frozenset_difference(fs1, fs2);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(pydos_frozenset_len(result), 2);

    PYDOS_DECREF(result);
    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items1[2]);
    PYDOS_DECREF(items2[0]);
}

/* ------------------------------------------------------------------ */
/* fzs_issubset: {1,2} subset of {1,2,3}                              */
/* ------------------------------------------------------------------ */
TEST(fzs_issubset)
{
    PyDosObj far *items1[2];
    PyDosObj far *items2[3];
    PyDosObj far *fs1;
    PyDosObj far *fs2;

    items1[0] = pydos_obj_new_int(1);
    items1[1] = pydos_obj_new_int(2);
    items2[0] = pydos_obj_new_int(1);
    items2[1] = pydos_obj_new_int(2);
    items2[2] = pydos_obj_new_int(3);
    fs1 = pydos_frozenset_new(items1, 2);
    fs2 = pydos_frozenset_new(items2, 3);

    ASSERT_TRUE(pydos_frozenset_issubset(fs1, fs2));
    ASSERT_FALSE(pydos_frozenset_issubset(fs2, fs1));

    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items2[0]);
    PYDOS_DECREF(items2[1]);
    PYDOS_DECREF(items2[2]);
}

/* ------------------------------------------------------------------ */
/* fzs_isdisjoint: no common elements                                  */
/* ------------------------------------------------------------------ */
TEST(fzs_isdisjoint)
{
    PyDosObj far *items1[2];
    PyDosObj far *items2[2];
    PyDosObj far *fs1;
    PyDosObj far *fs2;

    items1[0] = pydos_obj_new_int(1);
    items1[1] = pydos_obj_new_int(2);
    items2[0] = pydos_obj_new_int(3);
    items2[1] = pydos_obj_new_int(4);
    fs1 = pydos_frozenset_new(items1, 2);
    fs2 = pydos_frozenset_new(items2, 2);

    ASSERT_TRUE(pydos_frozenset_isdisjoint(fs1, fs2));

    PYDOS_DECREF(fs1);
    PYDOS_DECREF(fs2);
    PYDOS_DECREF(items1[0]);
    PYDOS_DECREF(items1[1]);
    PYDOS_DECREF(items2[0]);
    PYDOS_DECREF(items2[1]);
}

/* ------------------------------------------------------------------ */
/* fzs_truthy: non-empty is truthy, empty is falsy                     */
/* ------------------------------------------------------------------ */
TEST(fzs_truthy)
{
    PyDosObj far *items[1];
    PyDosObj far *fs_empty;
    PyDosObj far *fs_one;

    fs_empty = pydos_frozenset_new((PyDosObj far * far *)0, 0);
    ASSERT_FALSE(pydos_obj_is_truthy(fs_empty));

    items[0] = pydos_obj_new_int(1);
    fs_one = pydos_frozenset_new(items, 1);
    ASSERT_TRUE(pydos_obj_is_truthy(fs_one));

    PYDOS_DECREF(fs_empty);
    PYDOS_DECREF(fs_one);
    PYDOS_DECREF(items[0]);
}

/* ------------------------------------------------------------------ */
/* fzs_to_str: string representation                                   */
/* ------------------------------------------------------------------ */
TEST(fzs_to_str)
{
    PyDosObj far *fs;
    PyDosObj far *s;

    fs = pydos_frozenset_new((PyDosObj far * far *)0, 0);
    s = pydos_obj_to_str(fs);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    ASSERT_STR_EQ(s->v.str.data, (const char far *)"frozenset()");
    PYDOS_DECREF(s);
    PYDOS_DECREF(fs);
}

/* ------------------------------------------------------------------ */
/* fzs_conv_empty: frozenset() constructor with no args                */
/* ------------------------------------------------------------------ */
TEST(fzs_conv_empty)
{
    PyDosObj far *fs;
    fs = pydos_builtin_frozenset_conv(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(fs);
    ASSERT_EQ(fs->type, PYDT_FROZENSET);
    ASSERT_EQ(pydos_frozenset_len(fs), 0);
    PYDOS_DECREF(fs);
}

/* ------------------------------------------------------------------ */
/* fzs_conv_list: frozenset(list) constructor                          */
/* ------------------------------------------------------------------ */
TEST(fzs_conv_list)
{
    PyDosObj far *lst;
    PyDosObj far *argv[1];
    PyDosObj far *fs;
    PyDosObj far *elem;

    lst = pydos_obj_alloc();
    ASSERT_NOT_NULL(lst);
    lst->type = PYDT_LIST;
    lst->flags = 0;
    lst->v.list.items = (PyDosObj far * far *)pydos_far_alloc(
        2 * sizeof(PyDosObj far *));
    lst->v.list.len = 2;
    lst->v.list.cap = 2;
    lst->v.list.items[0] = pydos_obj_new_int(10);
    lst->v.list.items[1] = pydos_obj_new_int(20);

    argv[0] = lst;
    fs = pydos_builtin_frozenset_conv(1, argv);
    ASSERT_NOT_NULL(fs);
    ASSERT_EQ(fs->type, PYDT_FROZENSET);
    ASSERT_EQ(pydos_frozenset_len(fs), 2);

    elem = pydos_obj_new_int(10);
    ASSERT_TRUE(pydos_frozenset_contains(fs, elem));
    PYDOS_DECREF(elem);

    PYDOS_DECREF(fs);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_fzs_tests(void)
{
    SUITE("pdos_fzs");

    RUN(fzs_empty);
    RUN(fzs_single);
    RUN(fzs_dedup);
    RUN(fzs_contains_yes);
    RUN(fzs_contains_no);
    RUN(fzs_hash_cached);
    RUN(fzs_equal);
    RUN(fzs_not_equal);
    RUN(fzs_union);
    RUN(fzs_intersection);
    RUN(fzs_difference);
    RUN(fzs_issubset);
    RUN(fzs_isdisjoint);
    RUN(fzs_truthy);
    RUN(fzs_to_str);
    RUN(fzs_conv_empty);
    RUN(fzs_conv_list);
}
