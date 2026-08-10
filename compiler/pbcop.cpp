/*
 * pbcop.cpp - PyDOS portable bytecode metadata and code verifier
 */

#include "pbcop.h"
#include <stdlib.h>
#include <string.h>

#define OP(name, operand, width, pop, push, flags) \
    { name, operand, width, pop, push, flags }

static const PBCOpcodeInfo opcode_table[PBC_OP_COUNT] = {
    OP("NOP",               PBC_OPERAND_NONE,       0, 0, 0, 0),
    OP("LOAD_NONE",         PBC_OPERAND_NONE,       0, 0, 1, 0),
    OP("LOAD_TRUE",         PBC_OPERAND_NONE,       0, 0, 1, 0),
    OP("LOAD_FALSE",        PBC_OPERAND_NONE,       0, 0, 1, 0),
    OP("LOAD_CONST8",       PBC_OPERAND_CONST8,     1, 0, 1, 0),
    OP("LOAD_CONST16",      PBC_OPERAND_CONST16,    2, 0, 1, 0),
    OP("LOAD_LOCAL8",       PBC_OPERAND_LOCAL8,     1, 0, 1, 0),
    OP("LOAD_LOCAL16",      PBC_OPERAND_LOCAL16,    2, 0, 1, 0),
    OP("STORE_LOCAL8",      PBC_OPERAND_LOCAL8,     1, 1, 0, 0),
    OP("STORE_LOCAL16",     PBC_OPERAND_LOCAL16,    2, 1, 0, 0),
    OP("LOAD_GLOBAL16",     PBC_OPERAND_GLOBAL16,   2, 0, 1,
       PBC_OPF_MAY_RAISE),
    OP("STORE_GLOBAL16",    PBC_OPERAND_GLOBAL16,   2, 1, 0, 0),
    OP("POP_TOP",           PBC_OPERAND_NONE,       0, 1, 0, 0),
    OP("DUP_TOP",           PBC_OPERAND_NONE,       0, 1, 2, 0),
    OP("PY_ADD",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_SUB",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_MUL",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_TRUE_DIV",       PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_FLOOR_DIV",      PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_MOD",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_POW",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_NEG",            PBC_OPERAND_NONE,       0, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_POS",            PBC_OPERAND_NONE,       0, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_NOT",            PBC_OPERAND_NONE,       0, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("PY_BIT_NOT",        PBC_OPERAND_NONE,       0, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("CMP_EQ",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("CMP_NE",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("CMP_LT",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("CMP_LE",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("CMP_GT",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("CMP_GE",            PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("IS",                PBC_OPERAND_NONE,       0, 2, 1, 0),
    OP("IS_NOT",            PBC_OPERAND_NONE,       0, 2, 1, 0),
    OP("CONTAINS",          PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("NOT_CONTAINS",      PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("JUMP16",            PBC_OPERAND_BRANCH16,   2, 0, 0,
       PBC_OPF_TERMINATOR | PBC_OPF_BRANCH),
    OP("JUMP_IF_TRUE16",    PBC_OPERAND_BRANCH16,   2, 1, 0,
       PBC_OPF_BRANCH | PBC_OPF_CONDITIONAL),
    OP("JUMP_IF_FALSE16",   PBC_OPERAND_BRANCH16,   2, 1, 0,
       PBC_OPF_BRANCH | PBC_OPF_CONDITIONAL),
    OP("CALL8",             PBC_OPERAND_ARG_COUNT8, 1, 0, 1,
       PBC_OPF_MAY_RAISE | PBC_OPF_VARIABLE_STACK),
    OP("RETURN_VALUE",      PBC_OPERAND_NONE,       0, 1, 0,
       PBC_OPF_TERMINATOR),
    OP("RETURN_NONE",       PBC_OPERAND_NONE,       0, 0, 0,
       PBC_OPF_TERMINATOR),
    OP("RAISE",             PBC_OPERAND_NONE,       0, 1, 0,
       PBC_OPF_MAY_RAISE | PBC_OPF_TERMINATOR),
    OP("BUILD_LIST8",       PBC_OPERAND_ITEM_COUNT8, 1, 0, 1,
       PBC_OPF_MAY_RAISE | PBC_OPF_VARIABLE_STACK),
    OP("BUILD_TUPLE8",      PBC_OPERAND_ITEM_COUNT8, 1, 0, 1,
       PBC_OPF_MAY_RAISE | PBC_OPF_VARIABLE_STACK),
    OP("BUILD_SET8",        PBC_OPERAND_ITEM_COUNT8, 1, 0, 1,
       PBC_OPF_MAY_RAISE | PBC_OPF_VARIABLE_STACK),
    OP("BUILD_DICT8",       PBC_OPERAND_ITEM_COUNT8, 1, 0, 1,
       PBC_OPF_MAY_RAISE | PBC_OPF_VARIABLE_STACK),
    OP("GET_ITEM",          PBC_OPERAND_NONE,       0, 2, 1,
       PBC_OPF_MAY_RAISE),
    OP("SET_ITEM",          PBC_OPERAND_NONE,       0, 3, 0,
       PBC_OPF_MAY_RAISE),
    OP("GET_ATTR16",        PBC_OPERAND_SYMBOL16,   2, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("SET_ATTR16",        PBC_OPERAND_SYMBOL16,   2, 2, 0,
       PBC_OPF_MAY_RAISE),
    OP("GET_ITER",          PBC_OPERAND_NONE,       0, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("FOR_ITER16",        PBC_OPERAND_BRANCH16,   2, 1, 0,
       PBC_OPF_MAY_RAISE | PBC_OPF_BRANCH | PBC_OPF_CONDITIONAL |
       PBC_OPF_FOR_ITER),
    OP("YIELD_VALUE",       PBC_OPERAND_NONE,       0, 1, 1,
       PBC_OPF_SUSPENDS),
    OP("MAKE_FUNCTION16",   PBC_OPERAND_FUNCTION16, 2, 1, 1,
       PBC_OPF_MAY_RAISE),
    OP("MAKE_CELL16",       PBC_OPERAND_LOCAL16,    2, 0, 0,
       PBC_OPF_MAY_RAISE),
    OP("LOAD_CELL16",       PBC_OPERAND_LOCAL16,    2, 0, 1,
       PBC_OPF_MAY_RAISE),
    OP("STORE_CELL16",      PBC_OPERAND_LOCAL16,    2, 1, 0,
       PBC_OPF_MAY_RAISE),
    OP("LOAD_DEREF16",      PBC_OPERAND_DEREF16,    2, 0, 1,
       PBC_OPF_MAY_RAISE),
    OP("STORE_DEREF16",     PBC_OPERAND_DEREF16,    2, 1, 0,
       PBC_OPF_MAY_RAISE),
    OP("CHECK_EXCEPTION16", PBC_OPERAND_BRANCH16,   2, 0, 0,
       PBC_OPF_BRANCH | PBC_OPF_CONDITIONAL),
    OP("CLEAR_EXCEPTION",   PBC_OPERAND_NONE,       0, 0, 0, 0),
    OP("RERAISE",           PBC_OPERAND_NONE,       0, 0, 0,
       PBC_OPF_MAY_RAISE | PBC_OPF_TERMINATOR),
    OP("EXC_MATCH16",       PBC_OPERAND_EXCEPTION16, 2, 1, 1, 0)
};

#undef OP

static PBCU16 read_u16(const PBCU8 *p)
{
    return (PBCU16)((PBCU16)p[0] | ((PBCU16)p[1] << 8));
}

static PBCI16 read_i16(const PBCU8 *p)
{
    PBCU16 raw = read_u16(p);
    if ((raw & 0x8000U) != 0)
        return (PBCI16)((long)raw - 65536L);
    return (PBCI16)raw;
}

const PBCOpcodeInfo *pbc_opcode_info(PBCU8 opcode)
{
    if (opcode >= PBC_OP_COUNT) return 0;
    return &opcode_table[opcode];
}

static void set_result(PBCCodeVerifyResult *result,
                       PBCCodeVerifyError error,
                       PBCU32 offset, PBCU32 related)
{
    if (result != 0) {
        result->error = error;
        result->offset = offset;
        result->related_offset = related;
    }
}

const char *pbc_code_verify_error_name(PBCCodeVerifyError error)
{
    switch (error) {
    case PBC_CODE_OK: return "ok";
    case PBC_CODE_NULL_INPUT: return "null code or limits";
    case PBC_CODE_EMPTY: return "empty function body";
    case PBC_CODE_TOO_LARGE: return "function body exceeds 65535 bytes";
    case PBC_CODE_UNKNOWN_OPCODE: return "unknown opcode";
    case PBC_CODE_TRUNCATED_OPERAND: return "truncated instruction operand";
    case PBC_CODE_CONSTANT_OUT_OF_RANGE: return "constant index out of range";
    case PBC_CODE_LOCAL_OUT_OF_RANGE: return "local index out of range";
    case PBC_CODE_GLOBAL_OUT_OF_RANGE: return "global index out of range";
    case PBC_CODE_SYMBOL_OUT_OF_RANGE: return "symbol index out of range";
    case PBC_CODE_FUNCTION_OUT_OF_RANGE: return "function index out of range";
    case PBC_CODE_DEREF_OUT_OF_RANGE: return "closure index out of range";
    case PBC_CODE_TOO_MANY_ARGUMENTS: return "call argument limit exceeded";
    case PBC_CODE_TOO_MANY_ITEMS: return "collection item limit exceeded";
    case PBC_CODE_BRANCH_OUT_OF_RANGE: return "branch target out of range";
    case PBC_CODE_BRANCH_TO_OPERAND: return "branch target is not an opcode";
    case PBC_CODE_STACK_UNDERFLOW: return "operand stack underflow";
    case PBC_CODE_STACK_OVERFLOW: return "operand stack limit exceeded";
    case PBC_CODE_STACK_MERGE_MISMATCH:
        return "incompatible stack depths at control-flow merge";
    case PBC_CODE_FALLS_OFF_END: return "reachable path falls off function";
    case PBC_CODE_BAD_HANDLER_RANGE: return "invalid exception range";
    case PBC_CODE_BAD_HANDLER_TARGET: return "invalid exception handler target";
    case PBC_CODE_BAD_HANDLER_STACK: return "invalid exception handler stack";
    case PBC_CODE_OUT_OF_MEMORY: return "cannot allocate verifier state";
    }
    return "unknown code verifier error";
}

static PBCU16 operand_u16(const PBCU8 *code, PBCU32 offset,
                          const PBCOpcodeInfo *info)
{
    if (info->operand_width == 1) return code[offset + 1];
    return read_u16(code + offset + 1);
}

static int validate_index_operand(const PBCU8 *code, PBCU32 offset,
                                  const PBCOpcodeInfo *info,
                                  const PBCCodeLimits *limits,
                                  PBCCodeVerifyResult *result)
{
    PBCU16 value = operand_u16(code, offset, info);
    PBCCodeVerifyError error = PBC_CODE_OK;
    PBCU16 limit = 0;

    switch ((PBCOperandKind)info->operand_kind) {
    case PBC_OPERAND_CONST8:
    case PBC_OPERAND_CONST16:
        limit = limits->constant_count;
        error = PBC_CODE_CONSTANT_OUT_OF_RANGE;
        break;
    case PBC_OPERAND_LOCAL8:
    case PBC_OPERAND_LOCAL16:
        limit = limits->local_count;
        error = PBC_CODE_LOCAL_OUT_OF_RANGE;
        break;
    case PBC_OPERAND_GLOBAL16:
        limit = limits->global_count;
        error = PBC_CODE_GLOBAL_OUT_OF_RANGE;
        break;
    case PBC_OPERAND_SYMBOL16:
        limit = limits->symbol_count;
        error = PBC_CODE_SYMBOL_OUT_OF_RANGE;
        break;
    case PBC_OPERAND_FUNCTION16:
        limit = limits->function_count;
        error = PBC_CODE_FUNCTION_OUT_OF_RANGE;
        break;
    case PBC_OPERAND_DEREF16:
        limit = limits->closure_count;
        error = PBC_CODE_DEREF_OUT_OF_RANGE;
        break;
    case PBC_OPERAND_ARG_COUNT8:
        if (value > limits->max_call_args) {
            set_result(result, PBC_CODE_TOO_MANY_ARGUMENTS, offset, value);
            return 0;
        }
        return 1;
    case PBC_OPERAND_ITEM_COUNT8:
        if (value > limits->max_collection_items) {
            set_result(result, PBC_CODE_TOO_MANY_ITEMS, offset, value);
            return 0;
        }
        return 1;
    case PBC_OPERAND_NONE:
    case PBC_OPERAND_BRANCH16:
    case PBC_OPERAND_EXCEPTION16:
        return 1;
    }

    if (error != PBC_CODE_OK && value >= limit) {
        set_result(result, error, offset, value);
        return 0;
    }
    return 1;
}

static int branch_target(const PBCU8 *code, PBCU32 offset,
                         const PBCOpcodeInfo *info, PBCU32 code_size,
                         PBCU32 *target)
{
    long next = (long)offset + 1L + (long)info->operand_width;
    long calculated = next + (long)read_i16(code + offset + 1);
    if (calculated < 0L || calculated >= (long)code_size) return 0;
    *target = (PBCU32)calculated;
    return 1;
}

static int merge_depth(int *depths, PBCU32 *worklist, PBCU32 *work_count,
                       PBCU32 target, int depth, PBCU32 from,
                       PBCCodeVerifyResult *result)
{
    if (depths[target] < 0) {
        depths[target] = depth;
        worklist[(*work_count)++] = target;
        return 1;
    }
    if (depths[target] != depth) {
        set_result(result, PBC_CODE_STACK_MERGE_MISMATCH, target, from);
        return 0;
    }
    return 1;
}

int pbc_verify_code_with_handlers(const PBCU8 *code, PBCU32 code_size,
                                  const PBCCodeLimits *limits,
                                  const PBCCodeHandler *handlers,
                                  PBCU16 handler_count,
                                  PBCCodeVerifyResult *result)
{
    PBCU8 *boundaries;
    PBCU8 *lengths;
    int *depths;
    PBCU32 *worklist;
    PBCU32 offset;
    PBCU32 work_count;

    set_result(result, PBC_CODE_OK, 0, 0);
    if (code == 0 || limits == 0 ||
        (handler_count != 0 && handlers == 0)) {
        set_result(result, PBC_CODE_NULL_INPUT, 0, 0);
        return 0;
    }
    if (code_size == 0) {
        set_result(result, PBC_CODE_EMPTY, 0, 0);
        return 0;
    }
    if (code_size > 65535U) {
        set_result(result, PBC_CODE_TOO_LARGE, 0, code_size);
        return 0;
    }

    boundaries = (PBCU8 *)calloc((size_t)code_size + 1U, 1);
    lengths = (PBCU8 *)calloc((size_t)code_size + 1U, 1);
    depths = (int *)malloc(((size_t)code_size + 1U) * sizeof(int));
    worklist = (PBCU32 *)malloc(((size_t)code_size + 1U) * sizeof(PBCU32));
    if (boundaries == 0 || lengths == 0 || depths == 0 || worklist == 0) {
        free(boundaries);
        free(lengths);
        free(depths);
        free(worklist);
        set_result(result, PBC_CODE_OUT_OF_MEMORY, 0, 0);
        return 0;
    }
    for (offset = 0; offset <= code_size; offset++) depths[offset] = -1;

    offset = 0;
    while (offset < code_size) {
        const PBCOpcodeInfo *info = pbc_opcode_info(code[offset]);
        PBCU32 length;
        if (info == 0) {
            set_result(result, PBC_CODE_UNKNOWN_OPCODE, offset, code[offset]);
            goto fail;
        }
        length = 1U + info->operand_width;
        if (length > code_size - offset) {
            set_result(result, PBC_CODE_TRUNCATED_OPERAND, offset, 0);
            goto fail;
        }
        boundaries[offset] = 1;
        lengths[offset] = (PBCU8)length;
        if (!validate_index_operand(code, offset, info, limits, result))
            goto fail;
        offset += length;
    }
    boundaries[code_size] = 1;

    for (offset = 0; offset < code_size; offset++) {
        const PBCOpcodeInfo *info;
        PBCU32 target;
        if (!boundaries[offset]) continue;
        info = pbc_opcode_info(code[offset]);
        if ((info->flags & PBC_OPF_BRANCH) == 0) continue;
        if (!branch_target(code, offset, info, code_size, &target)) {
            set_result(result, PBC_CODE_BRANCH_OUT_OF_RANGE, offset, 0);
            goto fail;
        }
        if (!boundaries[target]) {
            set_result(result, PBC_CODE_BRANCH_TO_OPERAND, offset, target);
            goto fail;
        }
    }

    for (offset = 0; offset < handler_count; offset++) {
        const PBCCodeHandler *handler = &handlers[offset];
        if (handler->start_offset >= handler->end_offset ||
            handler->end_offset > code_size ||
            !boundaries[handler->start_offset] ||
            !boundaries[handler->end_offset]) {
            set_result(result, PBC_CODE_BAD_HANDLER_RANGE,
                       offset, handler->start_offset);
            goto fail;
        }
        if (handler->handler_offset >= code_size ||
            !boundaries[handler->handler_offset]) {
            set_result(result, PBC_CODE_BAD_HANDLER_TARGET,
                       offset, handler->handler_offset);
            goto fail;
        }
        if (handler->stack_depth >= limits->max_stack) {
            set_result(result, PBC_CODE_BAD_HANDLER_STACK,
                       offset, handler->stack_depth);
            goto fail;
        }
    }

    depths[0] = 0;
    worklist[0] = 0;
    work_count = 1;
    for (offset = 0; offset < handler_count; offset++) {
        if (!merge_depth(depths, worklist, &work_count,
                         handlers[offset].handler_offset,
                         (int)handlers[offset].stack_depth + 1,
                         handlers[offset].start_offset, result))
            goto fail;
    }
    while (work_count > 0) {
        const PBCOpcodeInfo *info;
        PBCU32 next;
        PBCU32 target;
        PBCU16 operand;
        int pop_count;
        int push_count;
        int depth;
        int after;

        offset = worklist[--work_count];
        info = pbc_opcode_info(code[offset]);
        next = offset + lengths[offset];
        depth = depths[offset];
        pop_count = info->stack_pop;
        push_count = info->stack_push;
        operand = operand_u16(code, offset, info);

        if (code[offset] == PBC_OP_CALL8)
            pop_count = (int)operand + 1;
        else if (code[offset] == PBC_OP_BUILD_LIST8 ||
                 code[offset] == PBC_OP_BUILD_TUPLE8 ||
                 code[offset] == PBC_OP_BUILD_SET8)
            pop_count = (int)operand;
        else if (code[offset] == PBC_OP_BUILD_DICT8)
            pop_count = (int)operand * 2;

        if (depth < pop_count) {
            set_result(result, PBC_CODE_STACK_UNDERFLOW, offset, depth);
            goto fail;
        }

        if ((info->flags & PBC_OPF_FOR_ITER) != 0) {
            int success_depth = depth + 1;
            int exhausted_depth = depth - 1;
            if (success_depth > limits->max_stack) {
                set_result(result, PBC_CODE_STACK_OVERFLOW,
                           offset, success_depth);
                goto fail;
            }
            if (next >= code_size) {
                set_result(result, PBC_CODE_FALLS_OFF_END, offset, next);
                goto fail;
            }
            if (!branch_target(code, offset, info, code_size, &target)) {
                set_result(result, PBC_CODE_BRANCH_OUT_OF_RANGE, offset, 0);
                goto fail;
            }
            if (!merge_depth(depths, worklist, &work_count, next,
                             success_depth, offset, result) ||
                !merge_depth(depths, worklist, &work_count, target,
                             exhausted_depth, offset, result))
                goto fail;
            continue;
        }

        after = depth - pop_count + push_count;
        if (after > limits->max_stack) {
            set_result(result, PBC_CODE_STACK_OVERFLOW, offset, after);
            goto fail;
        }
        if ((info->flags & PBC_OPF_BRANCH) != 0) {
            if (!branch_target(code, offset, info, code_size, &target)) {
                set_result(result, PBC_CODE_BRANCH_OUT_OF_RANGE, offset, 0);
                goto fail;
            }
            if (!merge_depth(depths, worklist, &work_count, target,
                             after, offset, result))
                goto fail;
            if ((info->flags & PBC_OPF_CONDITIONAL) == 0) continue;
        } else if ((info->flags & PBC_OPF_TERMINATOR) != 0) {
            continue;
        }

        if (next >= code_size) {
            set_result(result, PBC_CODE_FALLS_OFF_END, offset, next);
            goto fail;
        }
        if (!merge_depth(depths, worklist, &work_count, next,
                         after, offset, result))
            goto fail;
    }

    free(boundaries);
    free(lengths);
    free(depths);
    free(worklist);
    return 1;

fail:
    free(boundaries);
    free(lengths);
    free(depths);
    free(worklist);
    return 0;
}

int pbc_verify_code(const PBCU8 *code, PBCU32 code_size,
                    const PBCCodeLimits *limits,
                    PBCCodeVerifyResult *result)
{
    return pbc_verify_code_with_handlers(code, code_size, limits,
                                         0, 0, result);
}
