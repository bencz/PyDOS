/*
 * t_blt.c - Unit tests for pdos_blt (builtins) module
 *
 * Tests len, type, int, str, bool, abs, min, max, ord, chr
 * built-in functions.
 */

#include "testfw.h"
#include "../runtime/pdos_blt.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_dic.h"
#include "../runtime/pdos_fzs.h"
#include "../runtime/pdos_bya.h"

/* ------------------------------------------------------------------ */
/* builtin_len_str: len("hello") = 5                                   */
/* ------------------------------------------------------------------ */

TEST(builtin_len_str)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"hello", 5);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_len(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 5L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_len_list: len of 3-item list = 3                            */
/* ------------------------------------------------------------------ */

TEST(builtin_len_list)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;
    PyDosObj far *list;

    list = pydos_list_new(4);
    ASSERT_NOT_NULL(list);
    pydos_list_append(list, pydos_obj_new_int(10L));
    pydos_list_append(list, pydos_obj_new_int(20L));
    pydos_list_append(list, pydos_obj_new_int(30L));

    argv[0] = list;
    result = pydos_builtin_len(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 3L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(list);
}

/* ------------------------------------------------------------------ */
/* builtin_len_dict: len of 2-entry dict = 2                           */
/* ------------------------------------------------------------------ */

TEST(builtin_len_dict)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;
    PyDosObj far *dict;
    PyDosObj far *k1;
    PyDosObj far *v1;
    PyDosObj far *k2;
    PyDosObj far *v2;

    dict = pydos_dict_new(8);
    ASSERT_NOT_NULL(dict);

    k1 = pydos_obj_new_str((const char far *)"a", 1);
    v1 = pydos_obj_new_int(1L);
    k2 = pydos_obj_new_str((const char far *)"b", 1);
    v2 = pydos_obj_new_int(2L);
    pydos_dict_set(dict, k1, v1);
    pydos_dict_set(dict, k2, v2);

    argv[0] = dict;
    result = pydos_builtin_len(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 2L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(k1);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(k2);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(dict);
}

/* ------------------------------------------------------------------ */
/* builtin_len_frozenset: len(frozenset([1,2,3])) = 3                  */
/* Regression: pydos_builtin_len had no PYDT_FROZENSET case            */
/* ------------------------------------------------------------------ */

TEST(builtin_len_frozenset)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;
    PyDosObj far *elems[3];
    PyDosObj far *fs;

    elems[0] = pydos_obj_new_int(10L);
    elems[1] = pydos_obj_new_int(20L);
    elems[2] = pydos_obj_new_int(30L);
    fs = pydos_frozenset_new(elems, 3);
    ASSERT_NOT_NULL(fs);

    argv[0] = fs;
    result = pydos_builtin_len(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 3L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(fs);
    PYDOS_DECREF(elems[0]);
    PYDOS_DECREF(elems[1]);
    PYDOS_DECREF(elems[2]);
}

/* ------------------------------------------------------------------ */
/* builtin_len_bytearray: len(bytearray(5)) = 5                       */
/* Regression: pydos_builtin_len had no PYDT_BYTEARRAY case            */
/* ------------------------------------------------------------------ */

TEST(builtin_len_bytearray)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;
    PyDosObj far *ba;

    ba = pydos_bytearray_new_zeroed(5);
    ASSERT_NOT_NULL(ba);

    argv[0] = ba;
    result = pydos_builtin_len(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 5L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(ba);
}

/* ------------------------------------------------------------------ */
/* builtin_type_int: type(42) returns string "int"                     */
/* ------------------------------------------------------------------ */

TEST(builtin_type_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_type(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "int");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_type_str: type("x") returns string "str"                    */
/* ------------------------------------------------------------------ */

TEST(builtin_type_str)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"x", 1);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_type(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "str");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_int_from_str: int("123") = 123                              */
/* ------------------------------------------------------------------ */

TEST(builtin_int_from_str)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"123", 3);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_int_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 123L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_int_from_bool: int(True) = 1                                */
/* ------------------------------------------------------------------ */

