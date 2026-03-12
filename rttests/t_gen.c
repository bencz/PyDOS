/*
 * t_gen.c - Unit tests for pdos_gen module
 *
 * Tests generator creation, next, exhaustion, real resume functions
 * that yield values, and the full generator protocol:
 * send/throw/close/check_throw.
 */

#include "testfw.h"
#include "../runtime/pdos_gen.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_exc.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_str.h"
#include "../runtime/pdos_mem.h"

#include <setjmp.h>

/* Dummy resume function for basic creation tests */
static void far dummy_resume(void)
{
    /* intentionally empty */
}

/*
 * Real resume function that yields 3 int values: 10, 20, 30
 * then returns NULL (StopIteration).
 *
 * This simulates the compiled state machine pattern:
 * - pc=0: yield 10, set pc=1
 * - pc=1: yield 20, set pc=2
 * - pc=2: yield 30, set pc=3
 * - pc=3: return NULL (exhausted)
 */
static PyDosObj far * PYDOS_API resume_3ints(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;

    if (pc == 0) {
        gen->v.gen.pc = 1;
        return pydos_obj_new_int(10L);
    }
    if (pc == 1) {
        gen->v.gen.pc = 2;
        return pydos_obj_new_int(20L);
    }
    if (pc == 2) {
        gen->v.gen.pc = 3;
        return pydos_obj_new_int(30L);
    }
    return (PyDosObj far *)0;
}

/*
 * Resume function that yields strings: "alpha", "beta"
 */
static PyDosObj far * PYDOS_API resume_strings(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;

    if (pc == 0) {
        gen->v.gen.pc = 1;
        return pydos_str_new((const char far *)"alpha", 5);
    }
    if (pc == 1) {
        gen->v.gen.pc = 2;
        return pydos_str_new((const char far *)"beta", 4);
    }
    return (PyDosObj far *)0;
}

/*
 * Resume function that uses gen->locals for state.
 * Yields: locals[0] + 1, locals[0] + 2, locals[0] + 3
 * (Simulates saving/restoring a counter across yields)
 */
static PyDosObj far * PYDOS_API resume_with_locals(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;
    PyDosObj far *base_obj;
    long base;

    if (gen->v.gen.locals == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    base_obj = pydos_list_get(gen->v.gen.locals, 0);
    base = (base_obj != (PyDosObj far *)0 && base_obj->type == PYDT_INT)
           ? base_obj->v.int_val : 0L;

    if (pc == 0) {
        gen->v.gen.pc = 1;
        return pydos_obj_new_int(base + 1L);
    }
    if (pc == 1) {
        gen->v.gen.pc = 2;
        return pydos_obj_new_int(base + 2L);
    }
    if (pc == 2) {
        gen->v.gen.pc = 3;
        return pydos_obj_new_int(base + 3L);
    }
    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* Generator creation                                                  */
/* ------------------------------------------------------------------ */

TEST(gen_new)
{
    PyDosObj far *g = pydos_gen_new(dummy_resume, 2);
    ASSERT_NOT_NULL(g);
    PYDOS_DECREF(g);
}

TEST(gen_new_type)
{
    PyDosObj far *g = pydos_gen_new(dummy_resume, 2);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(g->type, PYDT_GENERATOR);
    PYDOS_DECREF(g);
}

TEST(gen_new_pc)
{
    PyDosObj far *g = pydos_gen_new(dummy_resume, 2);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(g->v.gen.pc, 0);
    PYDOS_DECREF(g);
}

TEST(gen_new_locals_prefilled)
{
    PyDosObj far *g = pydos_gen_new(dummy_resume, 3);
    ASSERT_NOT_NULL(g);
    ASSERT_NOT_NULL(g->v.gen.locals);
    ASSERT_EQ(pydos_list_len(g->v.gen.locals), 3L);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* Generator next with real resume (int yields)                        */
/* ------------------------------------------------------------------ */

TEST(gen_next_first)
{
    PyDosObj far *g;
    PyDosObj far *val;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    val = pydos_gen_next(g);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->type, PYDT_INT);
    ASSERT_EQ(val->v.int_val, 10L);
    PYDOS_DECREF(val);
    PYDOS_DECREF(g);
}

TEST(gen_next_sequence)
{
    PyDosObj far *g;
    PyDosObj far *v1;
    PyDosObj far *v2;
    PyDosObj far *v3;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    v1 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v1);
    ASSERT_EQ(v1->v.int_val, 10L);

    v2 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v2);
    ASSERT_EQ(v2->v.int_val, 20L);

    v3 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v3);
    ASSERT_EQ(v3->v.int_val, 30L);

    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(v3);
    PYDOS_DECREF(g);
}

