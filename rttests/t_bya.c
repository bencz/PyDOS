/*
 * t_bya.c - Unit tests for bytearray type (pdos_bya.h/.c)
 */

#include "testfw.h"
#include "../runtime/pdos_bya.h"
#include "../runtime/pdos_byt.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_lst.h"

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

TEST(bytes_from_data)
{
    unsigned char data[3];
    PyDosObj far *value;
    data[0] = 0;
    data[1] = 65;
    data[2] = 255;
    value = pydos_bytes_new((const unsigned char far *)data, 3);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ((int)value->type, PYDT_BYTES);
    ASSERT_EQ((int)value->v.str.len, 3);
    ASSERT_EQ(pydos_bytes_getitem(value, 0), 0);
    ASSERT_EQ(pydos_bytes_getitem(value, 1), 65);
    ASSERT_EQ(pydos_bytes_getitem(value, -1), 255);
    PYDOS_DECREF(value);
}

TEST(bytes_value_semantics)
{
    unsigned char data[2];
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *text;
    data[0] = 'A';
    data[1] = 255;
    a = pydos_bytes_new((const unsigned char far *)data, 2);
    b = pydos_bytes_new((const unsigned char far *)data, 2);
    ASSERT_TRUE(pydos_obj_equal(a, b));
    ASSERT_EQ(pydos_obj_hash(a), pydos_obj_hash(b));
    ASSERT_TRUE(pydos_obj_is_truthy(a));
    text = pydos_obj_to_str(a);
    ASSERT_NOT_NULL(text);
    ASSERT_STR_EQ(text->v.str.data, "b'A\\xff'");
    PYDOS_DECREF(text);
    PYDOS_DECREF(b);
    PYDOS_DECREF(a);
}

TEST(bytes_sequence_ops)
{
    unsigned char left_data[2];
    unsigned char right_data[1];
    PyDosObj far *left;
    PyDosObj far *right;
    PyDosObj far *joined;
    PyDosObj far *repeated;
    PyDosObj far *slice;
    left_data[0] = 1;
    left_data[1] = 2;
    right_data[0] = 3;
    left = pydos_bytes_new((const unsigned char far *)left_data, 2);
    right = pydos_bytes_new((const unsigned char far *)right_data, 1);
    joined = pydos_bytes_concat(left, right);
    repeated = pydos_bytes_repeat(right, 3);
    slice = pydos_bytes_slice(joined, 1, 3, 1);
    ASSERT_EQ((int)joined->v.str.len, 3);
    ASSERT_EQ(pydos_bytes_getitem(joined, 2), 3);
    ASSERT_EQ((int)repeated->v.str.len, 3);
    ASSERT_EQ(pydos_bytes_getitem(repeated, 1), 3);
    ASSERT_EQ((int)slice->v.str.len, 2);
    ASSERT_EQ(pydos_bytes_getitem(slice, 0), 2);
    ASSERT_EQ(pydos_bytes_getitem(slice, 1), 3);
    PYDOS_DECREF(slice);
    PYDOS_DECREF(repeated);
    PYDOS_DECREF(joined);
    PYDOS_DECREF(right);
    PYDOS_DECREF(left);
}

TEST(bytes_constructor_list)
{
    PyDosObj far *items;
    PyDosObj far *item;
    PyDosObj far *result;
    PyDosObj far *argv[1];
    items = pydos_list_new(3);
    item = pydos_obj_new_int(0); pydos_list_append(items, item); PYDOS_DECREF(item);
    item = pydos_obj_new_int(65); pydos_list_append(items, item); PYDOS_DECREF(item);
    item = pydos_obj_new_int(255); pydos_list_append(items, item); PYDOS_DECREF(item);
    argv[0] = items;
    result = pydos_builtin_bytes_conv(1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((int)result->type, PYDT_BYTES);
    ASSERT_EQ(pydos_bytes_getitem(result, 0), 0);
    ASSERT_EQ(pydos_bytes_getitem(result, 1), 65);
    ASSERT_EQ(pydos_bytes_getitem(result, 2), 255);
    PYDOS_DECREF(result);
    PYDOS_DECREF(items);
}

TEST(bya_insert_and_pop_at)
{
    PyDosObj far *ba;
    int value;

    ba = pydos_bytearray_new(0);
    pydos_bytearray_append(ba, 10);
    pydos_bytearray_append(ba, 30);
    pydos_bytearray_insert(ba, 1L, 20);
    pydos_bytearray_insert(ba, -1L, 25);

    ASSERT_EQ(pydos_bytearray_len(ba), 4);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 0), 10);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 1), 20);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 2), 25);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 3), 30);

    value = pydos_bytearray_pop_at(ba, 1L);
    ASSERT_EQ(value, 20);
    ASSERT_EQ(pydos_bytearray_len(ba), 3);
    ASSERT_EQ(pydos_bytearray_getitem(ba, 1), 25);
    ASSERT_EQ(pydos_bytearray_pop_at(ba, -1L), 30);
    ASSERT_EQ(pydos_bytearray_pop_at(ba, 99L), -1);
    PYDOS_DECREF(ba);
}

TEST(bya_sequence_ops)
{
    unsigned char left_data[2];
    unsigned char right_data[1];
    PyDosObj far *left;
    PyDosObj far *right;
    PyDosObj far *joined;
    PyDosObj far *repeated;
    PyDosObj far *slice;

    left_data[0] = 1;
    left_data[1] = 2;
    right_data[0] = 3;
    left = pydos_bytearray_from_data(
        (const unsigned char far *)left_data, 2);
    right = pydos_bytearray_from_data(
        (const unsigned char far *)right_data, 1);
    joined = pydos_bytearray_concat(left, right);
    repeated = pydos_bytearray_repeat(left, 3L);
    slice = pydos_bytearray_slice(joined, 1L, 3L, 1L);

    ASSERT_NOT_NULL(joined);
    ASSERT_NOT_NULL(repeated);
    ASSERT_NOT_NULL(slice);
    ASSERT_EQ(pydos_bytearray_len(joined), 3);
    ASSERT_EQ(pydos_bytearray_getitem(joined, 2), 3);
    ASSERT_EQ(pydos_bytearray_len(repeated), 6);
    ASSERT_EQ(pydos_bytearray_getitem(repeated, 4), 1);
    ASSERT_EQ(pydos_bytearray_len(slice), 2);
    ASSERT_EQ(pydos_bytearray_getitem(slice, 0), 2);
    ASSERT_EQ(pydos_bytearray_getitem(slice, 1), 3);

    PYDOS_DECREF(slice);
    PYDOS_DECREF(repeated);
    PYDOS_DECREF(joined);
    PYDOS_DECREF(right);
    PYDOS_DECREF(left);
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
    RUN(bytes_from_data);
    RUN(bytes_value_semantics);
    RUN(bytes_sequence_ops);
    RUN(bytes_constructor_list);
    RUN(bya_insert_and_pop_at);
    RUN(bya_sequence_ops);
}
