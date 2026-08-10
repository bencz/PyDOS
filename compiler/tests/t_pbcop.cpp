#include "../pbcop.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures++; \
        } \
    } while (0)

static PBCCodeLimits limits()
{
    PBCCodeLimits value;
    memset(&value, 0, sizeof(value));
    value.constant_count = 2;
    value.local_count = 2;
    value.global_count = 2;
    value.symbol_count = 2;
    value.function_count = 2;
    value.closure_count = 1;
    value.max_stack = 4;
    value.max_call_args = 3;
    value.max_collection_items = 4;
    return value;
}

static void put_i16(PBCU8 *p, int value)
{
    unsigned int raw = (unsigned int)(value & 0xFFFF);
    p[0] = (PBCU8)(raw & 0xFFU);
    p[1] = (PBCU8)((raw >> 8) & 0xFFU);
}

static void expect_error(const PBCU8 *code, PBCU32 size,
                         PBCCodeVerifyError expected)
{
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;
    CHECK(!pbc_verify_code(code, size, &value, &result));
    CHECK(result.error == expected);
}

static void test_metadata()
{
    const PBCOpcodeInfo *call = pbc_opcode_info(PBC_OP_CALL8);
    const PBCOpcodeInfo *add = pbc_opcode_info(PBC_OP_PY_ADD);
    const PBCOpcodeInfo *match = pbc_opcode_info(PBC_OP_EXC_MATCH16);
    CHECK(call != 0);
    CHECK(strcmp(call->name, "CALL8") == 0);
    CHECK(call->operand_width == 1);
    CHECK((call->flags & PBC_OPF_VARIABLE_STACK) != 0);
    CHECK(add != 0);
    CHECK(add->stack_pop == 2);
    CHECK(add->stack_push == 1);
    CHECK(match != 0);
    CHECK(match->operand_kind == PBC_OPERAND_EXCEPTION16);
    CHECK(match->operand_width == 2);
    CHECK(match->stack_pop == 1);
    CHECK(match->stack_push == 1);
    CHECK(pbc_opcode_info(255) == 0);
}

static void test_straight_line()
{
    const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_STORE_LOCAL8, 0,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_RETURN_VALUE
    };
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;
    CHECK(pbc_verify_code(code, sizeof(code), &value, &result));
    CHECK(result.error == PBC_CODE_OK);
}

static void test_dictionary_stack_effect()
{
    const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_BUILD_DICT8, 1,
        PBC_OP_RETURN_VALUE
    };
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;
    CHECK(pbc_verify_code(code, sizeof(code), &value, &result));
}

static void test_conditional_and_loop()
{
    PBCU8 conditional[] = {
        PBC_OP_LOAD_TRUE,
        PBC_OP_JUMP_IF_FALSE16, 0, 0,
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_RETURN_VALUE,
        PBC_OP_RETURN_NONE
    };
    PBCU8 loop[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_GET_ITER,
        PBC_OP_FOR_ITER16, 0, 0,
        PBC_OP_POP_TOP,
        PBC_OP_JUMP16, 0, 0,
        PBC_OP_RETURN_NONE
    };
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;

    put_i16(conditional + 2, 3);  /* byte 4 -> byte 7 */
    CHECK(pbc_verify_code(conditional, sizeof(conditional), &value, &result));

    put_i16(loop + 4, 4);         /* byte 6 -> byte 10 */
    put_i16(loop + 8, -7);        /* byte 10 -> byte 3 */
    CHECK(pbc_verify_code(loop, sizeof(loop), &value, &result));
}

static void test_decode_and_index_errors()
{
    const PBCU8 unknown[] = { 255 };
    const PBCU8 truncated[] = { PBC_OP_LOAD_CONST16, 0 };
    const PBCU8 bad_const[] = {
        PBC_OP_LOAD_CONST8, 2, PBC_OP_RETURN_VALUE
    };
    const PBCU8 bad_local[] = {
        PBC_OP_LOAD_LOCAL8, 2, PBC_OP_RETURN_VALUE
    };
    const PBCU8 bad_call[] = {
        PBC_OP_LOAD_NONE, PBC_OP_CALL8, 4, PBC_OP_RETURN_VALUE
    };
    expect_error(unknown, sizeof(unknown), PBC_CODE_UNKNOWN_OPCODE);
    expect_error(truncated, sizeof(truncated), PBC_CODE_TRUNCATED_OPERAND);
    expect_error(bad_const, sizeof(bad_const),
                 PBC_CODE_CONSTANT_OUT_OF_RANGE);
    expect_error(bad_local, sizeof(bad_local), PBC_CODE_LOCAL_OUT_OF_RANGE);
    expect_error(bad_call, sizeof(bad_call), PBC_CODE_TOO_MANY_ARGUMENTS);
}

