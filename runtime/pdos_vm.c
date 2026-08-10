/*
 * pdos_vm.c - Portable PBC stack virtual machine
 */

#include "pdos_vm.h"
#include "pdos_rt.h"
#include "pdos_mem.h"
#include "pdos_exc.h"
#include "pdos_lst.h"
#include "pdos_dic.h"
#include "pdos_cod.h"
#include "pdos_cll.h"
#include "pdos_gen.h"
#include <string.h>

/* Calling the refcount API keeps the interpreter compact on 8086 and avoids
 * expanding the overflow checks at every dispatch site. */
#undef PYDOS_INCREF
#undef PYDOS_DECREF
#define PYDOS_INCREF(obj) pydos_incref(obj)
#define PYDOS_DECREF(obj) pydos_decref(obj)

typedef struct PyDosVMFrame {
    const PyDosVMModule far *module;
    const PyDosVMFunction far *function;
    PyDosObj far * far *locals;
    PyDosObj far * far *stack;
    PyDosObj far *closure;
    PBCU16 stack_size;
    PBCU16 pc;
    PyDosObj far *owner;
    PyDosObj far *active_exception;
} PyDosVMFrame;

static PBCU16 vm_u16(const PBCU8 far *p)
{
    return (PBCU16)((PBCU16)p[0] | ((PBCU16)p[1] << 8));
}

static PBCI16 vm_i16(const PBCU8 far *p)
{
    PBCU16 raw = vm_u16(p);
    if ((raw & 0x8000U) != 0)
        return (PBCI16)((long)raw - 65536L);
    return (PBCI16)raw;
}

static void vm_result(PyDosVMResult far *result, PyDosVMStatus status,
                      PBCU16 function_index, PBCU16 offset, PBCU8 opcode)
{
    if (result == (PyDosVMResult far *)0) return;
    result->status = status;
    result->function_index = function_index;
    result->bytecode_offset = offset;
    result->opcode = opcode;
}

const char far * PYDOS_API pydos_vm_status_name(PyDosVMStatus status)
{
    switch (status) {
    case PYDOS_VM_OK: return (const char far *)"ok";
    case PYDOS_VM_INVALID_ARGUMENT: return (const char far *)"invalid argument";
    case PYDOS_VM_FUNCTION_OUT_OF_RANGE:
        return (const char far *)"function index out of range";
    case PYDOS_VM_ARGUMENT_COUNT: return (const char far *)"argument count mismatch";
    case PYDOS_VM_FRAME_TOO_LARGE: return (const char far *)"VM frame too large";
    case PYDOS_VM_OUT_OF_MEMORY: return (const char far *)"VM frame allocation failed";
    case PYDOS_VM_BAD_BYTECODE: return (const char far *)"invalid bytecode";
    case PYDOS_VM_UNSUPPORTED_OPCODE: return (const char far *)"unsupported opcode";
    case PYDOS_VM_PYTHON_EXCEPTION: return (const char far *)"Python exception";
    }
    return (const char far *)"unknown VM status";
}

static void vm_release_frame(PyDosVMFrame far *frame)
{
    PBCU16 i;
    if (frame->owner != (PyDosObj far *)0) {
        if (frame->active_exception != (PyDosObj far *)0) {
            PYDOS_DECREF(frame->active_exception);
            frame->active_exception = (PyDosObj far *)0;
        }
        return;
    }
    if (frame->locals != (PyDosObj far * far *)0) {
        for (i = 0; i < frame->function->local_count; i++) {
            if (frame->locals[i] != (PyDosObj far *)0)
                PYDOS_DECREF(frame->locals[i]);
        }
        pydos_far_free(frame->locals);
    }
    if (frame->stack != (PyDosObj far * far *)0) {
        for (i = 0; i < frame->stack_size; i++) {
            if (frame->stack[i] != (PyDosObj far *)0)
                PYDOS_DECREF(frame->stack[i]);
        }
        pydos_far_free(frame->stack);
    }
    if (frame->closure != (PyDosObj far *)0)
        PYDOS_DECREF(frame->closure);
    if (frame->active_exception != (PyDosObj far *)0)
        PYDOS_DECREF(frame->active_exception);
}

/* Transfer one owned exception reference into the executing C frame.  The
 * generated handler immediately materializes the caught object in a VM local;
 * keeping another pointer in PyDosGen would enlarge every PyDosObj on 8086. */
static void vm_take_active_exception(PyDosVMFrame far *frame,
                                     PyDosObj far *exception)
{
    if (frame->active_exception != (PyDosObj far *)0)
        PYDOS_DECREF(frame->active_exception);
    frame->active_exception = exception;
}

static void vm_clear_active_exception(PyDosVMFrame far *frame)
{
    if (frame->active_exception != (PyDosObj far *)0)
        PYDOS_DECREF(frame->active_exception);
    frame->active_exception = (PyDosObj far *)0;
}

static void vm_sync_stack(PyDosVMFrame far *frame)
{
    if (frame->owner != (PyDosObj far *)0 &&
        frame->owner->v.gen.vm_stack != (PyDosObj far *)0)
        frame->owner->v.gen.vm_stack->v.list.len = frame->stack_size;
}

static PyDosObj far *vm_closure_cell(PyDosVMFrame far *frame,
                                     PBCU16 index)
{
    if (frame->closure == (PyDosObj far *)0 ||
        (PyDosType)frame->closure->type != PYDT_TUPLE ||
        index >= frame->closure->v.tuple.len)
        return (PyDosObj far *)0;
    if (frame->closure->v.tuple.items[index] == (PyDosObj far *)0 ||
        (PyDosType)frame->closure->v.tuple.items[index]->type != PYDT_CELL)
        return (PyDosObj far *)0;
    return frame->closure->v.tuple.items[index];
}

