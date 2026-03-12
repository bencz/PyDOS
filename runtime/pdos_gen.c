/*
 * pydos_gen.c - Generator helpers for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Phase 3: full generator protocol — send/throw/close + check_throw.
 * Uses the state-machine architecture (no stack switching).
 */

#include "pdos_gen.h"
#include "pdos_exc.h"
#include "pdos_lst.h"
#include "pdos_obj.h"

#include "pdos_mem.h"

/* Resume function typedef */
typedef PyDosObj far * (PYDOS_API far *ResumeFn)(PyDosObj far *);

/* ------------------------------------------------------------------ */
/* Module globals                                                      */
/* ------------------------------------------------------------------ */

/* Sent value: set before calling resume(), read inside at yield resume.
 * pydos_gen_next() stores None, pydos_gen_send() stores user value. */
PyDosObj far * pydos_gen_sent = (PyDosObj far *)0;

/* Pending throw state: set by pydos_gen_throw(), read by check_throw(). */
static int gen_pending_throw_type = 0;
static const char far *gen_pending_throw_msg = (const char far *)0;

/* ------------------------------------------------------------------ */
/* pydos_gen_new                                                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_gen_new(void (far *resume)(void), int num_locals)
{
    PyDosObj far *gen;
    PyDosObj far *locals_list;
    PyDosObj far *none_val;
    int i;

    gen = pydos_obj_alloc();
    if (gen == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    gen->type = PYDT_GENERATOR;
    gen->flags = 0;
    gen->refcount = 1;
    gen->v.gen.resume = resume;
    gen->v.gen.pc = 0;
    gen->v.gen.state = (PyDosObj far *)0;

    /* Create a list to hold local variables, pre-filled with None */
    if (num_locals > 0) {
        locals_list = pydos_list_new((unsigned int)num_locals);
        gen->v.gen.locals = locals_list;
        /* Pre-fill with None so pydos_list_set works on all indices */
        none_val = pydos_obj_new_none();
        for (i = 0; i < num_locals; i++) {
            pydos_list_append(locals_list, none_val);
        }
        PYDOS_DECREF(none_val);
    } else {
        gen->v.gen.locals = (PyDosObj far *)0;
    }

    return gen;
}