TEST(gen_next_exhausted)
{
    PyDosObj far *g;
    PyDosObj far *val;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    /* Consume all values */
    val = pydos_gen_next(g);
    PYDOS_DECREF(val);
    val = pydos_gen_next(g);
    PYDOS_DECREF(val);
    val = pydos_gen_next(g);
    PYDOS_DECREF(val);

    /* Should now be exhausted (returns NULL, pc=-1) */
    val = pydos_gen_next(g);
    ASSERT_NULL(val);
    ASSERT_EQ(g->v.gen.pc, -1);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* Generator next with string yields                                   */
/* ------------------------------------------------------------------ */

TEST(gen_next_strings)
{
    PyDosObj far *g;
    PyDosObj far *v1;
    PyDosObj far *v2;
    PyDosObj far *v3;

    g = pydos_gen_new((void (far *)(void))resume_strings, 0);
    ASSERT_NOT_NULL(g);

    v1 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v1);
    ASSERT_EQ(v1->type, PYDT_STR);
    ASSERT_STR_EQ(v1->v.str.data, "alpha");

    v2 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v2);
    ASSERT_EQ(v2->type, PYDT_STR);
    ASSERT_STR_EQ(v2->v.str.data, "beta");

    /* Exhausted */
    v3 = pydos_gen_next(g);
    ASSERT_NULL(v3);

    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* Generator with locals storage                                       */
/* ------------------------------------------------------------------ */

TEST(gen_locals_resume)
{
    PyDosObj far *g;
    PyDosObj far *base;
    PyDosObj far *v1;
    PyDosObj far *v2;
    PyDosObj far *v3;

    g = pydos_gen_new((void (far *)(void))resume_with_locals, 1);
    ASSERT_NOT_NULL(g);

    /* Set locals[0] = 100 */
    base = pydos_obj_new_int(100L);
    pydos_list_set(g->v.gen.locals, 0, base);
    PYDOS_DECREF(base);

    v1 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v1);
    ASSERT_EQ(v1->v.int_val, 101L);

    v2 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v2);
    ASSERT_EQ(v2->v.int_val, 102L);

    v3 = pydos_gen_next(g);
    ASSERT_NOT_NULL(v3);
    ASSERT_EQ(v3->v.int_val, 103L);

    PYDOS_DECREF(v1);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(v3);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* Generator iteration via pydos_obj_get_iter / iter_next              */
/* ------------------------------------------------------------------ */