static int vm_push(PyDosVMFrame far *frame, PyDosObj far *value)
{
    if (value == (PyDosObj far *)0 ||
        frame->stack_size >= frame->function->max_stack) {
        if (value != (PyDosObj far *)0) PYDOS_DECREF(value);
        return 0;
    }
    frame->stack[frame->stack_size++] = value;
    vm_sync_stack(frame);
    return 1;
}

static PyDosObj far *vm_pop(PyDosVMFrame far *frame)
{
    PyDosObj far *value;
    if (frame->stack_size == 0) return (PyDosObj far *)0;
    value = frame->stack[--frame->stack_size];
    frame->stack[frame->stack_size] = (PyDosObj far *)0;
    vm_sync_stack(frame);
    return value;
}

static int vm_symbol_matches_exception(const PyDosVMFrame far *frame,
                                       PBCU16 symbol_index,
                                       PyDosObj far *exception)
{
    PyDosObj far *symbol;
    const char far *name;
    unsigned int length;
    if (symbol_index == 0xffffU) return 1;
    if (symbol_index >= frame->module->symbol_count ||
        exception == (PyDosObj far *)0 ||
        (PyDosType)exception->type != PYDT_EXCEPTION)
        return 0;
    symbol = frame->module->symbols[symbol_index];
    if (symbol == (PyDosObj far *)0 || (PyDosType)symbol->type != PYDT_STR)
        return 0;
    name = pydos_exc_type_name(exception->v.exc.type_code);
    if (name == (const char far *)0) return 0;
    length = (unsigned int)_fstrlen(name);
    return length == symbol->v.str.len &&
           _fmemcmp(name, symbol->v.str.data, length) == 0;
}

static int vm_dispatch_exception(PyDosVMFrame far *frame,
                                 PBCU16 fault_offset)
{
    PyDosObj far *exception;
    PBCU16 i;
    if (!pydos_exc_pending()) return 0;
    exception = pydos_exc_current();
    for (i = 0; i < frame->function->exception_count; i++) {
        const PyDosVMException far *handler =
            &frame->function->exceptions[i];
        PyDosObj far *owned;
        if (fault_offset < handler->start_offset ||
            fault_offset >= handler->end_offset ||
            !vm_symbol_matches_exception(frame, handler->match_symbol,
                                         exception))
            continue;
        if (handler->stack_depth >= frame->function->max_stack ||
            handler->stack_depth > frame->stack_size ||
            handler->handler_offset >= frame->function->code_size)
            return 0;
        while (frame->stack_size > handler->stack_depth) {
            PyDosObj far *discarded = vm_pop(frame);
            PYDOS_DECREF(discarded);
        }
        owned = pydos_exc_fetch();
        pydos_exc_clear();
        vm_take_active_exception(frame, owned);
        PYDOS_INCREF(owned);
        if (!vm_push(frame, owned)) {
            pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                            (const char far *)"invalid VM exception stack");
            return 0;
        }
        frame->pc = handler->handler_offset;
        return 1;
    }
    return 0;
}

static PyDosObj far *vm_binary(PBCU8 opcode, PyDosObj far *left,
                               PyDosObj far *right)
{
    switch (opcode) {
    case PBC_OP_PY_ADD: return pydos_obj_add(left, right);
    case PBC_OP_PY_SUB: return pydos_obj_sub(left, right);
    case PBC_OP_PY_MUL: return pydos_obj_mul(left, right);
    case PBC_OP_PY_TRUE_DIV: return pydos_obj_truediv(left, right);
    case PBC_OP_PY_FLOOR_DIV: return pydos_obj_floordiv(left, right);
    case PBC_OP_PY_MOD: return pydos_obj_mod(left, right);
    case PBC_OP_PY_POW: return pydos_obj_pow(left, right);
    }
    return (PyDosObj far *)0;
}

static PyDosObj far *vm_compare(PBCU8 opcode, PyDosObj far *left,
                                PyDosObj far *right)
{
    int comparison;
    int truth;
    if (opcode == PBC_OP_CMP_EQ || opcode == PBC_OP_CMP_NE) {
        truth = pydos_obj_equal(left, right);
        if (opcode == PBC_OP_CMP_NE) truth = !truth;
        return pydos_obj_new_bool(truth);
    }
    comparison = pydos_obj_compare(left, right);
    switch (opcode) {
    case PBC_OP_CMP_LT: truth = comparison < 0; break;
    case PBC_OP_CMP_LE: truth = comparison <= 0; break;
    case PBC_OP_CMP_GT: truth = comparison > 0; break;
    default: truth = comparison >= 0; break;
    }
    return pydos_obj_new_bool(truth);
}

static PyDosObj far *vm_build_collection(PyDosVMFrame far *frame,
                                         PBCU8 opcode, PBCU16 count)
{
    PyDosObj far *collection;
    PBCU16 base;
    PBCU16 i;
    if (count > frame->stack_size) return (PyDosObj far *)0;
    if (opcode == PBC_OP_BUILD_DICT8) {
        if (count > frame->stack_size / 2U)
            return (PyDosObj far *)0;
        base = (PBCU16)(frame->stack_size - count * 2U);
        collection = pydos_dict_new(count * 2U + 1U);
    } else {
        base = (PBCU16)(frame->stack_size - count);
        if (opcode == PBC_OP_BUILD_SET8)
        collection = pydos_set_empty((PyDosObj far *)0);
        else
            collection = pydos_list_new(count);
    }
    if (collection == (PyDosObj far *)0) return (PyDosObj far *)0;
    for (i = 0; i < count; i++) {
        if (opcode == PBC_OP_BUILD_DICT8) {
            pydos_dict_set(collection, frame->stack[base + i * 2U],
                           frame->stack[base + i * 2U + 1U]);
        } else if (opcode == PBC_OP_BUILD_SET8) {
            pydos_set_add(collection, frame->stack[base + i]);
        } else {
            PBCU16 old_length = (PBCU16)collection->v.list.len;
            PyDosObj far *none = pydos_list_append(
                collection, frame->stack[base + i]);
            if (none != (PyDosObj far *)0) PYDOS_DECREF(none);
            if (collection->v.list.len != (unsigned int)(old_length + 1U)) {
                PYDOS_DECREF(collection);
                pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                                (const char far *)"cannot grow VM collection");
                return (PyDosObj far *)0;
            }
        }
        if (pydos_exc_pending()) {
            PYDOS_DECREF(collection);
            return (PyDosObj far *)0;
        }
    }
    if (opcode == PBC_OP_BUILD_DICT8) count = (PBCU16)(count * 2U);
    for (i = 0; i < count; i++) {
        PyDosObj far *item = vm_pop(frame);
        PYDOS_DECREF(item);
    }
    if (opcode == PBC_OP_BUILD_TUPLE8)
        pydos_list_to_tuple(collection);
    return collection;
}

