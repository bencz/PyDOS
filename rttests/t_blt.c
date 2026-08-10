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
#include "../runtime/pdos_rng.h"
#include "../runtime/pdos_vtb.h"
#include "../runtime/pdos_exc.h"

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
/* builtin_type_int: type(42) returns the singleton int class          */
/* ------------------------------------------------------------------ */

TEST(builtin_type_int)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;
    PyDosObj far *expected;

    argv[0] = pydos_obj_new_int(42L);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_type(1, argv);
    ASSERT_NOT_NULL(result);
    expected = pydos_builtin_type_object(PYDT_INT);
    ASSERT_TRUE(result == expected);
    ASSERT_EQ(result->type, PYDT_CLASS);
    ASSERT_STR_EQ(result->v.cls.name, "int");

    PYDOS_DECREF(expected);
    PYDOS_DECREF(result);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* builtin_type_str: type("x") returns the singleton str class        */
/* ------------------------------------------------------------------ */

TEST(builtin_type_str)
{
    PyDosObj far *argv[1];
    PyDosObj far *result;
    PyDosObj far *expected;

    argv[0] = pydos_obj_new_str((const char far *)"x", 1);
    ASSERT_NOT_NULL(argv[0]);

    result = pydos_builtin_type(1, argv);
    ASSERT_NOT_NULL(result);
    expected = pydos_builtin_type_object(PYDT_STR);
    ASSERT_TRUE(result == expected);
    ASSERT_EQ(result->type, PYDT_CLASS);
    ASSERT_STR_EQ(result->v.cls.name, "str");

    PYDOS_DECREF(expected);
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

TEST(builtin_runtime_class_identity)
{
    PyDosObj far *base;
    PyDosObj far *derived;
    PyDosObj far *instance;
    PyDosObj far *argv[2];
    PyDosObj far *result;
    PyDosObj far *name;
    PyDosObj far *bases;
    PyDosObj far *instance_class;
    PyDosObj far *label;
    PyDosObj far *dict;
    PyDosObj far *object_class;
    PyDosObj far *root_object;

    base = pydos_class_new((const char far *)"Base",
                           (PyDosVTable far *)0);
    derived = pydos_class_new((const char far *)"Derived",
                              (PyDosVTable far *)0);
    ASSERT_NOT_NULL(base);
    ASSERT_NOT_NULL(derived);
    pydos_class_add_object_base(base);
    pydos_class_add_base(derived, base);
    ASSERT_TRUE(pydos_class_is_subclass(derived, base));
    ASSERT_TRUE(pydos_class_is_subclass(derived, derived));
    ASSERT_FALSE(pydos_class_is_subclass(base, derived));
    object_class = pydos_builtin_type_object(PYDT_INSTANCE);
    ASSERT_TRUE(pydos_class_is_subclass(base, object_class));
    ASSERT_TRUE(pydos_class_is_subclass(derived, object_class));

    root_object = pydos_builtin_object_conv(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(root_object);
    ASSERT_EQ(root_object->type, PYDT_INSTANCE);
    ASSERT_TRUE(root_object->v.instance.cls == object_class);

    instance = pydos_obj_alloc_type(PYDT_INSTANCE);
    ASSERT_NOT_NULL(instance);
    pydos_obj_set_vtable(instance, (PyDosVTable far *)0);
    pydos_obj_set_class(instance, derived);

    argv[0] = instance;
    result = pydos_builtin_type(1, argv);
    ASSERT_TRUE(result == derived);
    PYDOS_DECREF(result);

    instance_class = pydos_obj_get_attr(
        instance, (const char far *)"__class__");
    ASSERT_TRUE(instance_class == derived);
    PYDOS_DECREF(instance_class);

    name = pydos_obj_get_attr(derived, (const char far *)"__name__");
    ASSERT_NOT_NULL(name);
    ASSERT_STR_EQ(name->v.str.data, "Derived");
    PYDOS_DECREF(name);

    bases = pydos_obj_get_attr(derived, (const char far *)"__bases__");
    ASSERT_NOT_NULL(bases);
    ASSERT_EQ(bases->type, PYDT_TUPLE);
    ASSERT_EQ(bases->v.tuple.len, 1);
    ASSERT_TRUE(bases->v.tuple.items[0] == base);
    PYDOS_DECREF(bases);

    argv[0] = instance;
    argv[1] = derived;
    result = pydos_builtin_isinstance(2, argv);
    ASSERT_TRUE(result->v.bool_val);
    PYDOS_DECREF(result);

    argv[1] = base;
    result = pydos_builtin_isinstance(2, argv);
    ASSERT_TRUE(result->v.bool_val);
    PYDOS_DECREF(result);

    argv[0] = derived;
    argv[1] = base;
    result = pydos_builtin_issubclass(2, argv);
    ASSERT_TRUE(result->v.bool_val);
    PYDOS_DECREF(result);

    argv[0] = derived;
    result = pydos_builtin_callable(1, argv);
    ASSERT_TRUE(result->v.bool_val);
    PYDOS_DECREF(result);

    label = pydos_obj_new_str((const char far *)"base", 4);
    pydos_obj_set_attr(base, (const char far *)"label", label);
    PYDOS_DECREF(label);
    label = pydos_obj_get_attr(instance, (const char far *)"label");
    ASSERT_NOT_NULL(label);
    ASSERT_STR_EQ(label->v.str.data, "base");
    PYDOS_DECREF(label);
    ASSERT_TRUE(pydos_obj_has_attr(derived, (const char far *)"label"));

    argv[0] = instance;
    dict = pydos_builtin_vars(1, argv);
    ASSERT_NOT_NULL(dict);
    ASSERT_EQ(dict->type, PYDT_DICT);
    ASSERT_TRUE(dict == instance->v.instance.attrs);
    PYDOS_DECREF(dict);

    PYDOS_DECREF(instance);
    PYDOS_DECREF(derived);
    PYDOS_DECREF(base);
    PYDOS_DECREF(root_object);
    PYDOS_DECREF(object_class);
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

TEST(builtin_tuple_conv)
{
    PyDosObj far *items;
    PyDosObj far *tuple;
    PyDosObj far *same;
    PyDosObj far *empty;
    PyDosObj far *argv[1];
    PyDosObj far *item;

    items = pydos_list_new(2);
    item = pydos_obj_new_int(1L);
    pydos_list_append(items, item);
    PYDOS_DECREF(item);
    item = pydos_obj_new_int(2L);
    pydos_list_append(items, item);
    PYDOS_DECREF(item);

    argv[0] = items;
    tuple = pydos_builtin_tuple_conv(1, argv);
    ASSERT_NOT_NULL(tuple);
    ASSERT_EQ(tuple->type, PYDT_TUPLE);
    ASSERT_EQ(tuple->v.tuple.len, 2);
    ASSERT_EQ(tuple->v.tuple.items[0]->v.int_val, 1L);
    ASSERT_EQ(tuple->v.tuple.items[1]->v.int_val, 2L);

    argv[0] = tuple;
    same = pydos_builtin_tuple_conv(1, argv);
    ASSERT_TRUE(same == tuple);
    PYDOS_DECREF(same);

    empty = pydos_builtin_tuple_conv(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(empty);
    ASSERT_EQ(empty->type, PYDT_TUPLE);
    ASSERT_EQ(empty->v.tuple.len, 0);

    PYDOS_DECREF(empty);
    PYDOS_DECREF(tuple);
    PYDOS_DECREF(items);
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

TEST(builtin_set_conv_from_list)
{
    PyDosObj far *argv[1];
    PyDosObj far *src;
    PyDosObj far *one;
    PyDosObj far *two;
    PyDosObj far *result;

    src = pydos_list_new(4);
    one = pydos_obj_new_int(1L);
    two = pydos_obj_new_int(2L);
    pydos_list_append(src, one);
    pydos_list_append(src, two);
    pydos_list_append(src, two);
    argv[0] = src;

    result = pydos_builtin_set_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_SET);
    ASSERT_EQ(pydos_dict_len(result), 2L);
    ASSERT_TRUE(pydos_dict_contains(result, one));
    ASSERT_TRUE(pydos_dict_contains(result, two));

    PYDOS_DECREF(result);
    PYDOS_DECREF(one);
    PYDOS_DECREF(two);
    PYDOS_DECREF(src);
}

TEST(builtin_frozenset_from_list_bridge)
{
    PyDosObj far *argv[1];
    PyDosObj far *src;
    PyDosObj far *item;
    PyDosObj far *result;

    src = pydos_list_new(4);
    item = pydos_obj_new_int(7L);
    pydos_list_append(src, item);
    pydos_list_append(src, item);
    argv[0] = src;

    result = pydos_builtin_frozenset_from_list(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_FROZENSET);
    ASSERT_EQ(pydos_frozenset_len(result), 1);
    ASSERT_TRUE(pydos_frozenset_contains(result, item));

    PYDOS_DECREF(result);
    PYDOS_DECREF(item);
    PYDOS_DECREF(src);
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

TEST(builtin_super_rejects_one_argument)
{
    PyDosObj far *argv[1];
    PyDosObj far *obj;
    PyDosObj far *result;

    obj = pydos_obj_new_int(99L);
    argv[0] = obj;
    result = pydos_builtin_super(1, argv);
    ASSERT_NULL(result);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_TYPE_ERROR));
    pydos_exc_clear();
    PYDOS_DECREF(obj);
}

TEST(builtin_range_primitives)
{
    PyDosObj far *args[3];
    PyDosObj far *range;
    PyDosObj far *result;
    PyDosObj far *item;
    PyDosObj far *same;
    PyDosObj far *slice;

    args[0] = pydos_obj_new_int(2L);
    args[1] = pydos_obj_new_int(12L);
    args[2] = pydos_obj_new_int(3L);
    range = pydos_builtin_range(3, args);
    ASSERT_NOT_NULL(range);
    ASSERT_EQ(range->type, PYDT_RANGE);

    result = pydos_range_len(range);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 4L);
    PYDOS_DECREF(result);

    result = pydos_range_getitem(range, -1L);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 11L);
    PYDOS_DECREF(result);

    item = pydos_obj_new_float(5.0);
    ASSERT_TRUE(pydos_range_contains(range, item));
    PYDOS_DECREF(item);
    item = pydos_obj_new_int(6L);
    ASSERT_FALSE(pydos_range_contains(range, item));
    PYDOS_DECREF(item);

    same = pydos_range_new(2L, 13L, 3L);
    ASSERT_NOT_NULL(same);
    ASSERT_TRUE(pydos_range_equal(range, same));
    PYDOS_DECREF(same);

    slice = pydos_range_slice(range, 1L, 3L, 1L);
    ASSERT_NOT_NULL(slice);
    ASSERT_EQ(slice->v.range.start, 5L);
    ASSERT_EQ(slice->v.range.stop, 11L);
    ASSERT_EQ(slice->v.range.step, 3L);
    PYDOS_DECREF(slice);

    PYDOS_DECREF(range);
    PYDOS_DECREF(args[0]);
    PYDOS_DECREF(args[1]);
    PYDOS_DECREF(args[2]);
}

TEST(builtin_reflection_primitives)
{
    PyDosObj far *instance;
    PyDosObj far *name;
    PyDosObj far *value;
    PyDosObj far *fallback;
    PyDosObj far *args[3];
    PyDosObj far *result;
    PyDosObj far *function;

    instance = pydos_obj_alloc_type(PYDT_INSTANCE);
    ASSERT_NOT_NULL(instance);
    instance->v.instance.attrs = (PyDosObj far *)0;
    instance->v.instance.vtable = (PyDosVTable far *)0;
    instance->v.instance.cls = (PyDosObj far *)0;
    name = pydos_obj_new_str((const char far *)"answer", 6);
    value = pydos_obj_new_int(42L);
    fallback = pydos_obj_new_int(99L);

    args[0] = instance;
    args[1] = name;
    args[2] = value;
    result = pydos_builtin_setattr(3, args);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);
    ASSERT_TRUE(pydos_obj_has_attr(instance, (const char far *)"answer"));

    result = pydos_builtin_getattr(2, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 42L);
    PYDOS_DECREF(result);

    result = pydos_builtin_delattr(2, args);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);
    ASSERT_FALSE(pydos_obj_has_attr(instance, (const char far *)"answer"));

    args[2] = fallback;
    result = pydos_builtin_getattr(3, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 99L);
    PYDOS_DECREF(result);

    function = pydos_func_new((void (far *)(void))0,
                              (const char far *)"function");
    args[0] = function;
    result = pydos_builtin_callable(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result->v.bool_val);
    PYDOS_DECREF(result);

    PYDOS_DECREF(function);
    PYDOS_DECREF(fallback);
    PYDOS_DECREF(value);
    PYDOS_DECREF(name);
    PYDOS_DECREF(instance);
}

