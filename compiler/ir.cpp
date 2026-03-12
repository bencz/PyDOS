/*
 * ir.cpp - Three-address code IR data structures and utilities
 *
 * Contains IR data structures, constant pool management, allocation
 * helpers, and debug dump routines consumed by IROpt and Codegen.
 * IR generation is handled by PIRBuilder + PIRLowerer.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL - arrays, linked lists, manual memory only.
 */

#include "ir.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================ */
/* String utilities (no STL)                                         */
/* ================================================================ */

static char *str_dup(const char *s)
{
    if (!s) return 0;
    int len = (int)strlen(s);
    char *d = (char *)malloc(len + 1);
    if (d) {
        memcpy(d, s, len + 1);
    }
    return d;
}

/* ================================================================ */
/* IROp name table (for debug output)                                */
/* ================================================================ */

static const char *irop_names[] = {
    "CONST_INT", "CONST_STR", "CONST_FLOAT", "CONST_NONE", "CONST_BOOL",
    "LOAD_LOCAL", "STORE_LOCAL", "LOAD_GLOBAL", "STORE_GLOBAL",
    "LOAD_ATTR", "STORE_ATTR", "LOAD_SUBSCRIPT", "STORE_SUBSCRIPT", "DEL_SUBSCRIPT",
    "DEL_LOCAL", "DEL_GLOBAL", "DEL_ATTR",
    "ADD", "SUB", "MUL", "DIV", "FLOORDIV", "MOD", "POW",
    "MATMUL", "INPLACE",
    "NEG", "POS", "NOT", "BITNOT",
    "BITAND", "BITOR", "BITXOR", "SHL", "SHR",
    "CMP_EQ", "CMP_NE", "CMP_LT", "CMP_LE", "CMP_GT", "CMP_GE",
    "IS", "IS_NOT", "IN", "NOT_IN",
    "JUMP", "JUMP_IF_TRUE", "JUMP_IF_FALSE", "LABEL",
    "CALL", "CALL_METHOD", "PUSH_ARG", "RETURN",
    "ALLOC_OBJ", "INIT_VTABLE", "SET_VTABLE", "INCREF", "DECREF",
    "BUILD_LIST", "BUILD_DICT", "BUILD_TUPLE", "BUILD_SET",
    "GET_ITER", "FOR_ITER",
    "SETUP_TRY", "POP_TRY", "RAISE", "RERAISE", "GET_EXCEPTION",
    "YIELD", "YIELD_FROM",
    "MAKE_FUNCTION",
    "MAKE_GENERATOR", "MAKE_COROUTINE", "COR_SET_RESULT",
    "GEN_LOAD_PC", "GEN_SET_PC", "GEN_LOAD_LOCAL", "GEN_SAVE_LOCAL", "GEN_CHECK_THROW", "GEN_GET_SENT",
    "LOAD_SLICE", "EXC_MATCH",
    "ADD_I32", "SUB_I32", "MUL_I32", "DIV_I32", "MOD_I32", "NEG_I32",
    "CMP_I32_EQ", "CMP_I32_NE", "CMP_I32_LT", "CMP_I32_LE", "CMP_I32_GT", "CMP_I32_GE",
    "BOX_INT", "UNBOX_INT", "BOX_BOOL",
    "STR_JOIN",
    "MAKE_CELL", "CELL_GET", "CELL_SET", "LOAD_CLOSURE", "SET_CLOSURE",
    "SCOPE_ENTER", "SCOPE_TRACK", "SCOPE_EXIT",
    "NOP", "COMMENT"
};

const char *irop_name(IROp op)
{
    int idx = (int)op;
    int count = (int)(sizeof(irop_names) / sizeof(irop_names[0]));
    if (idx >= 0 && idx < count)
        return irop_names[idx];
    return "???";
}

/* ================================================================ */
/* IRModule allocation / deallocation helpers                        */
/* ================================================================ */

