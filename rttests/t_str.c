/*
 * t_str.c - Unit tests for pydos_str module
 *
 * Tests string creation, concatenation, repetition, slicing,
 * indexing, find, comparison, hashing, and formatting.
 */

#include "testfw.h"
#include "../runtime/pdos_str.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_lst.h"
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
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(str_new)
{
    PyDosObj far *s;
    s = pydos_str_new((const char far *)"Hello", 5);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    ASSERT_EQ(s->v.str.len, 5);
    ASSERT_TRUE(str_matches(s, "Hello", 5));
    PYDOS_DECREF(s);
}

TEST(str_from_cstr)
{
    PyDosObj far *s;
    s = pydos_str_from_cstr("World");
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    ASSERT_EQ(s->v.str.len, 5);
    ASSERT_TRUE(str_matches(s, "World", 5));
    PYDOS_DECREF(s);
}

TEST(str_empty)
{
    PyDosObj far *s;
    s = pydos_str_from_cstr("");
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);
    ASSERT_EQ(s->v.str.len, 0);
    PYDOS_DECREF(s);
}

TEST(str_concat)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_str_from_cstr("Hello");
    b = pydos_str_from_cstr(" World");
    r = pydos_str_concat(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 11);
    ASSERT_TRUE(str_matches(r, "Hello World", 11));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(str_concat_empty)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;
    a = pydos_str_from_cstr("abc");
    b = pydos_str_from_cstr("");
    r = pydos_str_concat(a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 3);
    ASSERT_TRUE(str_matches(r, "abc", 3));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

TEST(str_repeat)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_str_from_cstr("ab");
    r = pydos_str_repeat(s, 3);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 6);
    ASSERT_TRUE(str_matches(r, "ababab", 6));
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

TEST(str_repeat_zero)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_str_from_cstr("ab");
    r = pydos_str_repeat(s, 0);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 0);
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

TEST(str_slice_basic)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_str_from_cstr("Hello");
    r = pydos_str_slice(s, 1, 4, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 3);
    ASSERT_TRUE(str_matches(r, "ell", 3));
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

TEST(str_slice_step)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_str_from_cstr("abcdef");
    r = pydos_str_slice(s, 0, 6, 2);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 3);
    ASSERT_TRUE(str_matches(r, "ace", 3));
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

TEST(str_index)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_str_from_cstr("Hello");
    r = pydos_str_index(s, 1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 1);
    ASSERT_TRUE(str_matches(r, "e", 1));
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

TEST(str_index_negative)
{
    PyDosObj far *s;
    PyDosObj far *r;
    s = pydos_str_from_cstr("Hello");
    r = pydos_str_index(s, -1);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 1);
    ASSERT_TRUE(str_matches(r, "o", 1));
    PYDOS_DECREF(s);
    PYDOS_DECREF(r);
}

TEST(str_find_exists)
{
    PyDosObj far *s;
    PyDosObj far *sub;
    long idx;
    s = pydos_str_from_cstr("Hello");
    sub = pydos_str_from_cstr("ll");
    idx = pydos_str_find(s, sub);
    ASSERT_EQ(idx, 2);
    PYDOS_DECREF(s);
    PYDOS_DECREF(sub);
}

TEST(str_find_missing)
{
    PyDosObj far *s;
    PyDosObj far *sub;
    long idx;
    s = pydos_str_from_cstr("Hello");
    sub = pydos_str_from_cstr("xyz");
    idx = pydos_str_find(s, sub);
    ASSERT_EQ(idx, -1);
    PYDOS_DECREF(s);
    PYDOS_DECREF(sub);
}

TEST(str_find_empty)
{
    PyDosObj far *s;
    PyDosObj far *sub;
    long idx;
    s = pydos_str_from_cstr("Hello");
    sub = pydos_str_from_cstr("");
    idx = pydos_str_find(s, sub);
    ASSERT_EQ(idx, 0);
    PYDOS_DECREF(s);
    PYDOS_DECREF(sub);
}

TEST(str_len)
{
    PyDosObj far *s;
    long len;
    s = pydos_str_from_cstr("Hello");
    len = pydos_str_len(s);
    ASSERT_EQ(len, 5);
    PYDOS_DECREF(s);
}

