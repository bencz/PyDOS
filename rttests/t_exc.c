/*
 * t_exc.c - Unit tests for pdos_exc module
 *
 * Tests exception push/pop, raise/catch via setjmp/longjmp,
 * current exception, clear, nested frames, and raise_obj.
 */

#include "testfw.h"
#include "../runtime/pdos_exc.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_mem.h"
#include <setjmp.h>

/* ------------------------------------------------------------------ */
/* exc_clear_initial: pydos_exc_current() is NULL after clear          */
/* ------------------------------------------------------------------ */

TEST(exc_clear_initial)
{
    pydos_exc_clear();
    ASSERT_NULL(pydos_exc_current());
}

/* ------------------------------------------------------------------ */
/* exc_push_pop: push and pop a frame without crash                    */
/* ------------------------------------------------------------------ */

TEST(exc_push_pop)
{
    ExcFrame frame;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    pydos_exc_pop();
    /* If we reach here, no crash occurred */
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* exc_raise_catches: raise TYPE_ERROR, caught in handler              */
/* ------------------------------------------------------------------ */

TEST(exc_raise_catches)
{
    ExcFrame frame;
    int caught;
    caught = 0;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                         (const char far *)"test error");
        /* should NOT reach here */
        ASSERT_TRUE(0);
    } else {
        caught = 1;
    }
    pydos_exc_pop();
    ASSERT_TRUE(caught);
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_raise_value: caught exception has correct type_code             */
/* ------------------------------------------------------------------ */

TEST(exc_raise_value)
{
    ExcFrame frame;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                         (const char far *)"type error msg");
        ASSERT_TRUE(0);
    } else {
        ASSERT_NOT_NULL(frame.exc_value);
        ASSERT_EQ(frame.exc_value->v.exc.type_code,
                   PYDOS_EXC_TYPE_ERROR);
    }
    pydos_exc_pop();
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_raise_message: exception has message string                     */
/* ------------------------------------------------------------------ */

TEST(exc_raise_message)
{
    ExcFrame frame;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                         (const char far *)"bad value");
        ASSERT_TRUE(0);
    } else {
        ASSERT_NOT_NULL(frame.exc_value);
        ASSERT_NOT_NULL(frame.exc_value->v.exc.message);
        ASSERT_EQ(frame.exc_value->v.exc.message->type, PYDT_STR);
    }
    pydos_exc_pop();
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_current: after raise, pydos_exc_current() is not NULL           */
/* ------------------------------------------------------------------ */

TEST(exc_current)
{
    ExcFrame frame;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                         (const char far *)"runtime err");
        ASSERT_TRUE(0);
    } else {
        ASSERT_NOT_NULL(pydos_exc_current());
    }
    pydos_exc_pop();
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_clear: after clear, pydos_exc_current() is NULL                 */
/* ------------------------------------------------------------------ */

TEST(exc_clear)
{
    ExcFrame frame;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                         (const char far *)"runtime err");
        ASSERT_TRUE(0);
    } else {
        /* exception caught */
    }
    pydos_exc_pop();
    pydos_exc_clear();
    ASSERT_NULL(pydos_exc_current());
}

/* ------------------------------------------------------------------ */
/* exc_nested: push two frames, inner catches, outer doesn't see it    */
/* ------------------------------------------------------------------ */

TEST(exc_nested)
{
    ExcFrame outer;
    ExcFrame inner;
    int inner_caught;
    int outer_caught;

    inner_caught = 0;
    outer_caught = 0;

    outer.handled = 0;
    outer.exc_value = (PyDosObj far *)0;
    outer.cleanup = (void (*)(void))0;

    inner.handled = 0;
    inner.exc_value = (PyDosObj far *)0;
    inner.cleanup = (void (*)(void))0;

    pydos_exc_push(&outer);
    if (setjmp(outer.env) == 0) {
        pydos_exc_push(&inner);
        if (setjmp(inner.env) == 0) {
            pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                             (const char far *)"key error");
            ASSERT_TRUE(0);
        } else {
            inner_caught = 1;
        }
        pydos_exc_pop();
        pydos_exc_clear();
    } else {
        outer_caught = 1;
    }
    pydos_exc_pop();
    pydos_exc_clear();

    ASSERT_TRUE(inner_caught);
    ASSERT_FALSE(outer_caught);
}

/* ------------------------------------------------------------------ */
/* exc_raise_obj: create exception object manually, raise_obj catches  */
/* ------------------------------------------------------------------ */