static IRModule *alloc_module()
{
    IRModule *m = (IRModule *)malloc(sizeof(IRModule));
    if (!m) return 0;
    m->functions = 0;
    m->init_func = 0;
    m->max_constants = 256;
    m->num_constants = 0;
    m->constants = (IRConst *)malloc(sizeof(IRConst) * m->max_constants);
    if (!m->constants) {
        free(m);
        return 0;
    }
    m->num_class_vtables = 0;
    memset(m->class_vtables, 0, sizeof(m->class_vtables));
    return m;
}

static IRInstr *alloc_instr()
{
    IRInstr *ins = (IRInstr *)malloc(sizeof(IRInstr));
    if (!ins) return 0;
    ins->op = IR_NOP;
    ins->dest = -1;
    ins->src1 = -1;
    ins->src2 = -1;
    ins->extra = 0;
    ins->type_hint = 0;
    ins->next = 0;
    ins->prev = 0;
    return ins;
}

static IRFunc *alloc_irfunc(const char *name)
{
    IRFunc *f = (IRFunc *)malloc(sizeof(IRFunc));
    if (!f) return 0;
    f->name = str_dup(name);
    f->first = 0;
    f->last = 0;
    f->num_temps = 0;
    f->num_locals = 0;
    f->num_params = 0;
    f->ret_type = 0;
    f->next = 0;
    f->is_generator = 0;
    f->locals_count = 0;
    return f;
}

static void free_instr_list(IRInstr *first)
{
    IRInstr *cur = first;
    while (cur) {
        IRInstr *nxt = cur->next;
        free(cur);
        cur = nxt;
    }
}

static void free_irfunc(IRFunc *f)
{
    if (!f) return;
    free_instr_list(f->first);
    free((void *)f->name);
    free(f);
}

/* ================================================================ */
/* IR dump (debugging output)                                        */
/* ================================================================ */

static void dump_operand(int val, FILE *out)
{
    if (val >= 0) {
        fprintf(out, "t%d", val);
    } else {
        fprintf(out, "_");
    }
}

