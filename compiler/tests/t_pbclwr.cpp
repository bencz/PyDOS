#include "../pbclwr.h"
#include <stdio.h>
#include <stdlib.h>
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

static PBCU16 get_u16(const PBCU8 *p)
{
    return (PBCU16)((PBCU16)p[0] | ((PBCU16)p[1] << 8));
}

static PIRInst *append(PIRBlock *block, PIROp op)
{
    PIRInst *instruction = pir_inst_new(op);
    pir_block_append(block, instruction);
    return instruction;
}

static PIRFunction *add_init(PIRModule *module)
{
    PIRFunction *function = pir_func_new("__init__");
    PIRBlock *entry = pir_block_new(function, "entry");
    function->entry_block = entry;
    append(entry, PIR_RETURN_NONE);
    module->functions.push_back(function);
    module->init_func = function;
    return function;
}

static PIRModule *build_control_flow_module()
{
    PIRModule *module = pir_module_new();
    PIRFunction *choose = pir_func_new("choose");
    PIRBlock *entry = pir_block_new(choose, "entry");
    PIRBlock *when_true = pir_block_new(choose, "true");
    PIRBlock *when_false = pir_block_new(choose, "false");
    PIRBlock *merge = pir_block_new(choose, "merge");
    PIRValue condition = pir_func_alloc_value(choose, PIR_TYPE_PYOBJ);
    PIRValue left = pir_func_alloc_value(choose, PIR_TYPE_PYOBJ);
    PIRValue right = pir_func_alloc_value(choose, PIR_TYPE_PYOBJ);
    PIRValue selected = pir_func_alloc_value(choose, PIR_TYPE_PYOBJ);
    PIRInst *instruction;

    choose->entry_block = entry;
    choose->params.push_back(condition);
    choose->params.push_back(left);
    choose->params.push_back(right);
    choose->num_params = 3;

    instruction = append(entry, PIR_COND_BRANCH);
    instruction->operands[0] = condition;
    instruction->num_operands = 1;
    instruction->target_block = when_true;
    instruction->false_block = when_false;
    pir_block_add_edge(entry, when_true);
    pir_block_add_edge(entry, when_false);

    instruction = append(when_true, PIR_BRANCH);
    instruction->target_block = merge;
    pir_block_add_edge(when_true, merge);
    instruction = append(when_false, PIR_BRANCH);
    instruction->target_block = merge;
    pir_block_add_edge(when_false, merge);

    instruction = append(merge, PIR_PHI);
    instruction->result = selected;
    instruction->extra.phi.entries = (PIRPhiEntry *)malloc(
        2U * sizeof(PIRPhiEntry));
    instruction->extra.phi.count = 2;
    instruction->extra.phi.entries[0].value = left;
    instruction->extra.phi.entries[0].block = when_true;
    instruction->extra.phi.entries[1].value = right;
    instruction->extra.phi.entries[1].block = when_false;
    instruction = append(merge, PIR_RETURN);
    instruction->operands[0] = selected;
    instruction->num_operands = 1;

    module->module_name = "control";
    module->functions.push_back(choose);
    add_init(module);
    return module;
}

static PIRModule *build_call_module()
{
    PIRModule *module = pir_module_new();
    PIRFunction *init = pir_func_new("__init__");
    PIRBlock *entry = pir_block_new(init, "entry");
    PIRBlock *exception_exit = pir_block_new(init, "exception_exit");
    PIRValue text = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRValue result = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRInst *instruction;

    init->entry_block = entry;
    instruction = append(entry, PIR_CONST_STR);
    instruction->result = text;
    instruction->str_val = "Hello, PBC!";
    instruction = append(entry, PIR_PUSH_ARG);
    instruction->operands[0] = text;
    instruction->num_operands = 1;
    instruction = append(entry, PIR_CALL);
    instruction->result = result;
    instruction->str_val = "print";
    instruction->int_val = 1;
    instruction = append(entry, PIR_CHECK_EXCEPTION);
    instruction->handler_block = exception_exit;
    instruction = append(entry, PIR_RETURN_NONE);
    pir_block_add_edge(entry, exception_exit);
    append(exception_exit, PIR_PANIC_EXCEPTION);

    module->module_name = "hello_pbc";
    module->functions.push_back(init);
    module->init_func = init;
    return module;
}