/* ------------------------------------------------------------------ */
/* pydos_gen_next                                                      */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_gen_next(PyDosObj far *gen)
{
    PyDosObj far *result;
    PyDosObj far *none_val;
    ResumeFn fn;

    if (gen == (PyDosObj far *)0 ||
        ((PyDosType)gen->type != PYDT_GENERATOR &&
         (PyDosType)gen->type != PYDT_COROUTINE)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"next() on non-generator");
        return (PyDosObj far *)0;
    }

    /* Check if generator is exhausted (pc == -1) */
    if (gen->v.gen.pc < 0) {
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION, (const char far *)"");
        return (PyDosObj far *)0;
    }

    /* If no resume function, this is a built-in iterator stub */
    if (gen->v.gen.resume == (void (far *)(void))0) {
        gen->v.gen.pc = -1;
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION, (const char far *)"");
        return (PyDosObj far *)0;
    }

    /* Store None in sent value global (next() sends None) */
    none_val = pydos_obj_new_none();
    PYDOS_DECREF(pydos_gen_sent);
    pydos_gen_sent = none_val;

    /* Clear throw flag */
    gen->flags = (unsigned char)(gen->flags & ~OBJ_FLAG_GEN_THROW);

    /* Call the resume function: result = resume(gen) */
    fn = (ResumeFn)gen->v.gen.resume;
    result = fn(gen);

    /* NULL return means generator completed -> StopIteration */
    if (result == (PyDosObj far *)0) {
        gen->v.gen.pc = -1;
        return (PyDosObj far *)0;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_gen_send                                                      */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_gen_send(PyDosObj far *gen, PyDosObj far *value)
{
    PyDosObj far *result;
    ResumeFn fn;

    if (gen == (PyDosObj far *)0 ||
        ((PyDosType)gen->type != PYDT_GENERATOR &&
         (PyDosType)gen->type != PYDT_COROUTINE)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"send() on non-generator");
        return (PyDosObj far *)0;
    }

    /* If generator is exhausted */
    if (gen->v.gen.pc < 0) {
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION, (const char far *)"");
        return (PyDosObj far *)0;
    }

    /* If generator is just-started (pc == 0) and value is not None:
     * TypeError per Python spec */
    if (gen->v.gen.pc == 0 && value != (PyDosObj far *)0 &&
        (PyDosType)value->type != PYDT_NONE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
            (const char far *)"can't send non-None value to a just-started generator");
        return (PyDosObj far *)0;
    }

    /* If no resume function, built-in iterator stub */
    if (gen->v.gen.resume == (void (far *)(void))0) {
        gen->v.gen.pc = -1;
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION, (const char far *)"");
        return (PyDosObj far *)0;
    }

    /* Store sent value in global (INCREF new, DECREF old) */
    if (value != (PyDosObj far *)0) {
        PYDOS_INCREF(value);
    }
    PYDOS_DECREF(pydos_gen_sent);
    pydos_gen_sent = value;

    /* Clear throw flag */
    gen->flags = (unsigned char)(gen->flags & ~OBJ_FLAG_GEN_THROW);

    /* Call resume */
    fn = (ResumeFn)gen->v.gen.resume;
    result = fn(gen);

    /* NULL return means generator completed -> StopIteration */
    if (result == (PyDosObj far *)0) {
        gen->v.gen.pc = -1;
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION, (const char far *)"");
        return (PyDosObj far *)0;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_gen_throw                                                     */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_gen_throw(PyDosObj far *gen, int exc_type,
                                          const char far *exc_msg)
{
    PyDosObj far *result;
    PyDosObj far *none_val;
    ResumeFn fn;

    if (gen == (PyDosObj far *)0 ||
        ((PyDosType)gen->type != PYDT_GENERATOR &&
         (PyDosType)gen->type != PYDT_COROUTINE)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"throw() on non-generator");
        return (PyDosObj far *)0;
    }

    /* If generator is exhausted, raise the exception directly */
    if (gen->v.gen.pc < 0) {
        pydos_exc_raise(exc_type, exc_msg);
        return (PyDosObj far *)0;
    }

    /* If generator not started (pc==0), can't throw into body —
     * mark exhausted and raise directly */
    if (gen->v.gen.pc == 0) {
        gen->v.gen.pc = -1;
        pydos_exc_raise(exc_type, exc_msg);
        return (PyDosObj far *)0;
    }

    /* If no resume function, raise directly */
    if (gen->v.gen.resume == (void (far *)(void))0) {
        gen->v.gen.pc = -1;
        pydos_exc_raise(exc_type, exc_msg);
        return (PyDosObj far *)0;
    }

    /* Store pending throw info in module statics */
    gen_pending_throw_type = exc_type;
    gen_pending_throw_msg = exc_msg;

    /* Set throw flag on generator object */
    gen->flags = (unsigned char)(gen->flags | OBJ_FLAG_GEN_THROW);

    /* Store None in sent value (throw doesn't send a value) */
    none_val = pydos_obj_new_none();
    PYDOS_DECREF(pydos_gen_sent);
    pydos_gen_sent = none_val;

    /* Call resume — check_throw inside will return 1, causing resume
     * to return NULL. No setjmp wrapper needed since check_throw
     * no longer longjmps (avoids stale jmp_buf crash on DOS). */
    fn = (ResumeFn)gen->v.gen.resume;
    result = fn(gen);

    /* If resume returned a value: generator yielded successfully
     * (handler caught the throw and yielded) */
    if (result != (PyDosObj far *)0) {
        return result;
    }

    /* Resume returned NULL — check_throw signaled throw pending.
     * Mark exhausted and raise the original thrown exception
     * on the CURRENT (valid) C stack. */
    gen->v.gen.pc = -1;
    pydos_exc_raise(exc_type, exc_msg);
    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_gen_close                                                     */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gen_close(PyDosObj far *gen)
{
    PyDosObj far *result;
    PyDosObj far *none_val;
    ResumeFn fn;

    if (gen == (PyDosObj far *)0 ||
        ((PyDosType)gen->type != PYDT_GENERATOR &&
         (PyDosType)gen->type != PYDT_COROUTINE)) {
        return;
    }

    /* If already exhausted, nothing to do */
    if (gen->v.gen.pc < 0) {
        return;
    }

    /* Not started — just mark exhausted, no body to clean up */
    if (gen->v.gen.pc == 0) {
        gen->v.gen.pc = -1;
        return;
    }

    /* No resume function — mark exhausted */
    if (gen->v.gen.resume == (void (far *)(void))0) {
        gen->v.gen.pc = -1;
        return;
    }

    /* Set throw flag with GeneratorExit and call resume directly.
     * Since check_throw returns 1 (no longjmp), resume will take the
     * mini-epilogue and return NULL.  No setjmp/longjmp wrapper needed —
     * this avoids the stale-frame / CauseWay crash on DOS 386. */
    gen_pending_throw_type = PYDOS_EXC_GENERATOR_EXIT;
    gen_pending_throw_msg = (const char far *)"";
    gen->flags = (unsigned char)(gen->flags | OBJ_FLAG_GEN_THROW);

    none_val = pydos_obj_new_none();
    PYDOS_DECREF(pydos_gen_sent);
    pydos_gen_sent = none_val;

    fn = (ResumeFn)gen->v.gen.resume;
    result = fn(gen);

    gen->v.gen.pc = -1;

    if (result != (PyDosObj far *)0) {
        /* Generator yielded after GeneratorExit — RuntimeError per spec */
        PYDOS_DECREF(result);
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
            (const char far *)"generator ignored GeneratorExit");
    }
    /* result == NULL: generator closed cleanly, done. */
}

/* ------------------------------------------------------------------ */
/* pydos_gen_check_throw                                               */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_gen_check_throw(PyDosObj far *gen)
{
    if (gen == (PyDosObj far *)0) return 0;

    /* If no throw flag set, return immediately (fast path) */
    if (!(gen->flags & OBJ_FLAG_GEN_THROW)) return 0;

    /* Clear flag and pending state.
     * The actual exception will be raised by pydos_gen_throw()
     * on the current (valid) C stack after resume returns NULL. */
    gen->flags = (unsigned char)(gen->flags & ~OBJ_FLAG_GEN_THROW);
    gen_pending_throw_type = 0;
    gen_pending_throw_msg = (const char far *)0;

    return 1; /* throw pending — caller should return NULL */
}

/* ------------------------------------------------------------------ */
/* Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_gen_init(void)
{
    pydos_gen_sent = (PyDosObj far *)0;
    gen_pending_throw_type = 0;
    gen_pending_throw_msg = (const char far *)0;
}

void PYDOS_API pydos_gen_shutdown(void)
{
    if (pydos_gen_sent != (PyDosObj far *)0) {
        PYDOS_DECREF(pydos_gen_sent);
        pydos_gen_sent = (PyDosObj far *)0;
    }
    gen_pending_throw_type = 0;
    gen_pending_throw_msg = (const char far *)0;
}