void ir_dump_func(IRFunc *func, FILE *out)
{
    if (!func) return;

    fprintf(out, "\nfunction %s (params=%d, locals=%d, temps=%d):\n",
            func->name ? func->name : "<unnamed>",
            func->num_params, func->num_locals, func->num_temps);

    /* Print local variable table */
    if (func->locals_count > 0) {
        fprintf(out, "  locals:\n");
        for (int i = 0; i < func->locals_count; i++) {
            fprintf(out, "    [%d] %s", func->locals[i].slot,
                    func->locals[i].name ? func->locals[i].name : "?");
            if (func->locals[i].type) {
                fprintf(out, " : %s", type_to_string(func->locals[i].type));
            }
            fprintf(out, "\n");
        }
    }

    /* Print instructions */
    IRInstr *ins = func->first;
    while (ins) {
        fprintf(out, "  ");

        switch (ins->op) {
        case IR_LABEL:
            fprintf(out, "L%d:", ins->extra);
            break;

        case IR_JUMP:
            fprintf(out, "    JUMP L%d", ins->extra);
            break;

        case IR_JUMP_IF_TRUE:
            fprintf(out, "    JUMP_IF_TRUE ");
            dump_operand(ins->src1, out);
            fprintf(out, ", L%d", ins->extra);
            break;

        case IR_JUMP_IF_FALSE:
            fprintf(out, "    JUMP_IF_FALSE ");
            dump_operand(ins->src1, out);
            fprintf(out, ", L%d", ins->extra);
            break;

        case IR_RETURN:
            fprintf(out, "    RETURN ");
            dump_operand(ins->src1, out);
            break;

        case IR_PUSH_ARG:
            fprintf(out, "    PUSH_ARG ");
            dump_operand(ins->src1, out);
            fprintf(out, " [pos %d]", ins->extra);
            break;

        case IR_NOP:
            fprintf(out, "    NOP");
            break;

        case IR_COMMENT:
            fprintf(out, "    ; (comment const %d)", ins->src1);
            break;

        case IR_CONST_INT:
            dump_operand(ins->dest, out);
            fprintf(out, " = CONST_INT [%d]", ins->src1);
            break;

        case IR_CONST_STR:
            dump_operand(ins->dest, out);
            fprintf(out, " = CONST_STR [%d]", ins->src1);
            break;

        case IR_CONST_FLOAT:
            dump_operand(ins->dest, out);
            fprintf(out, " = CONST_FLOAT [%d]", ins->src1);
            break;

        case IR_CONST_NONE:
            dump_operand(ins->dest, out);
            fprintf(out, " = CONST_NONE");
            break;

        case IR_CONST_BOOL:
            dump_operand(ins->dest, out);
            fprintf(out, " = CONST_BOOL %d", ins->src1);
            break;

        case IR_LOAD_LOCAL:
            dump_operand(ins->dest, out);
            fprintf(out, " = LOAD_LOCAL [%d]", ins->src1);
            break;

        case IR_STORE_LOCAL:
            fprintf(out, "    STORE_LOCAL [%d] = ", ins->dest);
            dump_operand(ins->src1, out);
            break;

        case IR_LOAD_GLOBAL:
            dump_operand(ins->dest, out);
            fprintf(out, " = LOAD_GLOBAL [%d]", ins->src1);
            break;

        case IR_STORE_GLOBAL:
            fprintf(out, "    STORE_GLOBAL [%d] = ", ins->dest);
            dump_operand(ins->src1, out);
            break;

        case IR_LOAD_ATTR:
            dump_operand(ins->dest, out);
            fprintf(out, " = LOAD_ATTR ");
            dump_operand(ins->src1, out);
            fprintf(out, ".[%d]", ins->src2);
            break;

        case IR_STORE_ATTR:
            fprintf(out, "    STORE_ATTR [%d] ", ins->dest);
            dump_operand(ins->src1, out);
            fprintf(out, " = ");
            dump_operand(ins->src2, out);
            break;

        case IR_LOAD_SUBSCRIPT:
            dump_operand(ins->dest, out);
            fprintf(out, " = ");
            dump_operand(ins->src1, out);
            fprintf(out, "[");
            dump_operand(ins->src2, out);
            fprintf(out, "]");
            break;

        case IR_STORE_SUBSCRIPT:
            dump_operand(ins->src1, out);
            fprintf(out, "[");
            dump_operand(ins->src2, out);
            fprintf(out, "] = ");
            dump_operand(ins->dest, out);
            break;

        case IR_CALL:
            dump_operand(ins->dest, out);
            fprintf(out, " = CALL ");
            dump_operand(ins->src1, out);
            fprintf(out, " argc=%d", ins->extra);
            break;

        case IR_CALL_METHOD:
            dump_operand(ins->dest, out);
            fprintf(out, " = CALL_METHOD ");
            dump_operand(ins->src1, out);
            fprintf(out, ".[%d] argc=%d", ins->src2, ins->extra);
            break;

        case IR_GET_ITER:
            dump_operand(ins->dest, out);
            fprintf(out, " = GET_ITER ");
            dump_operand(ins->src1, out);
            break;

        case IR_FOR_ITER:
            dump_operand(ins->dest, out);
            fprintf(out, " = FOR_ITER ");
            dump_operand(ins->src1, out);
            fprintf(out, ", end=L%d", ins->extra);
            break;

        case IR_BUILD_LIST:
            dump_operand(ins->dest, out);
            fprintf(out, " = BUILD_LIST %d", ins->extra);
            break;

        case IR_BUILD_DICT:
            dump_operand(ins->dest, out);
            fprintf(out, " = BUILD_DICT %d", ins->extra);
            break;

        case IR_BUILD_TUPLE:
            dump_operand(ins->dest, out);
            fprintf(out, " = BUILD_TUPLE %d", ins->extra);
            break;

        case IR_BUILD_SET:
            dump_operand(ins->dest, out);
            fprintf(out, " = BUILD_SET %d", ins->extra);
            break;

        case IR_INCREF:
            fprintf(out, "    INCREF ");
            dump_operand(ins->src1, out);
            break;

        case IR_DECREF:
            fprintf(out, "    DECREF ");
            dump_operand(ins->src1, out);
            break;

        case IR_ALLOC_OBJ:
            dump_operand(ins->dest, out);
            fprintf(out, " = ALLOC_OBJ type=%d", ins->extra);
            break;

        case IR_INIT_VTABLE:
            fprintf(out, "    INIT_VTABLE class_idx=%d", ins->extra);
            break;

        case IR_SET_VTABLE:
            fprintf(out, "    SET_VTABLE ");
            dump_operand(ins->src1, out);
            fprintf(out, " class_idx=%d", ins->extra);
            break;

        case IR_SETUP_TRY:
            fprintf(out, "    SETUP_TRY handler=L%d", ins->extra);
            break;

        case IR_POP_TRY:
            fprintf(out, "    POP_TRY");
            break;

        case IR_RAISE:
            fprintf(out, "    RAISE ");
            dump_operand(ins->src1, out);
            break;

        case IR_RERAISE:
            fprintf(out, "    RERAISE");
            break;

        case IR_GET_EXCEPTION:
            dump_operand(ins->dest, out);
            fprintf(out, " = GET_EXCEPTION");
            break;

        case IR_YIELD:
            dump_operand(ins->dest, out);
            fprintf(out, " = YIELD ");
            dump_operand(ins->src1, out);
            break;

        case IR_YIELD_FROM:
            fprintf(out, "    YIELD_FROM ");
            dump_operand(ins->src1, out);
            break;

        default:
        {
            /* Generic binary / unary format */
            const char *opname = irop_name(ins->op);
            if (ins->dest >= 0) {
                dump_operand(ins->dest, out);
                fprintf(out, " = %s ", opname);
            } else {
                fprintf(out, "    %s ", opname);
            }
            if (ins->src1 >= 0) dump_operand(ins->src1, out);
            if (ins->src2 >= 0) {
                fprintf(out, ", ");
                dump_operand(ins->src2, out);
            }
            break;
        }
        }

        /* Print type hint if present */
        if (ins->type_hint) {
            fprintf(out, "  {%s}", type_to_string(ins->type_hint));
        }

        fprintf(out, "\n");
        ins = ins->next;
    }
}

