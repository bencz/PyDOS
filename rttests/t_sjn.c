/*
 * t_sjn.c - Unit tests for pdos_sjn module (string join)
 *
 * Tests pydos_str_join_n() batched string concatenation.
 * C89 compatible.
 */

#include "testfw.h"
#include "../runtime/pdos_sjn.h"
#include "../runtime/pdos_obj.h"

/* Helper: check string content matches expected C string */
static int sjn_str_matches(PyDosObj far *obj, const char *expected, unsigned int len)
{
    if (obj == (PyDosObj far *)0) return 0;
    if (obj->type != PYDT_STR) return 0;
    if (obj->v.str.len != len) return 0;
    if (len == 0) return 1;
    return _fmemcmp(obj->v.str.data, (const char far *)expected, len) == 0;
}

/* ------------------------------------------------------------------ */
/* sjn_zero_parts: count=0 produces empty string                       */
/* ------------------------------------------------------------------ */

TEST(sjn_zero_parts)
{
    PyDosObj far *r;
    r = pydos_str_join_n(0);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_EQ(r->v.str.len, 0);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_single_part: 1 string part returned as-is                       */
/* ------------------------------------------------------------------ */

TEST(sjn_single_part)
{
    PyDosObj far *a;
    PyDosObj far *r;

    a = pydos_obj_new_str((const char far *)"Hello", 5);
    r = pydos_str_join_n(1, a);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(sjn_str_matches(r, "Hello", 5));
    PYDOS_DECREF(a);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_two_parts: two strings concatenated                             */
/* ------------------------------------------------------------------ */

TEST(sjn_two_parts)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;

    a = pydos_obj_new_str((const char far *)"Hello", 5);
    b = pydos_obj_new_str((const char far *)" World", 6);
    r = pydos_str_join_n(2, a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(sjn_str_matches(r, "Hello World", 11));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_multiple_parts: 4 parts concatenated                            */
/* ------------------------------------------------------------------ */

TEST(sjn_multiple_parts)
{
    PyDosObj far *p1;
    PyDosObj far *p2;
    PyDosObj far *p3;
    PyDosObj far *p4;
    PyDosObj far *r;

    p1 = pydos_obj_new_str((const char far *)"a", 1);
    p2 = pydos_obj_new_str((const char far *)"bc", 2);
    p3 = pydos_obj_new_str((const char far *)"d", 1);
    p4 = pydos_obj_new_str((const char far *)"ef", 2);
    r = pydos_str_join_n(4, p1, p2, p3, p4);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(sjn_str_matches(r, "abcdef", 6));
    PYDOS_DECREF(p1);
    PYDOS_DECREF(p2);
    PYDOS_DECREF(p3);
    PYDOS_DECREF(p4);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_null_part_skipped: NULL in middle is skipped                     */
/* ------------------------------------------------------------------ */

TEST(sjn_null_part_skipped)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;

    a = pydos_obj_new_str((const char far *)"AB", 2);
    b = pydos_obj_new_str((const char far *)"CD", 2);
    r = pydos_str_join_n(3, a, (PyDosObj far *)0, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(sjn_str_matches(r, "ABCD", 4));
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_non_string_skipped: int obj in middle is skipped                 */
/* ------------------------------------------------------------------ */

TEST(sjn_non_string_skipped)
{
    PyDosObj far *a;
    PyDosObj far *intobj;
    PyDosObj far *b;
    PyDosObj far *r;

    a = pydos_obj_new_str((const char far *)"X", 1);
    intobj = pydos_obj_new_int(42);
    b = pydos_obj_new_str((const char far *)"Y", 1);
    r = pydos_str_join_n(3, a, intobj, b);
    ASSERT_NOT_NULL(r);
    ASSERT_TRUE(sjn_str_matches(r, "XY", 2));
    PYDOS_DECREF(a);
    PYDOS_DECREF(intobj);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_empty_strings: all empty parts produce empty result              */
/* ------------------------------------------------------------------ */

TEST(sjn_empty_strings)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *r;

    a = pydos_obj_new_str((const char far *)"", 0);
    b = pydos_obj_new_str((const char far *)"", 0);
    r = pydos_str_join_n(2, a, b);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->type, PYDT_STR);
    ASSERT_EQ(r->v.str.len, 0);
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);
    PYDOS_DECREF(r);
}

/* ------------------------------------------------------------------ */
/* sjn_max_parts: 16 parts all concatenated                            */
/* ------------------------------------------------------------------ */

TEST(sjn_max_parts)
{
    PyDosObj far *parts[16];
    PyDosObj far *r;
    int i;

    for (i = 0; i < 16; i++) {
        parts[i] = pydos_obj_new_str((const char far *)"x", 1);
    }
    r = pydos_str_join_n(16,
        parts[0], parts[1], parts[2], parts[3],
        parts[4], parts[5], parts[6], parts[7],
        parts[8], parts[9], parts[10], parts[11],
        parts[12], parts[13], parts[14], parts[15]);
    ASSERT_NOT_NULL(r);
    ASSERT_EQ(r->v.str.len, 16);
    PYDOS_DECREF(r);
    for (i = 0; i < 16; i++) {
        PYDOS_DECREF(parts[i]);
    }
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_sjn_tests(void)
{
    SUITE("pdos_sjn");
    RUN(sjn_zero_parts);
    RUN(sjn_single_part);
    RUN(sjn_two_parts);
    RUN(sjn_multiple_parts);
    RUN(sjn_null_part_skipped);
    RUN(sjn_non_string_skipped);
    RUN(sjn_empty_strings);
    RUN(sjn_max_parts);
}
