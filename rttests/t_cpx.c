/*
 * t_cpx.c - Unit tests for complex number type (pdos_cpx)
 */

#include "testfw.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_cpx.h"
#include <string.h>

/* ---- Basic creation ---- */

TEST(cpx_new_zero)
{
    PyDosObj far *c = pydos_complex_new(0.0, 0.0);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ((int)c->type, (int)PYDT_COMPLEX);
    ASSERT_TRUE(c->v.complex_val.real == 0.0);
    ASSERT_TRUE(c->v.complex_val.imag == 0.0);
    PYDOS_DECREF(c);
}

TEST(cpx_new_values)
{
    PyDosObj far *c = pydos_complex_new(3.0, 4.0);
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE(c->v.complex_val.real == 3.0);
    ASSERT_TRUE(c->v.complex_val.imag == 4.0);
    PYDOS_DECREF(c);
}

/* ---- Arithmetic ---- */

TEST(cpx_add)
{
    PyDosObj far *a = pydos_complex_new(1.0, 2.0);
    PyDosObj far *b = pydos_complex_new(3.0, 4.0);
    PyDosObj far *r = pydos_complex_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == 4.0);
    ASSERT_TRUE(r->v.complex_val.imag == 6.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(cpx_sub)
{
    PyDosObj far *a = pydos_complex_new(5.0, 7.0);
    PyDosObj far *b = pydos_complex_new(2.0, 3.0);
    PyDosObj far *r = pydos_complex_sub(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == 3.0);
    ASSERT_TRUE(r->v.complex_val.imag == 4.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(cpx_mul)
{
    /* (1+2j)*(3+4j) = (1*3-2*4) + (1*4+2*3)j = -5+10j */
    PyDosObj far *a = pydos_complex_new(1.0, 2.0);
    PyDosObj far *b = pydos_complex_new(3.0, 4.0);
    PyDosObj far *r = pydos_complex_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == -5.0);
    ASSERT_TRUE(r->v.complex_val.imag == 10.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(cpx_div)
{
    /* (10+5j)/(2+1j) = (10*2+5*1)/(4+1) + (5*2-10*1)/(4+1)j = 25/5 + 0/5j = 5+0j */
    PyDosObj far *a = pydos_complex_new(10.0, 5.0);
    PyDosObj far *b = pydos_complex_new(2.0, 1.0);
    PyDosObj far *r = pydos_complex_div(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == 5.0);
    ASSERT_TRUE(r->v.complex_val.imag == 0.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(cpx_neg)
{
    PyDosObj far *a = pydos_complex_new(3.0, -4.0);
    PyDosObj far *r = pydos_complex_neg(a);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == -3.0);
    ASSERT_TRUE(r->v.complex_val.imag == 4.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(cpx_pos)
{
    PyDosObj far *a = pydos_complex_new(3.0, 4.0);
    PyDosObj far *r = pydos_complex_pos(a);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == 3.0);
    ASSERT_TRUE(r->v.complex_val.imag == 4.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

/* ---- abs/conjugate ---- */

TEST(cpx_abs)
{
    /* abs(3+4j) = 5.0 */
    PyDosObj far *a = pydos_complex_new(3.0, 4.0);
    PyDosObj far *r = pydos_complex_abs(a);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ((int)r->type, (int)PYDT_FLOAT);
    ASSERT_TRUE(r->v.float_val == 5.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

TEST(cpx_conjugate)
{
    PyDosObj far *a = pydos_complex_new(3.0, 4.0);
    PyDosObj far *r = pydos_complex_conjugate(a);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(r->v.complex_val.real == 3.0);
    ASSERT_TRUE(r->v.complex_val.imag == -4.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

/* ---- Equality / hash ---- */

TEST(cpx_equal)
{
    PyDosObj far *a = pydos_complex_new(1.0, 2.0);
    PyDosObj far *b = pydos_complex_new(1.0, 2.0);
    ASSERT_TRUE(pydos_obj_equal(a, b));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(cpx_not_equal)
{
    PyDosObj far *a = pydos_complex_new(1.0, 2.0);
    PyDosObj far *b = pydos_complex_new(1.0, 3.0);
    ASSERT_FALSE(pydos_obj_equal(a, b));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(cpx_hash)
{
    PyDosObj far *a = pydos_complex_new(1.0, 2.0);
    PyDosObj far *b = pydos_complex_new(1.0, 2.0);
    unsigned int ha = pydos_obj_hash(a);
    unsigned int hb = pydos_obj_hash(b);
    ASSERT_EQ((long)ha, (long)hb);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

/* ---- is_truthy ---- */

TEST(cpx_truthy_nonzero)
{
    PyDosObj far *c = pydos_complex_new(0.0, 1.0);
    ASSERT_TRUE(pydos_obj_is_truthy(c));
    PYDOS_DECREF(c);
}

TEST(cpx_truthy_zero)
{
    PyDosObj far *c = pydos_complex_new(0.0, 0.0);
    ASSERT_FALSE(pydos_obj_is_truthy(c));
    PYDOS_DECREF(c);
}

/* ---- to_str ---- */

TEST(cpx_to_str_pure_imag)
{
    PyDosObj far *c = pydos_complex_new(0.0, 3.0);
    PyDosObj far *s = pydos_obj_to_str(c);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "3j");
    PYDOS_DECREF(c);
    PYDOS_DECREF(s);
}

TEST(cpx_to_str_general)
{
    PyDosObj far *c = pydos_complex_new(1.0, 2.0);
    PyDosObj far *s = pydos_obj_to_str(c);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "(1+2j)");
    PYDOS_DECREF(c);
    PYDOS_DECREF(s);
}

TEST(cpx_to_str_neg_imag)
{
    PyDosObj far *c = pydos_complex_new(1.0, -2.0);
    PyDosObj far *s = pydos_obj_to_str(c);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "(1-2j)");
    PYDOS_DECREF(c);
    PYDOS_DECREF(s);
}

/* ---- Polymorphic dispatch (through pydos_obj_add etc.) ---- */

TEST(cpx_poly_add_int)
{
    /* (1+2j) + 3 = (4+2j) */
    PyDosObj far *a = pydos_complex_new(1.0, 2.0);
    PyDosObj far *b = pydos_obj_new_int(3L);
    PyDosObj far *r = pydos_obj_add(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ((int)r->type, (int)PYDT_COMPLEX);
    ASSERT_TRUE(r->v.complex_val.real == 4.0);
    ASSERT_TRUE(r->v.complex_val.imag == 2.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(cpx_poly_mul_float)
{
    /* (2+3j) * 2.0 = (4+6j) */
    PyDosObj far *a = pydos_complex_new(2.0, 3.0);
    PyDosObj far *b = pydos_obj_new_float(2.0);
    PyDosObj far *r = pydos_obj_mul(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ((int)r->type, (int)PYDT_COMPLEX);
    ASSERT_TRUE(r->v.complex_val.real == 4.0);
    ASSERT_TRUE(r->v.complex_val.imag == 6.0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ---- Builtin constructor ---- */

TEST(cpx_conv_empty)
{
    /* complex() -> 0+0j */
    PyDosObj far *r = pydos_builtin_complex_conv(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ((int)r->type, (int)PYDT_COMPLEX);
    ASSERT_TRUE(r->v.complex_val.real == 0.0);
    ASSERT_TRUE(r->v.complex_val.imag == 0.0);
    PYDOS_DECREF(r);
}

TEST(cpx_conv_two_args)
{
    /* complex(3, 4) -> 3+4j */
    PyDosObj far *args[2];
    PyDosObj far *r;
    args[0] = pydos_obj_new_int(3L);
    args[1] = pydos_obj_new_int(4L);
    r = pydos_builtin_complex_conv(2, args);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ((int)r->type, (int)PYDT_COMPLEX);
    ASSERT_TRUE(r->v.complex_val.real == 3.0);
    ASSERT_TRUE(r->v.complex_val.imag == 4.0);
    PYDOS_DECREF(args[0]);
    PYDOS_DECREF(args[1]);
    PYDOS_DECREF(r);
}

/* ---- type_name ---- */

TEST(cpx_type_name)
{
    PyDosObj far *c = pydos_complex_new(1.0, 2.0);
    const char far *name = pydos_obj_type_name(c);
    ASSERT_STR_EQ(name, "complex");
    PYDOS_DECREF(c);
}

void run_cpx_tests(void)
{
    SUITE("pdos_cpx");
    RUN(cpx_new_zero);
    RUN(cpx_new_values);
    RUN(cpx_add);
    RUN(cpx_sub);
    RUN(cpx_mul);
    RUN(cpx_div);
    RUN(cpx_neg);
    RUN(cpx_pos);
    RUN(cpx_abs);
    RUN(cpx_conjugate);
    RUN(cpx_equal);
    RUN(cpx_not_equal);
    RUN(cpx_hash);
    RUN(cpx_truthy_nonzero);
    RUN(cpx_truthy_zero);
    RUN(cpx_to_str_pure_imag);
    RUN(cpx_to_str_general);
    RUN(cpx_to_str_neg_imag);
    RUN(cpx_poly_add_int);
    RUN(cpx_poly_mul_float);
    RUN(cpx_conv_empty);
    RUN(cpx_conv_two_args);
    RUN(cpx_type_name);
}