TEST(exc_raise_obj)
{
    ExcFrame frame;
    PyDosObj far *exc;
    int caught;

    caught = 0;

    /* Build an exception object manually */
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_INDEX_ERROR;
    exc->v.exc.message = pydos_obj_new_str(
        (const char far *)"index error", 11);
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise_obj(exc);
        ASSERT_TRUE(0);
    } else {
        caught = 1;
        ASSERT_NOT_NULL(frame.exc_value);
        ASSERT_EQ(frame.exc_value->v.exc.type_code,
                   PYDOS_EXC_INDEX_ERROR);
    }
    pydos_exc_pop();
    pydos_exc_clear();

    ASSERT_TRUE(caught);
}

/* ------------------------------------------------------------------ */
/* try_enter: normal path returns 0, then pop                          */
/* ------------------------------------------------------------------ */

TEST(try_enter_normal)
{
    int rv;
    rv = pydos_try_enter();
    ASSERT_EQ(rv, 0);
    pydos_exc_pop();
}

/* ------------------------------------------------------------------ */
/* try_enter: multiple sequential calls reuse pool frames              */
/* ------------------------------------------------------------------ */

TEST(try_enter_multiple)
{
    int rv1;
    int rv2;

    rv1 = pydos_try_enter();
    ASSERT_EQ(rv1, 0);
    pydos_exc_pop();

    rv2 = pydos_try_enter();
    ASSERT_EQ(rv2, 0);
    pydos_exc_pop();
}

/* ------------------------------------------------------------------ */
/* try_enter: nested calls allocate consecutive pool frames            */
/* ------------------------------------------------------------------ */

TEST(try_enter_nested)
{
    int rv_outer;
    int rv_inner;

    rv_outer = pydos_try_enter();
    ASSERT_EQ(rv_outer, 0);

    rv_inner = pydos_try_enter();
    ASSERT_EQ(rv_inner, 0);

    /* Pop inner first, then outer */
    pydos_exc_pop();
    pydos_exc_pop();
}

/* ------------------------------------------------------------------ */
/* exc_new_exception: constructor creates Exception object             */
/* ------------------------------------------------------------------ */

TEST(exc_new_exception)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"test", 4);
    exc = pydos_exc_new_exception(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_EXCEPTION);
    ASSERT_NOT_NULL(exc->v.exc.message);
    ASSERT_EQ(exc->v.exc.message->type, PYDT_STR);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_valueerror: constructor creates ValueError object            */
/* ------------------------------------------------------------------ */

TEST(exc_new_valueerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"bad val", 7);
    exc = pydos_exc_new_valueerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_VALUE_ERROR);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_typeerror: constructor creates TypeError object              */
/* ------------------------------------------------------------------ */

TEST(exc_new_typeerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"bad type", 8);
    exc = pydos_exc_new_typeerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_TYPE_ERROR);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_runtimeerror: constructor creates RuntimeError object        */
/* ------------------------------------------------------------------ */

TEST(exc_new_runtimeerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"rt err", 6);
    exc = pydos_exc_new_runtimeerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_RUNTIME_ERROR);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_indexerror: constructor creates IndexError object            */
/* ------------------------------------------------------------------ */

TEST(exc_new_indexerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"idx", 3);
    exc = pydos_exc_new_indexerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_INDEX_ERROR);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_keyerror: constructor creates KeyError object                */
/* ------------------------------------------------------------------ */

TEST(exc_new_keyerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"key", 3);
    exc = pydos_exc_new_keyerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_KEY_ERROR);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_stopiteration: constructor creates StopIteration object      */
/* ------------------------------------------------------------------ */

TEST(exc_new_stopiteration)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"stop", 4);
    exc = pydos_exc_new_stopiteration(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_STOP_ITERATION);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_no_args: constructor with 0 args still works                */
/* ------------------------------------------------------------------ */

TEST(exc_new_no_args)
{
    PyDosObj far *exc;

    exc = pydos_exc_new_valueerror(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_VALUE_ERROR);

    PYDOS_DECREF(exc);
}

/* ------------------------------------------------------------------ */
/* exc_ctor_raise_catch: create with ctor, raise, catch                */
/* ------------------------------------------------------------------ */

TEST(exc_ctor_raise_catch)
{
    ExcFrame frame;
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    int caught;

    caught = 0;
    argv[0] = pydos_obj_new_str((const char far *)"test", 4);
    exc = pydos_exc_new_valueerror(1, argv);

    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        pydos_exc_raise_obj(exc);
        ASSERT_TRUE(0);
    } else {
        caught = 1;
        ASSERT_NOT_NULL(frame.exc_value);
        ASSERT_EQ(frame.exc_value->v.exc.type_code,
                   PYDOS_EXC_VALUE_ERROR);
    }
    pydos_exc_pop();
    pydos_exc_clear();

    ASSERT_TRUE(caught);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_matches_exact: ValueError matches PYDOS_EXC_VALUE_ERROR          */
/* ------------------------------------------------------------------ */

