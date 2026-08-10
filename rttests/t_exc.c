/*
 * t_exc.c - Unit tests for pdos_exc module
 *
 * Tests explicit pending-exception state, owned fetch, clear, hierarchy,
 * constructors, and raise_obj ownership.
 */

#include "testfw.h"
#include "../runtime/pdos_exc.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_mem.h"

/* ------------------------------------------------------------------ */
/* exc_clear_initial: pydos_exc_current() is NULL after clear          */
/* ------------------------------------------------------------------ */

TEST(exc_clear_initial)
{
    pydos_exc_clear();
    ASSERT_NULL(pydos_exc_current());
}

TEST(exc_raise_sets_pending)
{
    int continued;
    continued = 0;
    pydos_exc_clear();
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"test error");
    continued = 1;
    ASSERT_TRUE(continued);
    ASSERT_TRUE(pydos_exc_pending());
    ASSERT_NOT_NULL(pydos_exc_current());
    ASSERT_EQ(pydos_exc_current()->v.exc.type_code, PYDOS_EXC_TYPE_ERROR);
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_raise_value: caught exception has correct type_code             */
/* ------------------------------------------------------------------ */

TEST(exc_raise_value)
{
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"type error msg");
    ASSERT_NOT_NULL(pydos_exc_current());
    ASSERT_EQ(pydos_exc_current()->v.exc.type_code,
              PYDOS_EXC_TYPE_ERROR);
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_raise_message: exception has message string                     */
/* ------------------------------------------------------------------ */

TEST(exc_raise_message)
{
    PyDosObj far *current;
    pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                    (const char far *)"bad value");
    current = pydos_exc_current();
    ASSERT_NOT_NULL(current);
    ASSERT_NOT_NULL(current->v.exc.message);
    ASSERT_EQ(current->v.exc.message->type, PYDT_STR);
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_current: after raise, pydos_exc_current() is not NULL           */
/* ------------------------------------------------------------------ */

TEST(exc_current)
{
    pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                    (const char far *)"runtime err");
    ASSERT_NOT_NULL(pydos_exc_current());
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_clear: after clear, pydos_exc_current() is NULL                 */
/* ------------------------------------------------------------------ */

TEST(exc_clear)
{
    pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                    (const char far *)"runtime err");
    ASSERT_TRUE(pydos_exc_pending());
    pydos_exc_clear();
    ASSERT_FALSE(pydos_exc_pending());
    ASSERT_NULL(pydos_exc_current());
}

TEST(exc_fetch_owned)
{
    PyDosObj far *current;
    PyDosObj far *fetched;
    unsigned int before;
    pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                    (const char far *)"key error");
    current = pydos_exc_current();
    before = (unsigned int)current->refcount;
    fetched = pydos_exc_fetch();
    ASSERT_TRUE(fetched == current);
    ASSERT_EQ(fetched->refcount, before + 1);
    PYDOS_DECREF(fetched);
    ASSERT_EQ(current->refcount, before);
    pydos_exc_clear();
}

/* ------------------------------------------------------------------ */
/* exc_raise_obj: create exception object manually, raise_obj catches  */
/* ------------------------------------------------------------------ */

TEST(exc_raise_obj)
{
    PyDosObj far *exc;

    /* Build an exception object manually */
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    ASSERT_NOT_NULL(exc);
    exc->refcount = 1;
    exc->v.exc.type_code = PYDOS_EXC_INDEX_ERROR;
    exc->v.exc.message = pydos_obj_new_str(
        (const char far *)"index error", 11);
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    pydos_exc_raise_obj(exc);
    ASSERT_TRUE(pydos_exc_current() == exc);
    ASSERT_EQ(exc->refcount, 2);
    PYDOS_DECREF(exc);
    ASSERT_EQ(pydos_exc_current()->refcount, 1);
    pydos_exc_clear();
}

TEST(exc_raise_obj_same)
{
    PyDosObj far *exc;
    pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                    (const char far *)"same");
    exc = pydos_exc_current();
    pydos_exc_raise_obj(exc);
    ASSERT_TRUE(pydos_exc_current() == exc);
    ASSERT_EQ(exc->refcount, 1);
    pydos_exc_clear();
}

