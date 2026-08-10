/* pbclwr.cpp - Direct lowering from SSA PIR to stack-based PBC. */

#include "pbclwr.h"
#include <stdlib.h>
#include <string.h>

struct PBCLowerBuffer {
    PBCU8 *data;
    PBCU32 size;
    PBCU32 capacity;
    int stack_depth;
    int max_stack;
};

struct PBCLowerFixup {
    PBCU32 operand_offset;
    PIRBlock *target;
};

struct PBCLowerBlockOffset {
    PIRBlock *block;
    PBCU32 offset;
};

struct PBCLowerException {
    PBCExceptionSpec spec;
    PIRBlock *handler;
    PIRBlock *source;
};

static int pbc_lower_is_handler(const PdVector<PIRBlock *> &handlers,
                                PIRBlock *block)
{
    int i;
    for (i = 0; i < handlers.size(); i++) {
        if (handlers[i] == block) return 1;
    }
    return 0;
}

static void pbc_lower_add_handler(PdVector<PIRBlock *> *handlers,
                                  PIRBlock *block)
{
    if (block != 0 && !pbc_lower_is_handler(*handlers, block))
        handlers->push_back(block);
}

static int pbc_lower_add_exception(
    PdVector<PBCLowerException> *exceptions, PIRBlock *source,
    PIRBlock *handler, PBCU32 start, PBCU32 end)
{
    PBCLowerException item;
    if (handler == 0 || start >= end || end > 65535U) return 0;
    if (!exceptions->empty()) {
        PBCLowerException &previous = exceptions->back();
        if (previous.source == source && previous.handler == handler &&
            previous.spec.end_offset <= start) {
            previous.spec.end_offset = (PBCU16)end;
            return 1;
        }
    }
    memset(&item, 0, sizeof(item));
    item.spec.start_offset = (PBCU16)start;
    item.spec.end_offset = (PBCU16)end;
    item.spec.stack_depth = 0;
    item.spec.match_symbol = 0xffffU;
    item.handler = handler;
    item.source = source;
    exceptions->push_back(item);
    return 1;
}

static int pbc_lower_reserve(PBCLowerBuffer *buffer, PBCU32 extra)
{
    PBCU32 required;
    PBCU32 capacity;
    PBCU8 *replacement;
    if (buffer->size > (PBCU32)0xffffffffU - extra) return 0;
    required = buffer->size + extra;
    if (required <= buffer->capacity) return 1;
    capacity = buffer->capacity == 0 ? 128U : buffer->capacity;
    while (capacity < required) {
        if (capacity > (PBCU32)0x7fffffffU) {
            capacity = required;
            break;
        }
        capacity *= 2U;
    }
    replacement = (PBCU8 *)realloc(buffer->data, (size_t)capacity);
    if (replacement == 0) return 0;
    buffer->data = replacement;
    buffer->capacity = capacity;
    return 1;
}

static int pbc_lower_byte(PBCLowerBuffer *buffer, PBCU8 value)
{
    if (!pbc_lower_reserve(buffer, 1)) return 0;
    buffer->data[buffer->size++] = value;
    return 1;
}

static int pbc_lower_u16(PBCLowerBuffer *buffer, PBCU16 value)
{
    if (!pbc_lower_reserve(buffer, 2)) return 0;
    buffer->data[buffer->size++] = (PBCU8)(value & 0xffU);
    buffer->data[buffer->size++] = (PBCU8)((value >> 8) & 0xffU);
    return 1;
}

static int pbc_lower_stack(PBCLowerBuffer *buffer, int pop, int push)
{
    if (pop > buffer->stack_depth) return 0;
    buffer->stack_depth -= pop;
    buffer->stack_depth += push;
    if (buffer->stack_depth > buffer->max_stack)
        buffer->max_stack = buffer->stack_depth;
    return buffer->stack_depth <= 65535;
}

static int pbc_lower_op(PBCLowerBuffer *buffer, PBCU8 opcode,
                        int pop, int push)
{
    return pbc_lower_byte(buffer, opcode) &&
           pbc_lower_stack(buffer, pop, push);
}

static int pbc_lower_op8(PBCLowerBuffer *buffer, PBCU8 opcode,
                         PBCU8 operand, int pop, int push)
{
    return pbc_lower_byte(buffer, opcode) &&
           pbc_lower_byte(buffer, operand) &&
           pbc_lower_stack(buffer, pop, push);
}

static int pbc_lower_op16(PBCLowerBuffer *buffer, PBCU8 opcode,
                          PBCU16 operand, int pop, int push)
{
    return pbc_lower_byte(buffer, opcode) &&
           pbc_lower_u16(buffer, operand) &&
           pbc_lower_stack(buffer, pop, push);
}