TEST(exc_matches_exact)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_VALUE_ERROR;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_VALUE_ERROR));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_TYPE_ERROR));
    pydos_far_free(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_base_all: BaseException matches any exception            */
/* ------------------------------------------------------------------ */

TEST(exc_matches_base_all)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_TYPE_ERROR;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    pydos_far_free(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_exception_standard: Exception matches TypeError          */
/* ------------------------------------------------------------------ */

TEST(exc_matches_exception_standard)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_TYPE_ERROR;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_exception_not_base: Exception does NOT match BaseExc     */
/* ------------------------------------------------------------------ */

TEST(exc_matches_exception_not_base)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_BASE;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_runtime_user: RuntimeError matches USER_BASE excs        */
/* ------------------------------------------------------------------ */

TEST(exc_matches_runtime_user)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_USER_BASE;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_RUNTIME_ERROR));
    pydos_far_free(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_null: NULL exc returns 0                                 */
/* ------------------------------------------------------------------ */

TEST(exc_matches_null)
{
    ASSERT_FALSE(pydos_exc_matches((PyDosObj far *)0, PYDOS_EXC_EXCEPTION));
}

/* ------------------------------------------------------------------ */
/* New exception constructor tests                                     */
/* ------------------------------------------------------------------ */

TEST(exc_new_assertionerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"assert fail", 11);
    exc = pydos_exc_new_assertionerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_ASSERTION_ERROR);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_attributeerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"no attr", 7);
    exc = pydos_exc_new_attributeerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_ATTRIBUTE_ERROR);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_nameerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"no name", 7);
    exc = pydos_exc_new_nameerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_NAME_ERROR);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_zerodivisionerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"div zero", 8);
    exc = pydos_exc_new_zerodivisionerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_ZERO_DIVISION);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_overflowerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"overflow", 8);
    exc = pydos_exc_new_overflowerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_OVERFLOW);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_oserror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"os err", 6);
    exc = pydos_exc_new_oserror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_OS_ERROR);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_notimplementederror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"not impl", 8);
    exc = pydos_exc_new_notimplementederror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_NOT_IMPLEMENTED);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

TEST(exc_new_memoryerror)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;
    argv[0] = pydos_obj_new_str((const char far *)"oom", 3);
    exc = pydos_exc_new_memoryerror(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_MEMORY_ERROR);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_new_baseexception: BaseException constructor creates type_code 0*/
/* Regression: BaseException was missing from stdlib, code was -1      */
/* ------------------------------------------------------------------ */

TEST(exc_new_baseexception)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"test", 4);
    ASSERT_NOT_NULL(argv[0]);

    exc = pydos_exc_new_baseexception(1, argv);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_BASE);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* Helper to create a bare exception object with given type_code       */
/* ------------------------------------------------------------------ */

static PyDosObj far *make_test_exc(int type_code)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    if (exc == (PyDosObj far *)0) return exc;
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = type_code;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;
    return exc;
}

/* ------------------------------------------------------------------ */
/* Phase 6A: parent-chain matching tests                               */
/* ------------------------------------------------------------------ */

/* KeyError → LookupError → Exception → BaseException */
TEST(exc_matches_keyerror_lookuperror)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_KEY_ERROR);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_KEY_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_LOOKUP_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_ARITHMETIC_ERROR));
    pydos_far_free(exc);
}

/* IndexError → LookupError → Exception → BaseException */
TEST(exc_matches_indexerror_lookuperror)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_INDEX_ERROR);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_INDEX_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_LOOKUP_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    pydos_far_free(exc);
}

/* ZeroDivisionError → ArithmeticError → Exception → BaseException */
TEST(exc_matches_zerodiv_arithmetic)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_ZERO_DIVISION);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_ZERO_DIVISION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_ARITHMETIC_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_LOOKUP_ERROR));
    pydos_far_free(exc);
}

/* OverflowError → ArithmeticError → Exception → BaseException */
TEST(exc_matches_overflow_arithmetic)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_OVERFLOW);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_OVERFLOW));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_ARITHMETIC_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* NotImplementedError → RuntimeError → Exception → BaseException */
TEST(exc_matches_notimpl_runtime)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_NOT_IMPLEMENTED);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_NOT_IMPLEMENTED));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_RUNTIME_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    pydos_far_free(exc);
}

/* GeneratorExit → BaseException (NOT Exception) */
TEST(exc_matches_genexit_not_exception)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_GENERATOR_EXIT);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_GENERATOR_EXIT));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* SystemExit → BaseException (NOT Exception) */
TEST(exc_matches_sysexit_not_exception)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_SYSTEM_EXIT);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_SYSTEM_EXIT));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* KeyboardInterrupt → BaseException (NOT Exception) */
TEST(exc_matches_kbinterrupt_not_exception)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_KEYBOARD_INTERRUPT);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_KEYBOARD_INTERRUPT));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* ModuleNotFoundError → ImportError → Exception → BaseException */
TEST(exc_matches_modfound_import)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_MODULE_NOT_FOUND);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_MODULE_NOT_FOUND));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_IMPORT_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    pydos_far_free(exc);
}

