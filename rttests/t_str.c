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
/* String method tests                                                 */
/* ------------------------------------------------------------------ */

TEST(str_upper)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *r = pydos_str_upper(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_STR_EQ(r->v.str.data, "HELLO");
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_lower)
{
    PyDosObj far *s = pydos_str_from_cstr("HELLO");
    PyDosObj far *r = pydos_str_lower(s);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->v.str.data, "hello");
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_strip)
{
    PyDosObj far *s = pydos_str_from_cstr("  hi  ");
    PyDosObj far *r = pydos_str_strip(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 2);
    ASSERT_STR_EQ(r->v.str.data, "hi");
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_replace)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *old_s = pydos_str_from_cstr("l");
    PyDosObj far *new_s = pydos_str_from_cstr("r");
    PyDosObj far *r = pydos_str_replace_m(s, old_s, new_s);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->v.str.data, "herro");
    PYDOS_DECREF(r);
    PYDOS_DECREF(new_s);
    PYDOS_DECREF(old_s);
    PYDOS_DECREF(s);
}

TEST(str_split)
{
    PyDosObj far *s = pydos_str_from_cstr("a,b,c");
    PyDosObj far *sep = pydos_str_from_cstr(",");
    PyDosObj far *r = pydos_str_split_m(s, sep);
    PyDosObj far *item;
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_LIST);
    ASSERT_EQ(r->v.list.len, 3);
    item = pydos_list_get(r, 0L);
    ASSERT_STR_EQ(item->v.str.data, "a");
    PYDOS_DECREF(item);
    item = pydos_list_get(r, 2L);
    ASSERT_STR_EQ(item->v.str.data, "c");
    PYDOS_DECREF(item);
    PYDOS_DECREF(r);
    PYDOS_DECREF(sep);
    PYDOS_DECREF(s);
}

TEST(str_join)
{
    PyDosObj far *sep = pydos_str_from_cstr(",");
    PyDosObj far *lst = pydos_list_new(4);
    PyDosObj far *r;
    pydos_list_append(lst, pydos_str_from_cstr("a"));
    pydos_list_append(lst, pydos_str_from_cstr("b"));
    pydos_list_append(lst, pydos_str_from_cstr("c"));
    r = pydos_str_join_m(sep, lst);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->v.str.data, "a,b,c");
    PYDOS_DECREF(r);
    PYDOS_DECREF(lst);
    PYDOS_DECREF(sep);
}

TEST(str_startswith_true)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *pfx = pydos_str_from_cstr("hel");
    PyDosObj far *r = pydos_str_startswith(s, pfx);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_BOOL);
    ASSERT_EQ(r->v.bool_val, 1);
    PYDOS_DECREF(r);
    PYDOS_DECREF(pfx);
    PYDOS_DECREF(s);
}

TEST(str_endswith_true)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *sfx = pydos_str_from_cstr("llo");
    PyDosObj far *r = pydos_str_endswith(s, sfx);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_BOOL);
    ASSERT_EQ(r->v.bool_val, 1);
    PYDOS_DECREF(r);
    PYDOS_DECREF(sfx);
    PYDOS_DECREF(s);
}

TEST(str_isdigit_true)
{
    PyDosObj far *s = pydos_str_from_cstr("123");
    PyDosObj far *r = pydos_str_isdigit(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_BOOL);
    ASSERT_EQ(r->v.bool_val, 1);
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_isdigit_false)
{
    PyDosObj far *s = pydos_str_from_cstr("12a");
    PyDosObj far *r = pydos_str_isdigit(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_BOOL);
    ASSERT_EQ(r->v.bool_val, 0);
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_isalpha_true)
{
    PyDosObj far *s = pydos_str_from_cstr("abc");
    PyDosObj far *r = pydos_str_isalpha(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_BOOL);
    ASSERT_EQ(r->v.bool_val, 1);
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_isalpha_false)
{
    PyDosObj far *s = pydos_str_from_cstr("ab1");
    PyDosObj far *r = pydos_str_isalpha(s);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_BOOL);
    ASSERT_EQ(r->v.bool_val, 0);
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_count)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *sub = pydos_str_from_cstr("l");
    PyDosObj far *r = pydos_str_count_m(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 2L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(sub);
    PYDOS_DECREF(s);
}

TEST(str_rfind)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *sub = pydos_str_from_cstr("l");
    PyDosObj far *r = pydos_str_rfind_m(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 3L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(sub);
    PYDOS_DECREF(s);
}

TEST(str_find_method)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *sub = pydos_str_from_cstr("ll");
    PyDosObj far *r = pydos_str_find_m(s, sub);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_INT);
    ASSERT_EQ(r->v.int_val, 2L);
    PYDOS_DECREF(r);
    PYDOS_DECREF(sub);
    PYDOS_DECREF(s);
}

TEST(str_title)
{
    PyDosObj far *s = pydos_str_from_cstr("hello world");
    PyDosObj far *r = pydos_str_title(s);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->v.str.data, "Hello World");
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_capitalize)
{
    PyDosObj far *s = pydos_str_from_cstr("hello");
    PyDosObj far *r = pydos_str_capitalize(s);
    ASSERT_NOT_NULL(r);
    ASSERT_STR_EQ(r->v.str.data, "Hello");
    PYDOS_DECREF(r);
    PYDOS_DECREF(s);
}

TEST(str_center)
{
    PyDosObj far *s = pydos_str_from_cstr("hi");
    PyDosObj far *w = pydos_obj_new_int(6L);
    PyDosObj far *r = pydos_str_center_m(s, w);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 6);
    ASSERT_STR_EQ(r->v.str.data, "  hi  ");
    PYDOS_DECREF(r);
    PYDOS_DECREF(w);
    PYDOS_DECREF(s);
}

TEST(str_ljust)
{
    PyDosObj far *s = pydos_str_from_cstr("hi");
    PyDosObj far *w = pydos_obj_new_int(5L);
    PyDosObj far *r = pydos_str_ljust_m(s, w);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 5);
    ASSERT_STR_EQ(r->v.str.data, "hi   ");
    PYDOS_DECREF(r);
    PYDOS_DECREF(w);
    PYDOS_DECREF(s);
}

TEST(str_rjust)
{
    PyDosObj far *s = pydos_str_from_cstr("hi");
    PyDosObj far *w = pydos_obj_new_int(5L);
    PyDosObj far *r = pydos_str_rjust_m(s, w);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 5);
    ASSERT_STR_EQ(r->v.str.data, "   hi");
    PYDOS_DECREF(r);
    PYDOS_DECREF(w);
    PYDOS_DECREF(s);
}

TEST(str_zfill)
{
    PyDosObj far *s = pydos_str_from_cstr("42");
    PyDosObj far *w = pydos_obj_new_int(5L);
    PyDosObj far *r = pydos_str_zfill_m(s, w);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 5);
    ASSERT_STR_EQ(r->v.str.data, "00042");
    PYDOS_DECREF(r);
    PYDOS_DECREF(w);
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

    /* String method tests */
    RUN(str_upper);
    RUN(str_lower);
    RUN(str_strip);
    RUN(str_replace);
    RUN(str_split);
    RUN(str_join);
    RUN(str_startswith_true);
    RUN(str_endswith_true);
    RUN(str_isdigit_true);
    RUN(str_isdigit_false);
    RUN(str_isalpha_true);
    RUN(str_isalpha_false);
    RUN(str_count);
    RUN(str_rfind);
    RUN(str_find_method);
    RUN(str_title);
    RUN(str_capitalize);
    RUN(str_center);
    RUN(str_ljust);
    RUN(str_rjust);
    RUN(str_zfill);
}
