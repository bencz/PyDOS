/*
 * t_unp.c - Unit tests for pydos_unpack_ex() star unpacking helper
 *
 * Tests the runtime helper used by generated code for:
 *   a, *b, c = [1, 2, 3, 4, 5]
 */

#include "testfw.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_mem.h"

/* Helper: call pydos_unpack_ex with builtin calling convention */
static PyDosObj far *call_unpack_ex(PyDosObj far *seq, int before, int after)
{
    PyDosObj far *argv[3];
    PyDosObj far *before_obj;
    PyDosObj far *after_obj;
    PyDosObj far *result;

    before_obj = pydos_obj_new_int((long)before);
    after_obj = pydos_obj_new_int((long)after);
    argv[0] = seq;
    argv[1] = before_obj;
    argv[2] = after_obj;

    result = pydos_unpack_ex(3, argv);

    PYDOS_DECREF(before_obj);
    PYDOS_DECREF(after_obj);
    return result;
}

/* ------------------------------------------------------------------ */
/* Helper: build a list [1, 2, 3, 4, 5]                               */
/* ------------------------------------------------------------------ */
static PyDosObj far *make_list_5(void)
{
    PyDosObj far *lst = pydos_list_new(5);
    PyDosObj far *v;
    int i;
    for (i = 1; i <= 5; i++) {
        v = pydos_obj_new_int((long)i);
        pydos_list_append(lst, v);
        PYDOS_DECREF(v);
    }
    return lst;
}

/* ------------------------------------------------------------------ */
/* a, *b, c = [1, 2, 3, 4, 5]  →  result = [1, [2,3,4], 5]         */
/* ------------------------------------------------------------------ */
TEST(unpack_middle)
{
    PyDosObj far *lst = make_list_5();
    PyDosObj far *result = call_unpack_ex(lst, 1, 1);
    PyDosObj far *elem;

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_LIST);
    ASSERT_EQ((long)result->v.list.len, 3L);

    /* result[0] = 1 */
    elem = pydos_list_get(result, 0L);
    ASSERT_NOT_NULL(elem);
    ASSERT_EQ(elem->v.int_val, 1L);
    PYDOS_DECREF(elem);

    /* result[1] = [2, 3, 4] */
    elem = pydos_list_get(result, 1L);
    ASSERT_NOT_NULL(elem);
    ASSERT_EQ(elem->type, PYDT_LIST);
    ASSERT_EQ((long)elem->v.list.len, 3L);
    {
        PyDosObj far *inner;
        inner = pydos_list_get(elem, 0L);
        ASSERT_EQ(inner->v.int_val, 2L);
        PYDOS_DECREF(inner);
        inner = pydos_list_get(elem, 1L);
        ASSERT_EQ(inner->v.int_val, 3L);
        PYDOS_DECREF(inner);
        inner = pydos_list_get(elem, 2L);
        ASSERT_EQ(inner->v.int_val, 4L);
        PYDOS_DECREF(inner);
    }
    PYDOS_DECREF(elem);

    /* result[2] = 5 */
    elem = pydos_list_get(result, 2L);
    ASSERT_NOT_NULL(elem);
    ASSERT_EQ(elem->v.int_val, 5L);
    PYDOS_DECREF(elem);

    PYDOS_DECREF(result);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* first, *rest = [1, 2, 3]  →  result = [1, [2, 3]]                */
