/*
 * pdos_asn.c - Coroutine / async runtime for PyDOS
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Phase 4: async def / await — cooperative scheduling.
 * Coroutines reuse PyDosGen struct layout and pydos_gen_send() for resumption.
 * The event loop (async_run) drives coroutines via a trampoline pattern.
 */

#include "pdos_asn.h"
#include "pdos_gen.h"
#include "pdos_exc.h"
#include "pdos_lst.h"
#include "pdos_obj.h"
#include "pdos_mem.h"

/* Resume function typedef (same as pdos_gen.c) */
typedef PyDosObj far * (PYDOS_API far *ResumeFn)(PyDosObj far *);

/* Maximum awaiter stack depth for async_run trampoline */
#define ASYNC_MAX_DEPTH  16

/* Maximum concurrent tasks for async_gather */
#define ASYNC_MAX_TASKS  16

/* Per-task state for gather round-robin */
typedef struct {
    PyDosObj far *stack[8]; /* awaiter chain */
    int           depth;
    PyDosObj far *current;
    PyDosObj far *result;
    int           done;
} GatherTask;

/* ------------------------------------------------------------------ */
/* pydos_cor_new                                                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_cor_new(void (far *resume)(void), int num_locals)
{
    PyDosObj far *cor;
    PyDosObj far *locals_list;
    PyDosObj far *none_val;
    int i;

    cor = pydos_obj_alloc();
    if (cor == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    cor->type = PYDT_COROUTINE;
    cor->flags = 0;
    cor->refcount = 1;
    cor->v.gen.resume = resume;
    cor->v.gen.pc = 0;

    /* Initialize state to None (will hold return value) */
    none_val = pydos_obj_new_none();
    cor->v.gen.state = none_val;

    /* Create locals list pre-filled with None */
    if (num_locals > 0) {
        locals_list = pydos_list_new((unsigned int)num_locals);
        cor->v.gen.locals = locals_list;
        none_val = pydos_obj_new_none();
        for (i = 0; i < num_locals; i++) {
            pydos_list_append(locals_list, none_val);
        }
        PYDOS_DECREF(none_val);
    } else {
        cor->v.gen.locals = (PyDosObj far *)0;
    }

    return cor;
}

/* ------------------------------------------------------------------ */
/* cor_send — helper to send a value into a coroutine                  */
/* Reuses generator send machinery but accepts PYDT_COROUTINE type.    */
/* ------------------------------------------------------------------ */
static PyDosObj far *cor_send(PyDosObj far *cor, PyDosObj far *value)
{
    PyDosObj far *result;
    ResumeFn fn;

    if (cor == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    /* If coroutine is exhausted */
    if (cor->v.gen.pc < 0) {
        return (PyDosObj far *)0;
    }

    /* If no resume function */
    if (cor->v.gen.resume == (void (far *)(void))0) {
        cor->v.gen.pc = -1;
        return (PyDosObj far *)0;
    }

    /* Store sent value in global (INCREF new, DECREF old) */
    if (value != (PyDosObj far *)0) {
        PYDOS_INCREF(value);
    }
    PYDOS_DECREF(pydos_gen_sent);
    pydos_gen_sent = value;

    /* Clear throw flag */
    cor->flags = (unsigned char)(cor->flags & ~OBJ_FLAG_GEN_THROW);

    /* Call resume */
    fn = (ResumeFn)cor->v.gen.resume;
    result = fn(cor);

    /* NULL return = coroutine finished */
    if (result == (PyDosObj far *)0) {
        cor->v.gen.pc = -1;
        return (PyDosObj far *)0;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_async_run — trampoline event loop                             */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_async_run(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *stack[ASYNC_MAX_DEPTH];
    PyDosObj far *current;
    PyDosObj far *send_val;
    PyDosObj far *result;
    PyDosObj far *ret_val;
    int depth;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"async_run() requires a coroutine argument");
        return pydos_obj_new_none();
    }

    current = argv[0];
    if ((PyDosType)current->type != PYDT_COROUTINE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"async_run() argument must be a coroutine");
        return pydos_obj_new_none();
    }

    PYDOS_INCREF(current);
    depth = 0;
    send_val = pydos_obj_new_none();

    for (;;) {
        result = cor_send(current, send_val);
        PYDOS_DECREF(send_val);
        send_val = (PyDosObj far *)0;

        if (result == (PyDosObj far *)0) {
            /* Current coroutine finished — get return value from state */
            ret_val = current->v.gen.state;
            if (ret_val != (PyDosObj far *)0) {
                PYDOS_INCREF(ret_val);
            } else {
                ret_val = pydos_obj_new_none();
            }

            if (depth == 0) {
                /* Root coroutine done — return its result */
                PYDOS_DECREF(current);
                return ret_val;
            }

            /* Pop parent from stack, send return value to it */
            PYDOS_DECREF(current);
            depth--;
            current = stack[depth];
            send_val = ret_val;
        } else if ((PyDosType)result->type == PYDT_COROUTINE) {
            /* await sub-coroutine: push current, switch to sub */
            if (depth >= ASYNC_MAX_DEPTH) {
                PYDOS_DECREF(result);
                PYDOS_DECREF(current);
                pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                    (const char far *)"async_run: await depth overflow");
                return pydos_obj_new_none();
            }
            stack[depth] = current;
            depth++;
            current = result; /* result already has refcount from yield */
            send_val = pydos_obj_new_none();
        } else {
            /* await non-coroutine (e.g. None from sleep) — send it right back */
            send_val = result;
        }
    }
}