void ir_dump(IRModule *mod, FILE *out)
{
    if (!mod) return;

    fprintf(out, "=== IR Module ===\n");

    /* Dump constant pool */
    fprintf(out, "\nConstant pool (%d entries):\n", mod->num_constants);
    for (int i = 0; i < mod->num_constants; i++) {
        fprintf(out, "  [%d] ", i);
        switch (mod->constants[i].kind) {
        case IRConst::CONST_INT:
            fprintf(out, "INT %ld", mod->constants[i].int_val);
            break;
        case IRConst::CONST_FLOAT:
            fprintf(out, "FLOAT %g", mod->constants[i].float_val);
            break;
        case IRConst::CONST_STR:
            fprintf(out, "STR \"%.*s\"",
                    mod->constants[i].str_val.len,
                    mod->constants[i].str_val.data);
            break;
        }
        fprintf(out, "\n");
    }

    /* Dump init function */
    if (mod->init_func) {
        fprintf(out, "\n--- Module Init ---");
        ir_dump_func(mod->init_func, out);
    }

    /* Dump all other functions */
    IRFunc *func = mod->functions;
    while (func) {
        if (func != mod->init_func) {
            fprintf(out, "\n--- Function ---");
            ir_dump_func(func, out);
        }
        func = func->next;
    }

    fprintf(out, "\n=== End IR Module ===\n");
}