TEST(gen_iter_protocol)
{
    PyDosObj far *g;
    PyDosObj far *it;
    PyDosObj far *val;
    int count;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    it = pydos_obj_get_iter(g);
    ASSERT_NOT_NULL(it);

    count = 0;
    val = pydos_obj_iter_next(it);
    while (val != (PyDosObj far *)0) {
        count++;
        PYDOS_DECREF(val);
        val = pydos_obj_iter_next(it);
    }
    ASSERT_EQ(count, 3);

    PYDOS_DECREF(it);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* Generator drain via pydos_list_from_iter                            */
/* ------------------------------------------------------------------ */

TEST(gen_list_from_iter)
{
    /* pydos_list_from_iter on a generator should drain all values */
    PyDosObj far *g;
    PyDosObj far *lst;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    lst = pydos_list_from_iter(g);
    ASSERT_NOT_NULL(lst);
    ASSERT_EQ(lst->type, PYDT_LIST);
    ASSERT_EQ(lst->v.list.len, 3);
    ASSERT_EQ(lst->v.list.items[0]->v.int_val, 10L);
    ASSERT_EQ(lst->v.list.items[1]->v.int_val, 20L);
    ASSERT_EQ(lst->v.list.items[2]->v.int_val, 30L);

    PYDOS_DECREF(lst);
    PYDOS_DECREF(g);
}

TEST(gen_list_from_iter_strings)
{
    /* pydos_list_from_iter on a string-yielding generator */
    PyDosObj far *g;
    PyDosObj far *lst;

    g = pydos_gen_new((void (far *)(void))resume_strings, 0);
    ASSERT_NOT_NULL(g);

    lst = pydos_list_from_iter(g);
    ASSERT_NOT_NULL(lst);
    ASSERT_EQ(lst->v.list.len, 2);
    ASSERT_STR_EQ(lst->v.list.items[0]->v.str.data, "alpha");
    ASSERT_STR_EQ(lst->v.list.items[1]->v.str.data, "beta");

    PYDOS_DECREF(lst);
    PYDOS_DECREF(g);
}

/* ================================================================== */
/* Phase 3: Mock resume functions for send/throw/close tests          */
/* ================================================================== */

/*
 * resume_echo: Simulates a generator that yields the sent value back.
 *
 *   def echo():
 *       val = yield "ready"
 *       while val is not None:
 *           val = yield val
 *
 * pc=0: yield "ready", set pc=1
 * pc=1: check_throw, read pydos_gen_sent → if None, return NULL; else yield it
 */
static PyDosObj far * PYDOS_API resume_echo(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;

    if (pc == 0) {
        gen->v.gen.pc = 1;
        return pydos_str_new((const char far *)"ready", 5);
    }
    if (pc == 1) {
        PyDosObj far *sent;

        /* At yield resumption: check for pending throw.
         * Returns 1 if throw pending — return NULL to propagate. */
        if (pydos_gen_check_throw(gen)) {
            return (PyDosObj far *)0;
        }

        /* Read sent value */
        sent = pydos_gen_sent;
        if (sent == (PyDosObj far *)0 ||
            (PyDosType)sent->type == PYDT_NONE) {
            return (PyDosObj far *)0;  /* StopIteration */
        }
        /* Yield the sent value back (INCREF since we return a new ref) */
        PYDOS_INCREF(sent);
        gen->v.gen.pc = 1;  /* stay at same yield point */
        return sent;
    }
    return (PyDosObj far *)0;
}

/*
 * resume_with_handler: Simulates a generator with try/except around yield.
 *
 * In the simplified model (check_throw returns int, no longjmp),
 * the try/except handler inside the generator cannot catch the thrown
 * exception. The exception propagates out to the caller.
 *
 * pc=0: yield 1, set pc=1
 * pc=1: check_throw — if throw pending, return NULL to propagate
 */
static PyDosObj far * PYDOS_API resume_with_handler(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;

    if (pc == 0) {
        gen->v.gen.pc = 1;
        return pydos_obj_new_int(1L);
    }
    if (pc == 1) {
        /* check_throw returns 1 if throw pending — return NULL to propagate */
        if (pydos_gen_check_throw(gen)) {
            return (PyDosObj far *)0;
        }
        /* No throw: normal resumption, return NULL to exhaust */
        return (PyDosObj far *)0;
    }
    return (PyDosObj far *)0;
}

/*
 * resume_ignores_exit: In the simplified model, check_throw returns 1
 * and resume returns NULL. The generator can't catch GeneratorExit
 * inside the body (no longjmp), so the "ignored GeneratorExit" path
 * is unreachable. close() works correctly because pydos_gen_throw()
 * raises GeneratorExit on the current stack, which close() catches.
 *
 * pc=0: yield 1, set pc=1
 * pc=1: check_throw — if throw pending, return NULL
 */
static PyDosObj far * PYDOS_API resume_ignores_exit(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;

    if (pc == 0) {
        gen->v.gen.pc = 1;
        return pydos_obj_new_int(1L);
    }
    if (pc == 1) {
        if (pydos_gen_check_throw(gen)) {
            return (PyDosObj far *)0;
        }
        return (PyDosObj far *)0;
    }
    return (PyDosObj far *)0;
}

/*
 * resume_accumulator: Simulates an accumulator generator.
 *
 *   def accumulator():
 *       total = 0
 *       while True:
 *           val = yield total
 *           if val is None: break
 *           total = total + val
 *
 * Uses gen->locals[0] as 'total'.
 * pc=0: yield 0 (initial total), set pc=1
 * pc=1: check_throw, read sent → if None break, else total+=sent, yield total
 */
static PyDosObj far * PYDOS_API resume_accumulator(PyDosObj far *gen)
{
    int pc = gen->v.gen.pc;

    if (pc == 0) {
        /* total = 0, yield 0 */
        gen->v.gen.pc = 1;
        return pydos_obj_new_int(0L);
    }
    if (pc == 1) {
        PyDosObj far *sent;
        PyDosObj far *total_obj;
        long total;
        long val;

        if (pydos_gen_check_throw(gen)) {
            return (PyDosObj far *)0;
        }

        sent = pydos_gen_sent;
        if (sent == (PyDosObj far *)0 ||
            (PyDosType)sent->type == PYDT_NONE) {
            return (PyDosObj far *)0;  /* break → StopIteration */
        }

        /* Read current total from locals[0] */
        total_obj = pydos_list_get(gen->v.gen.locals, 0);
        total = (total_obj != (PyDosObj far *)0 &&
                 (PyDosType)total_obj->type == PYDT_INT)
                ? total_obj->v.int_val : 0L;

        /* Add sent value */
        val = ((PyDosType)sent->type == PYDT_INT) ? sent->v.int_val : 0L;
        total = total + val;

        /* Store updated total in locals[0] */
        total_obj = pydos_obj_new_int(total);
        pydos_list_set(gen->v.gen.locals, 0, total_obj);
        PYDOS_DECREF(total_obj);

        /* Yield total */
        gen->v.gen.pc = 1;
        return pydos_obj_new_int(total);
    }
    return (PyDosObj far *)0;
}

/* ================================================================== */
/* Phase 3: send() tests                                               */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* gen_send_none: send(None) on just-started gen is like next()        */
/* ------------------------------------------------------------------ */

TEST(gen_send_none)
{
    PyDosObj far *g;
    PyDosObj far *none_val;
    PyDosObj far *result;

    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    none_val = pydos_obj_new_none();
    result = pydos_gen_send(g, none_val);
    PYDOS_DECREF(none_val);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_STR);
    ASSERT_STR_EQ(result->v.str.data, "ready");

    PYDOS_DECREF(result);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_send_value: send(42) visible at yield resumption                */
/* ------------------------------------------------------------------ */

TEST(gen_send_value)
{
    PyDosObj far *g;
    PyDosObj far *result;
    PyDosObj far *val;

    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    /* First: next() to reach first yield */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result->v.str.data, "ready");
    PYDOS_DECREF(result);

    /* Send 42 → should yield 42 back */
    val = pydos_obj_new_int(42L);
    result = pydos_gen_send(g, val);
    PYDOS_DECREF(val);

    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_send_first_nonnone_error: send non-None on pc=0 → TypeError    */
/* ------------------------------------------------------------------ */

TEST(gen_send_first_nonnone_error)
{
    PyDosObj far *g;
    PyDosObj far *val;
    ExcFrame frame;

    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    val = pydos_obj_new_int(42L);

    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        (void)pydos_gen_send(g, val);
        pydos_exc_pop();
        /* Should not reach here — TypeError expected */
        ASSERT_TRUE(0);
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_TYPE_ERROR);
        pydos_exc_clear();
    }

    PYDOS_DECREF(val);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_send_sequence: alternating send values                          */
