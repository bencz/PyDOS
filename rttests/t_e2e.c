/*
 * t_e2e.c - End-to-end integration tests for PyDOS runtime
 *
 * Each test simulates what compiled Python code would do:
 * create objects, call builtins, verify results.
 */

#include "testfw.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_blt.h"
#include "../runtime/pdos_int.h"
#include "../runtime/pdos_str.h"
#include "../runtime/pdos_lst.h"

/* ------------------------------------------------------------------ */
/* e2e_hello_world: print("Hello, DOS!") -> returns None               */
/* ------------------------------------------------------------------ */

TEST(e2e_hello_world)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"Hello, DOS!", 11);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_print(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* e2e_print_int: print(42)                                            */
/* ------------------------------------------------------------------ */

TEST(e2e_print_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_print(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* e2e_print_multi: print("x", "y")                                    */
/* ------------------------------------------------------------------ */

TEST(e2e_print_multi)
{
    PyDosObj far *argv[2];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"x", 1);
    argv[1] = pydos_obj_new_str((const char far *)"y", 1);
    ASSERT_NOT_NULL(argv[0]);
    ASSERT_NOT_NULL(argv[1]);

    result = pydos_builtin_print(2, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
    PYDOS_DECREF(argv[1]);
}

/* ------------------------------------------------------------------ */
/* e2e_print_none: print(None)                                         */
/* ------------------------------------------------------------------ */

TEST(e2e_print_none)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_none();
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_print(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* e2e_arith_add: x = 3 + 4 -> 7                                      */
/* ------------------------------------------------------------------ */

TEST(e2e_arith_add)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *result;

    a = pydos_obj_new_int(3L);
    b = pydos_obj_new_int(4L);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    result = pydos_int_add(a, b);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 7L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* e2e_arith_sub: x = 10 - 3 -> 7                                     */
/* ------------------------------------------------------------------ */

TEST(e2e_arith_sub)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *result;

    a = pydos_obj_new_int(10L);
    b = pydos_obj_new_int(3L);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    result = pydos_int_sub(a, b);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 7L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* e2e_arith_mul: x = 6 * 7 -> 42                                     */
/* ------------------------------------------------------------------ */

TEST(e2e_arith_mul)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *result;

    a = pydos_obj_new_int(6L);
    b = pydos_obj_new_int(7L);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    result = pydos_int_mul(a, b);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* e2e_str_concat: x = "ab" + "cd" -> "abcd"                          */
/* ------------------------------------------------------------------ */

TEST(e2e_str_concat)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *result;

    a = pydos_obj_new_str((const char far *)"ab", 2);
    b = pydos_obj_new_str((const char far *)"cd", 2);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    result = pydos_str_concat(a, b);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_EQ(result->v.str.len, 4);
    ASSERT_STR_EQ(result->v.str.data, "abcd");

    PYDOS_DECREF(result);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* e2e_comparison: x = 3 < 5 -> True                                   */
/* ------------------------------------------------------------------ */

TEST(e2e_comparison)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    PyDosObj far *result;

    a = pydos_obj_new_int(3L);
    b = pydos_obj_new_int(5L);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);

    cmp = pydos_int_compare(a, b);
    /* compare returns -1 for a < b */
    result = pydos_obj_new_bool(cmp < 0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_BOOL);
    ASSERT_EQ(result->v.bool_val, 1);

    PYDOS_DECREF(result);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* e2e_list_basic: x = [1,2,3]; len(x) -> 3                           */
/* ------------------------------------------------------------------ */

TEST(e2e_list_basic)
{
    PyDosObj far *list;
    PyDosObj far *len_argv[1];
    PyDosObj far *result;

    list = pydos_list_new(4);
    ASSERT_NOT_NULL(list);
    pydos_list_append(list, pydos_obj_new_int(1L));
    pydos_list_append(list, pydos_obj_new_int(2L));
    pydos_list_append(list, pydos_obj_new_int(3L));

    len_argv[0] = list;
    result = pydos_builtin_len(1, len_argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 3L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(list);
}

/* ------------------------------------------------------------------ */
/* e2e_range_iter: for i in range(3) -> produces 0, 1, 2              */
/* ------------------------------------------------------------------ */

TEST(e2e_range_iter)
{
    PyDosObj far *range_argv[1];
    PyDosObj far *range_obj;
    long cur;
    int count;

    range_argv[0] = pydos_obj_new_int(3L);
    ASSERT_NOT_NULL(range_argv[0]);

    range_obj = pydos_builtin_range(1, range_argv);
    ASSERT_NOT_NULL(range_obj);
    ASSERT_EQ(range_obj->type, PYDT_RANGE);

    /* Iterate: same pattern generated code would use */
    count = 0;
    for (cur = range_obj->v.range.start;
         cur < range_obj->v.range.stop;
         cur += range_obj->v.range.step)
    {
        ASSERT_EQ(cur, (long)count);
        count++;
    }
    ASSERT_EQ(count, 3);

    PYDOS_DECREF(range_argv[0]);
    PYDOS_DECREF(range_obj);
}

/* ------------------------------------------------------------------ */
/* e2e_nested_calls: print(str(len("hi"))) -> chains builtins          */
/* ------------------------------------------------------------------ */

TEST(e2e_nested_calls)
{
    PyDosObj far *argv[1];
    PyDosObj far *len_result;
    PyDosObj far *str_result;
    PyDosObj far *print_result;

    /* len("hi") -> 2 */
    argv[0] = pydos_obj_new_str((const char far *)"hi", 2);
    ASSERT_NOT_NULL(argv[0]);
    len_result = pydos_builtin_len(1, argv);
    ASSERT_NOT_NULL(len_result);
    ASSERT_EQ(len_result->type, PYDT_INT);
    ASSERT_EQ(len_result->v.int_val, 2L);
    PYDOS_DECREF(argv[0]);

    /* str(2) -> "2" */
    argv[0] = len_result;
    str_result = pydos_builtin_str_conv(1, argv);
    ASSERT_NOT_NULL(str_result);
    ASSERT_EQ(str_result->type, PYDT_STR);
    ASSERT_STR_EQ(str_result->v.str.data, "2");
    PYDOS_DECREF(len_result);

    /* print("2") -> None */
    argv[0] = str_result;
    print_result = pydos_builtin_print(1, argv);
    ASSERT_NOT_NULL(print_result);
    ASSERT_EQ(print_result->type, PYDT_NONE);

    PYDOS_DECREF(print_result);
    PYDOS_DECREF(str_result);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_e2e_tests(void)
{
    SUITE("e2e (integration)");

    RUN(e2e_hello_world);
    RUN(e2e_print_int);
    RUN(e2e_print_multi);
    RUN(e2e_print_none);
    RUN(e2e_arith_add);
    RUN(e2e_arith_sub);
    RUN(e2e_arith_mul);
    RUN(e2e_str_concat);
    RUN(e2e_comparison);
    RUN(e2e_list_basic);
    RUN(e2e_range_iter);
    RUN(e2e_nested_calls);
}