static void test_control_flow_errors()
{
    PBCU8 outside[] = {
        PBC_OP_JUMP16, 0, 0, PBC_OP_RETURN_NONE
    };
    PBCU8 operand_target[] = {
        PBC_OP_JUMP16, 0, 0,
        PBC_OP_LOAD_CONST16, 0, 0,
        PBC_OP_RETURN_VALUE
    };
    PBCU8 merge[] = {
        PBC_OP_LOAD_TRUE,
        PBC_OP_JUMP_IF_TRUE16, 0, 0,
        PBC_OP_LOAD_NONE,
        PBC_OP_JUMP16, 0, 0,
        PBC_OP_RETURN_NONE
    };
    const PBCU8 fall_off[] = { PBC_OP_NOP };

    put_i16(outside + 1, 100);
    expect_error(outside, sizeof(outside), PBC_CODE_BRANCH_OUT_OF_RANGE);

    put_i16(operand_target + 1, 1); /* byte 3 -> operand at byte 4 */
    expect_error(operand_target, sizeof(operand_target),
                 PBC_CODE_BRANCH_TO_OPERAND);

    put_i16(merge + 2, 4);          /* byte 4 -> byte 8 */
    put_i16(merge + 6, 0);          /* byte 8 -> byte 8 */
    expect_error(merge, sizeof(merge), PBC_CODE_STACK_MERGE_MISMATCH);
    expect_error(fall_off, sizeof(fall_off), PBC_CODE_FALLS_OFF_END);
}

static void test_stack_errors()
{
    const PBCU8 underflow[] = { PBC_OP_POP_TOP, PBC_OP_RETURN_NONE };
    const PBCU8 overflow[] = {
        PBC_OP_LOAD_NONE,
        PBC_OP_LOAD_NONE,
        PBC_OP_RETURN_VALUE
    };
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;

    expect_error(underflow, sizeof(underflow), PBC_CODE_STACK_UNDERFLOW);
    value.max_stack = 1;
    CHECK(!pbc_verify_code(overflow, sizeof(overflow), &value, &result));
    CHECK(result.error == PBC_CODE_STACK_OVERFLOW);
}

static void test_closure_operands()
{
    const PBCU8 deref[] = {
        PBC_OP_LOAD_DEREF16, 0, 0,
        PBC_OP_RETURN_VALUE
    };
    const PBCU8 bad_deref[] = {
        PBC_OP_LOAD_DEREF16, 1, 0,
        PBC_OP_RETURN_VALUE
    };
    const PBCU8 make_without_tuple[] = {
        PBC_OP_MAKE_FUNCTION16, 0, 0,
        PBC_OP_RETURN_VALUE
    };
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;
    CHECK(pbc_verify_code(deref, sizeof(deref), &value, &result));
    expect_error(bad_deref, sizeof(bad_deref),
                 PBC_CODE_DEREF_OUT_OF_RANGE);
    expect_error(make_without_tuple, sizeof(make_without_tuple),
                 PBC_CODE_STACK_UNDERFLOW);
}

static void test_exception_handlers()
{
    const PBCU8 code[] = {
        PBC_OP_LOAD_NONE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    const PBCU8 operand_target[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_RETURN_VALUE
    };
    PBCCodeHandler handler;
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;

    handler.start_offset = 0;
    handler.end_offset = 2;
    handler.handler_offset = 3;
    handler.stack_depth = 0;
    CHECK(pbc_verify_code_with_handlers(code, sizeof(code), &value,
                                        &handler, 1, &result));

    handler.start_offset = 0;
    handler.end_offset = 2;
    handler.handler_offset = 1;
    CHECK(!pbc_verify_code_with_handlers(operand_target,
                                         sizeof(operand_target), &value,
                                         &handler, 1, &result));
    CHECK(result.error == PBC_CODE_BAD_HANDLER_TARGET);

    handler.start_offset = 0;
    handler.end_offset = 2;
    handler.handler_offset = 5;
    CHECK(!pbc_verify_code_with_handlers(code, sizeof(code), &value,
                                         &handler, 1, &result));
    CHECK(result.error == PBC_CODE_BAD_HANDLER_TARGET);

    handler.handler_offset = 3;
    handler.stack_depth = value.max_stack;
    CHECK(!pbc_verify_code_with_handlers(code, sizeof(code), &value,
                                         &handler, 1, &result));
    CHECK(result.error == PBC_CODE_BAD_HANDLER_STACK);
}

static void test_reraise_is_a_terminator()
{
    const PBCU8 code[] = { PBC_OP_RERAISE };
    PBCCodeLimits value = limits();
    PBCCodeVerifyResult result;
    CHECK(pbc_verify_code(code, sizeof(code), &value, &result));
}

int main()
{
    test_metadata();
    test_straight_line();
    test_dictionary_stack_effect();
    test_conditional_and_loop();
    test_decode_and_index_errors();
    test_control_flow_errors();
    test_stack_errors();
    test_closure_operands();
    test_exception_handlers();
    test_reraise_is_a_terminator();

    if (failures != 0) {
        fprintf(stderr, "%d PBC opcode test failure(s)\n", failures);
        return 1;
    }
    printf("PBC opcode and control-flow tests passed\n");
    return 0;
}
