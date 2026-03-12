/*
 * pirprt.cpp - PIR textual printer implementation
 *
 * Outputs human-readable PIR text for debugging and --dump-pir.
 * Format:
 *   function @name(%0: pyobj, %1: pyobj) {
 *     @entry:
 *       %2:pyobj = const_int 42
 *       %3:pyobj = py_add %0, %2
 *       return %3
 *   }
 */

#include "pirprt.h"

#include <stdio.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* Print a single PIR value reference                               */
/* --------------------------------------------------------------- */
static void print_val(PIRValue v, FILE *out)
{
    if (!pir_value_valid(v)) {
        fprintf(out, "undef");
    } else {
        fprintf(out, "%%%d", v.id);
    }
}

static void print_val_typed(PIRValue v, FILE *out)
{
    if (!pir_value_valid(v)) {
        fprintf(out, "undef");
    } else {
        fprintf(out, "%%%d:%s", v.id, pir_type_name(v.type));
    }
}

/* --------------------------------------------------------------- */
/* Print a single PIR instruction                                    */
/* --------------------------------------------------------------- */
static void print_inst(PIRInst *inst, FILE *out)
{
    int i;

    fprintf(out, "    ");

    /* Result assignment */
    if (pir_value_valid(inst->result)) {
        print_val_typed(inst->result, out);
        fprintf(out, " = ");
    }

    /* Opcode name */
    fprintf(out, "%s", pir_op_name(inst->op));

    /* Instruction-specific formatting */
    switch (inst->op) {
    case PIR_CONST_INT:
        fprintf(out, " %d", inst->int_val);
        break;

    case PIR_CONST_FLOAT:
        if (inst->str_val) {
            fprintf(out, " %s", inst->str_val);
        }
        break;

    case PIR_CONST_BOOL:
        fprintf(out, " %s", inst->int_val ? "true" : "false");
        break;

    case PIR_CONST_STR:
        if (inst->str_val) {
            fprintf(out, " \"");
            /* Print escaped string */
            {
                const char *s = inst->str_val;
                int len = inst->int_val > 0 ? inst->int_val : (int)strlen(s);
                int j;
                for (j = 0; j < len && j < 60; j++) {
                    char c = s[j];
                    if (c == '\n') fprintf(out, "\\n");
                    else if (c == '\t') fprintf(out, "\\t");
                    else if (c == '\\') fprintf(out, "\\\\");
                    else if (c == '"') fprintf(out, "\\\"");
                    else if (c >= 32 && c < 127) fputc(c, out);
                    else fprintf(out, "\\x%02x", (unsigned char)c);
                }
                if (j < len) fprintf(out, "...");
            }
            fprintf(out, "\"");
        }
        break;

    case PIR_CONST_NONE:
        /* No extra info needed */
        break;

    case PIR_ALLOCA:
        if (inst->str_val) {
            fprintf(out, " \"%s\"", inst->str_val);
        }
        break;

    case PIR_LOAD:
    case PIR_STORE:
        for (i = 0; i < inst->num_operands; i++) {
            fprintf(out, " ");
            print_val(inst->operands[i], out);
        }
        break;

    case PIR_LOAD_GLOBAL:
    case PIR_STORE_GLOBAL:
        if (inst->num_operands > 0) {
            fprintf(out, " ");
            print_val(inst->operands[0], out);
        }
        if (inst->str_val) {
            fprintf(out, " @%s", inst->str_val);
        }
        break;

    case PIR_PY_ADD: case PIR_PY_SUB: case PIR_PY_MUL:
    case PIR_PY_DIV: case PIR_PY_FLOORDIV: case PIR_PY_MOD:
    case PIR_PY_POW: case PIR_PY_MATMUL: case PIR_PY_INPLACE:
    case PIR_ADD_I32: case PIR_SUB_I32: case PIR_MUL_I32:
    case PIR_DIV_I32: case PIR_MOD_I32:
    case PIR_ADD_F64: case PIR_SUB_F64: case PIR_MUL_F64:
    case PIR_DIV_F64:
    case PIR_PY_CMP_EQ: case PIR_PY_CMP_NE: case PIR_PY_CMP_LT:
    case PIR_PY_CMP_LE: case PIR_PY_CMP_GT: case PIR_PY_CMP_GE:
    case PIR_CMP_I32_EQ: case PIR_CMP_I32_NE: case PIR_CMP_I32_LT:
    case PIR_CMP_I32_LE: case PIR_CMP_I32_GT: case PIR_CMP_I32_GE:
    case PIR_PY_BIT_AND: case PIR_PY_BIT_OR: case PIR_PY_BIT_XOR:
    case PIR_PY_LSHIFT: case PIR_PY_RSHIFT:
    case PIR_PY_IS: case PIR_PY_IS_NOT:
    case PIR_PY_IN: case PIR_PY_NOT_IN:
    case PIR_BOX_INT: case PIR_BOX_FLOAT: case PIR_BOX_BOOL:
    case PIR_UNBOX_INT: case PIR_UNBOX_FLOAT:
    case PIR_STR_CONCAT:
        /* Binary/unary ops: print operands */
        for (i = 0; i < inst->num_operands; i++) {
            fprintf(out, " ");
            print_val(inst->operands[i], out);
        }
        break;

    case PIR_PY_NEG: case PIR_PY_POS: case PIR_PY_NOT:
    case PIR_PY_BIT_NOT:
    case PIR_NEG_I32: case PIR_NEG_F64:
    case PIR_STR_FORMAT:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_LIST_NEW:
    case PIR_DICT_NEW:
    case PIR_SET_NEW:
        /* No operands */
        break;

    case PIR_TUPLE_NEW:
        fprintf(out, " %d", inst->int_val);
        break;

    case PIR_LIST_APPEND:
    case PIR_SET_ADD:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, ", ");
        print_val(inst->operands[1], out);
        break;

    case PIR_DICT_SET:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, ", ");
        print_val(inst->operands[1], out);
        fprintf(out, ", ");
        print_val(inst->operands[2], out);
        break;

    case PIR_TUPLE_SET:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, "[%d], ", inst->int_val);
        print_val(inst->operands[1], out);
        break;

    case PIR_BUILD_LIST: case PIR_BUILD_DICT:
    case PIR_BUILD_TUPLE: case PIR_BUILD_SET:
    case PIR_STR_JOIN:
        fprintf(out, " %d", inst->int_val);
        break;

    case PIR_SUBSCR_GET:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, "[");
        print_val(inst->operands[1], out);
        fprintf(out, "]");
        break;

    case PIR_SUBSCR_SET:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, "[");
        print_val(inst->operands[1], out);
        fprintf(out, "] = ");
        print_val(inst->operands[2], out);
        break;

    case PIR_SLICE:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, "[");
        print_val(inst->operands[1], out);
        fprintf(out, ":");
        print_val(inst->operands[2], out);
        fprintf(out, "]");
        break;

    case PIR_GET_ATTR:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, ".%s", inst->str_val);
        }
        break;

    case PIR_SET_ATTR:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, ".%s", inst->str_val);
        }
        fprintf(out, " = ");
        print_val(inst->operands[1], out);
        break;

    case PIR_DEL_ATTR:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, ".%s", inst->str_val);
        }
        break;

    case PIR_DEL_NAME:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, " @%s", inst->str_val);
        }
        break;

    case PIR_DEL_GLOBAL:
        if (inst->str_val) {
            fprintf(out, " @%s", inst->str_val);
        }
        break;

    case PIR_PUSH_ARG:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_CALL:
        if (inst->str_val) {
            fprintf(out, " @%s", inst->str_val);
        }
        fprintf(out, " argc=%d", inst->int_val);
        break;

    case PIR_CALL_METHOD:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, ".%s", inst->str_val);
        }
        fprintf(out, " argc=%d", inst->int_val);
        break;

    case PIR_BRANCH:
        if (inst->target_block) {
            fprintf(out, " @%s",
                    inst->target_block->label
                    ? inst->target_block->label : "?");
        }
        break;

    case PIR_COND_BRANCH:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->target_block) {
            fprintf(out, ", @%s",
                    inst->target_block->label
                    ? inst->target_block->label : "?");
        }
        if (inst->false_block) {
            fprintf(out, ", @%s",
                    inst->false_block->label
                    ? inst->false_block->label : "?");
        }
        break;

    case PIR_RETURN:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_RETURN_NONE:
        break;

    case PIR_PHI:
        fprintf(out, " [");
        for (i = 0; i < inst->extra.phi.count; i++) {
            if (i > 0) fprintf(out, ", ");
            print_val(inst->extra.phi.entries[i].value, out);
            fprintf(out, " from @%s",
                    inst->extra.phi.entries[i].block->label
                    ? inst->extra.phi.entries[i].block->label : "?");
        }
        fprintf(out, "]");
        break;

    case PIR_ALLOC_OBJ:
        if (inst->int_val) {
            fprintf(out, " type=%d", inst->int_val);
        }
        break;

    case PIR_INIT_VTABLE:
        fprintf(out, " vtable=%d", inst->int_val);
        break;

    case PIR_SET_VTABLE:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, " \"%s\"", inst->str_val);
        } else {
            fprintf(out, " vtable=%d", inst->int_val);
        }
        break;

    case PIR_CHECK_VTABLE:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, " isinstance \"%s\"", inst->str_val);
        } else {
            fprintf(out, " isinstance vtable=%d", inst->int_val);
        }
        break;

    case PIR_INCREF:
    case PIR_DECREF:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_MAKE_FUNCTION:
        if (inst->str_val) {
            fprintf(out, " @%s", inst->str_val);
        }
        break;

    case PIR_MAKE_GENERATOR:
    case PIR_MAKE_COROUTINE:
        if (inst->str_val) {
            fprintf(out, " @%s", inst->str_val);
        }
        fprintf(out, " locals=%d", inst->int_val);
        break;

    case PIR_COR_SET_RESULT:
        /* operands[0] = gen, operands[1] = value */
        break;

    case PIR_SETUP_TRY:
        if (inst->handler_block) {
            fprintf(out, " handler=@%s",
                    inst->handler_block->label
                    ? inst->handler_block->label : "?");
        }
        break;

    case PIR_POP_TRY:
        break;

    case PIR_RAISE:
    case PIR_GET_EXCEPTION:
        if (inst->num_operands > 0) {
            fprintf(out, " ");
            print_val(inst->operands[0], out);
        }
        break;

    case PIR_RERAISE:
        break;

    case PIR_EXC_MATCH:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, ", ");
        print_val(inst->operands[1], out);
        break;

    case PIR_GET_ITER:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_FOR_ITER:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->handler_block) {
            fprintf(out, " end=@%s",
                    inst->handler_block->label
                    ? inst->handler_block->label : "?");
        }
        break;

    case PIR_GEN_LOAD_PC:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_GEN_SET_PC:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, " state=%d", inst->int_val);
        break;

    case PIR_GEN_LOAD_LOCAL:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, "[%d]", inst->int_val);
        break;

    case PIR_GEN_SAVE_LOCAL:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        fprintf(out, "[%d] = ", inst->int_val);
        print_val(inst->operands[1], out);
        break;

    case PIR_YIELD:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_GEN_CHECK_THROW:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_GEN_GET_SENT:
        /* reads global, no operands to print */
        break;

    case PIR_SCOPE_ENTER:
    case PIR_SCOPE_EXIT:
        break;

    case PIR_SCOPE_TRACK:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        break;

    case PIR_IMPORT:
        if (inst->str_val) {
            fprintf(out, " \"%s\"", inst->str_val);
        }
        break;

    case PIR_IMPORT_FROM:
        fprintf(out, " ");
        print_val(inst->operands[0], out);
        if (inst->str_val) {
            fprintf(out, " \"%s\"", inst->str_val);
        }
        break;

    case PIR_NOP:
        break;

    case PIR_COMMENT:
        if (inst->str_val) {
            fprintf(out, " ; %s", inst->str_val);
        }
        break;

    default:
        /* Fallback: print all operands */
        for (i = 0; i < inst->num_operands; i++) {
            fprintf(out, " ");
            print_val(inst->operands[i], out);
        }
        if (inst->int_val != 0) {
            fprintf(out, " %d", inst->int_val);
        }
        if (inst->str_val) {
            fprintf(out, " \"%s\"", inst->str_val);
        }
        break;
    }

    /* Line number annotation */
    if (inst->line > 0) {
        fprintf(out, "  ; line %d", inst->line);
    }

    fprintf(out, "\n");
}

