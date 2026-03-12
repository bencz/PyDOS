/*
 * pirutil.cpp - PIR utility functions
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirutil.h"

#include <string.h>

/* --------------------------------------------------------------- */
/* Instruction removal                                               */
/* --------------------------------------------------------------- */
void pir_inst_remove(PIRBlock *block, PIRInst *inst)
{
    if (!block || !inst) return;

    if (inst->prev) {
        inst->prev->next = inst->next;
    } else {
        block->first = inst->next;
    }

    if (inst->next) {
        inst->next->prev = inst->prev;
    } else {
        block->last = inst->prev;
    }

    block->inst_count--;

    /* Free phi entries if any */
    if (inst->op == PIR_PHI && inst->extra.phi.entries) {
        free(inst->extra.phi.entries);
    }
    free(inst);
}

/* --------------------------------------------------------------- */
/* Replace all uses of a value                                       */
/* --------------------------------------------------------------- */

static void replace_in_operands(PIRInst *inst, PIRValue old_val, PIRValue new_val)
{
    int i;
    for (i = 0; i < inst->num_operands; i++) {
        if (inst->operands[i].id == old_val.id) {
            inst->operands[i] = new_val;
        }
    }
    /* Also check phi entries */
    if (inst->op == PIR_PHI) {
        int j;
        for (j = 0; j < inst->extra.phi.count; j++) {
            if (inst->extra.phi.entries[j].value.id == old_val.id) {
                inst->extra.phi.entries[j].value = new_val;
            }
        }
    }
}

void pir_replace_all_uses(PIRFunction *func, PIRValue old_val, PIRValue new_val)
{
    int i;
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *block = func->blocks[i];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            replace_in_operands(inst, old_val, new_val);
        }
    }
}

/* --------------------------------------------------------------- */
/* Use counting                                                      */
/* --------------------------------------------------------------- */

static int count_uses_in_inst(PIRInst *inst, PIRValue val)
{
    int count = 0;
    int i;
    for (i = 0; i < inst->num_operands; i++) {
        if (inst->operands[i].id == val.id) count++;
    }
    if (inst->op == PIR_PHI) {
        int j;
        for (j = 0; j < inst->extra.phi.count; j++) {
            if (inst->extra.phi.entries[j].value.id == val.id) count++;
        }
    }
    return count;
}

int pir_value_has_uses(PIRFunction *func, PIRValue val)
{
    int i;
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *block = func->blocks[i];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            if (count_uses_in_inst(inst, val) > 0) return 1;
        }
    }
    return 0;
}

int pir_value_use_count(PIRFunction *func, PIRValue val)
{
    int total = 0;
    int i;
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *block = func->blocks[i];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            total += count_uses_in_inst(inst, val);
        }
    }
    return total;
}

/* --------------------------------------------------------------- */
/* Classification helpers                                            */
/* --------------------------------------------------------------- */

int pir_is_terminator(PIROp op)
{
    switch (op) {
    case PIR_BRANCH:
    case PIR_COND_BRANCH:
    case PIR_RETURN:
    case PIR_RETURN_NONE:
    case PIR_RAISE:
    case PIR_RERAISE:
        return 1;
    default:
        return 0;
    }
}

