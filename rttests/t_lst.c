/*
 * t_lst.c - Unit tests for pdos_lst (list) module
 *
 * Tests list creation, append, get, set, len, slice, concat,
 * contains, pop, insert, reverse, and growth behavior.
 */

#include "testfw.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_dic.h"

/* ------------------------------------------------------------------ */
/* Creation                                                            */
/* ------------------------------------------------------------------ */

TEST(list_new)
{
    PyDosObj far *lst = pydos_list_new(4);
    ASSERT_NOT_NULL(lst);
    ASSERT_EQ(lst->type, PYDT_LIST);
    ASSERT_EQ(lst->v.list.len, 0);
    PYDOS_DECREF(lst);
}

TEST(list_new_cap)
{
    PyDosObj far *lst = pydos_list_new(16);
    ASSERT_NOT_NULL(lst);
    ASSERT_TRUE(lst->v.list.cap >= 16);
    ASSERT_EQ(lst->v.list.len, 0);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Append                                                              */
/* ------------------------------------------------------------------ */

TEST(list_append_one)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *item = pydos_obj_new_int(42L);
    pydos_list_append(lst, item);
    ASSERT_EQ(lst->v.list.len, 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(lst);
}

TEST(list_append_multi)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *items[5];
    int i;

    for (i = 0; i < 5; i++) {
        items[i] = pydos_obj_new_int((long)(i + 1));
        pydos_list_append(lst, items[i]);
    }
    ASSERT_EQ(lst->v.list.len, 5);

    for (i = 0; i < 5; i++) {
        PYDOS_DECREF(items[i]);
    }
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Get                                                                 */
/* ------------------------------------------------------------------ */

TEST(list_get_first)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *item = pydos_obj_new_int(10L);
    PyDosObj far *got;

    pydos_list_append(lst, item);
    got = pydos_list_get(lst, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 10L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(item);
    PYDOS_DECREF(lst);
}

TEST(list_get_last)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *c = pydos_obj_new_int(30L);
    PyDosObj far *got;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    got = pydos_list_get(lst, -1L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 30L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(lst);
}

TEST(list_get_middle)
{
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *items[5];
    PyDosObj far *got;
    int i;

    for (i = 0; i < 5; i++) {
        items[i] = pydos_obj_new_int((long)((i + 1) * 10));
        pydos_list_append(lst, items[i]);
    }

    got = pydos_list_get(lst, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 30L);

    PYDOS_DECREF(got);
    for (i = 0; i < 5; i++) {
        PYDOS_DECREF(items[i]);
    }
    PYDOS_DECREF(lst);
}

TEST(list_get_out_of_bounds)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(1L);
    PyDosObj far *b = pydos_obj_new_int(2L);
    PyDosObj far *c = pydos_obj_new_int(3L);
    PyDosObj far *got;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    got = pydos_list_get(lst, 10L);
    ASSERT_NULL(got);

    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Set                                                                 */
/* ------------------------------------------------------------------ */

TEST(list_set)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *replacement = pydos_obj_new_int(99L);
    PyDosObj far *got;
    int rc;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);

    rc = pydos_list_set(lst, 1L, replacement);
    ASSERT_EQ(rc, 0);

    got = pydos_list_get(lst, 1L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 99L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(replacement);
    PYDOS_DECREF(lst);
}

TEST(list_set_negative)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *c = pydos_obj_new_int(30L);
    PyDosObj far *replacement = pydos_obj_new_int(77L);
    PyDosObj far *got;
    int rc;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    rc = pydos_list_set(lst, -1L, replacement);
    ASSERT_EQ(rc, 0);

    got = pydos_list_get(lst, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 77L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(replacement);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Len                                                                 */
/* ------------------------------------------------------------------ */

TEST(list_len)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(1L);
    PyDosObj far *b = pydos_obj_new_int(2L);
    PyDosObj far *c = pydos_obj_new_int(3L);

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    ASSERT_EQ(pydos_list_len(lst), 3L);

    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(lst);
}