/* ------------------------------------------------------------------ */

TEST(gen_send_sequence)
{
    PyDosObj far *g;
    PyDosObj far *result;
    PyDosObj far *val;

    g = pydos_gen_new((void (far *)(void))resume_accumulator, 1);
    ASSERT_NOT_NULL(g);

    /* next() → prime, yields 0 */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 0L);
    PYDOS_DECREF(result);

    /* send(10) → total=10, yields 10 */
    val = pydos_obj_new_int(10L);
    result = pydos_gen_send(g, val);
    PYDOS_DECREF(val);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 10L);
    PYDOS_DECREF(result);

    /* send(20) → total=30, yields 30 */
    val = pydos_obj_new_int(20L);
    result = pydos_gen_send(g, val);
    PYDOS_DECREF(val);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 30L);
    PYDOS_DECREF(result);

    /* send(5) → total=35, yields 35 */
    val = pydos_obj_new_int(5L);
    result = pydos_gen_send(g, val);
    PYDOS_DECREF(val);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 35L);
    PYDOS_DECREF(result);

    PYDOS_DECREF(g);
}

/* ================================================================== */
/* Phase 3: throw() tests                                              */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* gen_throw_propagates: throw into gen with no handler → propagates   */
/* ------------------------------------------------------------------ */

TEST(gen_throw_propagates)
{
    PyDosObj far *g;
    PyDosObj far *result;
    ExcFrame frame;
    int caught;

    /* resume_echo calls check_throw at pc=1 with no exception handler,
     * so a thrown exception should propagate out. */
    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    /* Advance to first yield ("ready") */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);

    /* Throw ValueError — resume_echo has no handler, should propagate */
    caught = 0;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        result = pydos_gen_throw(g, PYDOS_EXC_VALUE_ERROR,
                                  (const char far *)"test throw");
        pydos_exc_pop();
        if (result != (PyDosObj far *)0) {
            PYDOS_DECREF(result);
        }
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_VALUE_ERROR));
        caught = 1;
        pydos_exc_clear();
    }

    ASSERT_TRUE(caught);
    /* Generator should be exhausted */
    ASSERT_EQ(g->v.gen.pc, -1);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_throw_handled: In simplified model, throw always propagates.    */