static int pbc_lower_local(PBCLowerBuffer *buffer, int store,
                           PBCU16 local)
{
    if (local <= 255U)
        return pbc_lower_op8(buffer,
            store ? PBC_OP_STORE_LOCAL8 : PBC_OP_LOAD_LOCAL8,
            (PBCU8)local, store ? 1 : 0, store ? 0 : 1);
    return pbc_lower_op16(buffer,
        store ? PBC_OP_STORE_LOCAL16 : PBC_OP_LOAD_LOCAL16,
        local, store ? 1 : 0, store ? 0 : 1);
}

static int pbc_lower_constant(PBCLowerBuffer *buffer, PBCU16 constant)
{
    if (constant <= 255U)
        return pbc_lower_op8(buffer, PBC_OP_LOAD_CONST8,
                            (PBCU8)constant, 0, 1);
    return pbc_lower_op16(buffer, PBC_OP_LOAD_CONST16, constant, 0, 1);
}

static int pbc_lower_patch(PBCLowerBuffer *buffer, PBCU32 operand_offset,
                           PBCU32 target)
{
    long displacement;
    PBCU16 raw;
    if (operand_offset + 1U >= buffer->size) return 0;
    displacement = (long)target - (long)(operand_offset + 2U);
    if (displacement < -32768L || displacement > 32767L) return 0;
    raw = (PBCU16)(PBCI16)displacement;
    buffer->data[operand_offset] = (PBCU8)(raw & 0xffU);
    buffer->data[operand_offset + 1U] = (PBCU8)((raw >> 8) & 0xffU);
    return 1;
}

static long pbc_lower_block_offset(
    const PdVector<PBCLowerBlockOffset> &blocks, PIRBlock *block)
{
    int i;
    for (i = 0; i < blocks.size(); i++) {
        if (blocks[i].block == block) return (long)blocks[i].offset;
    }
    return -1;
}

PBCPIRLowerer::PBCPIRLowerer()
{
    builder = 0;
    pir_module = 0;
    error_count = 0;
    last_error = 0;
    last_function = 0;
    last_op = PIR_NOP;
    last_line = 0;
}

int PBCPIRLowerer::get_error_count() const { return error_count; }
const char *PBCPIRLowerer::get_error() const { return last_error; }
const char *PBCPIRLowerer::get_error_function() const
{
    return last_function;
}
PIROp PBCPIRLowerer::get_error_op() const { return last_op; }
int PBCPIRLowerer::get_error_line() const { return last_line; }

void PBCPIRLowerer::fail(const char *message, const PIRFunction *function,
                         const PIRInst *instruction)
{
    if (error_count == 0) {
        last_error = message;
        last_function = function != 0 ? function->name : 0;
        last_op = instruction != 0 ? instruction->op : PIR_NOP;
        last_line = instruction != 0 ? instruction->line : 0;
    }
    error_count++;
}

long PBCPIRLowerer::symbol(const char *name)
{
    int i;
    long string_index;
    long symbol_index;
    NamedIndex value;
    if (name == 0) return -1;
    for (i = 0; i < symbols.size(); i++) {
        if (strcmp(symbols[i].name, name) == 0) return symbols[i].index;
    }
    string_index = builder->add_string(name);
    if (string_index < 0) return -1;
    symbol_index = builder->add_symbol((PBCU16)string_index, 0);
    if (symbol_index < 0) return -1;
    value.name = name;
    value.index = (PBCU16)symbol_index;
    symbols.push_back(value);
    return symbol_index;
}

long PBCPIRLowerer::function_index(const char *name) const
{
    int i;
    if (name == 0 || pir_module == 0) return -1;
    for (i = 0; i < pir_module->functions.size(); i++) {
        if (pir_module->functions[i]->name != 0 &&
            strcmp(pir_module->functions[i]->name, name) == 0)
            return i;
    }
    return -1;
}

static int pbc_lower_phi_copies(PBCLowerBuffer *code,
                                const PdVector<PBCU16> &locals,
                                PIRBlock *from, PIRBlock *to)
{
    PIRInst *instruction;
    int i;
    if (to == 0) return 0;
    for (instruction = to->first; instruction != 0;
         instruction = instruction->next) {
        if (instruction->op != PIR_PHI) continue;
        for (i = 0; i < instruction->extra.phi.count; i++) {
            PIRPhiEntry *entry = &instruction->extra.phi.entries[i];
            if (entry->block != from) continue;
            if (!pir_value_valid(entry->value) ||
                !pir_value_valid(instruction->result) ||
                entry->value.id < 0 || entry->value.id >= locals.size() ||
                instruction->result.id < 0 ||
                instruction->result.id >= locals.size())
                return 0;
            if (!pbc_lower_local(code, 0, locals[entry->value.id]) ||
                !pbc_lower_local(code, 1,
                                 locals[instruction->result.id]))
                return 0;
            break;
        }
        if (i == instruction->extra.phi.count) return 0;
    }
    return 1;
}