static PIRModule *build_exception_module()
{
    PIRModule *module = pir_module_new();
    PIRFunction *init = pir_func_new("__init__");
    PIRBlock *entry = pir_block_new(init, "entry");
    PIRBlock *handler = pir_block_new(init, "handler");
    PIRValue ten = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRValue zero = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRValue quotient = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRValue exception = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRValue matches = pir_func_alloc_value(init, PIR_TYPE_PYOBJ);
    PIRInst *instruction;

    init->entry_block = entry;
    instruction = append(entry, PIR_SETUP_TRY);
    instruction->handler_block = handler;
    instruction = append(entry, PIR_CONST_INT);
    instruction->result = ten;
    instruction->int_val = 10;
    instruction = append(entry, PIR_CONST_INT);
    instruction->result = zero;
    instruction->int_val = 0;
    instruction = append(entry, PIR_PY_DIV);
    instruction->result = quotient;
    instruction->operands[0] = ten;
    instruction->operands[1] = zero;
    instruction->num_operands = 2;
    instruction = append(entry, PIR_CHECK_EXCEPTION);
    instruction->handler_block = handler;
    append(entry, PIR_POP_TRY);
    instruction = append(entry, PIR_RETURN);
    instruction->operands[0] = quotient;
    instruction->num_operands = 1;
    pir_block_add_edge(entry, handler);

    append(handler, PIR_POP_TRY);
    instruction = append(handler, PIR_GET_EXCEPTION);
    instruction->result = exception;
    instruction = append(handler, PIR_EXC_MATCH);
    instruction->result = matches;
    instruction->operands[0] = exception;
    instruction->num_operands = 1;
    instruction->int_val = 10;
    append(handler, PIR_CLEAR_EXCEPTION);
    instruction = append(handler, PIR_RETURN);
    instruction->operands[0] = matches;
    instruction->num_operands = 1;

    module->module_name = "exceptions";
    module->functions.push_back(init);
    module->init_func = init;
    return module;
}

static void test_lowering_round_trip_and_determinism()
{
    PIRModule *module = build_call_module();
    PBCPIRLowerer first_lowerer;
    PBCPIRLowerer second_lowerer;
    PBCWriter first;
    PBCWriter second;
    PBCModuleVerifyResult verification;
    PBCSectionView functions;

    CHECK(first_lowerer.lower(module, first));
    CHECK(second_lowerer.lower(module, second));
    CHECK(first.size() == second.size());
    CHECK(memcmp(first.data(), second.data(), (size_t)first.size()) == 0);
    CHECK(pbc_verify_module(first.data(), first.size(), &verification));
    CHECK(pbc_find_section(first.data(), first.size(),
                           PBC_SECTION_FUNCTIONS, &functions));
    CHECK(functions.item_count == 1);
    CHECK((get_u16(functions.data + 2) & PBC_FUNC_MODULE_INIT) != 0);
    pir_module_free(module);
}

static void test_phi_edges_lower_to_valid_stack_code()
{
    PIRModule *module = build_control_flow_module();
    PBCPIRLowerer lowerer;
    PBCWriter writer;
    PBCModuleVerifyResult verification;
    CHECK(lowerer.lower(module, writer));
    CHECK(pbc_verify_module(writer.data(), writer.size(), &verification));
    pir_module_free(module);
}

static void test_exception_regions_and_matches_are_materialized()
{
    PIRModule *module = build_exception_module();
    PBCPIRLowerer lowerer;
    PBCWriter writer;
    PBCModuleVerifyResult verification;
    PBCSectionView exceptions;
    PBCSectionView code;
    PBCU32 i;
    int found_match = 0;

    CHECK(lowerer.lower(module, writer));
    CHECK(pbc_verify_module(writer.data(), writer.size(), &verification));
    CHECK(pbc_find_section(writer.data(), writer.size(),
                           PBC_SECTION_EXCEPTIONS, &exceptions));
    CHECK(exceptions.item_count == 1);
    CHECK(exceptions.size == PBC_EXCEPTION_RECORD_SIZE);
    CHECK(get_u16(exceptions.data) < get_u16(exceptions.data + 2));
    CHECK(get_u16(exceptions.data + 6) == 0);
    CHECK(get_u16(exceptions.data + 8) == 0xffffU);
    CHECK(pbc_find_section(writer.data(), writer.size(),
                           PBC_SECTION_CODE, &code));
    for (i = 0; i < code.size; i++) {
        if (code.data[i] == PBC_OP_EXC_MATCH16) found_match = 1;
    }
    CHECK(found_match);
    pir_module_free(module);
}

static void test_unsupported_opcode_is_explicit()
{
    PIRModule *module = pir_module_new();
    PIRFunction *init = pir_func_new("__init__");
    PIRBlock *entry = pir_block_new(init, "entry");
    PIRInst *instruction = append(entry, PIR_PY_MATMUL);
    PBCPIRLowerer lowerer;
    PBCWriter writer;
    init->entry_block = entry;
    instruction->line = 17;
    module->module_name = "unsupported";
    module->functions.push_back(init);
    module->init_func = init;
    CHECK(!lowerer.lower(module, writer));
    CHECK(lowerer.get_error_count() == 1);
    CHECK(lowerer.get_error_op() == PIR_PY_MATMUL);
    CHECK(lowerer.get_error_line() == 17);
    CHECK(strcmp(lowerer.get_error_function(), "__init__") == 0);
    CHECK(strstr(lowerer.get_error(), "no semantics-preserving") != 0);
    pir_module_free(module);
}

int main()
{
    test_lowering_round_trip_and_determinism();
    test_phi_edges_lower_to_valid_stack_code();
    test_exception_regions_and_matches_are_materialized();
    test_unsupported_opcode_is_explicit();
    if (failures != 0) {
        fprintf(stderr, "%d PIR to PBC lowering test failure(s)\n", failures);
        return 1;
    }
    printf("PIR to PBC lowering tests passed\n");
    return 0;
}
