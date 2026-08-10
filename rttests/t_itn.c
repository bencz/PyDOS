/*
 * t_itn.c - Unit tests for pdos_itn (string interning) module
 *
 * Tests intern, intern deduplication, immortal flag,
 * lookup found/missing, and distinct strings.
 */

#include "testfw.h"
#include "../runtime/pdos_itn.h"
#include "../runtime/pdos_obj.h"

/* ------------------------------------------------------------------ */
/* intern_basic: intern a string, returns non-null                     */
/* ------------------------------------------------------------------ */

TEST(intern_basic)
{
    PyDosObj far *s;
    PyDosObj far *interned;

    s = pydos_obj_new_str((const char far *)"hello", 5);
    ASSERT_NOT_NULL(s);

    interned = pydos_intern(s);
    ASSERT_NOT_NULL(interned);
}

/* ------------------------------------------------------------------ */
/* intern_same_pointer: intern same string twice, same pointer         */
/* ------------------------------------------------------------------ */

TEST(intern_same_pointer)
{
    PyDosObj far *s1;
    PyDosObj far *s2;
    PyDosObj far *i1;
    PyDosObj far *i2;

    s1 = pydos_obj_new_str((const char far *)"same", 4);
    s2 = pydos_obj_new_str((const char far *)"same", 4);
    ASSERT_NOT_NULL(s1);
    ASSERT_NOT_NULL(s2);

    i1 = pydos_intern(s1);
    i2 = pydos_intern(s2);
    ASSERT_NOT_NULL(i1);
    ASSERT_NOT_NULL(i2);
    ASSERT_TRUE(i1 == i2);
}

/* ------------------------------------------------------------------ */
/* intern_immortal: interned string has OBJ_FLAG_IMMORTAL set          */
/* ------------------------------------------------------------------ */

TEST(intern_immortal)
{
    PyDosObj far *s;
    PyDosObj far *interned;

    s = pydos_obj_new_str((const char far *)"immortal", 8);
    ASSERT_NOT_NULL(s);

    interned = pydos_intern(s);
    ASSERT_NOT_NULL(interned);
    ASSERT_TRUE(interned->flags & OBJ_FLAG_IMMORTAL);
}

/* ------------------------------------------------------------------ */
/* intern_lookup_found: intern "hello", lookup returns non-null        */
/* ------------------------------------------------------------------ */

TEST(intern_lookup_found)
{
    PyDosObj far *s;
    PyDosObj far *interned;
    PyDosObj far *found;

    s = pydos_obj_new_str((const char far *)"lookup_test", 11);
    ASSERT_NOT_NULL(s);

    interned = pydos_intern(s);
    ASSERT_NOT_NULL(interned);

    found = pydos_intern_lookup((const char far *)"lookup_test", 11);
    ASSERT_NOT_NULL(found);
}

/* ------------------------------------------------------------------ */
/* intern_lookup_missing: lookup "xyz" when not interned returns NULL   */
/* ------------------------------------------------------------------ */

TEST(intern_lookup_missing)
{
    PyDosObj far *found;
    found = pydos_intern_lookup((const char far *)"xyz_not_interned", 16);
    ASSERT_NULL(found);
}

/* ------------------------------------------------------------------ */
/* intern_different_strings: intern "a" and "b", different pointers    */
/* ------------------------------------------------------------------ */

TEST(intern_different_strings)
{
    PyDosObj far *sa;
    PyDosObj far *sb;
    PyDosObj far *ia;
    PyDosObj far *ib;

    sa = pydos_obj_new_str((const char far *)"a", 1);
    sb = pydos_obj_new_str((const char far *)"b", 1);
    ASSERT_NOT_NULL(sa);
    ASSERT_NOT_NULL(sb);

    ia = pydos_intern(sa);
    ib = pydos_intern(sb);
    ASSERT_NOT_NULL(ia);
    ASSERT_NOT_NULL(ib);
    ASSERT_TRUE(ia != ib);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_itn_tests(void)
{
    SUITE("pdos_itn");

    RUN(intern_basic);
    RUN(intern_same_pointer);
    RUN(intern_immortal);
    RUN(intern_lookup_found);
    RUN(intern_lookup_missing);
    RUN(intern_different_strings);
}
