/*
 * t_asn.c - Unit tests for pdos_asn module (coroutine / async runtime)
 *
 * Tests coroutine creation, type identity, state field, iteration refusal,
 * send/resume, async_run trampoline, and async_gather round-robin.
 */

#include "testfw.h"
#include "../runtime/pdos_asn.h"
#include "../runtime/pdos_gen.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_exc.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_str.h"
#include "../runtime/pdos_mem.h"

#include <setjmp.h>

/* ------------------------------------------------------------------ */
/* Dummy / test resume functions                                       */
/* ------------------------------------------------------------------ */

/* Resume function typedef (same as runtime) */
typedef PyDosObj far * (PYDOS_API far *ResumeFn)(PyDosObj far *);

/* Dummy resume: returns NULL immediately (coroutine exhausted) */
static PyDosObj far * PYDOS_API dummy_cor_resume(PyDosObj far *cor)
{
    (void)cor;
    return (PyDosObj far *)0;
}

/*
 * Resume function that returns 42 immediately (no yields).
 * pc=0: set state=42, return NULL (exhausted)
 */
static PyDosObj far * PYDOS_API resume_return_42(PyDosObj far *cor)
{
    if (cor->v.gen.pc == 0) {
        /* Store return value in state */
        PyDosObj far *val = pydos_obj_new_int(42L);
        PyDosObj far *old_state = cor->v.gen.state;
        PYDOS_INCREF(val);
        cor->v.gen.state = val;
        PYDOS_DECREF(old_state);
        PYDOS_DECREF(val);
        cor->v.gen.pc = -1;
        return (PyDosObj far *)0; /* exhausted */
    }
    return (PyDosObj far *)0;
}

/*
 * Resume function that yields one coroutine (await sub), then returns.
 * pc=0: yield None (sleep point), set pc=1
 * pc=1: return NULL (exhausted), state = 99
 */
static PyDosObj far * PYDOS_API resume_one_yield(PyDosObj far *cor)
{
    int pc = cor->v.gen.pc;

    if (pc == 0) {
        cor->v.gen.pc = 1;
        return pydos_obj_new_none(); /* yield None (sleep point) */
    }
    if (pc == 1) {
        /* Store return value 99 in state */
        PyDosObj far *val = pydos_obj_new_int(99L);
        PyDosObj far *old_state = cor->v.gen.state;
        PYDOS_INCREF(val);
        cor->v.gen.state = val;
        PYDOS_DECREF(old_state);
        PYDOS_DECREF(val);
        cor->v.gen.pc = -1;
        return (PyDosObj far *)0; /* exhausted */
    }
    return (PyDosObj far *)0;
}

/*
 * Resume function that yields a sub-coroutine (simulating "await sub_cor()").
 * pc=0: create + yield a sub-coroutine that returns 42 immediately
 * pc=1: get sent value (result of sub-cor), store it as state, return NULL
 */
static PyDosObj far * PYDOS_API resume_await_sub(PyDosObj far *cor)
{
    int pc = cor->v.gen.pc;

    if (pc == 0) {
        /* Create sub-coroutine */
        PyDosObj far *sub = pydos_cor_new(
            (void (far *)(void))resume_return_42, 0);
        cor->v.gen.pc = 1;
        return sub; /* yield coroutine = await */
    }
    if (pc == 1) {
        /* Result of await comes via pydos_gen_sent */
        PyDosObj far *sent = pydos_gen_sent;
        PyDosObj far *old_state;
        if (sent != (PyDosObj far *)0) {
            PYDOS_INCREF(sent);
        }
        /* Store in state as return value */
        old_state = cor->v.gen.state;
        cor->v.gen.state = sent;
        PYDOS_DECREF(old_state);
        cor->v.gen.pc = -1;
        return (PyDosObj far *)0; /* exhausted */
    }
    return (PyDosObj far *)0;
}

/*
 * Resume function for gather test: yields None n times then finishes.
 * Uses locals[0] as counter, locals[1] as limit.
 * Returns "done" string as final value.
 */