TEST(list_len_empty)
{
    PyDosObj far *lst = pydos_list_new(4);
    ASSERT_EQ(pydos_list_len(lst), 0L);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Slice                                                               */
/* ------------------------------------------------------------------ */

TEST(list_slice_basic)
{
    /* [10,20,30,40,50][1:3] -> [20,30] */
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *items[5];
    PyDosObj far *sliced;
    PyDosObj far *got;
    int i;

    for (i = 0; i < 5; i++) {
        items[i] = pydos_obj_new_int((long)((i + 1) * 10));
        pydos_list_append(lst, items[i]);
    }

    sliced = pydos_list_slice(lst, 1L, 3L, 1L);
    ASSERT_NOT_NULL(sliced);
    ASSERT_EQ(pydos_list_len(sliced), 2L);

    got = pydos_list_get(sliced, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 20L);
    PYDOS_DECREF(got);

    got = pydos_list_get(sliced, 1L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 30L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(sliced);
    for (i = 0; i < 5; i++) {
        PYDOS_DECREF(items[i]);
    }
    PYDOS_DECREF(lst);
}

TEST(list_slice_step)
{
    /* [10,20,30,40,50][0:5:2] -> [10,30,50] */
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *items[5];
    PyDosObj far *sliced;
    PyDosObj far *got;
    int i;

    for (i = 0; i < 5; i++) {
        items[i] = pydos_obj_new_int((long)((i + 1) * 10));
        pydos_list_append(lst, items[i]);
    }

    sliced = pydos_list_slice(lst, 0L, 5L, 2L);
    ASSERT_NOT_NULL(sliced);
    ASSERT_EQ(pydos_list_len(sliced), 3L);

    got = pydos_list_get(sliced, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 10L);
    PYDOS_DECREF(got);

    got = pydos_list_get(sliced, 1L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 30L);
    PYDOS_DECREF(got);

    got = pydos_list_get(sliced, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 50L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(sliced);
    for (i = 0; i < 5; i++) {
        PYDOS_DECREF(items[i]);
    }
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Concat                                                              */
/* ------------------------------------------------------------------ */

TEST(list_concat)
{
    PyDosObj far *a = pydos_list_new(4);
    PyDosObj far *b = pydos_list_new(4);
    PyDosObj far *i1 = pydos_obj_new_int(1L);
    PyDosObj far *i2 = pydos_obj_new_int(2L);
    PyDosObj far *i3 = pydos_obj_new_int(3L);
    PyDosObj far *i4 = pydos_obj_new_int(4L);
    PyDosObj far *result;

    pydos_list_append(a, i1);
    pydos_list_append(a, i2);
    pydos_list_append(b, i3);
    pydos_list_append(b, i4);

    result = pydos_list_concat(a, b);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(pydos_list_len(result), 4L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(i1);
    PYDOS_DECREF(i2);
    PYDOS_DECREF(i3);
    PYDOS_DECREF(i4);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* Contains                                                            */
/* ------------------------------------------------------------------ */

TEST(list_contains_found)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *needle = pydos_obj_new_int(20L);

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);

    ASSERT_EQ(pydos_list_contains(lst, needle), 1);

    PYDOS_DECREF(needle);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(lst);
}

TEST(list_contains_missing)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *needle = pydos_obj_new_int(99L);

    pydos_list_append(lst, a);

    ASSERT_EQ(pydos_list_contains(lst, needle), 0);

    PYDOS_DECREF(needle);
    PYDOS_DECREF(a);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Pop                                                                 */
/* ------------------------------------------------------------------ */

TEST(list_pop_last)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *c = pydos_obj_new_int(30L);
    PyDosObj far *popped;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    popped = pydos_list_pop(lst, -1L);
    ASSERT_NOT_NULL(popped);
    ASSERT_EQ(popped->v.int_val, 30L);
    ASSERT_EQ(lst->v.list.len, 2);

    PYDOS_DECREF(popped);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(lst);
}

TEST(list_pop_first)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *c = pydos_obj_new_int(30L);
    PyDosObj far *popped;
    PyDosObj far *got;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    popped = pydos_list_pop(lst, 0L);
    ASSERT_NOT_NULL(popped);
    ASSERT_EQ(popped->v.int_val, 10L);
    ASSERT_EQ(lst->v.list.len, 2);

    /* Verify the rest shifted */
    got = pydos_list_get(lst, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 20L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(popped);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Insert                                                              */
/* ------------------------------------------------------------------ */

TEST(list_insert_begin)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(10L);
    PyDosObj far *b = pydos_obj_new_int(20L);
    PyDosObj far *newitem = pydos_obj_new_int(5L);
    PyDosObj far *got;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);

    pydos_list_insert(lst, 0L, newitem);
    ASSERT_EQ(lst->v.list.len, 3);

    got = pydos_list_get(lst, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 5L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(newitem);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(lst);
}

TEST(list_insert_middle)
{
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *items[4];
    PyDosObj far *newitem = pydos_obj_new_int(99L);
    PyDosObj far *got;
    int i;

    for (i = 0; i < 4; i++) {
        items[i] = pydos_obj_new_int((long)((i + 1) * 10));
        pydos_list_append(lst, items[i]);
    }

    pydos_list_insert(lst, 2L, newitem);
    ASSERT_EQ(lst->v.list.len, 5);

    got = pydos_list_get(lst, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 99L);
    PYDOS_DECREF(got);

    /* Verify element that was at index 2 moved to index 3 */
    got = pydos_list_get(lst, 3L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 30L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(newitem);
    for (i = 0; i < 4; i++) {
        PYDOS_DECREF(items[i]);
    }
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Reverse                                                             */
/* ------------------------------------------------------------------ */

TEST(list_reverse)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *a = pydos_obj_new_int(1L);
    PyDosObj far *b = pydos_obj_new_int(2L);
    PyDosObj far *c = pydos_obj_new_int(3L);
    PyDosObj far *got;

    pydos_list_append(lst, a);
    pydos_list_append(lst, b);
    pydos_list_append(lst, c);

    pydos_list_reverse(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 2L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
    PYDOS_DECREF(lst);
}

TEST(list_reverse_empty)
{
    PyDosObj far *lst = pydos_list_new(4);
    pydos_list_reverse(lst);
    ASSERT_EQ(lst->v.list.len, 0);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Sort                                                                */
/* ------------------------------------------------------------------ */

TEST(list_sort_ints)
{
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(5L));
    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(8L));
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(4L));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 4L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 3L);
    ASSERT_EQ(got->v.int_val, 5L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 4L);
    ASSERT_EQ(got->v.int_val, 8L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_strings)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_str((const char far *)"banana", 6));
    pydos_list_append(lst, pydos_obj_new_str((const char far *)"apple", 5));
    pydos_list_append(lst, pydos_obj_new_str((const char far *)"cherry", 6));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_STR_EQ(got->v.str.data, "apple");
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_STR_EQ(got->v.str.data, "banana");
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_STR_EQ(got->v.str.data, "cherry");
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_already_sorted)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    pydos_list_append(lst, pydos_obj_new_int(3L));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_empty)
{
    PyDosObj far *lst = pydos_list_new(4);
    pydos_list_sort(lst);
    ASSERT_EQ(lst->v.list.len, 0);
    PYDOS_DECREF(lst);
}