TEST(str_equal_same)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int eq;
    a = pydos_str_from_cstr("Hello");
    b = pydos_str_from_cstr("Hello");
    eq = pydos_str_equal(a, b);
    ASSERT_EQ(eq, 1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(str_equal_diff)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int eq;
    a = pydos_str_from_cstr("Hello");
    b = pydos_str_from_cstr("World");
    eq = pydos_str_equal(a, b);
    ASSERT_EQ(eq, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(str_compare_less)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    a = pydos_str_from_cstr("abc");
    b = pydos_str_from_cstr("abd");
    cmp = pydos_str_compare(a, b);
    ASSERT_EQ(cmp, -1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(str_compare_greater)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    a = pydos_str_from_cstr("abd");
    b = pydos_str_from_cstr("abc");
    cmp = pydos_str_compare(a, b);
    ASSERT_EQ(cmp, 1);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(str_compare_equal)
{
    PyDosObj far *a;
    PyDosObj far *b;
    int cmp;
    a = pydos_str_from_cstr("abc");
    b = pydos_str_from_cstr("abc");
    cmp = pydos_str_compare(a, b);
    ASSERT_EQ(cmp, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(str_hash_consistent)
{
    PyDosObj far *s;
    unsigned int h1;
    unsigned int h2;
    s = pydos_str_from_cstr("Hello");
    h1 = pydos_str_hash(s);
    h2 = pydos_str_hash(s);
    ASSERT_EQ(h1, h2);
    PYDOS_DECREF(s);
}

TEST(str_hash_different)
{
    PyDosObj far *a;
    PyDosObj far *b;
    unsigned int h1;
    unsigned int h2;
    a = pydos_str_from_cstr("Hello");
    b = pydos_str_from_cstr("World");
    h1 = pydos_str_hash(a);
    h2 = pydos_str_hash(b);
    ASSERT_NEQ(h1, h2);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
}

TEST(str_format_int_pos)
{
    PyDosObj far *r;
    r = pydos_str_format_int(42);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_EQ(r->v.str.len, 2);
    ASSERT_TRUE(str_matches(r, "42", 2));
    PYDOS_DECREF(r);
}

TEST(str_format_int_neg)
{
    PyDosObj far *r;
    r = pydos_str_format_int(-7);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_EQ(r->v.str.len, 2);
    ASSERT_TRUE(str_matches(r, "-7", 2));
    PYDOS_DECREF(r);
}

TEST(str_format_int_zero)
{
    PyDosObj far *r;
    r = pydos_str_format_int(0);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_EQ(r->v.str.len, 1);
    ASSERT_TRUE(str_matches(r, "0", 1));
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* str_iter_chars: iterate "abc" yields "a", "b", "c" one at a time    */
/* ------------------------------------------------------------------ */

TEST(str_iter_chars)
{
    PyDosObj far *s;
    PyDosObj far *iter;
    PyDosObj far *ch;

    s = pydos_str_from_cstr("abc");
    iter = pydos_obj_get_iter(s);
    ASSERT_NOT_NULL(iter);

    ch = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(ch);
    ASSERT_TRUE(str_matches(ch, "a", 1));
    PYDOS_DECREF(ch);

    ch = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(ch);
    ASSERT_TRUE(str_matches(ch, "b", 1));
    PYDOS_DECREF(ch);

    ch = pydos_obj_iter_next(iter);
    ASSERT_NOT_NULL(ch);
    ASSERT_TRUE(str_matches(ch, "c", 1));
    PYDOS_DECREF(ch);

    ch = pydos_obj_iter_next(iter);
    ASSERT_NULL(ch);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* str_iter_empty: iterate "" yields nothing                            */
/* ------------------------------------------------------------------ */

TEST(str_iter_empty)
{
    PyDosObj far *s;
    PyDosObj far *iter;
    PyDosObj far *ch;

    s = pydos_str_from_cstr("");
    iter = pydos_obj_get_iter(s);
    ASSERT_NOT_NULL(iter);

    ch = pydos_obj_iter_next(iter);
    ASSERT_NULL(ch);

    PYDOS_DECREF(iter);
    PYDOS_DECREF(s);
}

/* ------------------------------------------------------------------ */
/* Suite runner                                                        */
/* ------------------------------------------------------------------ */
void run_str_tests(void)
{
    SUITE("pydos_str");
    RUN(str_new);
    RUN(str_from_cstr);
    RUN(str_empty);
    RUN(str_concat);
    RUN(str_concat_empty);
    RUN(str_repeat);
    RUN(str_repeat_zero);
    RUN(str_slice_basic);
    RUN(str_slice_step);
    RUN(str_index);
    RUN(str_index_negative);
    RUN(str_find_exists);
    RUN(str_find_missing);
    RUN(str_find_empty);
    RUN(str_len);
    RUN(str_equal_same);
    RUN(str_equal_diff);
    RUN(str_compare_less);
    RUN(str_compare_greater);
    RUN(str_compare_equal);
    RUN(str_hash_consistent);
    RUN(str_hash_different);
    RUN(str_format_int_pos);
    RUN(str_format_int_neg);
    RUN(str_format_int_zero);
    RUN(str_iter_chars);
    RUN(str_iter_empty);
}
