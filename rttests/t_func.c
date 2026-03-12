/*
 * t_func.c - Unit tests for pydos_func_new (PYDT_FUNCTION)
 *
 * Tests function object creation, type checking, code pointer storage,
 * name field, defaults field, reference counting, and type_name.
 */

#include "testfw.h"
#include "../runtime/pdos_obj.h"

/* Dummy code functions for testing.
 * Watcom aggressively merges functions with identical or trivially
 * similar bodies (COMDAT folding).  Each function must have a unique
 * side-effect so the optimizer cannot fold them together. */
static volatile int dummy_sink_a = 0;
static void far dummy_code(void)
{
    dummy_sink_a = 1;
}

static volatile int dummy_sink_b = 0;
static void far another_code(void)
{
    dummy_sink_b = 2;
}

/* ------------------------------------------------------------------ */
/* Function object creation                                            */
/* ------------------------------------------------------------------ */

TEST(func_new_basic)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    PYDOS_DECREF(f);
}

TEST(func_new_type)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->type, PYDT_FUNCTION);
    PYDOS_DECREF(f);
}

TEST(func_new_refcount)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->refcount, 1);
    PYDOS_DECREF(f);
}

TEST(func_new_code_ptr)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_TRUE(f->v.func.code == (void (far *)(void))dummy_code);
    PYDOS_DECREF(f);
}

TEST(func_new_name)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_STR_EQ(f->v.func.name, "test_fn");
    PYDOS_DECREF(f);
}

TEST(func_new_defaults_null)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_NULL(f->v.func.defaults);
    PYDOS_DECREF(f);
}

/* ------------------------------------------------------------------ */
/* Different function objects                                           */
/* ------------------------------------------------------------------ */

TEST(func_two_different)
{
    PyDosObj far *f1;
    PyDosObj far *f2;

    f1 = pydos_func_new(dummy_code, (const char far *)"fn_a");
    f2 = pydos_func_new(another_code, (const char far *)"fn_b");
    ASSERT_NOT_NULL(f1);
    ASSERT_NOT_NULL(f2);

    /* Different code pointers */
    ASSERT_TRUE(f1->v.func.code != f2->v.func.code);

    /* Different names */
    ASSERT_STR_EQ(f1->v.func.name, "fn_a");
    ASSERT_STR_EQ(f2->v.func.name, "fn_b");

    PYDOS_DECREF(f1);
    PYDOS_DECREF(f2);
}

TEST(func_type_name)
{
    PyDosObj far *f;
    const char far *tn;

    f = pydos_func_new(dummy_code, (const char far *)"my_func");
    ASSERT_NOT_NULL(f);

    tn = pydos_obj_type_name(f);
    ASSERT_STR_EQ(tn, "function");

    PYDOS_DECREF(f);
}

TEST(func_is_truthy)
{
    PyDosObj far *f;

    f = pydos_func_new(dummy_code, (const char far *)"truthy_fn");
    ASSERT_NOT_NULL(f);

    /* Function objects are always truthy */
    ASSERT_TRUE(pydos_obj_is_truthy(f));

    PYDOS_DECREF(f);
}

TEST(func_to_str)
{
    PyDosObj far *f;
    PyDosObj far *s;

    f = pydos_func_new(dummy_code, (const char far *)"show_me");
    ASSERT_NOT_NULL(f);

    s = pydos_obj_to_str(f);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);

    PYDOS_DECREF(s);
    PYDOS_DECREF(f);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_func_tests(void)
{
    SUITE("pdos_func");
    RUN(func_new_basic);
    RUN(func_new_type);
    RUN(func_new_refcount);
    RUN(func_new_code_ptr);
    RUN(func_new_name);
    RUN(func_new_defaults_null);
    RUN(func_two_different);
    RUN(func_type_name);
    RUN(func_is_truthy);
    RUN(func_to_str);
}