TEST(list_sort_single)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *got;
    pydos_list_append(lst, pydos_obj_new_int(42L));
    pydos_list_sort(lst);
    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 42L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(lst);
}

TEST(list_sort_reverse_order)
{
    /* Sort a list that's in descending order */
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(9L));
    pydos_list_append(lst, pydos_obj_new_int(7L));
    pydos_list_append(lst, pydos_obj_new_int(5L));
    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(1L));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 4L);
    ASSERT_EQ(got->v.int_val, 9L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_duplicates)
{
    /* Sort list with duplicate values — stability: order of equals preserved */
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    pydos_list_append(lst, pydos_obj_new_int(1L));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 2L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 3L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 4L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_negative_numbers)
{
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(5L));
    pydos_list_append(lst, pydos_obj_new_int(-3L));
    pydos_list_append(lst, pydos_obj_new_int(0L));
    pydos_list_append(lst, pydos_obj_new_int(-10L));
    pydos_list_append(lst, pydos_obj_new_int(2L));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, -10L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_EQ(got->v.int_val, -3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 0L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 4L);
    ASSERT_EQ(got->v.int_val, 5L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_two_elements)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(5L));

    pydos_list_sort(lst);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 5L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_EQ(got->v.int_val, 10L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Sort via call_method (reverse=True parameter)                       */
/* ------------------------------------------------------------------ */

TEST(list_sort_call_method)
{
    /* sort() via call_method — same as direct pydos_list_sort */
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[1];
    PyDosObj far *ret;
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));

    argv[0] = lst;
    ret = pydos_obj_call_method((const char far *)"sort", 1, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

TEST(list_sort_reverse_param)
{
    /* sort(reverse=True) via call_method — sorts then reverses */
    PyDosObj far *lst = pydos_list_new(8);
    PyDosObj far *argv[2];
    PyDosObj far *ret;
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(5L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    pydos_list_append(lst, pydos_obj_new_int(4L));

    argv[0] = lst;
    argv[1] = pydos_obj_new_bool(1);  /* reverse=True */
    ret = pydos_obj_call_method((const char far *)"sort", 2, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    /* Should be sorted descending: 5, 4, 3, 2, 1 */
    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 5L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_EQ(got->v.int_val, 4L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 3L);
    ASSERT_EQ(got->v.int_val, 2L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 4L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(lst);
}

TEST(list_sort_reverse_false)
{
    /* sort(reverse=False) via call_method — ascending (same as default) */
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[2];
    PyDosObj far *ret;
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_int(3L));
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));

    argv[0] = lst;
    argv[1] = pydos_obj_new_bool(0);  /* reverse=False */
    ret = pydos_obj_call_method((const char far *)"sort", 2, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(lst);
}

TEST(list_sort_reverse_strings)
{
    /* Sort strings with reverse=True */
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *argv[2];
    PyDosObj far *ret;
    PyDosObj far *got;

    pydos_list_append(lst, pydos_obj_new_str((const char far *)"apple", 5));
    pydos_list_append(lst, pydos_obj_new_str((const char far *)"cherry", 6));
    pydos_list_append(lst, pydos_obj_new_str((const char far *)"banana", 6));

    argv[0] = lst;
    argv[1] = pydos_obj_new_bool(1);  /* reverse=True */
    ret = pydos_obj_call_method((const char far *)"sort", 2, argv);
    ASSERT_NOT_NULL(ret);
    PYDOS_DECREF(ret);

    /* Descending: cherry, banana, apple */
    got = pydos_list_get(lst, 0L);
    ASSERT_STR_EQ(got->v.str.data, "cherry");
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 1L);
    ASSERT_STR_EQ(got->v.str.data, "banana");
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_STR_EQ(got->v.str.data, "apple");
    PYDOS_DECREF(got);

    PYDOS_DECREF(argv[1]);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Reverse via call_method                                             */
/* ------------------------------------------------------------------ */

TEST(list_reverse_call_method)
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
    ASSERT_EQ(got->v.int_val, 3L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 2L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Sort dict keys/values: get from dict C API, sort them               */
/* ------------------------------------------------------------------ */

TEST(list_sort_dict_keys)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *keys;
    PyDosObj far *got;

    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"cherry", 6),
        pydos_obj_new_int(3L));
    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"apple", 5),
        pydos_obj_new_int(1L));
    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"banana", 6),
        pydos_obj_new_int(2L));

    keys = pydos_dict_keys(d);
    ASSERT_NOT_NULL(keys);
    ASSERT_EQ(keys->type, PYDT_LIST);
    ASSERT_EQ(pydos_list_len(keys), 3L);

    pydos_list_sort(keys);

    got = pydos_list_get(keys, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got->v.str.data, "apple");
    PYDOS_DECREF(got);

    got = pydos_list_get(keys, 1L);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got->v.str.data, "banana");
    PYDOS_DECREF(got);

    got = pydos_list_get(keys, 2L);
    ASSERT_NOT_NULL(got);
    ASSERT_STR_EQ(got->v.str.data, "cherry");
    PYDOS_DECREF(got);

    PYDOS_DECREF(keys);
    PYDOS_DECREF(d);
}