int pir_has_side_effects(PIROp op)
{
    switch (op) {
    /* Stores and loads of mutable state */
    case PIR_STORE:
    case PIR_LOAD:          /* reads from alloca — value may change inside loops */
    case PIR_STORE_GLOBAL:
    case PIR_LOAD_GLOBAL:   /* globals can change inside loops — must not hoist */
    case PIR_GET_ATTR:      /* reads mutable state — GVN has no alias analysis */
    case PIR_SET_ATTR:
    case PIR_SUBSCR_GET:    /* reads mutable state — GVN has no alias analysis */
    case PIR_SUBSCR_SET:
    case PIR_DEL_SUBSCR:
    case PIR_DEL_ATTR:
    case PIR_DEL_NAME:
    case PIR_DEL_GLOBAL:

    /* Calls */
    case PIR_CALL:
    case PIR_CALL_METHOD:
    case PIR_PUSH_ARG:

    /* Control flow */
    case PIR_BRANCH:
    case PIR_COND_BRANCH:
    case PIR_RETURN:
    case PIR_RETURN_NONE:

    /* Object operations */
    case PIR_INIT_VTABLE:
    case PIR_SET_VTABLE:
    case PIR_INCREF:
    case PIR_DECREF:

    /* Exception handling */
    case PIR_SETUP_TRY:
    case PIR_POP_TRY:
    case PIR_RAISE:
    case PIR_RERAISE:
    case PIR_GET_EXCEPTION:

    /* Iteration (mutates iterator state) */
    case PIR_FOR_ITER:
    case PIR_GET_ITER:

    /* Generators */
    case PIR_YIELD:
    case PIR_GEN_SET_PC:
    case PIR_GEN_SAVE_LOCAL:
    case PIR_GEN_CHECK_THROW:
    case PIR_GEN_GET_SENT:

    /* GC Scope */
    case PIR_SCOPE_ENTER:
    case PIR_SCOPE_TRACK:
    case PIR_SCOPE_EXIT:

    /* Object/collection allocation (each call returns a DISTINCT heap object) */
    case PIR_ALLOC_OBJ:
    case PIR_LIST_NEW:
    case PIR_DICT_NEW:
    case PIR_TUPLE_NEW:
    case PIR_SET_NEW:
    case PIR_BUILD_LIST:
    case PIR_BUILD_DICT:
    case PIR_BUILD_TUPLE:
    case PIR_BUILD_SET:
    case PIR_MAKE_FUNCTION:
    case PIR_MAKE_GENERATOR:
    case PIR_MAKE_COROUTINE:
    case PIR_COR_SET_RESULT:

    /* Closure cells (reads/writes mutable shared state) */
    case PIR_MAKE_CELL:
    case PIR_CELL_GET:
    case PIR_CELL_SET:
    case PIR_LOAD_CLOSURE:
    case PIR_SET_CLOSURE:

    /* Collection mutation */
    case PIR_LIST_APPEND:
    case PIR_DICT_SET:
    case PIR_TUPLE_SET:
    case PIR_SET_ADD:

    /* Generator state reads (mutable — changes after each yield) */
    case PIR_GEN_LOAD_PC:
    case PIR_GEN_LOAD_LOCAL:

    /* Arithmetic that can raise exceptions or call user code */
    case PIR_PY_FLOORDIV:   /* ZeroDivisionError */
    case PIR_PY_MOD:        /* ZeroDivisionError */
    case PIR_PY_DIV:        /* ZeroDivisionError */
    case PIR_PY_MATMUL:     /* calls user __matmul__ */
    case PIR_PY_INPLACE:    /* calls user __iadd__ etc */

    /* Object creation (new heap object each time) */
    case PIR_SLICE:
    case PIR_STR_CONCAT:
    case PIR_STR_FORMAT:
    case PIR_STR_JOIN:
    case PIR_BOX_INT:
    case PIR_BOX_FLOAT:
    case PIR_BOX_BOOL:

    /* Runtime dispatch (reads mutable state, can iterate) */
    case PIR_EXC_MATCH:
    case PIR_PY_IN:
    case PIR_PY_NOT_IN:

    /* I/O */
    case PIR_COMMENT:
        return 1;

    default:
        return 0;
    }
}

int pir_is_pure(PIROp op)
{
    return !pir_has_side_effects(op) && !pir_is_terminator(op);
}

/* --------------------------------------------------------------- */
/* Block lookup                                                      */
/* --------------------------------------------------------------- */

PIRBlock *pir_func_get_block(PIRFunction *func, int block_id)
{
    int i;
    for (i = 0; i < func->blocks.size(); i++) {
        if (func->blocks[i]->id == block_id)
            return func->blocks[i];
    }
    return 0;
}