/* The handler inside the generator can't catch (no longjmp at yield). */
/* ------------------------------------------------------------------ */

TEST(gen_throw_handled)
{
    PyDosObj far *g;
    PyDosObj far *result;
    ExcFrame frame;
    int caught;

    g = pydos_gen_new((void (far *)(void))resume_with_handler, 0);
    ASSERT_NOT_NULL(g);

    /* next() → yields 1 */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 1L);
    PYDOS_DECREF(result);

    /* Throw ValueError — propagates out (handler can't fire) */
    caught = 0;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        result = pydos_gen_throw(g, PYDOS_EXC_VALUE_ERROR,
                                  (const char far *)"test");
        pydos_exc_pop();
        if (result != (PyDosObj far *)0) {
            PYDOS_DECREF(result);
        }
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_VALUE_ERROR));
        caught = 1;
        pydos_exc_clear();
    }

    ASSERT_TRUE(caught);
    ASSERT_EQ(g->v.gen.pc, -1);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_throw_exhausted: throw on exhausted gen → raises directly       */
/* ------------------------------------------------------------------ */

TEST(gen_throw_exhausted)
{
    PyDosObj far *g;
    PyDosObj far *result;
    ExcFrame frame;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    /* Exhaust the generator */
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);  /* returns NULL, sets pc=-1 */
    ASSERT_NULL(result);
    ASSERT_EQ(g->v.gen.pc, -1);

    /* Throw on exhausted generator → raises exception directly */
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        result = pydos_gen_throw(g, PYDOS_EXC_VALUE_ERROR,
                                  (const char far *)"exhausted throw");
        pydos_exc_pop();
        ASSERT_TRUE(0);  /* Should not reach here */
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_VALUE_ERROR);
        pydos_exc_clear();
    }

    PYDOS_DECREF(g);
}

/* ================================================================== */
/* Phase 3: close() tests                                              */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* gen_close_exhausted: close on already-exhausted gen is no-op        */
/* ------------------------------------------------------------------ */

TEST(gen_close_exhausted)
{
    PyDosObj far *g;
    PyDosObj far *result;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    /* Exhaust */
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);  /* exhausted */

    ASSERT_EQ(g->v.gen.pc, -1);

    /* close() on exhausted gen — should be no-op */
    pydos_gen_close(g);
    ASSERT_EQ(g->v.gen.pc, -1);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_close_active: close running gen via check_throw mechanism       */
/* ------------------------------------------------------------------ */

TEST(gen_close_active)
{
    PyDosObj far *g;
    PyDosObj far *result;

    /* resume_echo calls check_throw at pc=1, so close() will set the
     * throw flag and call resume directly.  check_throw returns 1,
     * resume returns NULL, close() sets pc=-1. */
    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    /* Advance to first yield ("ready") */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    ASSERT_STR_EQ(result->v.str.data, "ready");
    PYDOS_DECREF(result);

    /* close() — sets throw flag, calls resume, resume returns NULL,
     * close marks gen exhausted (pc=-1). No setjmp/longjmp needed. */
    pydos_gen_close(g);
    ASSERT_EQ(g->v.gen.pc, -1);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_close_active_simple: close on gen that returns NULL on throw    */
/* In simplified model, close() sets throw flag and calls resume      */
/* directly.  resume returns NULL, close() sets pc=-1.  No            */
/* setjmp/longjmp needed.                                              */
/* ------------------------------------------------------------------ */

TEST(gen_close_active_simple)
{
    PyDosObj far *g;
    PyDosObj far *result;

    g = pydos_gen_new((void (far *)(void))resume_ignores_exit, 0);
    ASSERT_NOT_NULL(g);

    /* Advance to first yield */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->v.int_val, 1L);
    PYDOS_DECREF(result);

    /* close() — sets throw flag, calls resume, resume returns NULL
     * (check_throw returns 1 → mini-epilogue), close sets pc=-1 */
    pydos_gen_close(g);
    ASSERT_EQ(g->v.gen.pc, -1);

    PYDOS_DECREF(g);
}

