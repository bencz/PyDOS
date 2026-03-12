/*
 * pir.cpp - Python Intermediate Representation implementation
 *
 * Allocation, deallocation, and name tables for PIR data structures.
 */

#include "pir.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* --------------------------------------------------------------- */
/* str_dup (local copy, same pattern as ir.cpp)                      */
/* --------------------------------------------------------------- */
static char *pir_str_dup(const char *s)
{
    int len;
    char *d;
    if (!s) return 0;
    len = (int)strlen(s);
    d = (char *)malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* --------------------------------------------------------------- */
/* Type name table                                                   */
/* --------------------------------------------------------------- */
static const char *type_names[] = {
    "void",     /* PIR_TYPE_VOID */
    "pyobj",    /* PIR_TYPE_PYOBJ */
    "i32",      /* PIR_TYPE_I32 */
    "f64",      /* PIR_TYPE_F64 */
    "bool",     /* PIR_TYPE_BOOL */
    "ptr"       /* PIR_TYPE_PTR */
};

const char *pir_type_name(PIRTypeKind t)
{
    if (t >= 0 && t <= PIR_TYPE_PTR) return type_names[t];
    return "?type";
}

/* --------------------------------------------------------------- */
/* Opcode name table                                                 */
/* --------------------------------------------------------------- */
static const char *op_names[] = {
    /* Constants */
    "const_int",        /* PIR_CONST_INT */
    "const_float",      /* PIR_CONST_FLOAT */
    "const_bool",       /* PIR_CONST_BOOL */
    "const_str",        /* PIR_CONST_STR */
    "const_none",       /* PIR_CONST_NONE */

    /* Variables */
    "alloca",           /* PIR_ALLOCA */
    "load",             /* PIR_LOAD */
    "store",            /* PIR_STORE */
    "load_global",      /* PIR_LOAD_GLOBAL */
    "store_global",     /* PIR_STORE_GLOBAL */

    /* Generic arithmetic */
    "py_add",           /* PIR_PY_ADD */
    "py_sub",           /* PIR_PY_SUB */
    "py_mul",           /* PIR_PY_MUL */
    "py_div",           /* PIR_PY_DIV */
    "py_floordiv",      /* PIR_PY_FLOORDIV */
    "py_mod",           /* PIR_PY_MOD */
    "py_pow",           /* PIR_PY_POW */
    "py_matmul",        /* PIR_PY_MATMUL */
    "py_inplace",       /* PIR_PY_INPLACE */
    "py_neg",           /* PIR_PY_NEG */
    "py_pos",           /* PIR_PY_POS */
    "py_not",           /* PIR_PY_NOT */

    /* Typed i32 */
    "add_i32",          /* PIR_ADD_I32 */
    "sub_i32",          /* PIR_SUB_I32 */
    "mul_i32",          /* PIR_MUL_I32 */
    "div_i32",          /* PIR_DIV_I32 */
    "mod_i32",          /* PIR_MOD_I32 */
    "neg_i32",          /* PIR_NEG_I32 */

    /* Typed f64 */
    "add_f64",          /* PIR_ADD_F64 */
    "sub_f64",          /* PIR_SUB_F64 */
    "mul_f64",          /* PIR_MUL_F64 */
    "div_f64",          /* PIR_DIV_F64 */
    "neg_f64",          /* PIR_NEG_F64 */

    /* Generic comparison */
    "py_cmp_eq",        /* PIR_PY_CMP_EQ */
    "py_cmp_ne",        /* PIR_PY_CMP_NE */
    "py_cmp_lt",        /* PIR_PY_CMP_LT */
    "py_cmp_le",        /* PIR_PY_CMP_LE */
    "py_cmp_gt",        /* PIR_PY_CMP_GT */
    "py_cmp_ge",        /* PIR_PY_CMP_GE */

    /* Typed i32 comparison */
    "cmp_i32_eq",       /* PIR_CMP_I32_EQ */
    "cmp_i32_ne",       /* PIR_CMP_I32_NE */
    "cmp_i32_lt",       /* PIR_CMP_I32_LT */
    "cmp_i32_le",       /* PIR_CMP_I32_LE */
    "cmp_i32_gt",       /* PIR_CMP_I32_GT */
    "cmp_i32_ge",       /* PIR_CMP_I32_GE */

    /* Bitwise */
    "py_bit_and",       /* PIR_PY_BIT_AND */
    "py_bit_or",        /* PIR_PY_BIT_OR */
    "py_bit_xor",       /* PIR_PY_BIT_XOR */
    "py_bit_not",       /* PIR_PY_BIT_NOT */
    "py_lshift",        /* PIR_PY_LSHIFT */
    "py_rshift",        /* PIR_PY_RSHIFT */

    /* Boolean/identity */
    "py_is",            /* PIR_PY_IS */
    "py_is_not",        /* PIR_PY_IS_NOT */
    "py_in",            /* PIR_PY_IN */
    "py_not_in",        /* PIR_PY_NOT_IN */

    /* Box/Unbox */
    "box_int",          /* PIR_BOX_INT */
    "box_float",        /* PIR_BOX_FLOAT */
    "box_bool",         /* PIR_BOX_BOOL */
    "unbox_int",        /* PIR_UNBOX_INT */
    "unbox_float",      /* PIR_UNBOX_FLOAT */

    /* Collections */
    "list_new",         /* PIR_LIST_NEW */
    "list_append",      /* PIR_LIST_APPEND */
    "dict_new",         /* PIR_DICT_NEW */
    "dict_set",         /* PIR_DICT_SET */
    "tuple_new",        /* PIR_TUPLE_NEW */
    "tuple_set",        /* PIR_TUPLE_SET */
    "set_new",          /* PIR_SET_NEW */
    "set_add",          /* PIR_SET_ADD */
    "build_list",       /* PIR_BUILD_LIST */
    "build_dict",       /* PIR_BUILD_DICT */
    "build_tuple",      /* PIR_BUILD_TUPLE */
    "build_set",        /* PIR_BUILD_SET */

    /* Subscript/attribute */
    "subscr_get",       /* PIR_SUBSCR_GET */
    "subscr_set",       /* PIR_SUBSCR_SET */
    "del_subscr",       /* PIR_DEL_SUBSCR */
    "slice",            /* PIR_SLICE */
    "get_attr",         /* PIR_GET_ATTR */
    "set_attr",         /* PIR_SET_ATTR */
    "del_attr",         /* PIR_DEL_ATTR */
    "del_name",         /* PIR_DEL_NAME */
    "del_global",       /* PIR_DEL_GLOBAL */

    /* Strings */
    "str_concat",       /* PIR_STR_CONCAT */
    "str_format",       /* PIR_STR_FORMAT */
    "str_join",         /* PIR_STR_JOIN */

    /* Calls */
    "push_arg",         /* PIR_PUSH_ARG */
    "call",             /* PIR_CALL */
    "call_method",      /* PIR_CALL_METHOD */

    /* Control flow */
    "branch",           /* PIR_BRANCH */
    "cond_branch",      /* PIR_COND_BRANCH */
    "return",           /* PIR_RETURN */
    "return_none",      /* PIR_RETURN_NONE */

    /* SSA */
    "phi",              /* PIR_PHI */

    /* Object operations */
    "alloc_obj",        /* PIR_ALLOC_OBJ */
    "init_vtable",      /* PIR_INIT_VTABLE */
    "set_vtable",       /* PIR_SET_VTABLE */
    "check_vtable",     /* PIR_CHECK_VTABLE */
    "incref",           /* PIR_INCREF */
    "decref",           /* PIR_DECREF */

    /* Functions */
    "make_function",    /* PIR_MAKE_FUNCTION */
    "make_generator",   /* PIR_MAKE_GENERATOR */
    "make_coroutine",   /* PIR_MAKE_COROUTINE */
    "cor_set_result",   /* PIR_COR_SET_RESULT */

    /* Exceptions */
    "setup_try",        /* PIR_SETUP_TRY */
    "pop_try",          /* PIR_POP_TRY */
    "raise",            /* PIR_RAISE */
    "reraise",          /* PIR_RERAISE */
    "get_exception",    /* PIR_GET_EXCEPTION */
    "exc_match",        /* PIR_EXC_MATCH */

    /* Iteration */
    "get_iter",         /* PIR_GET_ITER */
    "for_iter",         /* PIR_FOR_ITER */

    /* Generators */
    "gen_load_pc",      /* PIR_GEN_LOAD_PC */
    "gen_set_pc",       /* PIR_GEN_SET_PC */
    "gen_load_local",   /* PIR_GEN_LOAD_LOCAL */
    "gen_save_local",   /* PIR_GEN_SAVE_LOCAL */
    "yield",            /* PIR_YIELD */
    "gen_check_throw",  /* PIR_GEN_CHECK_THROW */
    "gen_get_sent",     /* PIR_GEN_GET_SENT */

    /* Cell objects (closures) */
    "make_cell",        /* PIR_MAKE_CELL */
    "cell_get",         /* PIR_CELL_GET */
    "cell_set",         /* PIR_CELL_SET */
    "load_closure",     /* PIR_LOAD_CLOSURE */
    "set_closure",      /* PIR_SET_CLOSURE */

    /* GC Scope */
    "scope_enter",      /* PIR_SCOPE_ENTER */
    "scope_track",      /* PIR_SCOPE_TRACK */
    "scope_exit",       /* PIR_SCOPE_EXIT */

    /* Import */
    "import",           /* PIR_IMPORT */
    "import_from",      /* PIR_IMPORT_FROM */

    /* Misc */
    "nop",              /* PIR_NOP */
    "comment",          /* PIR_COMMENT */
};

const char *pir_op_name(PIROp op)
{
    if (op >= 0 && op < PIR_OP_COUNT) return op_names[op];
    return "?op";
}

/* --------------------------------------------------------------- */
/* Module allocation                                                 */
/* --------------------------------------------------------------- */
PIRModule *pir_module_new()
{
    PIRModule *m = (PIRModule *)malloc(sizeof(PIRModule));
    if (!m) {
        fprintf(stderr, "pir: out of memory allocating module\n");
        exit(1);
    }
    memset(m, 0, sizeof(PIRModule));
    /* After memset(0), PdVector members are in valid state:
       data_=0, size_=0, cap_=0 (same as default constructor) */
    m->module_name = 0;
    m->is_main_module = 1;
    return m;
}

static void pir_inst_free(PIRInst *inst)
{
    if (!inst) return;
    if (inst->op == PIR_PHI && inst->extra.phi.entries) {
        free(inst->extra.phi.entries);
    }
    free(inst);
}

static void pir_block_free(PIRBlock *block)
{
    PIRInst *inst, *next;
    if (!block) return;
    inst = block->first;
    while (inst) {
        next = inst->next;
        pir_inst_free(inst);
        inst = next;
    }
    /* Destroy PdVector members */
    block->preds.destroy();
    block->succs.destroy();
    free(block);
}

void pir_func_free(PIRFunction *func)
{
    int i;
    if (!func) return;
    for (i = 0; i < func->blocks.size(); i++) {
        pir_block_free(func->blocks[i]);
    }
    func->blocks.destroy();
    func->params.destroy();
    if (func->type_info) {
        free(func->type_info->values);
        free(func->type_info);
    }
    if (func->escape_info) {
        free(func->escape_info->values);
        free(func->escape_info);
    }
    free(func);
}

void pir_module_free(PIRModule *mod)
{
    int i;
    if (!mod) return;
    for (i = 0; i < mod->functions.size(); i++) {
        pir_func_free(mod->functions[i]);
    }
    /* init_func is also in the functions list, don't double-free */
    mod->functions.destroy();
    mod->string_constants.destroy();
    free(mod);
}

/* --------------------------------------------------------------- */
/* Function allocation                                               */
/* --------------------------------------------------------------- */
PIRFunction *pir_func_new(const char *name)
{
    PIRFunction *f = (PIRFunction *)malloc(sizeof(PIRFunction));
    if (!f) {
        fprintf(stderr, "pir: out of memory allocating function\n");
        exit(1);
    }
    memset(f, 0, sizeof(PIRFunction));
    /* PdVector members valid after memset(0) */
    f->name = pir_str_dup(name);
    f->entry_block = 0;
    f->next_value_id = 0;
    f->next_block_id = 0;
    f->num_locals = 0;
    f->num_params = 0;
    f->is_generator = 0;
    f->is_coroutine = 0;
    f->type_info = 0;
    f->escape_info = 0;
    return f;
}

/* --------------------------------------------------------------- */
/* Block allocation                                                  */
/* --------------------------------------------------------------- */
PIRBlock *pir_block_new(PIRFunction *func, const char *label)
{
    PIRBlock *b = (PIRBlock *)malloc(sizeof(PIRBlock));
    if (!b) {
        fprintf(stderr, "pir: out of memory allocating block\n");
        exit(1);
    }
    memset(b, 0, sizeof(PIRBlock));
    /* PdVector members valid after memset(0) */
    b->id = func->next_block_id++;
    b->label = pir_str_dup(label);
    b->first = 0;
    b->last = 0;
    b->inst_count = 0;
    b->sealed = 0;
    b->filled = 0;
    func->blocks.push_back(b);
    return b;
}

/* --------------------------------------------------------------- */
/* Instruction allocation                                            */
/* --------------------------------------------------------------- */
PIRInst *pir_inst_new(PIROp op)
{
    PIRInst *inst = (PIRInst *)malloc(sizeof(PIRInst));
    if (!inst) {
        fprintf(stderr, "pir: out of memory allocating instruction\n");
        exit(1);
    }
    memset(inst, 0, sizeof(PIRInst));
    inst->op = op;
    inst->result = pir_value_none();
    inst->operands[0] = pir_value_none();
    inst->operands[1] = pir_value_none();
    inst->operands[2] = pir_value_none();
    inst->num_operands = 0;
    inst->extra.phi.entries = 0;
    inst->extra.phi.count = 0;
    inst->int_val = 0;
    inst->str_val = 0;
    inst->target_block = 0;
    inst->false_block = 0;
    inst->handler_block = 0;
    inst->next = 0;
    inst->prev = 0;
    inst->line = 0;
    return inst;
}

/* --------------------------------------------------------------- */
/* Append instruction to block                                       */
/* --------------------------------------------------------------- */
void pir_block_append(PIRBlock *block, PIRInst *inst)
{
    inst->prev = block->last;
    inst->next = 0;
    if (block->last) {
        block->last->next = inst;
    } else {
        block->first = inst;
    }
    block->last = inst;
    block->inst_count++;
}

/* --------------------------------------------------------------- */
/* CFG edge management                                               */
/* --------------------------------------------------------------- */
void pir_block_add_edge(PIRBlock *from, PIRBlock *to)
{
    from->succs.push_back(to);
    to->preds.push_back(from);
}

/* --------------------------------------------------------------- */
/* SSA value allocation                                              */
/* --------------------------------------------------------------- */
PIRValue pir_func_alloc_value(PIRFunction *func, PIRTypeKind type)
{
    PIRValue v;
    v.id = func->next_value_id++;
    v.type = type;
    return v;
}

/* --------------------------------------------------------------- */
/* Debug dump (basic — pirprt.cpp has the full version)              */
/* --------------------------------------------------------------- */
void pir_dump_module(PIRModule *mod, FILE *out)
{
    int fi, bi;
    PIRInst *inst;

    if (!mod || !out) return;

    fprintf(out, "; PIR Module (%d functions)\n\n",
            mod->functions.size());

    for (fi = 0; fi < mod->functions.size(); fi++) {
        PIRFunction *func = mod->functions[fi];
        fprintf(out, "function @%s (", func->name ? func->name : "?");
        {
            int pi;
            for (pi = 0; pi < func->params.size(); pi++) {
                if (pi > 0) fprintf(out, ", ");
                fprintf(out, "%%%d:%s",
                        func->params[pi].id,
                        pir_type_name(func->params[pi].type));
            }
        }
        fprintf(out, ") {\n");

        for (bi = 0; bi < func->blocks.size(); bi++) {
            PIRBlock *block = func->blocks[bi];
            fprintf(out, "  @%s:  ; block %d, %d inst(s)\n",
                    block->label ? block->label : "?",
                    block->id, block->inst_count);

            for (inst = block->first; inst; inst = inst->next) {
                fprintf(out, "    ");
                if (pir_value_valid(inst->result)) {
                    fprintf(out, "%%%d:%s = ",
                            inst->result.id,
                            pir_type_name(inst->result.type));
                }
                fprintf(out, "%s", pir_op_name(inst->op));
                {
                    int oi;
                    for (oi = 0; oi < inst->num_operands; oi++) {
                        if (pir_value_valid(inst->operands[oi])) {
                            fprintf(out, " %%%d", inst->operands[oi].id);
                        }
                    }
                }
                if (inst->int_val != 0) {
                    fprintf(out, " %d", inst->int_val);
                }
                if (inst->str_val) {
                    fprintf(out, " \"%s\"", inst->str_val);
                }
                if (inst->target_block) {
                    fprintf(out, " @%s",
                            inst->target_block->label
                            ? inst->target_block->label : "?");
                }
                if (inst->false_block) {
                    fprintf(out, " @%s",
                            inst->false_block->label
                            ? inst->false_block->label : "?");
                }
                if (inst->op == PIR_PHI) {
                    int ei;
                    fprintf(out, " [");
                    for (ei = 0; ei < inst->extra.phi.count; ei++) {
                        if (ei > 0) fprintf(out, ", ");
                        fprintf(out, "%%%d from @%s",
                                inst->extra.phi.entries[ei].value.id,
                                inst->extra.phi.entries[ei].block->label
                                ? inst->extra.phi.entries[ei].block->label
                                : "?");
                    }
                    fprintf(out, "]");
                }
                fprintf(out, "\n");
            }
        }

        fprintf(out, "}\n\n");
    }
}