static PyDosObj far * PYDOS_API resume_worker(PyDosObj far *cor)
{
    int pc = cor->v.gen.pc;
    PyDosObj far *counter_obj;
    PyDosObj far *limit_obj;
    long counter;
    long limit;
    PyDosObj far *old_state;

    if (pc == 0) {
        /* First call: read limit from locals[1], init counter 0 in locals[0] */
        limit_obj = pydos_list_get(cor->v.gen.locals, 1);
        limit = limit_obj->v.int_val;
        counter = 0;
        if (counter >= limit) {
            /* Return immediately */
            old_state = cor->v.gen.state;
            cor->v.gen.state = pydos_str_new((const char far *)"done", 4);
            PYDOS_INCREF(cor->v.gen.state);
            PYDOS_DECREF(old_state);
            cor->v.gen.pc = -1;
            return (PyDosObj far *)0;
        }
        /* Save counter, yield None */
        {
            PyDosObj far *cnt = pydos_obj_new_int(counter + 1);
            pydos_list_set(cor->v.gen.locals, 0, cnt);
            PYDOS_DECREF(cnt);
        }
        cor->v.gen.pc = 1;
        return pydos_obj_new_none(); /* yield point */
    }

    if (pc == 1) {
        /* Subsequent calls: check counter vs limit */
        counter_obj = pydos_list_get(cor->v.gen.locals, 0);
        limit_obj = pydos_list_get(cor->v.gen.locals, 1);
        counter = counter_obj->v.int_val;
        limit = limit_obj->v.int_val;

        if (counter >= limit) {
            /* Done */
            old_state = cor->v.gen.state;
            cor->v.gen.state = pydos_str_new((const char far *)"done", 4);
            PYDOS_INCREF(cor->v.gen.state);
            PYDOS_DECREF(old_state);
            cor->v.gen.pc = -1;
            return (PyDosObj far *)0;
        }
        /* Increment counter, yield None */
        {
            PyDosObj far *cnt = pydos_obj_new_int(counter + 1);
            pydos_list_set(cor->v.gen.locals, 0, cnt);
            PYDOS_DECREF(cnt);
        }
        return pydos_obj_new_none(); /* yield point */
    }

    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* Tests                                                               */
/* ------------------------------------------------------------------ */

TEST(cor_new)
{
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 2);
    ASSERT_NOT_NULL(c);
    PYDOS_DECREF(c);
}

TEST(cor_type)
{
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->type, PYDT_COROUTINE);
    PYDOS_DECREF(c);
}

TEST(cor_pc_init)
{
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->v.gen.pc, 0);
    PYDOS_DECREF(c);
}

TEST(cor_state_init)
{
    /* State should be initialized to None */
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->v.gen.state);
    ASSERT_EQ(c->v.gen.state->type, PYDT_NONE);
    PYDOS_DECREF(c);
}

TEST(cor_locals_alloc)
{
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 3);
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(c->v.gen.locals);
    ASSERT_EQ(pydos_list_len(c->v.gen.locals), 3L);
    PYDOS_DECREF(c);
}

TEST(cor_locals_zero)
{
    /* 0 locals => locals ptr is NULL */
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    ASSERT_NOT_NULL(c);
    ASSERT_NULL(c->v.gen.locals);
    PYDOS_DECREF(c);
}

TEST(cor_type_str)
{
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    PyDosObj far *s;
    ASSERT_NOT_NULL(c);
    s = pydos_obj_to_str(c);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "<coroutine object>");
    PYDOS_DECREF(s);
    PYDOS_DECREF(c);
}

TEST(cor_not_iterable)
{
    /* Coroutine type is PYDT_COROUTINE, not PYDT_GENERATOR;
     * get_iter raises TypeError (via pydos_exc_raise which longjmps),
     * so we verify the type tag instead of calling get_iter directly. */
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->type, PYDT_COROUTINE);
    /* PYDT_COROUTINE != PYDT_GENERATOR ensures for-loop won't iterate it */
    ASSERT_TRUE(c->type != PYDT_GENERATOR);
    PYDOS_DECREF(c);
}