/* --------------------------------------------------------------- */
/* Print a single PIR function                                       */
/* --------------------------------------------------------------- */
void pir_print_function(PIRFunction *func, FILE *out)
{
    int bi;
    PIRInst *inst;

    if (!func || !out) return;

    /* Function header */
    fprintf(out, "function @%s(", func->name ? func->name : "?");
    {
        int pi;
        for (pi = 0; pi < func->params.size(); pi++) {
            if (pi > 0) fprintf(out, ", ");
            print_val_typed(func->params[pi], out);
        }
    }
    fprintf(out, ")");

    /* Metadata */
    if (func->is_generator) {
        fprintf(out, " generator");
    }
    fprintf(out, " {\n");

    /* Blocks */
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        fprintf(out, "  @%s:", block->label ? block->label : "?");

        /* Print predecessor info */
        if (block->preds.size() > 0) {
            int pi;
            fprintf(out, "  ; preds:");
            for (pi = 0; pi < block->preds.size(); pi++) {
                fprintf(out, " @%s",
                        block->preds[pi]->label
                        ? block->preds[pi]->label : "?");
            }
        }
        fprintf(out, "\n");

        /* Instructions */
        for (inst = block->first; inst; inst = inst->next) {
            print_inst(inst, out);
        }

        /* Blank line between blocks */
        if (bi < func->blocks.size() - 1) {
            fprintf(out, "\n");
        }
    }

    fprintf(out, "}\n");
}

/* --------------------------------------------------------------- */
/* Print entire PIR module                                           */
/* --------------------------------------------------------------- */
void pir_print_module(PIRModule *mod, FILE *out)
{
    int fi;

    if (!mod || !out) return;

    fprintf(out, "; PIR Module — %d function(s)\n",
            mod->functions.size());

    /* String constants */
    if (mod->string_constants.size() > 0) {
        int si;
        fprintf(out, "; String constants:\n");
        for (si = 0; si < mod->string_constants.size(); si++) {
            fprintf(out, ";   [%d] = \"%s\"\n", si,
                    mod->string_constants[si]
                    ? mod->string_constants[si] : "(null)");
        }
    }

    fprintf(out, "\n");

    /* Functions */
    for (fi = 0; fi < mod->functions.size(); fi++) {
        pir_print_function(mod->functions[fi], out);
        fprintf(out, "\n");
    }
}