int PBCPIRLowerer::lower_function(PIRFunction *function,
                                  PBCU16 function_number)
{
    PBCLowerBuffer code;
    PdVector<PBCU16> locals;
    PdVector<PIRValue> pending_args;
    PdVector<PBCLowerFixup> fixups;
    PdVector<PBCLowerBlockOffset> block_offsets;
    PdVector<PBCLowerException> exceptions;
    PdVector<PIRBlock *> handler_blocks;
    PdVector<PBCExceptionSpec> exception_specs;
    PBCFunctionSpec spec;
    int next_local;
    int i;
    int bi;
    int ok = 1;
    PIRInst *failed_instruction = 0;
    const char *failure = "cannot lower PIR instruction";
    PBCU32 previous_start = 0;
    PBCU32 previous_end = 0;
    PIRBlock *previous_block = 0;
    int previous_valid = 0;

    memset(&code, 0, sizeof(code));
    if (function == 0 || function->next_value_id < 0 ||
        function->next_value_id > 65535 || function->params.size() > 255) {
        fail("PBC function limits exceeded", function, 0);
        return 0;
    }
    locals.resize(function->next_value_id);
    for (i = 0; i < locals.size(); i++) locals[i] = 0xffffU;
    next_local = 0;
    for (i = 0; i < function->params.size(); i++) {
        int id = function->params[i].id;
        if (id < 0 || id >= locals.size() || locals[id] != 0xffffU) {
            fail("invalid PIR parameter value", function, 0);
            return 0;
        }
        locals[id] = (PBCU16)next_local++;
    }
    for (i = 0; i < locals.size(); i++) {
        if (locals[i] == 0xffffU) locals[i] = (PBCU16)next_local++;
    }

    /* Exception checks in PIR identify the exact exceptional successor of
     * each operation.  Keeping that information is more precise than trying
     * to reconstruct lexical try stacks after CFG optimizations. */
    for (bi = 0; bi < function->blocks.size(); bi++) {
        PIRBlock *scan_block = function->blocks[bi];
        PIRInst *scan;
        if (scan_block == 0) continue;
        for (scan = scan_block->first; scan != 0; scan = scan->next) {
            if (scan->op == PIR_SETUP_TRY ||
                scan->op == PIR_CHECK_EXCEPTION ||
                scan->op == PIR_RAISE || scan->op == PIR_RERAISE)
                pbc_lower_add_handler(&handler_blocks,
                                      scan->handler_block);
            if (scan->op == PIR_FOR_ITER)
                pbc_lower_add_handler(&handler_blocks, scan->false_block);
        }
    }

#define PBC_FAIL(message) \
    do { failure = (message); failed_instruction = instruction; ok = 0; } while (0)
#define PBC_LOAD(value) \
    (pir_value_valid(value) && (value).id >= 0 && \
     (value).id < locals.size() && \
     pbc_lower_local(&code, 0, locals[(value).id]))
#define PBC_STORE_RESULT() \
    (pir_value_valid(instruction->result) && \
     instruction->result.id >= 0 && \
     instruction->result.id < locals.size() && \
     pbc_lower_local(&code, 1, locals[instruction->result.id]))

    for (bi = 0; ok && bi < function->blocks.size(); bi++) {
        PIRBlock *block = function->blocks[bi];
        PIRInst *instruction;
        PBCLowerBlockOffset block_offset;
        int emitted_terminator = 0;
        if (!pending_args.empty() || block == 0) {
            failure = "invalid PIR stack state at block boundary";
            ok = 0;
            break;
        }
        code.stack_depth = pbc_lower_is_handler(handler_blocks, block) ? 1 : 0;
        if (code.stack_depth > code.max_stack)
            code.max_stack = code.stack_depth;
        if (code.size > 65535U) {
            failure = "PBC function exceeds 65535 bytes";
            ok = 0;
            break;
        }
        block_offset.block = block;
        block_offset.offset = code.size;
        block_offsets.push_back(block_offset);

        for (instruction = block->first;
             ok && instruction != 0 && !emitted_terminator;
             instruction = instruction->next) {
            long index;
            PBCU8 opcode = PBC_OP_NOP;
            PBCU32 instruction_start = code.size;
            switch (instruction->op) {
            case PIR_CONST_INT:
                index = builder->add_constant_int32(instruction->int_val);
                if (index < 0 || !pbc_lower_constant(&code, (PBCU16)index) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit integer constant");
                break;
            case PIR_CONST_FLOAT:
                index = builder->add_constant_float64(
                    instruction->str_val != 0 ? atof(instruction->str_val) : 0.0);
                if (index < 0 || !pbc_lower_constant(&code, (PBCU16)index) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit float constant");
                break;
            case PIR_CONST_BOOL:
                if (!pbc_lower_op(&code, instruction->int_val != 0
                                  ? PBC_OP_LOAD_TRUE : PBC_OP_LOAD_FALSE,
                                  0, 1) || !PBC_STORE_RESULT())
                    PBC_FAIL("cannot emit bool constant");
                break;
            case PIR_CONST_NONE:
                if (!pbc_lower_op(&code, PBC_OP_LOAD_NONE, 0, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit None constant");
                break;
            case PIR_CONST_STR:
                index = builder->add_string(instruction->str_val != 0
                                            ? instruction->str_val : "");
                if (index >= 0)
                    index = builder->add_constant_string((PBCU16)index);
                if (index < 0 || !pbc_lower_constant(&code, (PBCU16)index) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit string constant");
                break;

            case PIR_ALLOCA:
            case PIR_PHI:
            case PIR_SCOPE_ENTER:
            case PIR_SCOPE_TRACK:
            case PIR_SCOPE_EXIT:
            case PIR_INCREF:
            case PIR_DECREF:
            case PIR_NOP:
            case PIR_COMMENT:
                break;
            case PIR_LOAD:
                if (instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit local load");
                break;
            case PIR_STORE:
                if (instruction->num_operands < 2 ||
                    !PBC_LOAD(instruction->operands[1]) ||
                    instruction->operands[0].id < 0 ||
                    instruction->operands[0].id >= locals.size() ||
                    !pbc_lower_local(&code, 1,
                        locals[instruction->operands[0].id]))
                    PBC_FAIL("cannot emit local store");
                break;
            case PIR_LOAD_GLOBAL:
                index = symbol(instruction->str_val);
                if (index < 0 || !pbc_lower_op16(&code, PBC_OP_LOAD_GLOBAL16,
                                                (PBCU16)index, 0, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit global load");
                break;
            case PIR_STORE_GLOBAL:
                index = symbol(instruction->str_val);
                if (index < 0 || instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op16(&code, PBC_OP_STORE_GLOBAL16,
                                    (PBCU16)index, 1, 0))
                    PBC_FAIL("cannot emit global store");
                break;

            case PIR_PY_ADD: case PIR_ADD_I32: case PIR_ADD_F64:
            case PIR_STR_CONCAT: opcode = PBC_OP_PY_ADD; goto binary_op;
            case PIR_PY_SUB: case PIR_SUB_I32: case PIR_SUB_F64:
                opcode = PBC_OP_PY_SUB; goto binary_op;
            case PIR_PY_MUL: case PIR_MUL_I32: case PIR_MUL_F64:
                opcode = PBC_OP_PY_MUL; goto binary_op;
            case PIR_PY_DIV: case PIR_DIV_F64:
                opcode = PBC_OP_PY_TRUE_DIV; goto binary_op;
            case PIR_PY_FLOORDIV: opcode = PBC_OP_PY_FLOOR_DIV; goto binary_op;
            case PIR_PY_MOD: opcode = PBC_OP_PY_MOD; goto binary_op;
            case PIR_PY_POW: opcode = PBC_OP_PY_POW; goto binary_op;
            case PIR_PY_CMP_EQ: case PIR_CMP_I32_EQ:
                opcode = PBC_OP_CMP_EQ; goto binary_op;
            case PIR_PY_CMP_NE: case PIR_CMP_I32_NE:
                opcode = PBC_OP_CMP_NE; goto binary_op;
            case PIR_PY_CMP_LT: case PIR_CMP_I32_LT:
                opcode = PBC_OP_CMP_LT; goto binary_op;
            case PIR_PY_CMP_LE: case PIR_CMP_I32_LE:
                opcode = PBC_OP_CMP_LE; goto binary_op;
            case PIR_PY_CMP_GT: case PIR_CMP_I32_GT:
                opcode = PBC_OP_CMP_GT; goto binary_op;
            case PIR_PY_CMP_GE: case PIR_CMP_I32_GE:
                opcode = PBC_OP_CMP_GE;
                goto binary_op;
            case PIR_PY_IS: opcode = PBC_OP_IS; goto binary_op;
            case PIR_PY_IS_NOT: opcode = PBC_OP_IS_NOT; goto binary_op;
            case PIR_PY_IN: opcode = PBC_OP_CONTAINS; goto binary_op;
            case PIR_PY_NOT_IN: opcode = PBC_OP_NOT_CONTAINS;
binary_op:
                if (instruction->num_operands < 2 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !PBC_LOAD(instruction->operands[1]) ||
                    !pbc_lower_op(&code, opcode, 2, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit binary operation");
                break;

            case PIR_PY_NEG: case PIR_NEG_I32: case PIR_NEG_F64:
                opcode = PBC_OP_PY_NEG; goto unary_op;
            case PIR_PY_POS: opcode = PBC_OP_PY_POS; goto unary_op;
            case PIR_PY_NOT: opcode = PBC_OP_PY_NOT; goto unary_op;
            case PIR_PY_BIT_NOT: opcode = PBC_OP_PY_BIT_NOT;
unary_op:
                if (instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op(&code, opcode, 1, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit unary operation");
                break;
            case PIR_BOX_INT: case PIR_BOX_FLOAT: case PIR_BOX_BOOL:
            case PIR_UNBOX_INT: case PIR_UNBOX_FLOAT:
                if (instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit boxed value copy");
                break;

            case PIR_PUSH_ARG:
                if (instruction->num_operands < 1 ||
                    !pir_value_valid(instruction->operands[0]) ||
                    pending_args.size() >= 255)
                    PBC_FAIL("invalid or excessive pending arguments");
                else pending_args.push_back(instruction->operands[0]);
                break;
            case PIR_BUILD_LIST:
            case PIR_BUILD_TUPLE:
            case PIR_BUILD_SET:
                if (instruction->int_val != pending_args.size()) {
                    PBC_FAIL("collection argument count does not match PIR");
                    break;
                }
                for (i = 0; ok && i < pending_args.size(); i++) {
                    if (!PBC_LOAD(pending_args[i])) PBC_FAIL("cannot load collection item");
                }
                if (!ok) break;
                opcode = instruction->op == PIR_BUILD_LIST
                    ? PBC_OP_BUILD_LIST8
                    : (instruction->op == PIR_BUILD_TUPLE
                       ? PBC_OP_BUILD_TUPLE8 : PBC_OP_BUILD_SET8);
                if (!pbc_lower_op8(&code, opcode,
                                   (PBCU8)pending_args.size(),
                                   pending_args.size(), 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit collection build");
                pending_args.clear();
                break;
            case PIR_BUILD_DICT:
                if (instruction->int_val < 0 ||
                    instruction->int_val > 255 ||
                    pending_args.size() != instruction->int_val * 2) {
                    PBC_FAIL("dictionary item count does not match PIR");
                    break;
                }
                for (i = 0; ok && i < pending_args.size(); i++) {
                    if (!PBC_LOAD(pending_args[i]))
                        PBC_FAIL("cannot load dictionary item");
                }
                if (ok && (!pbc_lower_op8(&code, PBC_OP_BUILD_DICT8,
                                          (PBCU8)instruction->int_val,
                                          pending_args.size(), 1) ||
                           !PBC_STORE_RESULT()))
                    PBC_FAIL("cannot emit dictionary build");
                pending_args.clear();
                break;
            case PIR_LIST_NEW:
            case PIR_SET_NEW:
                opcode = instruction->op == PIR_LIST_NEW
                    ? PBC_OP_BUILD_LIST8 : PBC_OP_BUILD_SET8;
                if (!pbc_lower_op8(&code, opcode, 0, 0, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit empty collection");
                break;
            case PIR_DICT_NEW:
                index = symbol("dict");
                if (index < 0 ||
                    !pbc_lower_op16(&code, PBC_OP_LOAD_GLOBAL16,
                                    (PBCU16)index, 0, 1) ||
                    !pbc_lower_op8(&code, PBC_OP_CALL8, 0, 1, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit empty dictionary");
                break;
            case PIR_DICT_SET:
            case PIR_SUBSCR_SET:
                if (instruction->num_operands < 3 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !PBC_LOAD(instruction->operands[1]) ||
                    !PBC_LOAD(instruction->operands[2]) ||
                    !pbc_lower_op(&code, PBC_OP_SET_ITEM, 3, 0))
                    PBC_FAIL("cannot emit item store");
                break;
            case PIR_SUBSCR_GET:
                if (instruction->num_operands < 2 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !PBC_LOAD(instruction->operands[1]) ||
                    !pbc_lower_op(&code, PBC_OP_GET_ITEM, 2, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit item load");
                break;
            case PIR_GET_ATTR:
                index = symbol(instruction->str_val);
                if (index < 0 || instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op16(&code, PBC_OP_GET_ATTR16,
                                    (PBCU16)index, 1, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit attribute load");
                break;
            case PIR_SET_ATTR:
                index = symbol(instruction->str_val);
                if (index < 0 || instruction->num_operands < 2 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !PBC_LOAD(instruction->operands[1]) ||
                    !pbc_lower_op16(&code, PBC_OP_SET_ATTR16,
                                    (PBCU16)index, 2, 0))
                    PBC_FAIL("cannot emit attribute store");
                break;

            case PIR_CALL:
                if (instruction->int_val != pending_args.size()) {
                    PBC_FAIL("call argument count does not match PIR");
                    break;
                }
                if (instruction->num_operands > 0 &&
                    pir_value_valid(instruction->operands[0])) {
                    if (!PBC_LOAD(instruction->operands[0]))
                        PBC_FAIL("cannot load indirect callable");
                } else {
                    index = symbol(instruction->str_val);
                    if (index < 0 ||
                        !pbc_lower_op16(&code, PBC_OP_LOAD_GLOBAL16,
                                        (PBCU16)index, 0, 1))
                        PBC_FAIL("cannot load global callable");
                }
                for (i = 0; ok && i < pending_args.size(); i++) {
                    if (!PBC_LOAD(pending_args[i])) PBC_FAIL("cannot load call argument");
                }
                if (ok && (!pbc_lower_op8(&code, PBC_OP_CALL8,
                                          (PBCU8)pending_args.size(),
                                          pending_args.size() + 1, 1) ||
                           !PBC_STORE_RESULT()))
                    PBC_FAIL("cannot emit function call");
                pending_args.clear();
                break;
            case PIR_CALL_METHOD:
            case PIR_GUARDED_CALL_METHOD:
                if (instruction->int_val != pending_args.size() ||
                    instruction->num_operands < 1) {
                    PBC_FAIL("method argument count does not match PIR");
                    break;
                }
                index = symbol(instruction->str_val);
                if (index < 0 || !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op16(&code, PBC_OP_GET_ATTR16,
                                    (PBCU16)index, 1, 1))
                    PBC_FAIL("cannot load bound method");
                for (i = 0; ok && i < pending_args.size(); i++) {
                    if (!PBC_LOAD(pending_args[i])) PBC_FAIL("cannot load method argument");
                }
                if (ok && (!pbc_lower_op8(&code, PBC_OP_CALL8,
                                          (PBCU8)pending_args.size(),
                                          pending_args.size() + 1, 1) ||
                           !PBC_STORE_RESULT()))
                    PBC_FAIL("cannot emit method call");
                pending_args.clear();
                break;

            case PIR_BRANCH: {
                PBCLowerFixup fixup;
                if (!pending_args.empty() || instruction->target_block == 0 ||
                    !pbc_lower_phi_copies(&code, locals, block,
                                          instruction->target_block) ||
                    !pbc_lower_byte(&code, PBC_OP_JUMP16)) {
                    PBC_FAIL("cannot emit unconditional branch");
                    break;
                }
                fixup.operand_offset = code.size;
                fixup.target = instruction->target_block;
                if (!pbc_lower_u16(&code, 0)) PBC_FAIL("cannot emit branch operand");
                else {
                    fixups.push_back(fixup);
                    emitted_terminator = 1;
                }
                break;
            }
            case PIR_COND_BRANCH: {
                PBCLowerFixup fixup;
                PBCU32 false_operand;
                if (!pending_args.empty() || instruction->target_block == 0 ||
                    instruction->false_block == 0 ||
                    instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_byte(&code, PBC_OP_JUMP_IF_FALSE16)) {
                    PBC_FAIL("cannot emit conditional branch");
                    break;
                }
                false_operand = code.size;
                if (!pbc_lower_u16(&code, 0) ||
                    !pbc_lower_stack(&code, 1, 0) ||
                    !pbc_lower_phi_copies(&code, locals, block,
                                          instruction->target_block) ||
                    !pbc_lower_byte(&code, PBC_OP_JUMP16)) {
                    PBC_FAIL("cannot emit true branch edge");
                    break;
                }
                fixup.operand_offset = code.size;
                fixup.target = instruction->target_block;
                if (!pbc_lower_u16(&code, 0)) {
                    PBC_FAIL("cannot emit true branch operand");
                    break;
                }
                fixups.push_back(fixup);
                if (!pbc_lower_patch(&code, false_operand, code.size) ||
                    !pbc_lower_phi_copies(&code, locals, block,
                                          instruction->false_block) ||
                    !pbc_lower_byte(&code, PBC_OP_JUMP16)) {
                    PBC_FAIL("cannot emit false branch edge");
                    break;
                }
                fixup.operand_offset = code.size;
                fixup.target = instruction->false_block;
                if (!pbc_lower_u16(&code, 0)) PBC_FAIL("cannot emit false branch operand");
                else {
                    fixups.push_back(fixup);
                    emitted_terminator = 1;
                }
                break;
            }
            case PIR_CHECK_EXCEPTION: {
                if (!previous_valid)
                    PBC_FAIL("exception check has no protected PBC operation");
                else if (previous_block != block)
                    PBC_FAIL("exception check crosses a PIR block boundary");
                else if (instruction->handler_block == 0)
                    PBC_FAIL("exception check has no handler block");
                else if (!pbc_lower_add_exception(
                             &exceptions, block, instruction->handler_block,
                             previous_start, previous_end))
                    PBC_FAIL("protected PBC range is invalid");
                previous_valid = 0;
                break;
            }
            case PIR_SETUP_TRY:
            case PIR_POP_TRY:
                /* PBC represents protected regions in the function exception
                 * table instead of mutating a runtime try stack. */
                break;
            case PIR_GET_EXCEPTION:
                if (!PBC_STORE_RESULT())
                    PBC_FAIL("exception handler has no active exception");
                break;
            case PIR_EXC_MATCH:
                if (instruction->int_val < 0 ||
                    instruction->int_val > 65535 ||
                    instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op16(&code, PBC_OP_EXC_MATCH16,
                                    (PBCU16)instruction->int_val, 1, 1) ||
                    !PBC_STORE_RESULT())
                    PBC_FAIL("cannot emit exception type match");
                break;
            case PIR_RETURN:
                if (!pir_value_valid(instruction->operands[0])) {
                    opcode = (function->is_generator || function->is_coroutine)
                        ? PBC_OP_RETURN_NONE : PBC_OP_RERAISE;
                    if (!pending_args.empty() ||
                        !pbc_lower_op(&code, opcode, 0, 0))
                        PBC_FAIL("cannot emit empty runtime return");
                    else emitted_terminator = 1;
                } else if (!pending_args.empty() ||
                           !PBC_LOAD(instruction->operands[0]) ||
                           !pbc_lower_op(&code, PBC_OP_RETURN_VALUE, 1, 0)) {
                    PBC_FAIL("cannot emit return value");
                } else {
                    emitted_terminator = 1;
                }
                break;
            case PIR_RETURN_NONE:
                if (!pending_args.empty() ||
                    !pbc_lower_op(&code, PBC_OP_RETURN_NONE, 0, 0))
                    PBC_FAIL("cannot emit return None");
                else emitted_terminator = 1;
                break;
            case PIR_RAISE:
                if (instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op(&code, PBC_OP_RAISE, 1, 0))
                    PBC_FAIL("cannot emit raise");
                else if (instruction->handler_block != 0 &&
                         !pbc_lower_add_exception(
                             &exceptions, block, instruction->handler_block,
                             instruction_start, code.size))
                    PBC_FAIL("cannot record raise handler");
                else emitted_terminator = 1;
                break;
            case PIR_RERAISE:
            case PIR_PANIC_EXCEPTION:
                if (!pbc_lower_op(&code, PBC_OP_RERAISE, 0, 0))
                    PBC_FAIL("cannot emit exception propagation");
                else if (instruction->handler_block != 0 &&
                         !pbc_lower_add_exception(
                             &exceptions, block, instruction->handler_block,
                             instruction_start, code.size))
                    PBC_FAIL("cannot record propagation handler");
                else emitted_terminator = 1;
                break;
            case PIR_CLEAR_EXCEPTION:
                if (!pbc_lower_op(&code, PBC_OP_CLEAR_EXCEPTION, 0, 0))
                    PBC_FAIL("cannot emit exception clear");
                break;
            case PIR_GET_ITER:
                if (instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op(&code, PBC_OP_GET_ITER, 1, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit iterator creation");
                break;
            case PIR_FOR_ITER: {
                PBCLowerFixup fixup;
                if (instruction->num_operands < 1 ||
                    instruction->handler_block == 0 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_byte(&code, PBC_OP_FOR_ITER16)) {
                    PBC_FAIL("cannot emit iterator advance");
                    break;
                }
                fixup.operand_offset = code.size;
                fixup.target = instruction->handler_block;
                if (!pbc_lower_u16(&code, 0) ||
                    !pbc_lower_stack(&code, 0, 1) ||
                    !PBC_STORE_RESULT() ||
                    !pbc_lower_op(&code, PBC_OP_POP_TOP, 1, 0)) {
                    PBC_FAIL("cannot emit iterator success edge");
                    break;
                }
                fixups.push_back(fixup);
                if (instruction->false_block != 0 &&
                    !pbc_lower_add_exception(
                        &exceptions, block, instruction->false_block,
                        instruction_start, code.size))
                    PBC_FAIL("cannot record iterator exception handler");
                break;
            }
            case PIR_YIELD:
                if (instruction->num_operands < 1 ||
                    !PBC_LOAD(instruction->operands[0]) ||
                    !pbc_lower_op(&code, PBC_OP_YIELD_VALUE, 1, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit yield");
                break;

            case PIR_MAKE_FUNCTION:
                index = function_index(instruction->str_val);
                if (index < 0 || index > 65535L) {
                    PBC_FAIL("PIR function target was not found");
                    break;
                }
                if (instruction->num_operands > 0 &&
                    pir_value_valid(instruction->operands[0])) {
                    PBC_FAIL("PBC closure layout is not materialized yet");
                } else if (!pbc_lower_op8(&code, PBC_OP_BUILD_TUPLE8,
                                          0, 0, 1)) {
                    PBC_FAIL("cannot create empty function closure");
                }
                if (ok && (!pbc_lower_op16(&code, PBC_OP_MAKE_FUNCTION16,
                                           (PBCU16)index, 1, 1) ||
                           !PBC_STORE_RESULT()))
                    PBC_FAIL("cannot emit function creation");
                break;
            case PIR_MAKE_CELL:
                if (!pir_value_valid(instruction->result) ||
                    instruction->result.id < 0 ||
                    instruction->result.id >= locals.size() ||
                    !pbc_lower_op16(&code, PBC_OP_MAKE_CELL16,
                                    locals[instruction->result.id], 0, 0))
                    PBC_FAIL("cannot emit closure cell creation");
                break;
            case PIR_CELL_GET:
                if (instruction->num_operands < 1 ||
                    instruction->operands[0].id < 0 ||
                    instruction->operands[0].id >= locals.size() ||
                    !pbc_lower_op16(&code, PBC_OP_LOAD_CELL16,
                                    locals[instruction->operands[0].id], 0, 1) ||
                    !PBC_STORE_RESULT()) PBC_FAIL("cannot emit closure cell load");
                break;
            case PIR_CELL_SET:
                if (instruction->num_operands < 2 ||
                    instruction->operands[0].id < 0 ||
                    instruction->operands[0].id >= locals.size() ||
                    !PBC_LOAD(instruction->operands[1]) ||
                    !pbc_lower_op16(&code, PBC_OP_STORE_CELL16,
                                    locals[instruction->operands[0].id], 1, 0))
                    PBC_FAIL("cannot emit closure cell store");
                break;

            default:
                PBC_FAIL("PIR opcode has no semantics-preserving PBC lowering");
                break;
            }
            if (instruction->op != PIR_CHECK_EXCEPTION &&
                code.size > instruction_start) {
                previous_start = instruction_start;
                previous_end = code.size;
                previous_block = block;
                previous_valid = 1;
            }
            if (code.size > 65535U) PBC_FAIL("PBC function exceeds 65535 bytes");
        }
    }

#undef PBC_STORE_RESULT
#undef PBC_LOAD
#undef PBC_FAIL

    if (ok && !pending_args.empty()) {
        failure = "unterminated PIR argument or operand stack";
        ok = 0;
    }
    for (i = 0; ok && i < fixups.size(); i++) {
        long target = pbc_lower_block_offset(block_offsets, fixups[i].target);
        if (target < 0 || !pbc_lower_patch(&code,
                                           fixups[i].operand_offset,
                                           (PBCU32)target)) {
            failure = "PBC branch target is absent or out of range";
            ok = 0;
        }
    }
    for (i = 0; ok && i < exceptions.size(); i++) {
        long handler = pbc_lower_block_offset(block_offsets,
                                              exceptions[i].handler);
        if (handler < 0 || handler > 65535L) {
            failure = "PBC exception handler target is absent";
            ok = 0;
            break;
        }
        exceptions[i].spec.handler_offset = (PBCU16)handler;
        exception_specs.push_back(exceptions[i].spec);
    }
    if (ok && (code.size == 0 || code.size > 65535U)) {
        failure = "PBC function body has invalid size";
        ok = 0;
    }
    if (ok) {
        memset(&spec, 0, sizeof(spec));
        spec.name_symbol = function_symbols[function_number];
        spec.flags = (function->is_generator ? PBC_FUNC_GENERATOR : 0) |
                     (function->is_coroutine ? PBC_FUNC_COROUTINE : 0) |
                     (function == pir_module->init_func
                      ? PBC_FUNC_MODULE_INIT : 0);
        spec.arg_count = (PBCU16)function->params.size();
        spec.local_count = (PBCU16)next_local;
        spec.max_stack = (PBCU16)code.max_stack;
        spec.closure_count = 0;
        spec.signature_index = 0xffffU;
        if (builder->add_function(spec, code.data, (PBCU16)code.size,
                                  exception_specs.empty()
                                      ? 0 : &exception_specs[0],
                                  (PBCU16)exception_specs.size()) !=
                function_number) {
            failure = builder->error() != 0 ? builder->error()
                                            : "cannot add PBC function";
            ok = 0;
        }
    }
    if (!ok) fail(failure, function, failed_instruction);
    free(code.data);
    return ok;
}

int PBCPIRLowerer::lower(PIRModule *module, PBCWriter &writer)
{
    PBCModuleBuilder module_builder;
    long module_symbol;
    int i;

    builder = &module_builder;
    pir_module = module;
    symbols.clear();
    function_symbols.clear();
    error_count = 0;
    last_error = 0;
    last_function = 0;
    last_op = PIR_NOP;
    last_line = 0;
    if (module == 0 || module->module_name == 0 ||
        module->module_name[0] == '\0') {
        fail("PBC module requires a module name", 0, 0);
        builder = 0;
        return 0;
    }
    if (module->functions.size() > 65535) {
        fail("PBC module has more than 65535 functions", 0, 0);
        builder = 0;
        return 0;
    }
    module_symbol = symbol(module->module_name);
    if (module_symbol < 0 ||
        !module_builder.set_module((PBCU16)module_symbol, 0)) {
        fail(module_builder.error() != 0 ? module_builder.error()
                                        : "cannot set PBC module identity",
             0, 0);
        builder = 0;
        return 0;
    }
    for (i = 0; i < module->functions.size(); i++) {
        long function_symbol = symbol(module->functions[i]->name);
        if (function_symbol < 0) {
            fail("cannot create PBC function symbol",
                 module->functions[i], 0);
            builder = 0;
            return 0;
        }
        function_symbols.push_back((PBCU16)function_symbol);
    }
    for (i = 0; i < module->functions.size(); i++) {
        if (!lower_function(module->functions[i], (PBCU16)i)) {
            builder = 0;
            return 0;
        }
    }
    if (!module_builder.write(writer)) {
        fail(module_builder.error() != 0 ? module_builder.error()
                                        : "cannot write PBC module",
             0, 0);
        builder = 0;
        return 0;
    }
    builder = 0;
    return 1;
}