static void vm_clear_generator_frame(PyDosObj far *generator)
{
    if (generator->v.gen.locals != (PyDosObj far *)0) {
        PYDOS_DECREF(generator->v.gen.locals);
        generator->v.gen.locals = (PyDosObj far *)0;
    }
    if (generator->v.gen.vm_stack != (PyDosObj far *)0) {
        PYDOS_DECREF(generator->v.gen.vm_stack);
        generator->v.gen.vm_stack = (PyDosObj far *)0;
    }
    if (generator->v.gen.vm_closure != (PyDosObj far *)0) {
        PYDOS_DECREF(generator->v.gen.vm_closure);
        generator->v.gen.vm_closure = (PyDosObj far *)0;
    }
    pydos_code_ref_release(generator->v.gen.code_ref);
    generator->v.gen.code_ref = (PyDosCodeRef far *)0;
    generator->v.gen.vm_suspended = 0;
}

static PyDosObj far *pydos_vm_execute_frame(
    const PyDosVMModule far *module, PBCU16 function_index,
    PBCU16 argc, PyDosObj far * far *argv,
    PyDosVMResult far *result, PyDosObj far *generator)
{
    PyDosVMFrame frame;
    const PyDosVMFunction far *function;
    PyDosObj far *return_value = (PyDosObj far *)0;
    PyDosVMStatus status = PYDOS_VM_OK;
    PBCU16 fault_offset = 0;
    PBCU8 opcode = PBC_OP_NOP;
    PBCU16 i;
    int yielded = 0;

    vm_result(result, PYDOS_VM_OK, function_index, 0, PBC_OP_NOP);
    if (module == (const PyDosVMModule far *)0 ||
        module->functions == (const PyDosVMFunction far *)0 ||
        (module->constant_count != 0 &&
         module->constants == (PyDosObj far * far *)0) ||
        (module->symbol_count != 0 &&
         module->symbols == (PyDosObj far * far *)0) ||
        (generator == (PyDosObj far *)0 &&
         argc != 0 && argv == (PyDosObj far * far *)0)) {
        vm_result(result, PYDOS_VM_INVALID_ARGUMENT, function_index, 0, 0);
        return (PyDosObj far *)0;
    }
    if (function_index >= module->function_count) {
        vm_result(result, PYDOS_VM_FUNCTION_OUT_OF_RANGE,
                  function_index, 0, 0);
        return (PyDosObj far *)0;
    }
    function = &module->functions[function_index];
    if (function->code == (const PBCU8 far *)0 ||
        function->code_size == 0 ||
        function->arg_count > function->local_count ||
        (function->exception_count != 0 &&
         function->exceptions == (const PyDosVMException far *)0)) {
        vm_result(result, PYDOS_VM_BAD_BYTECODE, function_index, 0, 0);
        return (PyDosObj far *)0;
    }
    if (generator == (PyDosObj far *)0 &&
        argc != function->arg_count) {
        vm_result(result, PYDOS_VM_ARGUMENT_COUNT, function_index, 0, 0);
        return (PyDosObj far *)0;
    }
    if (function->closure_count != 0 &&
        ((generator == (PyDosObj far *)0 &&
          (pydos_active_closure == (PyDosObj far *)0 ||
           (PyDosType)pydos_active_closure->type != PYDT_TUPLE ||
           pydos_active_closure->v.tuple.len != function->closure_count)) ||
         (generator != (PyDosObj far *)0 &&
          (generator->v.gen.vm_closure == (PyDosObj far *)0 ||
           (PyDosType)generator->v.gen.vm_closure->type != PYDT_TUPLE ||
           generator->v.gen.vm_closure->v.tuple.len !=
               function->closure_count)))) {
        vm_result(result, PYDOS_VM_INVALID_ARGUMENT, function_index, 0, 0);
        return (PyDosObj far *)0;
    }
    if ((unsigned long)function->local_count *
            (unsigned long)sizeof(PyDosObj far *) > 0xff00UL ||
        (unsigned long)function->max_stack *
            (unsigned long)sizeof(PyDosObj far *) > 0xff00UL) {
        vm_result(result, PYDOS_VM_FRAME_TOO_LARGE, function_index, 0, 0);
        return (PyDosObj far *)0;
    }

    _fmemset(&frame, 0, sizeof(frame));
    frame.module = module;
    frame.function = function;
    frame.owner = generator;
    if (generator != (PyDosObj far *)0) {
        if (((PyDosType)generator->type != PYDT_GENERATOR &&
             (PyDosType)generator->type != PYDT_COROUTINE) ||
            generator->v.gen.locals == (PyDosObj far *)0 ||
            (PyDosType)generator->v.gen.locals->type != PYDT_LIST ||
            generator->v.gen.locals->v.list.len != function->local_count ||
            generator->v.gen.vm_stack == (PyDosObj far *)0 ||
            (PyDosType)generator->v.gen.vm_stack->type != PYDT_LIST ||
            generator->v.gen.vm_stack->v.list.len > function->max_stack ||
            generator->v.gen.vm_stack->v.list.cap < function->max_stack ||
            generator->v.gen.pc < 0 ||
            generator->v.gen.pc >= (int)function->code_size) {
            status = PYDOS_VM_BAD_BYTECODE;
            goto done;
        }
        frame.locals = generator->v.gen.locals->v.list.items;
        frame.stack = generator->v.gen.vm_stack->v.list.items;
        frame.stack_size = (PBCU16)generator->v.gen.vm_stack->v.list.len;
        frame.pc = (PBCU16)generator->v.gen.pc;
        frame.closure = generator->v.gen.vm_closure;
    } else {
        frame.closure = pydos_active_closure;
        if (frame.closure != (PyDosObj far *)0) PYDOS_INCREF(frame.closure);
    }
    if (generator == (PyDosObj far *)0 && function->local_count != 0) {
        frame.locals = (PyDosObj far * far *)pydos_mem_alloc(
            PYDOS_MEM_METADATA,
            (unsigned long)function->local_count * sizeof(PyDosObj far *));
        if (frame.locals != (PyDosObj far * far *)0)
            _fmemset(frame.locals, 0,
                     function->local_count * sizeof(PyDosObj far *));
    }
    if (generator == (PyDosObj far *)0 && function->max_stack != 0) {
        frame.stack = (PyDosObj far * far *)pydos_mem_alloc(
            PYDOS_MEM_METADATA,
            (unsigned long)function->max_stack * sizeof(PyDosObj far *));
        if (frame.stack != (PyDosObj far * far *)0)
            _fmemset(frame.stack, 0,
                     function->max_stack * sizeof(PyDosObj far *));
    }
    if ((function->local_count != 0 &&
         frame.locals == (PyDosObj far * far *)0) ||
        (function->max_stack != 0 &&
         frame.stack == (PyDosObj far * far *)0)) {
        status = PYDOS_VM_OUT_OF_MEMORY;
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate VM frame");
        goto done;
    }
    for (i = 0; generator == (PyDosObj far *)0 && i < argc; i++) {
        if (argv[i] == (PyDosObj far *)0) {
            status = PYDOS_VM_INVALID_ARGUMENT;
            goto done;
        }
        frame.locals[i] = argv[i];
        PYDOS_INCREF(argv[i]);
    }

    if (generator != (PyDosObj far *)0 &&
        generator->v.gen.vm_suspended) {
        generator->v.gen.vm_suspended = 0;
        fault_offset = frame.pc > 0 ? (PBCU16)(frame.pc - 1U) : 0;
        if (pydos_gen_raise_pending_throw(generator)) {
            if (!vm_dispatch_exception(&frame, fault_offset)) {
                status = PYDOS_VM_PYTHON_EXCEPTION;
                goto done;
            }
        } else {
            PyDosObj far *sent = pydos_gen_sent;
            if (sent == (PyDosObj far *)0)
                sent = pydos_obj_new_none();
            else
                PYDOS_INCREF(sent);
            if (!vm_push(&frame, sent)) {
                status = PYDOS_VM_BAD_BYTECODE;
                goto done;
            }
        }
    }

    while (frame.pc < function->code_size) {
        PBCU16 operand = 0;
        PyDosObj far *value = (PyDosObj far *)0;
        PyDosObj far *left = (PyDosObj far *)0;
        PyDosObj far *right = (PyDosObj far *)0;
        long target;
        fault_offset = frame.pc;
        opcode = function->code[frame.pc++];

        switch (opcode) {
        case PBC_OP_NOP:
            break;
        case PBC_OP_LOAD_NONE:
            value = pydos_obj_new_none();
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_LOAD_TRUE:
        case PBC_OP_LOAD_FALSE:
            value = pydos_obj_new_bool(opcode == PBC_OP_LOAD_TRUE);
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_LOAD_CONST8:
            if (frame.pc >= function->code_size) goto bad_bytecode;
            operand = function->code[frame.pc++];
            if (operand >= module->constant_count) goto bad_bytecode;
            value = module->constants[operand];
            PYDOS_INCREF(value);
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_LOAD_CONST16:
            if (frame.pc + 1U >= function->code_size) goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (operand >= module->constant_count) goto bad_bytecode;
            value = module->constants[operand];
            PYDOS_INCREF(value);
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_LOAD_LOCAL8:
        case PBC_OP_STORE_LOCAL8:
            if (frame.pc >= function->code_size) goto bad_bytecode;
            operand = function->code[frame.pc++];
            if (operand >= function->local_count) goto bad_bytecode;
            if (opcode == PBC_OP_LOAD_LOCAL8) {
                value = frame.locals[operand];
                if (value == (PyDosObj far *)0) {
                    pydos_exc_raise(PYDOS_EXC_UNBOUND_LOCAL,
                                    (const char far *)"unbound VM local");
                    goto python_exception;
                }
                PYDOS_INCREF(value);
                if (!vm_push(&frame, value)) goto bad_bytecode;
            } else {
                value = vm_pop(&frame);
                if (value == (PyDosObj far *)0) goto bad_bytecode;
                if (frame.locals[operand] != (PyDosObj far *)0)
                    PYDOS_DECREF(frame.locals[operand]);
                frame.locals[operand] = value;
            }
            break;
        case PBC_OP_LOAD_LOCAL16:
        case PBC_OP_STORE_LOCAL16:
            if (frame.pc + 1U >= function->code_size) goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (operand >= function->local_count) goto bad_bytecode;
            if (opcode == PBC_OP_LOAD_LOCAL16) {
                value = frame.locals[operand];
                if (value == (PyDosObj far *)0) {
                    pydos_exc_raise(PYDOS_EXC_UNBOUND_LOCAL,
                                    (const char far *)"unbound VM local");
                    goto python_exception;
                }
                PYDOS_INCREF(value);
                if (!vm_push(&frame, value)) goto bad_bytecode;
            } else {
                value = vm_pop(&frame);
                if (value == (PyDosObj far *)0) goto bad_bytecode;
                if (frame.locals[operand] != (PyDosObj far *)0)
                    PYDOS_DECREF(frame.locals[operand]);
                frame.locals[operand] = value;
            }
            break;
        case PBC_OP_LOAD_GLOBAL16:
        case PBC_OP_STORE_GLOBAL16:
            if (frame.pc + 1U >= function->code_size ||
                module->symbols == (PyDosObj far * far *)0)
                goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (operand >= module->symbol_count ||
                module->symbols[operand] == (PyDosObj far *)0)
                goto bad_bytecode;
            if (opcode == PBC_OP_LOAD_GLOBAL16) {
                PyDosObj far *globals = module->globals != (PyDosObj far *)0
                    ? module->globals : pydos_globals;
                value = pydos_dict_get(globals, module->symbols[operand]);
                if (value == (PyDosObj far *)0) {
                    pydos_exc_raise(PYDOS_EXC_NAME_ERROR,
                                    (const char far *)"VM global is not defined");
                    goto python_exception;
                }
                if (!vm_push(&frame, value)) goto bad_bytecode;
            } else {
                PyDosObj far *globals = module->globals != (PyDosObj far *)0
                    ? module->globals : pydos_globals;
                value = vm_pop(&frame);
                if (value == (PyDosObj far *)0) goto bad_bytecode;
                pydos_dict_set(globals, module->symbols[operand], value);
                PYDOS_DECREF(value);
                if (pydos_exc_pending()) goto python_exception;
            }
            break;
        case PBC_OP_POP_TOP:
            value = vm_pop(&frame);
            if (value == (PyDosObj far *)0) goto bad_bytecode;
            PYDOS_DECREF(value);
            break;
        case PBC_OP_DUP_TOP:
            if (frame.stack_size == 0) goto bad_bytecode;
            value = frame.stack[frame.stack_size - 1U];
            PYDOS_INCREF(value);
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_PY_ADD:
        case PBC_OP_PY_SUB:
        case PBC_OP_PY_MUL:
        case PBC_OP_PY_TRUE_DIV:
        case PBC_OP_PY_FLOOR_DIV:
        case PBC_OP_PY_MOD:
        case PBC_OP_PY_POW:
            right = vm_pop(&frame);
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0 || right == (PyDosObj far *)0)
                goto bad_binary_stack;
            value = vm_binary(opcode, left, right);
            PYDOS_DECREF(left);
            PYDOS_DECREF(right);
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_PY_NEG:
        case PBC_OP_PY_POS:
        case PBC_OP_PY_NOT:
        case PBC_OP_PY_BIT_NOT:
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0) goto bad_bytecode;
            if (opcode == PBC_OP_PY_NEG) value = pydos_obj_neg(left);
            else if (opcode == PBC_OP_PY_POS) value = pydos_obj_pos(left);
            else if (opcode == PBC_OP_PY_BIT_NOT) value = pydos_obj_invert(left);
            else value = pydos_obj_new_bool(!pydos_obj_is_truthy(left));
            PYDOS_DECREF(left);
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_CMP_EQ:
        case PBC_OP_CMP_NE:
        case PBC_OP_CMP_LT:
        case PBC_OP_CMP_LE:
        case PBC_OP_CMP_GT:
        case PBC_OP_CMP_GE:
            right = vm_pop(&frame);
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0 || right == (PyDosObj far *)0)
                goto bad_binary_stack;
            value = vm_compare(opcode, left, right);
            PYDOS_DECREF(left);
            PYDOS_DECREF(right);
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_IS:
        case PBC_OP_IS_NOT:
        case PBC_OP_CONTAINS:
        case PBC_OP_NOT_CONTAINS:
            right = vm_pop(&frame);
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0 || right == (PyDosObj far *)0)
                goto bad_binary_stack;
            if (opcode == PBC_OP_IS || opcode == PBC_OP_IS_NOT) {
                i = (PBCU16)(left == right);
                if (opcode == PBC_OP_IS_NOT) i = (PBCU16)!i;
            } else {
                i = (PBCU16)pydos_obj_contains(right, left);
                if (pydos_exc_pending()) {
                    PYDOS_DECREF(left);
                    PYDOS_DECREF(right);
                    goto python_exception;
                }
                if (opcode == PBC_OP_NOT_CONTAINS) i = (PBCU16)!i;
            }
            PYDOS_DECREF(left);
            PYDOS_DECREF(right);
            value = pydos_obj_new_bool(i != 0);
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_JUMP16:
        case PBC_OP_JUMP_IF_TRUE16:
        case PBC_OP_JUMP_IF_FALSE16:
        case PBC_OP_CHECK_EXCEPTION16:
            if (frame.pc + 1U >= function->code_size) goto bad_bytecode;
            target = (long)frame.pc + 2L +
                     (long)vm_i16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (target < 0L || target >= (long)function->code_size)
                goto bad_bytecode;
            if (opcode == PBC_OP_JUMP16) {
                frame.pc = (PBCU16)target;
            } else if (opcode == PBC_OP_CHECK_EXCEPTION16) {
                if (pydos_exc_pending()) frame.pc = (PBCU16)target;
            } else {
                value = vm_pop(&frame);
                if (value == (PyDosObj far *)0) goto bad_bytecode;
                i = (PBCU16)pydos_obj_is_truthy(value);
                PYDOS_DECREF(value);
                if ((opcode == PBC_OP_JUMP_IF_TRUE16 && i) ||
                    (opcode == PBC_OP_JUMP_IF_FALSE16 && !i))
                    frame.pc = (PBCU16)target;
            }
            break;
        case PBC_OP_CALL8:
            if (frame.pc >= function->code_size) goto bad_bytecode;
            operand = function->code[frame.pc++];
            if ((PBCU16)(operand + 1U) > frame.stack_size)
                goto bad_bytecode;
            i = (PBCU16)(frame.stack_size - operand - 1U);
            value = pydos_obj_call(frame.stack[i], operand,
                                   frame.stack + i + 1U);
            while (frame.stack_size > i) {
                PyDosObj far *discarded = vm_pop(&frame);
                PYDOS_DECREF(discarded);
            }
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_RETURN_VALUE:
            return_value = vm_pop(&frame);
            if (return_value == (PyDosObj far *)0) goto bad_bytecode;
            goto done;
        case PBC_OP_RETURN_NONE:
            return_value = pydos_obj_new_none();
            goto done;
        case PBC_OP_RAISE:
            value = vm_pop(&frame);
            if (value == (PyDosObj far *)0) goto bad_bytecode;
            pydos_exc_raise_obj(value);
            PYDOS_DECREF(value);
            goto python_exception;
        case PBC_OP_BUILD_LIST8:
        case PBC_OP_BUILD_TUPLE8:
        case PBC_OP_BUILD_SET8:
        case PBC_OP_BUILD_DICT8:
            if (frame.pc >= function->code_size) goto bad_bytecode;
            operand = function->code[frame.pc++];
            value = vm_build_collection(&frame, opcode, operand);
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_GET_ITEM:
            right = vm_pop(&frame);
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0 || right == (PyDosObj far *)0)
                goto bad_binary_stack;
            value = pydos_obj_getitem(left, right);
            PYDOS_DECREF(left);
            PYDOS_DECREF(right);
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
                goto python_exception;
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_SET_ITEM:
            value = vm_pop(&frame);
            right = vm_pop(&frame);
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0 || right == (PyDosObj far *)0 ||
                value == (PyDosObj far *)0)
                goto bad_ternary_stack;
            pydos_obj_setitem(left, right, value);
            PYDOS_DECREF(left);
            PYDOS_DECREF(right);
            PYDOS_DECREF(value);
            if (pydos_exc_pending()) goto python_exception;
            break;
        case PBC_OP_GET_ATTR16:
        case PBC_OP_SET_ATTR16:
            if (frame.pc + 1U >= function->code_size ||
                module->symbols == (PyDosObj far * far *)0)
                goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (operand >= module->symbol_count ||
                module->symbols[operand] == (PyDosObj far *)0 ||
                (PyDosType)module->symbols[operand]->type != PYDT_STR)
                goto bad_bytecode;
            if (opcode == PBC_OP_GET_ATTR16) {
                left = vm_pop(&frame);
                if (left == (PyDosObj far *)0) goto bad_bytecode;
                value = pydos_obj_get_attr(
                    left, module->symbols[operand]->v.str.data);
                PYDOS_DECREF(left);
                if (value == (PyDosObj far *)0 || pydos_exc_pending())
                    goto python_exception;
                if (!vm_push(&frame, value)) goto bad_bytecode;
            } else {
                value = vm_pop(&frame);
                left = vm_pop(&frame);
                if (left == (PyDosObj far *)0 || value == (PyDosObj far *)0) {
                    if (left != (PyDosObj far *)0) PYDOS_DECREF(left);
                    if (value != (PyDosObj far *)0) PYDOS_DECREF(value);
                    goto bad_bytecode;
                }
                pydos_obj_set_attr(left,
                    module->symbols[operand]->v.str.data, value);
                PYDOS_DECREF(left);
                PYDOS_DECREF(value);
                if (pydos_exc_pending()) goto python_exception;
            }
            break;
        case PBC_OP_GET_ITER:
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0) goto bad_bytecode;
            value = pydos_obj_get_iter(left);
            PYDOS_DECREF(left);
            if (value == (PyDosObj far *)0) {
                if (!pydos_exc_pending())
                    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                                    (const char far *)"object is not iterable");
                goto python_exception;
            }
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_FOR_ITER16:
            if (frame.pc + 1U >= function->code_size ||
                frame.stack_size == 0)
                goto bad_bytecode;
            target = (long)frame.pc + 2L +
                     (long)vm_i16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (target < 0L || target >= (long)function->code_size)
                goto bad_bytecode;
            value = pydos_obj_iter_next(frame.stack[frame.stack_size - 1U]);
            if (value == (PyDosObj far *)0) {
                if (pydos_exc_pending()) goto python_exception;
                left = vm_pop(&frame);
                PYDOS_DECREF(left);
                frame.pc = (PBCU16)target;
            } else if (!vm_push(&frame, value)) {
                goto bad_bytecode;
            }
            break;
        case PBC_OP_CLEAR_EXCEPTION:
            pydos_exc_clear();
            vm_clear_active_exception(&frame);
            break;
        case PBC_OP_RERAISE:
            if (!pydos_exc_pending()) {
                if (frame.active_exception != (PyDosObj far *)0)
                    pydos_exc_raise_obj(frame.active_exception);
                else
                    pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                                    (const char far *)"no active exception");
            }
            goto python_exception;
        case PBC_OP_EXC_MATCH16:
            if (frame.pc + 1U >= function->code_size)
                goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            value = vm_pop(&frame);
            if (value == (PyDosObj far *)0) goto bad_bytecode;
            left = pydos_obj_new_bool(
                pydos_exc_matches(value, (int)operand));
            PYDOS_DECREF(value);
            if (left == (PyDosObj far *)0) goto python_exception;
            if (!vm_push(&frame, left)) goto bad_bytecode;
            break;
        case PBC_OP_MAKE_FUNCTION16:
            if (frame.pc + 1U >= function->code_size)
                goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (operand >= module->function_count ||
                module->functions[operand].arg_count > 255U)
                goto bad_bytecode;
            left = vm_pop(&frame);
            if (left == (PyDosObj far *)0 ||
                (PyDosType)left->type != PYDT_TUPLE ||
                left->v.tuple.len !=
                    module->functions[operand].closure_count) {
                if (left != (PyDosObj far *)0) PYDOS_DECREF(left);
                goto bad_bytecode;
            }
            for (i = 0; i < left->v.tuple.len; i++) {
                if (left->v.tuple.items[i] == (PyDosObj far *)0 ||
                    (PyDosType)left->v.tuple.items[i]->type != PYDT_CELL) {
                    PYDOS_DECREF(left);
                    goto bad_bytecode;
                }
            }
            if (module->functions[operand].name_symbol <
                    module->symbol_count &&
                module->symbols != (PyDosObj far * far *)0 &&
                module->symbols[module->functions[operand].name_symbol] !=
                    (PyDosObj far *)0 &&
                (PyDosType)module->symbols[
                    module->functions[operand].name_symbol]->type == PYDT_STR) {
                value = pydos_func_new_pbc(
                    module, operand,
                    module->symbols[
                        module->functions[operand].name_symbol]->v.str.data);
            } else {
                value = pydos_func_new_pbc(
                    module, operand, (const char far *)"<pbc function>");
            }
            if (value == (PyDosObj far *)0 || pydos_exc_pending())
            {
                PYDOS_DECREF(left);
                goto python_exception;
            }
            pydos_func_set_closure(value, left);
            PYDOS_DECREF(left);
            if (pydos_exc_pending()) {
                PYDOS_DECREF(value);
                goto python_exception;
            }
            pydos_func_set_arg_count(
                value, module->functions[operand].arg_count);
            if (!vm_push(&frame, value)) goto bad_bytecode;
            break;
        case PBC_OP_MAKE_CELL16:
        case PBC_OP_LOAD_CELL16:
        case PBC_OP_STORE_CELL16:
            if (frame.pc + 1U >= function->code_size)
                goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            if (operand >= function->local_count)
                goto bad_bytecode;
            if (opcode == PBC_OP_MAKE_CELL16) {
                if (frame.locals[operand] != (PyDosObj far *)0 &&
                    (PyDosType)frame.locals[operand]->type == PYDT_CELL)
                    goto bad_bytecode;
                value = pydos_cell_new();
                if (value == (PyDosObj far *)0) goto python_exception;
                if (frame.locals[operand] != (PyDosObj far *)0) {
                    pydos_cell_set(value, frame.locals[operand]);
                    PYDOS_DECREF(frame.locals[operand]);
                }
                frame.locals[operand] = value;
            } else if (frame.locals[operand] == (PyDosObj far *)0 ||
                       (PyDosType)frame.locals[operand]->type != PYDT_CELL) {
                goto bad_bytecode;
            } else if (opcode == PBC_OP_LOAD_CELL16) {
                value = pydos_cell_get(frame.locals[operand]);
                if (value == (PyDosObj far *)0) {
                    pydos_exc_raise(PYDOS_EXC_UNBOUND_LOCAL,
                                    (const char far *)"empty VM cell");
                    goto python_exception;
                }
                if (!vm_push(&frame, value)) goto bad_bytecode;
            } else {
                value = vm_pop(&frame);
                if (value == (PyDosObj far *)0) goto bad_bytecode;
                pydos_cell_set(frame.locals[operand], value);
                PYDOS_DECREF(value);
            }
            break;
        case PBC_OP_LOAD_DEREF16:
        case PBC_OP_STORE_DEREF16:
            if (frame.pc + 1U >= function->code_size)
                goto bad_bytecode;
            operand = vm_u16(function->code + frame.pc);
            frame.pc = (PBCU16)(frame.pc + 2U);
            left = vm_closure_cell(&frame, operand);
            if (left == (PyDosObj far *)0) goto bad_bytecode;
            if (opcode == PBC_OP_LOAD_DEREF16) {
                value = pydos_cell_get(left);
                if (value == (PyDosObj far *)0) {
                    pydos_exc_raise(PYDOS_EXC_NAME_ERROR,
                                    (const char far *)"empty VM free variable");
                    goto python_exception;
                }
                if (!vm_push(&frame, value)) goto bad_bytecode;
            } else {
                value = vm_pop(&frame);
                if (value == (PyDosObj far *)0) goto bad_bytecode;
                pydos_cell_set(left, value);
                PYDOS_DECREF(value);
            }
            break;
        case PBC_OP_YIELD_VALUE:
            if (generator == (PyDosObj far *)0 ||
                (function->flags & (PBC_FUNC_GENERATOR |
                                    PBC_FUNC_COROUTINE)) == 0) {
                status = PYDOS_VM_UNSUPPORTED_OPCODE;
                goto done;
            }
            return_value = vm_pop(&frame);
            if (return_value == (PyDosObj far *)0) goto bad_bytecode;
            generator->v.gen.pc = (int)frame.pc;
            generator->v.gen.vm_suspended = 1;
            yielded = 1;
            goto done;
        default:
            goto bad_bytecode;
        }
        continue;