TEST(list_sort_dict_values)
{
    PyDosObj far *d = pydos_dict_new(8);
    PyDosObj far *vals;
    PyDosObj far *got;

    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"c", 1),
        pydos_obj_new_int(30L));
    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"a", 1),
        pydos_obj_new_int(10L));
    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"b", 1),
        pydos_obj_new_int(20L));

    vals = pydos_dict_values(d);
    ASSERT_NOT_NULL(vals);
    ASSERT_EQ(vals->type, PYDT_LIST);

    pydos_list_sort(vals);

    got = pydos_list_get(vals, 0L);
    ASSERT_EQ(got->v.int_val, 10L);
    PYDOS_DECREF(got);

    got = pydos_list_get(vals, 1L);
    ASSERT_EQ(got->v.int_val, 20L);
    PYDOS_DECREF(got);

    got = pydos_list_get(vals, 2L);
    ASSERT_EQ(got->v.int_val, 30L);
    PYDOS_DECREF(got);

    PYDOS_DECREF(vals);
    PYDOS_DECREF(d);
}

TEST(list_sort_large)
{
    /* Sort 15 elements to stress the insertion sort */
    PyDosObj far *lst = pydos_list_new(16);
    PyDosObj far *got;
    long vals[15];
    int i;

    vals[0] = 42; vals[1] = 17; vals[2] = 93; vals[3] = 5;
    vals[4] = 81; vals[5] = 29; vals[6] = 64; vals[7] = 3;
    vals[8] = 58; vals[9] = 11; vals[10] = 76; vals[11] = 22;
    vals[12] = 47; vals[13] = 90; vals[14] = 1;

    for (i = 0; i < 15; i++) {
        pydos_list_append(lst, pydos_obj_new_int(vals[i]));
    }

    pydos_list_sort(lst);

    /* Verify sorted ascending */
    got = pydos_list_get(lst, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 7L);
    ASSERT_EQ(got->v.int_val, 42L);
    PYDOS_DECREF(got);

    got = pydos_list_get(lst, 14L);
    ASSERT_EQ(got->v.int_val, 93L);
    PYDOS_DECREF(got);

    /* Verify monotonically increasing */
    for (i = 1; i < 15; i++) {
        PyDosObj far *prev = pydos_list_get(lst, (long)(i - 1));
        PyDosObj far *curr = pydos_list_get(lst, (long)i);
        ASSERT_TRUE(prev->v.int_val <= curr->v.int_val);
        PYDOS_DECREF(prev);
        PYDOS_DECREF(curr);
    }

    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Growth                                                              */
/* ------------------------------------------------------------------ */

TEST(list_grow)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *items[20];
    PyDosObj far *got;
    int i;

    for (i = 0; i < 20; i++) {
        items[i] = pydos_obj_new_int((long)(i + 1));
        pydos_list_append(lst, items[i]);
    }

    ASSERT_EQ(lst->v.list.len, 20);

    /* Verify all items accessible */
    for (i = 0; i < 20; i++) {
        got = pydos_list_get(lst, (long)i);
        ASSERT_NOT_NULL(got);
        ASSERT_EQ(got->v.int_val, (long)(i + 1));
        PYDOS_DECREF(got);
    }

    for (i = 0; i < 20; i++) {
        PYDOS_DECREF(items[i]);
    }
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* list_from_iter                                                      */
/* ------------------------------------------------------------------ */

TEST(list_from_iter_null)
{
    PyDosObj far *r = pydos_list_from_iter((PyDosObj far *)0);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.list.len, 0);
    PYDOS_DECREF(r);
}