TEST(builtin_bind_method_primitive)
{
    PyDosObj far *args[2];
    PyDosObj far *function;
    PyDosObj far *owner;
    PyDosObj far *bound;

    function = pydos_func_new((void (far *)(void))0,
                              (const char far *)"wrapped");
    owner = pydos_obj_new_int(7L);
    ASSERT_NOT_NULL(function);
    ASSERT_NOT_NULL(owner);

    args[0] = function;
    args[1] = owner;
    bound = pydos_builtin_bind_method(2, args);
    ASSERT_NOT_NULL(bound);
    ASSERT_EQ(bound->type, PYDT_FUNCTION);
    ASSERT_TRUE(bound->v.func.bound_self == owner);
    ASSERT_TRUE(bound->v.func.code_ref == function->v.func.code_ref);

    PYDOS_DECREF(bound);
    PYDOS_DECREF(owner);
    PYDOS_DECREF(function);
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
    RUN(builtin_runtime_class_identity);

    /* Phase 4 builtins */
    RUN(builtin_list_conv_empty);
    RUN(builtin_list_conv_from_list);
    RUN(builtin_tuple_conv);
    RUN(builtin_dict_conv_empty);
    RUN(builtin_set_conv_from_list);
    RUN(builtin_frozenset_from_list_bridge);
    RUN(builtin_iter_list);
    RUN(builtin_next_iter);
    RUN(builtin_super_rejects_one_argument);
    RUN(builtin_range_primitives);
    RUN(builtin_reflection_primitives);
    RUN(builtin_bind_method_primitive);
}