bad_binary_stack:
        if (left != (PyDosObj far *)0) PYDOS_DECREF(left);
        if (right != (PyDosObj far *)0) PYDOS_DECREF(right);
        goto bad_bytecode;
bad_ternary_stack:
        if (left != (PyDosObj far *)0) PYDOS_DECREF(left);
        if (right != (PyDosObj far *)0) PYDOS_DECREF(right);
        if (value != (PyDosObj far *)0) PYDOS_DECREF(value);
bad_bytecode:
        status = PYDOS_VM_BAD_BYTECODE;
        goto done;
python_exception:
        if (pydos_exc_pending() &&
            vm_dispatch_exception(&frame, fault_offset))
            continue;
        status = PYDOS_VM_PYTHON_EXCEPTION;
        goto done;
    }
    status = PYDOS_VM_BAD_BYTECODE;

done:
    vm_release_frame(&frame);
    vm_result(result, status, function_index, fault_offset, opcode);
    if (generator != (PyDosObj far *)0 && !yielded) {
        generator->v.gen.pc = -1;
        if (status == PYDOS_VM_OK) {
            if (generator->v.gen.state != (PyDosObj far *)0)
                PYDOS_DECREF(generator->v.gen.state);
            generator->v.gen.state = return_value;
            return_value = (PyDosObj far *)0;
        }
        vm_clear_generator_frame(generator);
    }
    if (status != PYDOS_VM_OK && return_value != (PyDosObj far *)0) {
        PYDOS_DECREF(return_value);
        return_value = (PyDosObj far *)0;
    }
    return return_value;
}