TEST(list_from_iter_list)
{
    /* list(list) -> copy */
    PyDosObj far *src = pydos_list_new(4);
    PyDosObj far *r;
    pydos_list_append(src, pydos_obj_new_int(10L));
    pydos_list_append(src, pydos_obj_new_int(20L));
    r = pydos_list_from_iter(src);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.list.len, 2);
    ASSERT_EQ(r->v.list.items[0]->v.int_val, 10L);
    ASSERT_EQ(r->v.list.items[1]->v.int_val, 20L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(src);
}

TEST(list_from_iter_range)
{
    /* list(range(3)) -> [0, 1, 2] */
    PyDosObj far *rng;
    PyDosObj far *r;
    rng = pydos_obj_alloc();
    rng->type = PYDT_RANGE;
    rng->refcount = 1;
    rng->flags = 0;
    rng->v.range.start = 0;
    rng->v.range.stop = 3;
    rng->v.range.step = 1;
    rng->v.range.current = 0;
    r = pydos_list_from_iter(rng);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.list.len, 3);
    ASSERT_EQ(r->v.list.items[0]->v.int_val, 0L);
    ASSERT_EQ(r->v.list.items[1]->v.int_val, 1L);
    ASSERT_EQ(r->v.list.items[2]->v.int_val, 2L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(rng);
}

