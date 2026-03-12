/*
 * t_bya.c - Unit tests for bytearray type (pdos_bya.h/.c)
 */

#include "testfw.h"
#include "../runtime/pdos_bya.h"
#include "../runtime/pdos_obj.h"

/* ---- new_empty ---- */
TEST(bya_new_empty)
{
    PyDosObj far *ba = pydos_bytearray_new(0);
    ASSERT_NOT_NULL(ba);
    ASSERT_EQ((int)ba->type, PYDT_BYTEARRAY);
    ASSERT_EQ(pydos_bytearray_len(ba), 0);
    PYDOS_DECREF(ba);
}

/* ---- new_with_cap ---- */
TEST(bya_new_with_cap)
{
    PyDosObj far *ba = pydos_bytearray_new(16);
    ASSERT_NOT_NULL(ba);
    ASSERT_EQ(pydos_bytearray_len(ba), 0);
    ASSERT_TRUE(ba->v.bytearray.cap >= 16);
    PYDOS_DECREF(ba);
}

/* ---- new_zeroed ---- */
TEST(bya_new_zeroed)
{
    PyDosObj far *ba = pydos_bytearray_new_zeroed(5);
    ASSERT_NOT_NULL(ba);
    ASSERT_EQ(pydos_bytearray_len(ba), 5);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 0), 0);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 4), 0);
    PYDOS_DECREF(ba);
}

/* ---- from_data ---- */
TEST(bya_from_data)
{
    unsigned char data[3];
    PyDosObj far *ba;
    data[0] = 0x41;
    data[1] = 0x42;
    data[2] = 0x43;
    ba = pydos_bytearray_from_data(
        (const unsigned char far *)data, 3);
    ASSERT_NOT_NULL(ba);
    ASSERT_EQ(pydos_bytearray_len(ba), 3);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 0), 0x41);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 1), 0x42);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 2), 0x43);
    PYDOS_DECREF(ba);
}

/* ---- append ---- */
TEST(bya_append)
{
    PyDosObj far *ba = pydos_bytearray_new(0);
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_append(ba, 10);
    pydos_bytearray_append(ba, 20);
    pydos_bytearray_append(ba, 30);
    ASSERT_EQ(pydos_bytearray_len(ba), 3);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 0), 10);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 1), 20);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 2), 30);
    PYDOS_DECREF(ba);
}

/* ---- extend ---- */
TEST(bya_extend)
{
    unsigned char ext[2];
    PyDosObj far *ba = pydos_bytearray_new(0);
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_append(ba, 1);
    ext[0] = 2;
    ext[1] = 3;
    pydos_bytearray_extend(ba, (const unsigned char far *)ext, 2);
    ASSERT_EQ(pydos_bytearray_len(ba), 3);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 1), 2);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 2), 3);
    PYDOS_DECREF(ba);
}

/* ---- setitem ---- */
TEST(bya_setitem)
{
    PyDosObj far *ba = pydos_bytearray_new_zeroed(3);
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_setitem(ba, 1, 0xFF);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 1), 0xFF);
    PYDOS_DECREF(ba);
}

/* ---- negative_index ---- */
TEST(bya_negative_index)
{
    PyDosObj far *ba = pydos_bytearray_new_zeroed(5);
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_setitem(ba, 4, 99);
    ASSERT_EQ(pydos_bytearray_getitem(ba, -1), 99);
    ASSERT_EQ(pydos_bytearray_getitem(ba, -5), 0);
    ASSERT_EQ(pydos_bytearray_getitem(ba, -6), -1); /* out of range */
    PYDOS_DECREF(ba);
}

/* ---- pop ---- */
TEST(bya_pop)
{
    int val;
    PyDosObj far *ba = pydos_bytearray_new(0);
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_append(ba, 10);
    pydos_bytearray_append(ba, 20);
    val = pydos_bytearray_pop(ba);
    ASSERT_EQ(val, 20);
    ASSERT_EQ(pydos_bytearray_len(ba), 1);
    val = pydos_bytearray_pop(ba);
    ASSERT_EQ(val, 10);
    ASSERT_EQ(pydos_bytearray_len(ba), 0);
    val = pydos_bytearray_pop(ba);
    ASSERT_EQ(val, -1); /* empty */
    PYDOS_DECREF(ba);
}