static PyDosObj far * PYDOS_API pydos_vm_resume_suspended(
    PyDosObj far *generator)
{
    PyDosCodeRef far *reference;
    PyDosVMResult result;

    if (generator == (PyDosObj far *)0 ||
        generator->v.gen.code_ref == (PyDosCodeRef far *)0 ||
        pydos_code_ref_kind(generator->v.gen.code_ref) != PYDOS_CODE_PBC) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"invalid suspended VM frame");
        return (PyDosObj far *)0;
    }
    reference = generator->v.gen.code_ref;
    return pydos_vm_execute_frame(
        reference->target.pbc.module,
        reference->target.pbc.function_index,
        0, (PyDosObj far * far *)0, &result, generator);
}

PyDosObj far * PYDOS_API pydos_vm_execute(
    const PyDosVMModule far *module, PBCU16 function_index,
    PBCU16 argc, PyDosObj far * far *argv,
    PyDosVMResult far *result)
{
    return pydos_vm_execute_frame(module, function_index, argc, argv,
                                  result, (PyDosObj far *)0);
}

PyDosObj far * PYDOS_API pydos_vm_create_suspended(
    const PyDosCodeRef far *reference, PBCU16 argc,
    PyDosObj far * far *argv, PyDosObj far *closure)
{
    const PyDosVMModule far *module;
    const PyDosVMFunction far *function;
    PyDosObj far *generator;
    PyDosObj far *locals;
    PyDosObj far *stack;
    PBCU16 i;

    if (reference == (const PyDosCodeRef far *)0 ||
        pydos_code_ref_kind(reference) != PYDOS_CODE_PBC) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"invalid suspended code reference");
        return (PyDosObj far *)0;
    }
    module = reference->target.pbc.module;
    if (module == (const PyDosVMModule far *)0 ||
        reference->target.pbc.function_index >= module->function_count) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"invalid suspended PBC function");
        return (PyDosObj far *)0;
    }
    function = &module->functions[reference->target.pbc.function_index];
    if (function->code == (const PBCU8 far *)0 ||
        function->code_size == 0 ||
        function->arg_count > function->local_count ||
        ((function->flags & PBC_FUNC_GENERATOR) != 0 &&
         (function->flags & PBC_FUNC_COROUTINE) != 0) ||
        (function->flags & (PBC_FUNC_GENERATOR | PBC_FUNC_COROUTINE)) == 0 ||
        argc != function->arg_count ||
        (argc != 0 && argv == (PyDosObj far * far *)0) ||
        (closure != (PyDosObj far *)0 &&
         (PyDosType)closure->type != PYDT_TUPLE) ||
        (function->closure_count != 0 &&
         (closure == (PyDosObj far *)0 ||
          closure->v.tuple.len != function->closure_count))) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid suspended PBC frame");
        return (PyDosObj far *)0;
    }
    if ((unsigned long)function->local_count *
            (unsigned long)sizeof(PyDosObj far *) > 0xff00UL ||
        (unsigned long)function->max_stack *
            (unsigned long)sizeof(PyDosObj far *) > 0xff00UL) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"suspended PBC frame is too large");
        return (PyDosObj far *)0;
    }
    if (closure != (PyDosObj far *)0) {
        for (i = 0; i < closure->v.tuple.len; i++) {
            if (closure->v.tuple.items[i] == (PyDosObj far *)0 ||
                (PyDosType)closure->v.tuple.items[i]->type != PYDT_CELL) {
                pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"invalid suspended closure cell");
                return (PyDosObj far *)0;
            }
        }
    }
    for (i = 0; i < argc; i++) {
        if (argv[i] == (PyDosObj far *)0) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"null PBC argument");
            return (PyDosObj far *)0;
        }
    }

    generator = pydos_gen_new(
        (void (far *)(void))pydos_vm_resume_suspended, 0);
    if (generator == (PyDosObj far *)0) return (PyDosObj far *)0;
    if ((function->flags & PBC_FUNC_COROUTINE) != 0)
        generator->type = PYDT_COROUTINE;
    locals = pydos_list_new(function->local_count);
    stack = pydos_list_new(function->max_stack);
    if (locals == (PyDosObj far *)0 || stack == (PyDosObj far *)0) {
        if (locals != (PyDosObj far *)0) PYDOS_DECREF(locals);
        if (stack != (PyDosObj far *)0) PYDOS_DECREF(stack);
        PYDOS_DECREF(generator);
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate suspended frame");
        return (PyDosObj far *)0;
    }
    locals->v.list.len = function->local_count;
    for (i = 0; i < argc; i++) {
        locals->v.list.items[i] = argv[i];
        PYDOS_INCREF(argv[i]);
    }
    generator->v.gen.locals = locals;
    generator->v.gen.vm_stack = stack;
    generator->v.gen.vm_closure = closure;
    if (closure != (PyDosObj far *)0) PYDOS_INCREF(closure);
    generator->v.gen.code_ref = (PyDosCodeRef far *)reference;
    pydos_code_ref_retain(generator->v.gen.code_ref);
    generator->v.gen.pc = 0;
    generator->v.gen.vm_suspended = 0;
    return generator;
}