/* ================================================================== */
/* Phase 3: check_throw() tests                                        */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* gen_check_throw_noop: no flag set → returns normally                */
/* ------------------------------------------------------------------ */

TEST(gen_check_throw_noop)
{
    PyDosObj far *g;
    int result;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    /* No throw flag set — check_throw should return 0 */
    ASSERT_FALSE(g->flags & OBJ_FLAG_GEN_THROW);
    result = pydos_gen_check_throw(g);
    ASSERT_EQ(result, 0);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_check_throw_signals: flag set → returns 1 (no longjmp)          */
/* ------------------------------------------------------------------ */

TEST(gen_check_throw_signals)
{
    PyDosObj far *g;
    int result;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    /* Manually set the throw flag */
    g->flags = (unsigned char)(g->flags | OBJ_FLAG_GEN_THROW);

    /* check_throw should return 1 and clear the flag */
    result = pydos_gen_check_throw(g);
    ASSERT_EQ(result, 1);

    /* Flag should be cleared */
    ASSERT_FALSE(g->flags & OBJ_FLAG_GEN_THROW);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_close_unstarted: close on pc==0 gen → immediate exhaust         */
/* ------------------------------------------------------------------ */

TEST(gen_close_unstarted)
{
    PyDosObj far *g;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(g->v.gen.pc, 0);

    /* close() on unstarted generator — should just set pc=-1 */
    pydos_gen_close(g);
    ASSERT_EQ(g->v.gen.pc, -1);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_throw_unstarted: throw on pc==0 gen → raises directly           */
/* ------------------------------------------------------------------ */

TEST(gen_throw_unstarted)
{
    PyDosObj far *g;
    ExcFrame frame;
    int caught;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);
    ASSERT_EQ(g->v.gen.pc, 0);

    /* throw on unstarted generator — should raise directly */
    caught = 0;
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        (void)pydos_gen_throw(g, PYDOS_EXC_VALUE_ERROR,
                               (const char far *)"unstarted");
        pydos_exc_pop();
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_VALUE_ERROR));
        caught = 1;
        pydos_exc_clear();
    }

    ASSERT_TRUE(caught);
    ASSERT_EQ(g->v.gen.pc, -1);
    PYDOS_DECREF(g);
}

/* ================================================================== */
/* Phase 3: GeneratorExit exception matching tests                     */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* gen_generatorexit_not_exception: GeneratorExit NOT match Exception  */
/* ------------------------------------------------------------------ */

TEST(gen_generatorexit_not_exception)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_GENERATOR_EXIT;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    /* GeneratorExit inherits from BaseException, NOT Exception */
    ASSERT_FALSE(pydos_exc_matches(exc, PYDOS_EXC_EXCEPTION));
    pydos_far_free(exc);
}

/* ------------------------------------------------------------------ */
/* gen_generatorexit_matches_base: GeneratorExit matches BaseException */
/* ------------------------------------------------------------------ */

TEST(gen_generatorexit_matches_base)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc();
    ASSERT_NOT_NULL(exc);
    exc->type = PYDT_EXCEPTION;
    exc->refcount = 1;
    exc->flags = 0;
    exc->v.exc.type_code = PYDOS_EXC_GENERATOR_EXIT;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    /* GeneratorExit DOES match BaseException */
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_BASE));
    /* And itself */
    ASSERT_TRUE(pydos_exc_matches(exc, PYDOS_EXC_GENERATOR_EXIT));
    pydos_far_free(exc);
}

/* ================================================================== */
/* Phase 3: Refcount tests                                             */
/* ================================================================== */

/* ------------------------------------------------------------------ */
/* gen_sent_refcount: verify sent value refcounting                    */
/* ------------------------------------------------------------------ */