/* ------------------------------------------------------------------ */
/* pydos_async_gather — round-robin cooperative scheduler              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_async_gather(int argc, PyDosObj far * far *argv)
{
    GatherTask tasks[ASYNC_MAX_TASKS];
    PyDosObj far *task_list;
    PyDosObj far *results;
    PyDosObj far *cor;
    PyDosObj far *result;
    PyDosObj far *ret_val;
    PyDosObj far *send_val;
    int num_tasks;
    int all_done;
    int i;
    int ti;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"async_gather() requires a list argument");
        return pydos_obj_new_none();
    }

    task_list = argv[0];
    if ((PyDosType)task_list->type != PYDT_LIST) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"async_gather() argument must be a list");
        return pydos_obj_new_none();
    }

    num_tasks = (int)pydos_list_len(task_list);
    if (num_tasks <= 0) {
        return pydos_list_new(0);
    }
    if (num_tasks > ASYNC_MAX_TASKS) {
        num_tasks = ASYNC_MAX_TASKS;
    }

    /* Initialize per-task state */
    for (i = 0; i < num_tasks; i++) {
        int j;
        cor = pydos_list_get(task_list, (unsigned int)i);
        PYDOS_INCREF(cor);
        tasks[i].current = cor;
        tasks[i].depth = 0;
        tasks[i].result = (PyDosObj far *)0;
        tasks[i].done = 0;
        for (j = 0; j < 8; j++) {
            tasks[i].stack[j] = (PyDosObj far *)0;
        }
    }

    /* Round-robin until all tasks are done */
    for (;;) {
        all_done = 1;
        for (ti = 0; ti < num_tasks; ti++) {
            if (tasks[ti].done) continue;
            all_done = 0;

            /* Step this task once */
            send_val = (tasks[ti].result != (PyDosObj far *)0)
                       ? tasks[ti].result
                       : pydos_obj_new_none();
            tasks[ti].result = (PyDosObj far *)0;

            result = cor_send(tasks[ti].current, send_val);
            PYDOS_DECREF(send_val);

            if (result == (PyDosObj far *)0) {
                /* Sub-coroutine or task finished */
                ret_val = tasks[ti].current->v.gen.state;
                if (ret_val != (PyDosObj far *)0) {
                    PYDOS_INCREF(ret_val);
                } else {
                    ret_val = pydos_obj_new_none();
                }

                if (tasks[ti].depth == 0) {
                    /* Task itself is done */
                    PYDOS_DECREF(tasks[ti].current);
                    tasks[ti].current = (PyDosObj far *)0;
                    tasks[ti].result = ret_val;
                    tasks[ti].done = 1;
                } else {
                    /* Pop awaiter */
                    PYDOS_DECREF(tasks[ti].current);
                    tasks[ti].depth--;
                    tasks[ti].current = tasks[ti].stack[tasks[ti].depth];
                    tasks[ti].stack[tasks[ti].depth] = (PyDosObj far *)0;
                    tasks[ti].result = ret_val;
                }
            } else if ((PyDosType)result->type == PYDT_COROUTINE) {
                /* await sub-coroutine */
                if (tasks[ti].depth < 8) {
                    tasks[ti].stack[tasks[ti].depth] = tasks[ti].current;
                    tasks[ti].depth++;
                    tasks[ti].current = result;
                    tasks[ti].result = (PyDosObj far *)0;
                } else {
                    PYDOS_DECREF(result);
                    tasks[ti].done = 1;
                }
            } else {
                /* Non-coroutine yield (e.g. None from "await None") — send back */
                PYDOS_DECREF(result);
                tasks[ti].result = pydos_obj_new_none();
            }
        }

        if (all_done) break;
    }

    /* Build results list */
    results = pydos_list_new((unsigned int)num_tasks);
    for (i = 0; i < num_tasks; i++) {
        if (tasks[i].result != (PyDosObj far *)0) {
            pydos_list_append(results, tasks[i].result);
            PYDOS_DECREF(tasks[i].result);
        } else {
            PyDosObj far *none_v = pydos_obj_new_none();
            pydos_list_append(results, none_v);
            PYDOS_DECREF(none_v);
        }
    }

    return results;
}

/* ------------------------------------------------------------------ */
/* Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_cor_init(void)
{
    /* No state to initialize — coroutines reuse gen_sent global */
}

void PYDOS_API pydos_cor_shutdown(void)
{
    /* No state to clean up — gen_sent cleaned by pydos_gen_shutdown */
}