TEST(builtin_int_from_bool)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_bool(1);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_int_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 1L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_str_from_int: str(42) -> "42"                               */
/* ------------------------------------------------------------------ */

TEST(builtin_str_from_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_str_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "42");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_bool_true: bool(1) = True (bool_val == 1)                   */
/* ------------------------------------------------------------------ */

TEST(builtin_bool_true)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(1L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_bool_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_BOOL);
    ASSERT_EQ(result->v.bool_val, 1);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_bool_false: bool(0) = False (bool_val == 0)                 */
/* ------------------------------------------------------------------ */

TEST(builtin_bool_false)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(0L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_bool_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_BOOL);
    ASSERT_EQ(result->v.bool_val, 0);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_abs_pos: abs(5) = 5                                         */
/* ------------------------------------------------------------------ */

TEST(builtin_abs_pos)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(5L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_abs(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 5L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_abs_neg: abs(-5) = 5                                        */
/* ------------------------------------------------------------------ */

TEST(builtin_abs_neg)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(-5L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_abs(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 5L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_ord: ord("A") = 65                                          */
/* ------------------------------------------------------------------ */

TEST(builtin_ord)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"A", 1);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_ord(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 65L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_chr: chr(65) -> "A"                                         */
/* ------------------------------------------------------------------ */

TEST(builtin_chr)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(65L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_chr(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_EQ(result->v.str.len, 1);
    ASSERT_STR_EQ(result->v.str.data, "A");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_hex_positive: hex(255) -> "0xff"                            */
/* ------------------------------------------------------------------ */

TEST(builtin_hex_positive)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(255L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_hex(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "0xff");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_hex_zero: hex(0) -> "0x0"                                   */
/* ------------------------------------------------------------------ */

TEST(builtin_hex_zero)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(0L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_hex(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "0x0");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_hex_sixteen: hex(16) -> "0x10"                              */
/* ------------------------------------------------------------------ */

TEST(builtin_hex_sixteen)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(16L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_hex(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "0x10");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_str_from_neg: str(-7) -> "-7"                               */
/* ------------------------------------------------------------------ */

TEST(builtin_str_from_neg)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(-7L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_str_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "-7");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_str_from_zero: str(0) -> "0"                                */
/* ------------------------------------------------------------------ */

TEST(builtin_str_from_zero)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(0L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_str_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "0");

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_int_from_neg_str: int("-5") = -5                            */
/* ------------------------------------------------------------------ */

TEST(builtin_int_from_neg_str)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_str((const char far *)"-5", 2);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_int_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, -5L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_bool_negative: bool(-1) = True                              */
/* ------------------------------------------------------------------ */

TEST(builtin_bool_negative)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(-1L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_bool_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_BOOL);
    ASSERT_EQ(result->v.bool_val, 1);

    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

TEST(builtin_float_conv_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    result = pydos_builtin_float_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_FLOAT);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

TEST(builtin_repr_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    result = pydos_builtin_repr(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "42");
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

TEST(builtin_hash_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    result = pydos_builtin_hash(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

TEST(builtin_id_not_zero)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(1L);
    result = pydos_builtin_id(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_TRUE(result->v.int_val != 0L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

TEST(builtin_isinstance_int)
{
    PyDosObj far *argv[2];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int(42L);
    argv[1] = pydos_obj_new_int((long)PYDT_INT);
    result = pydos_builtin_isinstance(2, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_BOOL);
    ASSERT_EQ(result->v.bool_val, 1);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
    PYDOS_DECREF(argv[1]);
}

TEST(builtin_issubclass_same)
{
    PyDosObj far *argv[2];
    PyDosObj far *result;

    argv[0] = pydos_obj_new_int((long)PYDT_INT);
    argv[1] = pydos_obj_new_int((long)PYDT_INT);
    result = pydos_builtin_issubclass(2, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_BOOL);
    ASSERT_EQ(result->v.bool_val, 1);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
    PYDOS_DECREF(argv[1]);
}

/* Phase 4 builtins: list_conv, dict_conv, iter, next, super */

TEST(builtin_list_conv_empty)
{
    PyDosObj far *result;
    result = pydos_builtin_list_conv(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_LIST);
    ASSERT_EQ(result->v.list.len, 0);
    PYDOS_DECREF(result);
}

TEST(builtin_list_conv_from_list)
{
    PyDosObj far *argv[1];
    PyDosObj far *src;
    PyDosObj far *result;

    src = pydos_list_new(4);
    pydos_list_append(src, pydos_obj_new_int(1L));
    pydos_list_append(src, pydos_obj_new_int(2L));
    argv[0] = src;
    result = pydos_builtin_list_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_LIST);
    ASSERT_EQ(result->v.list.len, 2);
    PYDOS_DECREF(result);
    PYDOS_DECREF(src);
}

TEST(builtin_dict_conv_empty)
{
    PyDosObj far *result;
    result = pydos_builtin_dict_conv(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_DICT);
    ASSERT_EQ(pydos_dict_len(result), 0L);
    PYDOS_DECREF(result);
}

TEST(builtin_iter_list)
{
    PyDosObj far *argv[1];
    PyDosObj far *lst;
    PyDosObj far *result;

    lst = pydos_list_new(4);
    pydos_list_append(lst, pydos_obj_new_int(1L));
    argv[0] = lst;
    result = pydos_builtin_iter(1, argv);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);
    PYDOS_DECREF(lst);
}

TEST(builtin_next_iter)
{
    PyDosObj far *argv[1];
    PyDosObj far *lst;
    PyDosObj far *iter;
    PyDosObj far *result;

    lst = pydos_list_new(4);
    pydos_list_append(lst, pydos_obj_new_int(42L));
    iter = pydos_obj_get_iter(lst);
    ASSERT_NOT_NULL(iter);

    argv[0] = iter;
    result = pydos_builtin_next(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(iter);
    PYDOS_DECREF(lst);
}

TEST(builtin_super_returns_self)
{
    PyDosObj far *argv[1];
    PyDosObj far *obj;
    PyDosObj far *result;

    obj = pydos_obj_new_int(99L);
    argv[0] = obj;
    result = pydos_builtin_super(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 99L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_blt_tests(void)
{
    SUITE("pdos_blt");

    RUN(builtin_len_str);
    RUN(builtin_len_list);
    RUN(builtin_len_dict);
    RUN(builtin_len_frozenset);
    RUN(builtin_len_bytearray);
    RUN(builtin_type_int);
    RUN(builtin_type_str);
    RUN(builtin_int_from_str);
    RUN(builtin_int_from_bool);
    RUN(builtin_str_from_int);
    RUN(builtin_bool_true);
    RUN(builtin_bool_false);
    RUN(builtin_abs_pos);
    RUN(builtin_abs_neg);
    RUN(builtin_ord);
    RUN(builtin_chr);
    RUN(builtin_hex_positive);
    RUN(builtin_hex_zero);
    RUN(builtin_hex_sixteen);
    RUN(builtin_str_from_neg);
    RUN(builtin_str_from_zero);
    RUN(builtin_int_from_neg_str);
    RUN(builtin_bool_negative);
    RUN(builtin_float_conv_int);
    RUN(builtin_repr_int);
    RUN(builtin_hash_int);
    RUN(builtin_id_not_zero);
    RUN(builtin_isinstance_int);
    RUN(builtin_issubclass_same);

    /* Phase 4 builtins */
    RUN(builtin_list_conv_empty);
    RUN(builtin_list_conv_from_list);
    RUN(builtin_dict_conv_empty);
    RUN(builtin_iter_list);
    RUN(builtin_next_iter);
    RUN(builtin_super_returns_self);
}
