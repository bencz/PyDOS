/*
 * pbcop.h - PyDOS portable bytecode instructions and code verifier
 *
 * The instruction metadata is authoritative for decoding, verification and,
 * later, VM dispatch.  Branch displacements are signed 16-bit values relative
 * to the end of the branch instruction.
 */

#ifndef PBCOP_H
#define PBCOP_H

#include "pbc.h"

enum PBCOperandKind {
    PBC_OPERAND_NONE = 0,
    PBC_OPERAND_CONST8,
    PBC_OPERAND_CONST16,
    PBC_OPERAND_LOCAL8,
    PBC_OPERAND_LOCAL16,
    PBC_OPERAND_GLOBAL16,
    PBC_OPERAND_SYMBOL16,
    PBC_OPERAND_FUNCTION16,
    PBC_OPERAND_DEREF16,
    PBC_OPERAND_BRANCH16,
    PBC_OPERAND_ARG_COUNT8,
    PBC_OPERAND_ITEM_COUNT8,
    PBC_OPERAND_EXCEPTION16
};

enum PBCOpcodeFlags {
    PBC_OPF_MAY_RAISE          = 1,
    PBC_OPF_TERMINATOR         = 2,
    PBC_OPF_BRANCH             = 4,
    PBC_OPF_CONDITIONAL        = 8,
    PBC_OPF_VARIABLE_STACK     = 16,
    PBC_OPF_FOR_ITER           = 32,
    PBC_OPF_SUSPENDS           = 64
};

struct PBCOpcodeInfo {
    const char *name;
    PBCU8 operand_kind;
    PBCU8 operand_width;
    signed char stack_pop;
    signed char stack_push;
    PBCU8 flags;
};

const PBCOpcodeInfo *pbc_opcode_info(PBCU8 opcode);

struct PBCCodeLimits {
    PBCU16 constant_count;
    PBCU16 local_count;
    PBCU16 global_count;
    PBCU16 symbol_count;
    PBCU16 function_count;
    PBCU16 closure_count;
    PBCU16 max_stack;
    PBCU16 max_call_args;
    PBCU16 max_collection_items;
};

/* A handler starts with the preserved prefix plus the active exception. */
struct PBCCodeHandler {
    PBCU16 start_offset;
    PBCU16 end_offset;
    PBCU16 handler_offset;
    PBCU16 stack_depth;
};

enum PBCCodeVerifyError {
    PBC_CODE_OK = 0,
    PBC_CODE_NULL_INPUT,
    PBC_CODE_EMPTY,
    PBC_CODE_TOO_LARGE,
    PBC_CODE_UNKNOWN_OPCODE,
    PBC_CODE_TRUNCATED_OPERAND,
    PBC_CODE_CONSTANT_OUT_OF_RANGE,
    PBC_CODE_LOCAL_OUT_OF_RANGE,
    PBC_CODE_GLOBAL_OUT_OF_RANGE,
    PBC_CODE_SYMBOL_OUT_OF_RANGE,
    PBC_CODE_FUNCTION_OUT_OF_RANGE,
    PBC_CODE_DEREF_OUT_OF_RANGE,
    PBC_CODE_TOO_MANY_ARGUMENTS,
    PBC_CODE_TOO_MANY_ITEMS,
    PBC_CODE_BRANCH_OUT_OF_RANGE,
    PBC_CODE_BRANCH_TO_OPERAND,
    PBC_CODE_STACK_UNDERFLOW,
    PBC_CODE_STACK_OVERFLOW,
    PBC_CODE_STACK_MERGE_MISMATCH,
    PBC_CODE_FALLS_OFF_END,
    PBC_CODE_BAD_HANDLER_RANGE,
    PBC_CODE_BAD_HANDLER_TARGET,
    PBC_CODE_BAD_HANDLER_STACK,
    PBC_CODE_OUT_OF_MEMORY
};

struct PBCCodeVerifyResult {
    PBCCodeVerifyError error;
    PBCU32 offset;
    PBCU32 related_offset;
};

const char *pbc_code_verify_error_name(PBCCodeVerifyError error);

int pbc_verify_code(const PBCU8 *code, PBCU32 code_size,
                    const PBCCodeLimits *limits,
                    PBCCodeVerifyResult *result);

int pbc_verify_code_with_handlers(const PBCU8 *code, PBCU32 code_size,
                                  const PBCCodeLimits *limits,
                                  const PBCCodeHandler *handlers,
                                  PBCU16 handler_count,
                                  PBCCodeVerifyResult *result);

#endif /* PBCOP_H */