TEST(cor_release)
{
    /* Create and release — verify no crash */
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 2);
    ASSERT_NOT_NULL(c);
    ASSERT_EQ(c->refcount, 1);
    PYDOS_DECREF(c);
    /* If we get here without crash, the test passes */
}

TEST(async_run_simple)
{
    /* Resume function returns 42 immediately (no yields) */
    PyDosObj far *cor;
    PyDosObj far *result;
    PyDosObj far *args[1];

    cor = pydos_cor_new((void (far *)(void))resume_return_42, 0);
    ASSERT_NOT_NULL(cor);
    args[0] = cor;
    result = pydos_async_run(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(cor);
}

TEST(async_run_one_yield)
{
    /* Coroutine yields once (None), then returns 99 */
    PyDosObj far *cor;
    PyDosObj far *result;
    PyDosObj far *args[1];

    cor = pydos_cor_new((void (far *)(void))resume_one_yield, 0);
    ASSERT_NOT_NULL(cor);
    args[0] = cor;
    result = pydos_async_run(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 99L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(cor);
}

TEST(async_run_await_sub)
{
    /* Coroutine awaits a sub-coroutine that returns 42 */
    PyDosObj far *cor;
    PyDosObj far *result;
    PyDosObj far *args[1];

    cor = pydos_cor_new((void (far *)(void))resume_await_sub, 0);
    ASSERT_NOT_NULL(cor);
    args[0] = cor;
    result = pydos_async_run(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(cor);
}

TEST(async_run_return_none)
{
    /* Coroutine with no explicit return value -> returns None */
    PyDosObj far *cor;
    PyDosObj far *result;
    PyDosObj far *args[1];

    cor = pydos_cor_new((void (far *)(void))dummy_cor_resume, 0);
    ASSERT_NOT_NULL(cor);
    args[0] = cor;
    result = pydos_async_run(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);
    PYDOS_DECREF(result);
    PYDOS_DECREF(cor);
}

TEST(async_gather_empty)
{
    /* Gather with empty list should return empty list */
    PyDosObj far *task_list;
    PyDosObj far *result;
    PyDosObj far *args[1];

    task_list = pydos_list_new(0);
    args[0] = task_list;
    result = pydos_async_gather(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_LIST);
    ASSERT_EQ(pydos_list_len(result), 0L);
    PYDOS_DECREF(result);
    PYDOS_DECREF(task_list);
}

TEST(async_gather_basic)
{
    /* Two coroutines that return immediately */
    PyDosObj far *task_list;
    PyDosObj far *cor1;
    PyDosObj far *cor2;
    PyDosObj far *result;
    PyDosObj far *r0;
    PyDosObj far *r1;
    PyDosObj far *args[1];

    cor1 = pydos_cor_new((void (far *)(void))resume_return_42, 0);
    cor2 = pydos_cor_new((void (far *)(void))resume_one_yield, 0);
    task_list = pydos_list_new(2);
    pydos_list_append(task_list, cor1);
    pydos_list_append(task_list, cor2);

    args[0] = task_list;
    result = pydos_async_gather(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_LIST);
    ASSERT_EQ(pydos_list_len(result), 2L);

    r0 = pydos_list_get(result, 0);
    ASSERT_EQ(r0->type, PYDT_INT);
    ASSERT_EQ(r0->v.int_val, 42L);

    r1 = pydos_list_get(result, 1);
    ASSERT_EQ(r1->type, PYDT_INT);
    ASSERT_EQ(r1->v.int_val, 99L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(cor1);
    PYDOS_DECREF(cor2);
    PYDOS_DECREF(task_list);
}

TEST(async_gather_workers)
{
    /* Two worker coroutines that yield multiple times */
    PyDosObj far *task_list;
    PyDosObj far *cor1;
    PyDosObj far *cor2;
    PyDosObj far *result;
    PyDosObj far *r0;
    PyDosObj far *r1;
    PyDosObj far *limit_obj;
    PyDosObj far *args[1];

    /* Worker 1: yields 2 times */
    cor1 = pydos_cor_new((void (far *)(void))resume_worker, 2);
    limit_obj = pydos_obj_new_int(2L);
    pydos_list_set(cor1->v.gen.locals, 1, limit_obj);
    PYDOS_DECREF(limit_obj);

    /* Worker 2: yields 3 times */
    cor2 = pydos_cor_new((void (far *)(void))resume_worker, 2);
    limit_obj = pydos_obj_new_int(3L);
    pydos_list_set(cor2->v.gen.locals, 1, limit_obj);
    PYDOS_DECREF(limit_obj);

    task_list = pydos_list_new(2);
    pydos_list_append(task_list, cor1);
    pydos_list_append(task_list, cor2);

    args[0] = task_list;
    result = pydos_async_gather(1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(pydos_list_len(result), 2L);

    /* Both workers return "done" */
    r0 = pydos_list_get(result, 0);
    ASSERT_EQ(r0->type, PYDT_STR);
    ASSERT_STR_EQ(r0->v.str.data, "done");

    r1 = pydos_list_get(result, 1);
    ASSERT_EQ(r1->type, PYDT_STR);
    ASSERT_STR_EQ(r1->v.str.data, "done");

    PYDOS_DECREF(result);
    PYDOS_DECREF(cor1);
    PYDOS_DECREF(cor2);
    PYDOS_DECREF(task_list);
}

TEST(cor_gen_send_compat)
{
    /* pydos_gen_send works on coroutines (shared dispatch) */
    PyDosObj far *cor;
    PyDosObj far *val;
    PyDosObj far *none_v;
    ExcFrame frame;

    cor = pydos_cor_new((void (far *)(void))resume_one_yield, 0);
    ASSERT_NOT_NULL(cor);

    /* First send: should yield None */
    none_v = pydos_obj_new_none();
    val = pydos_gen_send(cor, none_v);
    PYDOS_DECREF(none_v);
    ASSERT_NOT_NULL(val);
    ASSERT_EQ(val->type, PYDT_NONE);
    PYDOS_DECREF(val);

    /* Second send: should exhaust → StopIteration raised */
    none_v = pydos_obj_new_none();
    frame.handled = 0;
    frame.exc_value = (PyDosObj far *)0;
    frame.cleanup = (void (*)(void))0;
    pydos_exc_push(&frame);
    if (setjmp(frame.env) == 0) {
        val = pydos_gen_send(cor, none_v);
        pydos_exc_pop();
        /* StopIteration expected — should not reach here */
        PYDOS_DECREF(none_v);
        ASSERT_TRUE(0);
    } else {
        PyDosObj far *exc;
        pydos_exc_pop();
        exc = pydos_exc_current();
        ASSERT_NOT_NULL(exc);
        ASSERT_EQ(exc->v.exc.type_code, PYDOS_EXC_STOP_ITERATION);
        pydos_exc_clear();
    }
    PYDOS_DECREF(none_v);
    PYDOS_DECREF(cor);
}

TEST(cor_gc_type)
{
    /* Verify coroutine is treated as container type by GC */
    PyDosObj far *c = pydos_cor_new((void (far *)(void))dummy_cor_resume, 2);
    ASSERT_NOT_NULL(c);
    /* Container types have GC_TRACKED flag set if tracked */
    /* For now just verify the object can be freed cleanly */
    ASSERT_EQ(c->type, PYDT_COROUTINE);
    PYDOS_DECREF(c);
}

/* ------------------------------------------------------------------ */
/* Test runner                                                         */
/* ------------------------------------------------------------------ */

void run_asn_tests(void)
{
    SUITE("pdos_asn");
    RUN(cor_new);
    RUN(cor_type);
    RUN(cor_pc_init);
    RUN(cor_state_init);
    RUN(cor_locals_alloc);
    RUN(cor_locals_zero);
    RUN(cor_type_str);
    RUN(cor_not_iterable);
    RUN(cor_release);
    RUN(async_run_simple);
    RUN(async_run_one_yield);
    RUN(async_run_await_sub);
    RUN(async_run_return_none);
    RUN(async_gather_empty);
    RUN(async_gather_basic);
    RUN(async_gather_workers);
    RUN(cor_gen_send_compat);
    RUN(cor_gc_type);
}