TEST(list_from_iter_dict)
{
    /* list(dict) -> list of keys */
    PyDosObj far *d = pydos_dict_new(4);
    PyDosObj far *r;
    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"a", 1),
        pydos_obj_new_int(1L));
    pydos_dict_set(d,
        pydos_obj_new_str((const char far *)"b", 1),
        pydos_obj_new_int(2L));
    r = pydos_list_from_iter(d);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.list.len, 2);
    PYDOS_DECREF(r);
    PYDOS_DECREF(d);
}

TEST(list_from_iter_string)
{
    /* list("abc") -> ['a', 'b', 'c'] */
    PyDosObj far *s = pydos_obj_new_str((const char far *)"abc", 3);
    PyDosObj far *r = pydos_list_from_iter(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.list.len, 3);
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* List method tests: remove, clear, index, copy                       */
/* ------------------------------------------------------------------ */

TEST(list_remove_found)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *item = pydos_obj_new_int(20L);
    int rc;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_append(lst, pydos_obj_new_int(30L));
    rc = pydos_list_remove(lst, item);
    ASSERT_EQ(rc, 0);
    ASSERT_EQ(lst->v.list.len, 2);
    PYDOS_DECREF(item);
    PYDOS_DECREF(lst);
}

TEST(list_remove_not_found)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *item = pydos_obj_new_int(99L);
    int rc;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    rc = pydos_list_remove(lst, item);
    ASSERT_EQ(rc, -1);
    ASSERT_EQ(lst->v.list.len, 1);
    PYDOS_DECREF(item);
    PYDOS_DECREF(lst);
}

TEST(list_clear_items)
{
    PyDosObj far *lst = pydos_list_new(4);
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    pydos_list_append(lst, pydos_obj_new_int(3L));
    ASSERT_EQ(lst->v.list.len, 3);
    pydos_list_clear(lst);
    ASSERT_EQ(lst->v.list.len, 0);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Method wrappers: pop_m and insert_m (unbox int from PyDosObj*)       */
/* ------------------------------------------------------------------ */

TEST(list_pop_m_last)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *idx_obj = pydos_obj_new_int(-1L);
    PyDosObj far *popped;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_append(lst, pydos_obj_new_int(30L));
    popped = pydos_list_pop_m(lst, idx_obj);
    ASSERT_NOT_NULL(popped);
    ASSERT_EQ(popped->v.int_val, 30L);
    ASSERT_EQ(lst->v.list.len, 2);
    PYDOS_DECREF(popped);
    PYDOS_DECREF(idx_obj);
    PYDOS_DECREF(lst);
}