/* ------------------------------------------------------------------ */
TEST(unpack_rest)
{
    PyDosObj far *lst = pydos_list_new(3);
    PyDosObj far *v;
    PyDosObj far *result;
    PyDosObj far *elem;
    int i;

    for (i = 1; i <= 3; i++) {
        v = pydos_obj_new_int((long)i);
        pydos_list_append(lst, v);
        PYDOS_DECREF(v);
    }

    result = call_unpack_ex(lst, 1, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((long)result->v.list.len, 2L);

    /* result[0] = 1 */
    elem = pydos_list_get(result, 0L);
    ASSERT_EQ(elem->v.int_val, 1L);
    PYDOS_DECREF(elem);

    /* result[1] = [2, 3] */
    elem = pydos_list_get(result, 1L);
    ASSERT_EQ(elem->type, PYDT_LIST);
    ASSERT_EQ((long)elem->v.list.len, 2L);
    PYDOS_DECREF(elem);

    PYDOS_DECREF(result);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* *init, last = [1, 2, 3]  →  result = [[1, 2], 3]                 */
/* ------------------------------------------------------------------ */
TEST(unpack_init)
{
    PyDosObj far *lst = pydos_list_new(3);
    PyDosObj far *v;
    PyDosObj far *result;
    PyDosObj far *elem;
    int i;

    for (i = 1; i <= 3; i++) {
        v = pydos_obj_new_int((long)i);
        pydos_list_append(lst, v);
        PYDOS_DECREF(v);
    }

    result = call_unpack_ex(lst, 0, 1);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((long)result->v.list.len, 2L);

    /* result[0] = [1, 2] */
    elem = pydos_list_get(result, 0L);
    ASSERT_EQ(elem->type, PYDT_LIST);
    ASSERT_EQ((long)elem->v.list.len, 2L);
    PYDOS_DECREF(elem);

    /* result[1] = 3 */
    elem = pydos_list_get(result, 1L);
    ASSERT_EQ(elem->v.int_val, 3L);
    PYDOS_DECREF(elem);

    PYDOS_DECREF(result);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* a, *b, c = [1, 2]  →  result = [1, [], 2]  (empty star)          */
/* ------------------------------------------------------------------ */
TEST(unpack_empty_star)
{
    PyDosObj far *lst = pydos_list_new(2);
    PyDosObj far *v;
    PyDosObj far *result;
    PyDosObj far *elem;

    v = pydos_obj_new_int(1L);
    pydos_list_append(lst, v);
    PYDOS_DECREF(v);
    v = pydos_obj_new_int(2L);
    pydos_list_append(lst, v);
    PYDOS_DECREF(v);

    result = call_unpack_ex(lst, 1, 1);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((long)result->v.list.len, 3L);

    /* result[0] = 1 */
    elem = pydos_list_get(result, 0L);
    ASSERT_EQ(elem->v.int_val, 1L);
    PYDOS_DECREF(elem);

    /* result[1] = [] (empty list) */
    elem = pydos_list_get(result, 1L);
    ASSERT_EQ(elem->type, PYDT_LIST);
    ASSERT_EQ((long)elem->v.list.len, 0L);
    PYDOS_DECREF(elem);

    /* result[2] = 2 */
    elem = pydos_list_get(result, 2L);
    ASSERT_EQ(elem->v.int_val, 2L);
    PYDOS_DECREF(elem);

    PYDOS_DECREF(result);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* *star, = [7, 8, 9]  →  result = [[7, 8, 9]]  (before=0, after=0) */
/* ------------------------------------------------------------------ */
TEST(unpack_all_star)
{
    PyDosObj far *lst = pydos_list_new(3);
    PyDosObj far *v;
    PyDosObj far *result;
    PyDosObj far *elem;
    int i;

    for (i = 7; i <= 9; i++) {
        v = pydos_obj_new_int((long)i);
        pydos_list_append(lst, v);
        PYDOS_DECREF(v);
    }

    result = call_unpack_ex(lst, 0, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((long)result->v.list.len, 1L);

    /* result[0] = [7, 8, 9] */
    elem = pydos_list_get(result, 0L);
    ASSERT_EQ(elem->type, PYDT_LIST);
    ASSERT_EQ((long)elem->v.list.len, 3L);
    PYDOS_DECREF(elem);

    PYDOS_DECREF(result);
    PYDOS_DECREF(lst);
}

/* ------------------------------------------------------------------ */
/* Tuple source: x, y, *rest = (100, 200, 300)                       */
/* ------------------------------------------------------------------ */
TEST(unpack_tuple_src)
{
    PyDosObj far *tup;
    PyDosObj far *result;
    PyDosObj far *elem;

    tup = pydos_obj_alloc();
    ASSERT_NOT_NULL(tup);
    tup->type = PYDT_TUPLE;
    tup->flags = 0;
    tup->refcount = 1;
    tup->v.tuple.len = 3;
    tup->v.tuple.items = (PyDosObj far * far *)pydos_far_alloc(
        3UL * (unsigned long)sizeof(PyDosObj far *));
    tup->v.tuple.items[0] = pydos_obj_new_int(100L);
    tup->v.tuple.items[1] = pydos_obj_new_int(200L);
    tup->v.tuple.items[2] = pydos_obj_new_int(300L);

    result = call_unpack_ex(tup, 2, 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((long)result->v.list.len, 3L);

    /* result[0] = 100, result[1] = 200, result[2] = [300] */
    elem = pydos_list_get(result, 0L);
    ASSERT_EQ(elem->v.int_val, 100L);
    PYDOS_DECREF(elem);

    elem = pydos_list_get(result, 1L);
    ASSERT_EQ(elem->v.int_val, 200L);
    PYDOS_DECREF(elem);

    elem = pydos_list_get(result, 2L);
    ASSERT_EQ(elem->type, PYDT_LIST);
    ASSERT_EQ((long)elem->v.list.len, 1L);
    {
        PyDosObj far *inner = pydos_list_get(elem, 0L);
        ASSERT_EQ(inner->v.int_val, 300L);
        PYDOS_DECREF(inner);
    }
    PYDOS_DECREF(elem);

    PYDOS_DECREF(result);
    PYDOS_DECREF(tup);
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

void run_unp_tests(void)
{
    SUITE("pdos_unpack_ex");
    RUN(unpack_middle);
    RUN(unpack_rest);
    RUN(unpack_init);
    RUN(unpack_empty_star);
    RUN(unpack_all_star);
    RUN(unpack_tuple_src);
}