/* ---- clear ---- */
TEST(bya_clear)
{
    PyDosObj far *ba = pydos_bytearray_new_zeroed(10);
    ASSERT_NOT_NULL(ba);
    ASSERT_EQ(pydos_bytearray_len(ba), 10);
    pydos_bytearray_clear(ba);
    ASSERT_EQ(pydos_bytearray_len(ba), 0);
    PYDOS_DECREF(ba);
}

/* ---- truthy ---- */
TEST(bya_truthy)
{
    PyDosObj far *empty = pydos_bytearray_new(0);
    PyDosObj far *nonempty = pydos_bytearray_new_zeroed(1);
    ASSERT_NOT_NULL(empty);
    ASSERT_NOT_NULL(nonempty);
    ASSERT_FALSE(pydos_obj_is_truthy(empty));
    ASSERT_TRUE(pydos_obj_is_truthy(nonempty));
    PYDOS_DECREF(empty);
    PYDOS_DECREF(nonempty);
}

/* ---- equal ---- */
TEST(bya_equal)
{
    unsigned char data[3];
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *c;
    data[0] = 1; data[1] = 2; data[2] = 3;
    a = pydos_bytearray_from_data(
        (const unsigned char far *)data, 3);
    b = pydos_bytearray_from_data(
        (const unsigned char far *)data, 3);
    c = pydos_bytearray_new_zeroed(3);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);
    ASSERT_TRUE(pydos_obj_equal(a, b));
    ASSERT_FALSE(pydos_obj_equal(a, c));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(c);
}

/* ---- to_str ---- */
TEST(bya_to_str)
{
    PyDosObj far *ba = pydos_bytearray_new(0);
    PyDosObj far *s;
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_append(ba, 'A');
    pydos_bytearray_append(ba, 'B');
    s = pydos_obj_to_str(ba);
    ASSERT_NOT_NULL(s);
    /* Should contain "bytearray(b'" prefix */
    ASSERT_TRUE(s->v.str.len > 0);
    PYDOS_DECREF(s);
    PYDOS_DECREF(ba);
}

/* ---- type_name ---- */
TEST(bya_type_name)
{
    PyDosObj far *ba = pydos_bytearray_new(0);
    const char far *tn;
    ASSERT_NOT_NULL(ba);
    tn = pydos_obj_type_name(ba);
    ASSERT_STR_EQ(tn, "bytearray");
    PYDOS_DECREF(ba);
}

/* ---- conv_empty ---- */
TEST(bya_conv_empty)
{
    PyDosObj far *result = pydos_builtin_bytearray_conv(0,
        (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((int)result->type, PYDT_BYTEARRAY);
    ASSERT_EQ(pydos_bytearray_len(result), 0);
    PYDOS_DECREF(result);
}

/* ---- conv_int ---- */
TEST(bya_conv_int)
{
    PyDosObj far *n = pydos_obj_new_int(5);
    PyDosObj far * far *argv;
    PyDosObj far *result;
    ASSERT_NOT_NULL(n);
    argv = &n;
    result = pydos_builtin_bytearray_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(pydos_bytearray_len(result), 5);
    ASSERT_EQ(pydos_bytearray_getitem(result, 0), 0);
    ASSERT_EQ(pydos_bytearray_getitem(result, 4), 0);
    PYDOS_DECREF(result);
    PYDOS_DECREF(n);
}

/* ---- contains ---- */
TEST(bya_contains)
{
    PyDosObj far *ba = pydos_bytearray_new(0);
    PyDosObj far *val;
    ASSERT_NOT_NULL(ba);
    pydos_bytearray_append(ba, 42);
    pydos_bytearray_append(ba, 100);
    val = pydos_obj_new_int(42);
    ASSERT_TRUE(pydos_obj_contains(ba, val));
    PYDOS_DECREF(val);
    val = pydos_obj_new_int(99);
    ASSERT_FALSE(pydos_obj_contains(ba, val));
    PYDOS_DECREF(val);
    PYDOS_DECREF(ba);
}

void run_bya_tests(void)
{
    SUITE("pdos_bya");
    RUN(bya_new_empty);
    RUN(bya_new_with_cap);
    RUN(bya_new_zeroed);
    RUN(bya_from_data);
    RUN(bya_append);
    RUN(bya_extend);
    RUN(bya_setitem);
    RUN(bya_negative_index);
    RUN(bya_pop);
    RUN(bya_clear);
    RUN(bya_truthy);
    RUN(bya_equal);
    RUN(bya_to_str);
    RUN(bya_type_name);
    RUN(bya_conv_empty);
    RUN(bya_conv_int);
    RUN(bya_contains);
}