TEST(gen_sent_refcount)
{
    PyDosObj far *g;
    PyDosObj far *val;
    PyDosObj far *result;
    unsigned int rc_before;

    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    /* next() to reach first yield */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);

    /* Use a string object (never cached/immortal) to test refcounting */
    val = pydos_str_new((const char far *)"reftest", 7);
    ASSERT_NOT_NULL(val);
    ASSERT_FALSE(val->flags & OBJ_FLAG_IMMORTAL);
    rc_before = val->refcount;
    ASSERT_EQ(rc_before, 1);

    /* send(val) — should INCREF val when storing in global */
    result = pydos_gen_send(g, val);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);

    /* After send completes, pydos_gen_sent still holds a ref to val.
     * send() INCREFs when storing, so refcount should be > rc_before. */
    ASSERT_TRUE(val->refcount > rc_before);

    PYDOS_DECREF(val);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_send_exhausted: send on exhausted gen → StopIteration           */
/* ------------------------------------------------------------------ */

TEST(gen_send_exhausted)
{
    PyDosObj far *g;
    PyDosObj far *result;
    PyDosObj far *val;
    ExcFrame frame;

    g = pydos_gen_new((void (far *)(void))resume_3ints, 0);
    ASSERT_NOT_NULL(g);

    /* Exhaust the generator */
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);
    PYDOS_DECREF(result);
    result = pydos_gen_next(g);  /* NULL, pc=-1 */

    /* send on exhausted gen → StopIteration */
    val = pydos_obj_new_int(42L);
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        result = pydos_gen_send(g, val);
        pydos_exc_pop();
        /* send on exhausted raises StopIteration via pydos_exc_raise */
        ASSERT_NULL(result);
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_STOP_ITERATION);
        pydos_exc_clear();
    }

    PYDOS_DECREF(val);
    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_next_stores_none: next() stores None in pydos_gen_sent          */
/* ------------------------------------------------------------------ */

TEST(gen_next_stores_none)
{
    PyDosObj far *g;
    PyDosObj far *result;

    g = pydos_gen_new((void (far *)(void))resume_echo, 0);
    ASSERT_NOT_NULL(g);

    /* next() should store None in pydos_gen_sent */
    result = pydos_gen_next(g);
    ASSERT_NOT_NULL(result);
    PYDOS_DECREF(result);

    /* After next(), pydos_gen_sent should be None */
    ASSERT_NOT_NULL(pydos_gen_sent);
    ASSERT_EQ((PyDosType)pydos_gen_sent->type, PYDT_NONE);

    PYDOS_DECREF(g);
}

/* ------------------------------------------------------------------ */
/* gen_new_generatorexit_exc: constructor creates correct exception    */
/* ------------------------------------------------------------------ */

TEST(gen_new_generatorexit_exc)
{
    PyDosObj far *exc;
    exc = pydos_exc_new_generatorexit(0, (PyDosObj far * far *)0);
    ASSERT_NOT_NULL(exc);
    ASSERT_EQ(exc->type, PYDT_EXCEPTION);
    ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_GENERATOR_EXIT);
    PYDOS_DECREF(exc);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_gen_tests(void)
{
    SUITE("pdos_gen");
    RUN(gen_new);
    RUN(gen_new_type);
    RUN(gen_new_pc);
    RUN(gen_new_locals_prefilled);
    RUN(gen_next_first);
    RUN(gen_next_sequence);
    RUN(gen_next_exhausted);
    RUN(gen_next_strings);
    RUN(gen_locals_resume);
    RUN(gen_iter_protocol);
    RUN(gen_list_from_iter);
    RUN(gen_list_from_iter_strings);
    /* Phase 3: send/throw/close/check_throw tests */
    RUN(gen_send_none);
    RUN(gen_send_value);
    RUN(gen_send_first_nonnone_error);
    RUN(gen_send_sequence);
    RUN(gen_send_exhausted);
    RUN(gen_sent_refcount);
    RUN(gen_next_stores_none);
    RUN(gen_throw_propagates);
    RUN(gen_throw_handled);
    RUN(gen_throw_exhausted);
    RUN(gen_close_exhausted);
    RUN(gen_close_active);
    RUN(gen_close_active_simple);
    RUN(gen_close_unstarted);
    RUN(gen_throw_unstarted);
    RUN(gen_check_throw_noop);
    RUN(gen_check_throw_signals);
    RUN(gen_generatorexit_not_exception);
    RUN(gen_generatorexit_matches_base);
    RUN(gen_new_generatorexit_exc);
}