TEST(exc_raise_obj_null)
{
    pydos_exc_clear();
    pydos_exc_raise_obj((PyDosObj far *)0);
    ASSERT_TRUE(pydos_exc_pending());
    ASSERT_EQ(pydos_exc_current()->v.exc.type_code,
              PYDOS_EXC_RUNTIME_ERROR);
    pydos_exc_clear();
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
/* exc_ctor_raise_pending: constructor object becomes pending          */
/* ------------------------------------------------------------------ */

TEST(exc_ctor_raise_pending)
{
    PyDosObj far *argv[1];
    PyDosObj far *exc;

    argv[0] = pydos_obj_new_str((const char far *)"test", 4);
    exc = pydos_exc_new_valueerror(1, argv);

    pydos_exc_raise_obj(exc);
    ASSERT_TRUE(pydos_exc_pending());
    ASSERT_TRUE(pydos_exc_current() == exc);
    ASSERT_EQ(pydos_exc_current()->v.exc.type_code,
              PYDOS_EXC_VALUE_ERROR);
    PYDOS_DECREF(exc);
    pydos_exc_clear();

    PYDOS_DECREF(argv[0]);
}

/* ------------------------------------------------------------------ */
/* exc_matches_exact: ValueError matches PYDOS_EXC_VALUE_ERROR          */
/* ------------------------------------------------------------------ */

TEST(exc_matches_exact)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    ASSERT_NOT_NULL(exc);
    exc->refcount = 1;
    exc->v.exc.type_code = PYDOS_EXC_VALUE_ERROR;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_VALUE_ERROR));
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_TYPE_ERROR));
    PYDOS_DECREF(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_base_all: BaseException matches any exception            */
/* ------------------------------------------------------------------ */

TEST(exc_matches_base_all)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    ASSERT_NOT_NULL(exc);
    exc->refcount = 1;
    exc->v.exc.type_code = PYDOS_EXC_TYPE_ERROR;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    PYDOS_DECREF(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_exception_standard: Exception matches TypeError          */
/* ------------------------------------------------------------------ */

TEST(exc_matches_exception_standard)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    ASSERT_NOT_NULL(exc);
    exc->refcount = 1;
    exc->v.exc.type_code = PYDOS_EXC_TYPE_ERROR;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    PYDOS_DECREF(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_exception_not_base: Exception does NOT match BaseExc     */
/* ------------------------------------------------------------------ */

TEST(exc_matches_exception_not_base)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    ASSERT_NOT_NULL(exc);
    exc->refcount = 1;
    exc->v.exc.type_code = PYDOS_EXC_BASE;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    PYDOS_DECREF(exc);
}

/* ------------------------------------------------------------------ */
/* exc_matches_runtime_user: RuntimeError matches USER_BASE excs        */
/* ------------------------------------------------------------------ */

TEST(exc_matches_runtime_user)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    ASSERT_NOT_NULL(exc);
    exc->refcount = 1;
    exc->v.exc.type_code = PYDOS_EXC_USER_BASE;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_RUNTIME_ERROR));
    PYDOS_DECREF(exc);
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
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    if (exc == (PyDosObj far *)0) return exc;
    exc->refcount = 1;
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    PYDOS_DECREF(exc);
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
    RUN(exc_raise_sets_pending);
    RUN(exc_raise_value);
    RUN(exc_raise_message);
    RUN(exc_current);
    RUN(exc_clear);
    RUN(exc_fetch_owned);
    RUN(exc_raise_obj);
    RUN(exc_raise_obj_same);
    RUN(exc_raise_obj_null);
    RUN(exc_new_exception);
    RUN(exc_new_valueerror);
    RUN(exc_new_typeerror);
    RUN(exc_new_runtimeerror);
    RUN(exc_new_indexerror);
    RUN(exc_new_keyerror);
    RUN(exc_new_stopiteration);
    RUN(exc_new_no_args);
    RUN(exc_ctor_raise_pending);
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
