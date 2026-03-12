/*
 * piresc.cpp - PIR escape analysis pass
 *
 * For each SSA value, determines whether it escapes the function:
 *   ESC_NO_ESCAPE    - Value is local only, safe for arena allocation
 *   ESC_ARG_ESCAPE   - Value is passed to a call (callee may store it)
 *   ESC_GLOBAL_ESCAPE - Value is returned, stored globally, or put in a container
 *
 * Propagation: if a phi merges an escaped value, the phi result escapes.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "piresc.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------------- */
/* Helpers                                                           */
/* --------------------------------------------------------------- */

static const char *escape_name(int e)
{
    switch (e) {
    case ESC_NO_ESCAPE:     return "no_escape";
    case ESC_ARG_ESCAPE:    return "arg_escape";
    case ESC_GLOBAL_ESCAPE: return "global_escape";
    default:                return "?";
    }
}

/* Raise escape level (higher = more escaped) */
static int raise_escape(int current, int new_level)
{
    return (new_level > current) ? new_level : current;
}

/* --------------------------------------------------------------- */
/* Main escape analysis                                              */
/* --------------------------------------------------------------- */

void pir_escape_analyze(PIRFunction *func)
{
    int num_values, i, bi, changed, iteration;
    FuncEscapeResult *result;
    ValueEscapeInfo *ve;

    if (!func || func->blocks.size() == 0) return;

    num_values = func->next_value_id;
    if (num_values <= 0) return;

    /* Allocate result */
    result = (FuncEscapeResult *)malloc(sizeof(FuncEscapeResult));
    if (!result) return;
    ve = (ValueEscapeInfo *)malloc(sizeof(ValueEscapeInfo) * num_values);
    if (!ve) { free(result); return; }

    /* Initialize all values as no-escape */
    for (i = 0; i < num_values; i++) {
        ve[i].value_id = i;
        ve[i].escape = ESC_NO_ESCAPE;
    }

    /* Parameters escape via argument (caller may have stored them) */
    for (i = 0; i < func->params.size(); i++) {
        PIRValue p = func->params[i];
        if (p.id >= 0 && p.id < num_values) {
            ve[p.id].escape = ESC_ARG_ESCAPE;
        }
    }

    /* Pass 1: Mark direct escapes from instructions */
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;

        for (inst = block->first; inst; inst = inst->next) {
            switch (inst->op) {
            /* Global escape: value leaves the function */
            case PIR_RETURN:
                if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_STORE_GLOBAL:
                if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            /* Container stores: value goes into a heap object */
            case PIR_SET_ATTR:
                /* operands[1] = value being stored */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_SUBSCR_SET:
                /* operands[1] = key/index — escapes into container */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                /* operands[2] = value being stored */
                if (inst->operands[2].id >= 0 && inst->operands[2].id < num_values)
                    ve[inst->operands[2].id].escape =
                        raise_escape(ve[inst->operands[2].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_DEL_SUBSCR:
                /* operands[1] = key — escapes into runtime call */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_DEL_ATTR:
                /* operands[0] = object — escapes into runtime call */
                if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_DEL_NAME:
            case PIR_DEL_GLOBAL:
                /* No value operands escape */
                break;

            case PIR_LIST_APPEND:
                /* operands[1] = item appended */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_DICT_SET:
                /* operands[1] = key, operands[2] = value */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                if (inst->operands[2].id >= 0 && inst->operands[2].id < num_values)
                    ve[inst->operands[2].id].escape =
                        raise_escape(ve[inst->operands[2].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_TUPLE_SET:
                /* operands[1] = value */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_SET_ADD:
                /* operands[1] = item added */
                if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            /* Argument escape: callee may store the value */
            case PIR_PUSH_ARG:
                if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_ARG_ESCAPE);
                break;

            case PIR_CALL_METHOD:
                /* operands[0] = receiver (self) — receiver escapes to method */
                if (inst->num_operands >= 1 &&
                    inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_ARG_ESCAPE);
                break;

            case PIR_GET_ITER:
                /* operands[0] = container — creating iterator escapes container */
                if (inst->num_operands >= 1 &&
                    inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_ARG_ESCAPE);
                break;

            /* Raise: exception escapes */
            case PIR_RAISE:
                if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            /* Yield: value escapes to caller */
            case PIR_YIELD:
                if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            /* Generator check_throw: reads gen state, no new escapes */
            case PIR_GEN_CHECK_THROW:
                break;

            /* Generator get_sent: result comes from external source */
            case PIR_GEN_GET_SENT:
                if (pir_value_valid(inst->result) &&
                    inst->result.id >= 0 && inst->result.id < num_values)
                    ve[inst->result.id].escape =
                        raise_escape(ve[inst->result.id].escape, ESC_ARG_ESCAPE);
                break;

            /* Closure mechanism: values escape into heap-allocated objects
             * that outlive the current function scope. */
            case PIR_MAKE_FUNCTION:
                /* operands[0] = closure list (if present), stored into func object */
                if (inst->num_operands >= 1 &&
                    inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_SET_CLOSURE:
                /* operands[0] = closure list, stored to global pydos_active_closure */
                if (inst->num_operands >= 1 &&
                    inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                    ve[inst->operands[0].id].escape =
                        raise_escape(ve[inst->operands[0].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            case PIR_CELL_SET:
                /* operands[1] = value stored into cell (cell is shared across scopes) */
                if (inst->num_operands >= 2 &&
                    inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                    ve[inst->operands[1].id].escape =
                        raise_escape(ve[inst->operands[1].id].escape, ESC_GLOBAL_ESCAPE);
                break;

            default:
                break;
            }
        }
    }

    /* Pass 2: Propagate escape through phi nodes and stores.
     * If %a escapes and %b = phi(..., %a, ...), then %b escapes.
     * If %b = load from alloca X, and X has a store of escaped value, propagate. */
    changed = 1;
    iteration = 0;
    while (changed && iteration < 10) {
        changed = 0;
        iteration++;

        for (bi = 0; bi < func->blocks.size(); bi++) {
            PIRBlock *block = func->blocks[bi];
            PIRInst *inst;

            for (inst = block->first; inst; inst = inst->next) {
                if (inst->op == PIR_PHI) {
                    int rid = inst->result.id;
                    int ei;
                    if (rid < 0 || rid >= num_values) continue;

                    /* If the phi result escapes, all phi operands must escape too */
                    if (ve[rid].escape > ESC_NO_ESCAPE) {
                        for (ei = 0; ei < inst->extra.phi.count; ei++) {
                            int vid = inst->extra.phi.entries[ei].value.id;
                            if (vid >= 0 && vid < num_values) {
                                int old = ve[vid].escape;
                                ve[vid].escape = raise_escape(ve[vid].escape, ve[rid].escape);
                                if (ve[vid].escape != old) changed = 1;
                            }
                        }
                    }

                    /* If any phi operand escapes, the phi result escapes */
                    for (ei = 0; ei < inst->extra.phi.count; ei++) {
                        int vid = inst->extra.phi.entries[ei].value.id;
                        if (vid >= 0 && vid < num_values) {
                            int old = ve[rid].escape;
                            ve[rid].escape = raise_escape(ve[rid].escape, ve[vid].escape);
                            if (ve[rid].escape != old) changed = 1;
                        }
                    }
                }
            }
        }
    }

    /* Compute can_use_arena: 1 if any non-parameter value with a heap-allocating
     * instruction doesn't escape */
    result->can_use_arena = 0;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;

        for (inst = block->first; inst; inst = inst->next) {
            int rid = inst->result.id;
            if (rid < 0 || rid >= num_values) continue;

            /* Check if this is a heap-allocating instruction */
            switch (inst->op) {
            case PIR_CONST_STR:
            case PIR_LIST_NEW: case PIR_DICT_NEW:
            case PIR_TUPLE_NEW: case PIR_SET_NEW:
            case PIR_BUILD_LIST: case PIR_BUILD_DICT:
            case PIR_BUILD_TUPLE: case PIR_BUILD_SET:
            case PIR_ALLOC_OBJ:
            case PIR_BOX_INT: case PIR_BOX_FLOAT: case PIR_BOX_BOOL:
            case PIR_STR_CONCAT: case PIR_STR_FORMAT:
            case PIR_SLICE:
                if (ve[rid].escape == ESC_NO_ESCAPE) {
                    result->can_use_arena = 1;
                }
                break;
            default:
                break;
            }
        }
    }

    /* Disable arena for functions with exception handling.
     * Exception paths (raise/reraise) use setjmp/longjmp which
     * bypass SCOPE_EXIT, leaving arena state corrupted. */
    if (result->can_use_arena) {
        for (bi = 0; bi < func->blocks.size(); bi++) {
            PIRBlock *block = func->blocks[bi];
            PIRInst *inst;
            for (inst = block->first; inst; inst = inst->next) {
                if (inst->op == PIR_SETUP_TRY ||
                    inst->op == PIR_RAISE ||
                    inst->op == PIR_RERAISE) {
                    result->can_use_arena = 0;
                    goto done_arena_check;
                }
            }
        }
        done_arena_check:;
    }

    result->values = ve;
    result->count = num_values;

    /* Free previous result if any */
    if (func->escape_info) {
        free(func->escape_info->values);
        free(func->escape_info);
    }
    func->escape_info = result;
}

/* --------------------------------------------------------------- */
/* Debug dump                                                        */
/* --------------------------------------------------------------- */

void pir_dump_escape(PIRFunction *func, FILE *out)
{
    int i;
    FuncEscapeResult *ei;

    if (!func || !out) return;
    ei = func->escape_info;
    if (!ei) {
        fprintf(out, "  (no escape info)\n");
        return;
    }

    fprintf(out, "function @%s escape analysis (%d values, arena=%s):\n",
            func->name ? func->name : "?", ei->count,
            ei->can_use_arena ? "yes" : "no");

    for (i = 0; i < ei->count; i++) {
        if (ei->values[i].escape != ESC_NO_ESCAPE) {
            fprintf(out, "  %%%d: %s\n", i,
                    escape_name(ei->values[i].escape));
        }
    }
    fprintf(out, "\n");
}