/* IndentationError → SyntaxError → Exception → BaseException */
TEST(exc_matches_indent_syntax)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_INDENTATION_ERROR);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_INDENTATION_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_SYNTAX_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* UnboundLocalError → NameError → Exception → BaseException */
TEST(exc_matches_unboundlocal_name)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_UNBOUND_LOCAL);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_UNBOUND_LOCAL));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_NAME_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* FileNotFoundError → OSError → Exception → BaseException */
TEST(exc_matches_filenf_os)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_FILE_NOT_FOUND);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_FILE_NOT_FOUND));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_OS_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* UnicodeDecodeError → UnicodeError → ValueError → Exception */
TEST(exc_matches_unidec_chain)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_UNICODE_DECODE);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_UNICODE_DECODE));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_UNICODE_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_VALUE_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_TYPE_ERROR));
    pydos_far_free(exc);
}

/* RecursionError → RuntimeError → Exception → BaseException */
TEST(exc_matches_recursion_runtime)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_RECURSION_ERROR);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_RECURSION_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_RUNTIME_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* TimeoutError → OSError → Exception → BaseException */
TEST(exc_matches_timeout_os)
{
    PyDosObj far *exc;
    exc = make_test_exc(PYDOS_EXC_TIMEOUT_ERROR);
    ASSERT_NOT_NULL(exc);
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_TIMEOUT_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_OS_ERROR));
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* exc_type_name returns correct name for new types */
TEST(exc_type_name_new)
{
    ASSERT_STR_EQ(pydos_exc_type_name(PYDOS_EXC_LOOKUP_ERROR),
                  (const char far *)"LookupError");
    ASSERT_STR_EQ(pydos_exc_type_name(PYDOS_EXC_SYSTEM_EXIT),
                  (const char far *)"SystemExit");
    ASSERT_STR_EQ(pydos_exc_type_name(PYDOS_EXC_TIMEOUT_ERROR),
                  (const char far *)"TimeoutError");
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_exc_tests(void)
{
    SUITE("pdos_exc");

    RUN(exc_clear_initial);
    RUN(exc_push_pop);
    RUN(exc_raise_catches);
    RUN(exc_raise_value);
    RUN(exc_raise_message);
    RUN(exc_current);
    RUN(exc_clear);
    RUN(exc_nested);
    RUN(exc_raise_obj);
    RUN(try_enter_normal);
    RUN(try_enter_multiple);
    RUN(try_enter_nested);
    RUN(exc_new_exception);
    RUN(exc_new_valueerror);
    RUN(exc_new_typeerror);
    RUN(exc_new_runtimeerror);
    RUN(exc_new_indexerror);
    RUN(exc_new_keyerror);
    RUN(exc_new_stopiteration);
    RUN(exc_new_no_args);
    RUN(exc_ctor_raise_catch);
    RUN(exc_matches_exact);
    RUN(exc_matches_base_all);
    RUN(exc_matches_exception_standard);
    RUN(exc_matches_exception_not_base);
    RUN(exc_matches_runtime_user);
    RUN(exc_matches_null);

    /* Existing exception constructor tests */
    RUN(exc_new_assertionerror);
    RUN(exc_new_attributeerror);
    RUN(exc_new_nameerror);
    RUN(exc_new_zerodivisionerror);
    RUN(exc_new_overflowerror);
    RUN(exc_new_oserror);
    RUN(exc_new_notimplementederror);
    RUN(exc_new_memoryerror);
    RUN(exc_new_baseexception);

    /* Phase 6A: parent-chain matching tests */
    RUN(exc_matches_keyerror_lookuperror);
    RUN(exc_matches_indexerror_lookuperror);
    RUN(exc_matches_zerodiv_arithmetic);
    RUN(exc_matches_overflow_arithmetic);
    RUN(exc_matches_notimpl_runtime);
    RUN(exc_matches_genexit_not_exception);
    RUN(exc_matches_sysexit_not_exception);
    RUN(exc_matches_kbinterrupt_not_exception);
    RUN(exc_matches_modfound_import);
    RUN(exc_matches_indent_syntax);
    RUN(exc_matches_unboundlocal_name);
    RUN(exc_matches_filenf_os);
    RUN(exc_matches_unidec_chain);
    RUN(exc_matches_recursion_runtime);
    RUN(exc_matches_timeout_os);
    RUN(exc_type_name_new);
}
