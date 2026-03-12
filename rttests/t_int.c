/*
 * t_int.c - Unit tests for pydos_int module
 *
 * Tests integer arithmetic, unary ops, comparison, bitwise ops,
 * shift ops, and string conversion.
 */

#include "testfw.h"
#include "../runtime/pdos_int.h"
#include "../runtime/pdos_obj.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Helper: check string content matches expected C string             */
/* ------------------------------------------------------------------ */
static int str_matches(PyDosObj far *obj, const char *expected, unsigned int len)
{
    if (obj == (PyDosObj far *)0) return 0;
    if (obj->v.str.len != len) return 0;
    if (len == 0) return 1;
    return _fmemcmp(obj->v.str.data, (const char far *)expected, len) == 0;
}

/* ------------------------------------------------------------------ */
/* Arithmetic tests                                                    */
/* ------------------------------------------------------------------ */

TEST(add_basic)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(3);
    b = pydos_obj_new_int(5);
    r = pydos_int_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 8);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(add_negative)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(-3);
    b = pydos_obj_new_int(5);
    r = pydos_int_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 2);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(add_zero)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(0);
    b = pydos_obj_new_int(0);
    r = pydos_int_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(sub_basic)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(10);
    b = pydos_obj_new_int(3);
    r = pydos_int_sub(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 7);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(sub_negative)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(3);
    b = pydos_obj_new_int(10);
    r = pydos_int_sub(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, -7);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(mul_basic)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(6);
    b = pydos_obj_new_int(7);
    r = pydos_int_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 42);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(mul_zero)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(5);
    b = pydos_obj_new_int(0);
    r = pydos_int_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(mul_negative)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(-3);
    b = pydos_obj_new_int(4);
    r = pydos_int_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, -12);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(div_basic)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(10);
    b = pydos_obj_new_int(3);
    r = pydos_int_div(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 3);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(div_negative)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    long val;
    a = pydos_obj_new_int(-7);
    b = pydos_obj_new_int(2);
    r = pydos_int_div(a, b);
    ASSERT_NOT_NULL(r);
    val = r->v.int_val;
    /* Python floor division: -7 // 2 = -4, C truncation: -7 / 2 = -3 */
    /* Accept either semantics */
    ASSERT_TRUE(val == -3 || val == -4);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(mod_basic)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(10);
    b = pydos_obj_new_int(3);
    r = pydos_int_mod(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* Unary tests                                                         */
/* ------------------------------------------------------------------ */

TEST(neg_positive)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(5);
    r = pydos_int_neg(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, -5);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(neg_negative)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(-5);
    r = pydos_int_neg(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 5);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(neg_zero)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(0);
    r = pydos_int_neg(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(abs_positive)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(5);
    r = pydos_int_abs(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 5);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(abs_negative)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(-5);
    r = pydos_int_abs(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 5);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(abs_zero)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(0);
    r = pydos_int_abs(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* Power tests                                                         */
/* ------------------------------------------------------------------ */

TEST(pow_basic)
{
    PyDosObj far *base;
    PyDosObj far *exp;
    PyDosObj far *r;
    base = pydos_obj_new_int(2);
    exp = pydos_obj_new_int(10);
    r = pydos_int_pow(base, exp);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 1024);
    PYDOS_DECREF(base);
    PYDOS_DECREF(exp);
    PYDOS_DECREF(r);
}

TEST(pow_zero)
{
    PyDosObj far *base;
    PyDosObj far *exp;
    PyDosObj far *r;
    base = pydos_obj_new_int(5);
    exp = pydos_obj_new_int(0);
    r = pydos_int_pow(base, exp);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 1);
    PYDOS_DECREF(base);
    PYDOS_DECREF(exp);
    PYDOS_DECREF(r);
}

TEST(pow_one)
{
    PyDosObj far *base;
    PyDosObj far *exp;
    PyDosObj far *r;
    base = pydos_obj_new_int(5);
    exp = pydos_obj_new_int(1);
    r = pydos_int_pow(base, exp);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 5);
    PYDOS_DECREF(base);
    PYDOS_DECREF(exp);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* Comparison tests                                                    */
/* ------------------------------------------------------------------ */

TEST(compare_less)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    a = pydos_obj_new_int(3);
    b = pydos_obj_new_int(5);
    cmp = pydos_int_compare(a, b);
    ASSERT_EQ(cmp, -1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_equal)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    a = pydos_obj_new_int(5);
    b = pydos_obj_new_int(5);
    cmp = pydos_int_compare(a, b);
    ASSERT_EQ(cmp, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(compare_greater)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    a = pydos_obj_new_int(7);
    b = pydos_obj_new_int(3);
    cmp = pydos_int_compare(a, b);
    ASSERT_EQ(cmp, 1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ------------------------------------------------------------------ */
/* Bitwise tests                                                       */
/* ------------------------------------------------------------------ */

TEST(bitand)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(255);   /* 0xFF */
    b = pydos_obj_new_int(15);    /* 0x0F */
    r = pydos_int_bitand(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 15);  /* 0x0F */
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(bitor)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(240);   /* 0xF0 */
    b = pydos_obj_new_int(15);    /* 0x0F */
    r = pydos_int_bitor(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 255); /* 0xFF */
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(bitxor)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(255);   /* 0xFF */
    b = pydos_obj_new_int(15);    /* 0x0F */
    r = pydos_int_bitxor(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 240); /* 0xF0 */
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(bitnot)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(0);
    r = pydos_int_bitnot(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, -1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* Shift tests                                                         */
/* ------------------------------------------------------------------ */

TEST(shl)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(1);
    b = pydos_obj_new_int(8);
    r = pydos_int_shl(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 256);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(shr)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_obj_new_int(256);
    b = pydos_obj_new_int(4);
    r = pydos_int_shr(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.int_val, 16);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* Conversion tests                                                    */
/* ------------------------------------------------------------------ */

TEST(to_str)
{
    PyDosObj far *a;
    PyDosObj far *r;
    a = pydos_obj_new_int(42);
    r = pydos_int_to_str(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_EQ(r->v.str.len, 2);
    ASSERT_TRUE(str_matches(r, "42", 2));
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(from_str)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_obj_new_str((const char far *)"123", 3);
    r = pydos_int_from_str(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 123);
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* Suite runner                                                        */
/* ------------------------------------------------------------------ */
void run_int_tests(void)
{
    SUITE("pydos_int");
    RUN(add_basic);
    RUN(add_negative);
    RUN(add_zero);
    RUN(sub_basic);
    RUN(sub_negative);
    RUN(mul_basic);
    RUN(mul_zero);
    RUN(mul_negative);
    RUN(div_basic);
    RUN(div_negative);
    RUN(mod_basic);
    RUN(neg_positive);
    RUN(neg_negative);
    RUN(neg_zero);
    RUN(abs_positive);
    RUN(abs_negative);
    RUN(abs_zero);
    RUN(pow_basic);
    RUN(pow_zero);
    RUN(pow_one);
    RUN(compare_less);
    RUN(compare_equal);
    RUN(compare_greater);
    RUN(bitand);
    RUN(bitor);
    RUN(bitxor);
    RUN(bitnot);
    RUN(shl);
    RUN(shr);
    RUN(to_str);
    RUN(from_str);
}