TEST(list_insert_m_begin)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *idx_obj = pydos_obj_new_int(0L);
    PyDosObj far *newitem = pydos_obj_new_int(5L);
    PyDosObj far *got;
    pydos_list_append(lst, pydos_obj_new_int(10L));
    pydos_list_append(lst, pydos_obj_new_int(20L));
    pydos_list_insert_m(lst, idx_obj, newitem);
    ASSERT_EQ(lst->v.list.len, 3);
    got = pydos_list_get(lst, 0L);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 5L);
    PYDOS_DECREF(got);
    PYDOS_DECREF(idx_obj);
    PYDOS_DECREF(newitem);
    PYDOS_DECREF(lst);
}

TEST(list_copy_independent)
{
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *cp;
    PyDosObj far *got;
    pydos_list_append(lst, pydos_obj_new_int(1L));
    pydos_list_append(lst, pydos_obj_new_int(2L));
    cp = pydos_list_copy(lst);
    ASSERT_NOT_NULL(cp);
    ASSERT_EQ(cp->v.list.len, 2);
    got = pydos_list_get(cp, 0L);
    ASSERT_EQ(got->v.int_val, 1L);
    PYDOS_DECREF(got);
    /* Modify copy doesn't affect original */
    pydos_list_append(cp, pydos_obj_new_int(3L));
    ASSERT_EQ(cp->v.list.len, 3);
    ASSERT_EQ(lst->v.list.len, 2);
    PYDOS_DECREF(cp);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_lst_tests(void)
{
    SUITE("pdos_lst");

    RUN(list_new);
    RUN(list_new_cap);
    RUN(list_append_one);
    RUN(list_append_multi);
    RUN(list_get_first);
    RUN(list_get_last);
    RUN(list_get_middle);
    RUN(list_get_out_of_bounds);
    RUN(list_set);
    RUN(list_set_negative);
    RUN(list_len);
    RUN(list_len_empty);
    RUN(list_slice_basic);
    RUN(list_slice_step);
    RUN(list_concat);
    RUN(list_contains_found);
    RUN(list_contains_missing);
    RUN(list_pop_last);
    RUN(list_pop_first);
    RUN(list_insert_begin);
    RUN(list_insert_middle);
    RUN(list_reverse);
    RUN(list_reverse_empty);
    RUN(list_sort_ints);
    RUN(list_sort_strings);
    RUN(list_sort_already_sorted);
    RUN(list_sort_empty);
    RUN(list_sort_single);
    RUN(list_sort_reverse_order);
    RUN(list_sort_duplicates);
    RUN(list_sort_negative_numbers);
    RUN(list_sort_two_elements);
    RUN(list_sort_call_method);
    RUN(list_sort_reverse_param);
    RUN(list_sort_reverse_false);
    RUN(list_sort_reverse_strings);
    RUN(list_reverse_call_method);
    RUN(list_sort_dict_keys);
    RUN(list_sort_dict_values);
    RUN(list_sort_large);
    RUN(list_grow);
    RUN(list_from_iter_null);
    RUN(list_from_iter_list);
    RUN(list_from_iter_range);
    RUN(list_from_iter_dict);
    RUN(list_from_iter_string);

    /* List method tests */
    RUN(list_remove_found);
    RUN(list_remove_not_found);
    RUN(list_clear_items);
    RUN(list_pop_m_last);
    RUN(list_insert_m_begin);
    RUN(list_copy_independent);
}
