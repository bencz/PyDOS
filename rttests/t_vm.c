/* pdos_vm tests: execute PBC over the real PyDOS object runtime. */

#include "testfw.h"
#include "../runtime/pdos_vm.h"
#include "../runtime/pdos_rt.h"
#include "../runtime/pdos_dic.h"
#include "../runtime/pdos_exc.h"
#include "../runtime/pdos_cod.h"
#include "../runtime/pdos_vtb.h"
#include "../runtime/pdos_gen.h"
#include "../runtime/pdos_asn.h"
#include "../runtime/pdos_gc.h"
#include <string.h>

static PyDosObj far * PYDOS_API vm_test_add(
    int argc, PyDosObj far * far *argv)
{
    if (argc != 2) return (PyDosObj far *)0;
    return pydos_obj_add(argv[0], argv[1]);
}

static void init_module(PyDosVMModule *module,
                        PyDosObj far * far *constants, PBCU16 constant_count,
                        PyDosObj far * far *symbols, PBCU16 symbol_count,
                        const PyDosVMFunction *functions,
                        PBCU16 function_count, PyDosObj far *globals)
{
    module->constants = constants;
    module->constant_count = constant_count;
    module->symbols = symbols;
    module->symbol_count = symbol_count;
    module->functions = functions;
    module->function_count = function_count;
    module->globals = globals;
}

TEST(vm_arithmetic_and_locals)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_LOCAL8, 0, PBC_OP_LOAD_LOCAL8, 1,
        PBC_OP_PY_MUL, PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_PY_ADD, PBC_OP_RETURN_VALUE
    };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *args[2];
    PyDosObj far *value;
    PyDosObj far *native_product;
    PyDosObj far *native_value;

    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.arg_count = 2;
    function.local_count = 2;
    function.max_stack = 2;
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);
    args[0] = pydos_obj_new_int(6);
    args[1] = pydos_obj_new_int(7);
    value = pydos_vm_execute(&module, 0, 2, args, &report);
    native_product = pydos_obj_mul(args[0], args[1]);
    native_value = pydos_obj_add(native_product, args[0]);
    ASSERT_NOT_NULL(value);
    ASSERT_NOT_NULL(native_product);
    ASSERT_NOT_NULL(native_value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 48);
    ASSERT_TRUE(pydos_obj_equal(value, native_value));
    PYDOS_DECREF(value);
    PYDOS_DECREF(native_value);
    PYDOS_DECREF(native_product);
    PYDOS_DECREF(args[0]);
    PYDOS_DECREF(args[1]);
}

TEST(vm_collection_and_iteration)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_LOAD_CONST8, 2,
        PBC_OP_BUILD_LIST8, 3,
        PBC_OP_GET_ITER,
        PBC_OP_LOAD_CONST8, 3,
        PBC_OP_STORE_LOCAL8, 0,
        PBC_OP_FOR_ITER16, 8, 0,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_PY_ADD,
        PBC_OP_STORE_LOCAL8, 0,
        PBC_OP_JUMP16, 245, 255,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[4];
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;
    unsigned int i;

    constants[0] = pydos_obj_new_int(1);
    constants[1] = pydos_obj_new_int(2);
    constants[2] = pydos_obj_new_int(3);
    constants[3] = pydos_obj_new_int(0);
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.local_count = 1;
    function.max_stack = 3;
    init_module(&module, constants, 4, 0, 0, &function, 1, 0);

    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 6);
    PYDOS_DECREF(value);
    for (i = 0; i < 4; i++) PYDOS_DECREF(constants[i]);
}

TEST(vm_build_dictionary)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_BUILD_DICT8, 1,
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_GET_ITEM,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[2];
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;

    constants[0] = pydos_obj_new_str((const char far *)"answer", 6);
    constants[1] = pydos_obj_new_int(42);
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.max_stack = 2;
    init_module(&module, constants, 2, 0, 0, &function, 1, 0);
    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 42);
    PYDOS_DECREF(value);
    PYDOS_DECREF(constants[0]);
    PYDOS_DECREF(constants[1]);
}

TEST(vm_identity_and_membership)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_BUILD_TUPLE8, 2,
        PBC_OP_STORE_LOCAL8, 0,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_CONTAINS,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_IS,
        PBC_OP_BUILD_TUPLE8, 2,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[2];
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;

    constants[0] = pydos_obj_new_int(10);
    constants[1] = pydos_obj_new_int(20);
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.local_count = 1;
    function.max_stack = 3;
    init_module(&module, constants, 2, 0, 0, &function, 1, 0);
    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_EQ(value->type, PYDT_TUPLE);
    ASSERT_TRUE(value->v.tuple.items[0]->v.bool_val);
    ASSERT_TRUE(value->v.tuple.items[1]->v.bool_val);
    PYDOS_DECREF(value);
    PYDOS_DECREF(constants[0]);
    PYDOS_DECREF(constants[1]);
}

TEST(vm_branch_globals_and_native_call)
{
    static const PBCU8 branch_code[] = {
        PBC_OP_LOAD_LOCAL8, 0, PBC_OP_LOAD_CONST8, 0, PBC_OP_CMP_GT,
        PBC_OP_JUMP_IF_FALSE16, 3, 0,
        PBC_OP_LOAD_CONST8, 1, PBC_OP_RETURN_VALUE,
        PBC_OP_LOAD_CONST8, 2, PBC_OP_RETURN_VALUE
    };
    static const PBCU8 call_code[] = {
        PBC_OP_LOAD_GLOBAL16, 1, 0,
        PBC_OP_LOAD_CONST8, 3, PBC_OP_LOAD_CONST8, 4,
        PBC_OP_CALL8, 2, PBC_OP_STORE_GLOBAL16, 0, 0,
        PBC_OP_LOAD_GLOBAL16, 0, 0, PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[5];
    PyDosObj far *symbols[2];
    PyDosVMFunction functions[2];
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *globals;
    PyDosObj far *callable;
    PyDosObj far *arg;
    PyDosObj far *value;
    unsigned int i;

    constants[0] = pydos_obj_new_int(0);
    constants[1] = pydos_obj_new_str((const char far *)"positive", 8);
    constants[2] = pydos_obj_new_str((const char far *)"nonpositive", 11);
    constants[3] = pydos_obj_new_int(20);
    constants[4] = pydos_obj_new_int(22);
    symbols[0] = pydos_obj_new_str((const char far *)"answer", 6);
    symbols[1] = pydos_obj_new_str((const char far *)"adder", 5);
    globals = pydos_dict_new(8);
    callable = pydos_func_new_builtin((void (far *)(void))vm_test_add,
                                      (const char far *)"adder");
    pydos_dict_set(globals, symbols[1], callable);
    _fmemset(functions, 0, sizeof(functions));
    functions[0].code = branch_code;
    functions[0].code_size = sizeof(branch_code);
    functions[0].arg_count = 1;
    functions[0].local_count = 1;
    functions[0].max_stack = 2;
    functions[1].code = call_code;
    functions[1].code_size = sizeof(call_code);
    functions[1].max_stack = 3;
    init_module(&module, constants, 5, symbols, 2, functions, 2, globals);

    arg = pydos_obj_new_int(-1);
    value = pydos_vm_execute(&module, 0, 1, &arg, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->type, PYDT_STR);
    ASSERT_STR_EQ(value->v.str.data, "nonpositive");
    PYDOS_DECREF(value);
    PYDOS_DECREF(arg);
    value = pydos_vm_execute(&module, 1, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_EQ(value->v.int_val, 42);
    PYDOS_DECREF(value);
    value = pydos_dict_get(globals, symbols[0]);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->v.int_val, 42);
    PYDOS_DECREF(value);

    PYDOS_DECREF(callable);
    PYDOS_DECREF(globals);
    for (i = 0; i < 5; i++) PYDOS_DECREF(constants[i]);
    for (i = 0; i < 2; i++) PYDOS_DECREF(symbols[i]);
}

TEST(vm_exception_regions)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0, PBC_OP_LOAD_CONST8, 1,
        PBC_OP_PY_TRUE_DIV, PBC_OP_RETURN_VALUE,
        PBC_OP_POP_TOP, PBC_OP_LOAD_CONST8, 2, PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[3];
    PyDosVMException handler;
    PyDosVMFunction functions[2];
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;
    unsigned int i;

    constants[0] = pydos_obj_new_int(10);
    constants[1] = pydos_obj_new_int(0);
    constants[2] = pydos_obj_new_str((const char far *)"caught", 6);
    handler.start_offset = 0;
    handler.end_offset = 5;
    handler.handler_offset = 6;
    handler.stack_depth = 0;
    handler.match_symbol = 0xffffU;
    handler.flags = 0;
    _fmemset(functions, 0, sizeof(functions));
    functions[0].code = code;
    functions[0].code_size = sizeof(code);
    functions[0].max_stack = 2;
    functions[0].exceptions = &handler;
    functions[0].exception_count = 1;
    functions[1] = functions[0];
    functions[1].exceptions = 0;
    functions[1].exception_count = 0;
    init_module(&module, constants, 3, 0, 0, functions, 2, 0);

    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_STR_EQ(value->v.str.data, "caught");
    ASSERT_FALSE(pydos_exc_pending());
    PYDOS_DECREF(value);
    value = pydos_vm_execute(&module, 1, 0, 0, &report);
    ASSERT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_PYTHON_EXCEPTION);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_ZERO_DIVISION));
    pydos_exc_clear();
    for (i = 0; i < 3; i++) PYDOS_DECREF(constants[i]);
}

TEST(vm_reraise_preserves_the_active_exception)
{
    static const PBCU8 code[] = { PBC_OP_RERAISE };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;

    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);
    pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                    (const char far *)"preserved by reraise");
    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_PYTHON_EXCEPTION);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_VALUE_ERROR));
    pydos_exc_clear();
}

TEST(vm_handler_reraises_its_frame_active_exception)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0, PBC_OP_LOAD_CONST8, 1,
        PBC_OP_PY_TRUE_DIV, PBC_OP_RETURN_VALUE,
        PBC_OP_RERAISE
    };
    PyDosObj far *constants[2];
    PyDosVMException handler;
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;

    constants[0] = pydos_obj_new_int(10);
    constants[1] = pydos_obj_new_int(0);
    handler.start_offset = 0;
    handler.end_offset = 5;
    handler.handler_offset = 6;
    handler.stack_depth = 0;
    handler.match_symbol = 0xffffU;
    handler.flags = 0;
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.max_stack = 2;
    function.exceptions = &handler;
    function.exception_count = 1;
    init_module(&module, constants, 2, 0, 0, &function, 1, 0);

    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_PYTHON_EXCEPTION);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_ZERO_DIVISION));
    pydos_exc_clear();
    PYDOS_DECREF(constants[0]);
    PYDOS_DECREF(constants[1]);
}

TEST(vm_exception_match_uses_the_runtime_hierarchy)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_EXC_MATCH16,
        (PBCU8)(PYDOS_EXC_ARITHMETIC_ERROR & 0xff),
        (PBCU8)((PYDOS_EXC_ARITHMETIC_ERROR >> 8) & 0xff),
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[1];
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;

    pydos_exc_raise(PYDOS_EXC_ZERO_DIVISION,
                    (const char far *)"division by zero");
    constants[0] = pydos_exc_fetch();
    pydos_exc_clear();
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.max_stack = 1;
    init_module(&module, constants, 1, 0, 0, &function, 1, 0);

    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_TRUE(pydos_obj_is_truthy(value));
    PYDOS_DECREF(value);
    PYDOS_DECREF(constants[0]);
}

TEST(vm_function_reference_and_nested_call)
{
    static const PBCU8 caller_code[] = {
        PBC_OP_BUILD_TUPLE8, 0,
        PBC_OP_MAKE_FUNCTION16, 1, 0,
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_CALL8, 2,
        PBC_OP_RETURN_VALUE
    };
    static const PBCU8 add_code[] = {
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_LOAD_LOCAL8, 1,
        PBC_OP_PY_ADD,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[2];
    PyDosObj far *symbols[2];
    PyDosVMFunction functions[2];
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;
    unsigned int i;

    constants[0] = pydos_obj_new_int(19);
    constants[1] = pydos_obj_new_int(23);
    symbols[0] = pydos_obj_new_str((const char far *)"caller", 6);
    symbols[1] = pydos_obj_new_str((const char far *)"add", 3);
    _fmemset(functions, 0, sizeof(functions));
    functions[0].code = caller_code;
    functions[0].code_size = sizeof(caller_code);
    functions[0].max_stack = 3;
    functions[0].name_symbol = 0;
    functions[1].code = add_code;
    functions[1].code_size = sizeof(add_code);
    functions[1].arg_count = 2;
    functions[1].local_count = 2;
    functions[1].max_stack = 2;
    functions[1].name_symbol = 1;
    init_module(&module, constants, 2, symbols, 2, functions, 2, 0);

    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_OK);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 42);
    PYDOS_DECREF(value);
    for (i = 0; i < 2; i++) {
        PYDOS_DECREF(constants[i]);
        PYDOS_DECREF(symbols[i]);
    }
}

TEST(vm_closure_cells_survive_outer_frame)
{
    static const PBCU8 outer_code[] = {
        PBC_OP_MAKE_CELL16, 0, 0,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_BUILD_TUPLE8, 1,
        PBC_OP_MAKE_FUNCTION16, 1, 0,
        PBC_OP_RETURN_VALUE
    };
    static const PBCU8 inner_code[] = {
        PBC_OP_LOAD_DEREF16, 0, 0,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *symbols[2];
    PyDosVMFunction functions[2];
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *argument;
    PyDosObj far *closure_function;
    PyDosObj far *value;
    unsigned int i;

    symbols[0] = pydos_obj_new_str((const char far *)"outer", 5);
    symbols[1] = pydos_obj_new_str((const char far *)"inner", 5);
    _fmemset(functions, 0, sizeof(functions));
    functions[0].code = outer_code;
    functions[0].code_size = sizeof(outer_code);
    functions[0].arg_count = 1;
    functions[0].local_count = 1;
    functions[0].max_stack = 1;
    functions[0].name_symbol = 0;
    functions[1].code = inner_code;
    functions[1].code_size = sizeof(inner_code);
    functions[1].max_stack = 1;
    functions[1].name_symbol = 1;
    functions[1].closure_count = 1;
    init_module(&module, 0, 0, symbols, 2, functions, 2, 0);

    argument = pydos_obj_new_int(77);
    closure_function = pydos_vm_execute(
        &module, 0, 1, &argument, &report);
    ASSERT_NOT_NULL(closure_function);
    ASSERT_EQ(closure_function->type, PYDT_FUNCTION);
    PYDOS_DECREF(argument);
    value = pydos_obj_call(closure_function, 0, 0);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 77);
    PYDOS_DECREF(value);
    PYDOS_DECREF(closure_function);
    for (i = 0; i < 2; i++) PYDOS_DECREF(symbols[i]);
}

TEST(vm_function_supports_more_than_eight_arguments)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_LOCAL8, 9,
        PBC_OP_RETURN_VALUE
    };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosObj far *callable;
    PyDosObj far *arguments[10];
    PyDosObj far *value;
    unsigned int i;

    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.arg_count = 10;
    function.local_count = 10;
    function.max_stack = 1;
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);
    callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"many_arguments");
    ASSERT_NOT_NULL(callable);
    pydos_func_set_arg_count(callable, 10);
    for (i = 0; i < 10; i++)
        arguments[i] = pydos_obj_new_int((long)i);
    value = pydos_obj_call(callable, 10, arguments);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 9);
    PYDOS_DECREF(value);
    for (i = 0; i < 10; i++) PYDOS_DECREF(arguments[i]);
    PYDOS_DECREF(callable);
}

TEST(vm_code_ref_method_binds_and_dispatches)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_LOCAL8, 1,
        PBC_OP_RETURN_VALUE
    };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVTable far *vtable;
    PyDosCodeRef far *reference;
    PyDosObj far *cls;
    PyDosObj far *instance;
    PyDosObj far *method;
    PyDosObj far *argument;
    PyDosObj far *value;

    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.arg_count = 2;
    function.local_count = 2;
    function.max_stack = 1;
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);

    vtable = pydos_vtable_create();
    reference = pydos_code_ref_new_pbc(&module, 0);
    ASSERT_NOT_NULL(vtable);
    ASSERT_NOT_NULL(reference);
    pydos_vtable_add_code_ref_sig(
        vtable, (const char far *)"echo", reference, 2);
    ASSERT_TRUE(pydos_vtable_lookup_code_ref(
                    vtable, vtable->methods[0].name_hash) == reference);
    ASSERT_NULL(pydos_vtable_lookup(
                    vtable, vtable->methods[0].name_hash));
    pydos_code_ref_release(reference);

    cls = pydos_class_new((const char far *)"VMClass", vtable);
    instance = pydos_instance_new(cls);
    method = pydos_obj_get_attr(instance, (const char far *)"echo");
    argument = pydos_obj_new_int(91);
    ASSERT_NOT_NULL(cls);
    ASSERT_NOT_NULL(instance);
    ASSERT_NOT_NULL(method);
    ASSERT_TRUE(method->v.func.bound_self == instance);
    ASSERT_EQ(method->v.func.code_ref->kind, PYDOS_CODE_PBC);
    value = pydos_obj_call(method, 1, &argument);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 91);

    PYDOS_DECREF(value);
    PYDOS_DECREF(argument);
    PYDOS_DECREF(method);
    PYDOS_DECREF(instance);
    PYDOS_DECREF(cls);
    pydos_vtable_destroy(vtable);
}

TEST(vm_generator_suspends_sends_and_returns)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_YIELD_VALUE,
        PBC_OP_STORE_LOCAL8, 0,
        PBC_OP_LOAD_LOCAL8, 0,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[1];
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosObj far *callable;
    PyDosObj far *generator;
    PyDosObj far *sent;
    PyDosObj far *value;
    PyDosObj far *returned;

    constants[0] = pydos_obj_new_int(11);
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.local_count = 1;
    function.max_stack = 1;
    function.flags = PBC_FUNC_GENERATOR;
    init_module(&module, constants, 1, 0, 0, &function, 1, 0);
    callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"generator");
    pydos_func_set_arg_count(callable, 0);
    generator = pydos_obj_call(callable, 0, 0);
    ASSERT_NOT_NULL(generator);
    ASSERT_EQ(generator->type, PYDT_GENERATOR);
    ASSERT_EQ(generator->v.gen.pc, 0);
    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->v.int_val, 11);
    ASSERT_TRUE(generator->v.gen.vm_suspended);
    PYDOS_DECREF(value);

    sent = pydos_obj_new_int(37);
    value = pydos_gen_send(generator, sent);
    ASSERT_NULL(value);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_STOP_ITERATION));
    pydos_exc_clear();
    returned = pydos_gen_get_return_value(generator);
    ASSERT_NOT_NULL(returned);
    ASSERT_EQ(returned->v.int_val, 37);
    ASSERT_EQ(generator->v.gen.pc, -1);
    ASSERT_NULL(generator->v.gen.locals);
    ASSERT_NULL(generator->v.gen.vm_stack);
    ASSERT_NULL(generator->v.gen.code_ref);

    PYDOS_DECREF(returned);
    PYDOS_DECREF(sent);
    PYDOS_DECREF(generator);
    PYDOS_DECREF(callable);
    PYDOS_DECREF(constants[0]);
}

TEST(vm_generator_routes_throw_through_exception_table)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE,
        PBC_OP_POP_TOP,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    PyDosObj far *constants[2];
    PyDosVMException handler;
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosObj far *callable;
    PyDosObj far *generator;
    PyDosObj far *value;

    constants[0] = pydos_obj_new_int(1);
    constants[1] = pydos_obj_new_int(99);
    handler.start_offset = 0;
    handler.end_offset = 3;
    handler.handler_offset = 5;
    handler.stack_depth = 0;
    handler.match_symbol = 0xffffU;
    handler.flags = 0;
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.max_stack = 1;
    function.flags = PBC_FUNC_GENERATOR;
    function.exceptions = &handler;
    function.exception_count = 1;
    init_module(&module, constants, 2, 0, 0, &function, 1, 0);
    callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"throwing_generator");
    pydos_func_set_arg_count(callable, 0);
    generator = pydos_obj_call(callable, 0, 0);
    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->v.int_val, 1);
    PYDOS_DECREF(value);
    value = pydos_gen_throw(
        generator, PYDOS_EXC_VALUE_ERROR,
        (const char far *)"injected");
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->v.int_val, 99);
    ASSERT_FALSE(pydos_exc_pending());
    PYDOS_DECREF(value);
    value = pydos_gen_next(generator);
    ASSERT_NULL(value);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_STOP_ITERATION));
    pydos_exc_clear();

    PYDOS_DECREF(generator);
    PYDOS_DECREF(callable);
    PYDOS_DECREF(constants[0]);
    PYDOS_DECREF(constants[1]);
}

TEST(vm_coroutine_uses_same_persistent_frame)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_RETURN_VALUE
    };
    PyDosObj far *constants[2];
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosObj far *callable;
    PyDosObj far *coroutine;
    PyDosObj far *args[1];
    PyDosObj far *value;

    constants[0] = pydos_obj_new_none();
    constants[1] = pydos_obj_new_int(123);
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.max_stack = 1;
    function.flags = PBC_FUNC_COROUTINE;
    init_module(&module, constants, 2, 0, 0, &function, 1, 0);
    callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"coroutine");
    pydos_func_set_arg_count(callable, 0);
    coroutine = pydos_obj_call(callable, 0, 0);
    ASSERT_NOT_NULL(coroutine);
    ASSERT_EQ(coroutine->type, PYDT_COROUTINE);
    args[0] = coroutine;
    value = pydos_async_run(1, args);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->type, PYDT_INT);
    ASSERT_EQ(value->v.int_val, 123);
    ASSERT_EQ(coroutine->v.gen.pc, -1);

    PYDOS_DECREF(value);
    PYDOS_DECREF(coroutine);
    PYDOS_DECREF(callable);
    PYDOS_DECREF(constants[0]);
    PYDOS_DECREF(constants[1]);
}

TEST(vm_generator_throw_and_close_terminal_paths)
{
    static const PBCU8 plain_code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    static const PBCU8 close_yield_code[] = {
        PBC_OP_LOAD_CONST8, 0,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE,
        PBC_OP_POP_TOP,
        PBC_OP_LOAD_CONST8, 1,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    PyDosObj far *constants[2];
    PyDosVMException handler;
    PyDosVMFunction functions[2];
    PyDosVMModule module;
    PyDosObj far *plain_callable;
    PyDosObj far *close_callable;
    PyDosObj far *generator;
    PyDosObj far *value;

    constants[0] = pydos_obj_new_int(1);
    constants[1] = pydos_obj_new_int(2);
    handler.start_offset = 0;
    handler.end_offset = 3;
    handler.handler_offset = 5;
    handler.stack_depth = 0;
    handler.match_symbol = 0xffffU;
    handler.flags = 0;
    _fmemset(functions, 0, sizeof(functions));
    functions[0].code = plain_code;
    functions[0].code_size = sizeof(plain_code);
    functions[0].max_stack = 1;
    functions[0].flags = PBC_FUNC_GENERATOR;
    functions[1].code = close_yield_code;
    functions[1].code_size = sizeof(close_yield_code);
    functions[1].max_stack = 1;
    functions[1].flags = PBC_FUNC_GENERATOR;
    functions[1].exceptions = &handler;
    functions[1].exception_count = 1;
    init_module(&module, constants, 2, 0, 0, functions, 2, 0);
    plain_callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"plain");
    close_callable = pydos_func_new_pbc(
        &module, 1, (const char far *)"close_yield");
    pydos_func_set_arg_count(plain_callable, 0);
    pydos_func_set_arg_count(close_callable, 0);

    generator = pydos_obj_call(plain_callable, 0, 0);
    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    PYDOS_DECREF(value);
    value = pydos_gen_throw(
        generator, PYDOS_EXC_VALUE_ERROR,
        (const char far *)"unhandled");
    ASSERT_NULL(value);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_VALUE_ERROR));
    ASSERT_EQ(generator->v.gen.pc, -1);
    pydos_exc_clear();
    PYDOS_DECREF(generator);

    generator = pydos_obj_call(plain_callable, 0, 0);
    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    PYDOS_DECREF(value);
    pydos_gen_close(generator);
    ASSERT_FALSE(pydos_exc_pending());
    ASSERT_EQ(generator->v.gen.pc, -1);
    PYDOS_DECREF(generator);

    generator = pydos_obj_call(close_callable, 0, 0);
    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    PYDOS_DECREF(value);
    pydos_gen_close(generator);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_RUNTIME_ERROR));
    ASSERT_EQ(generator->v.gen.pc, -1);
    pydos_exc_clear();
    PYDOS_DECREF(generator);

    PYDOS_DECREF(plain_callable);
    PYDOS_DECREF(close_callable);
    PYDOS_DECREF(constants[0]);
    PYDOS_DECREF(constants[1]);
}

TEST(vm_generator_keeps_closure_across_suspension)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_DEREF16, 0, 0,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_LOAD_DEREF16, 0, 0,
        PBC_OP_RETURN_VALUE
    };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosObj far *cell;
    PyDosObj far *captured;
    PyDosObj far *closure;
    PyDosObj far *none;
    PyDosObj far *callable;
    PyDosObj far *generator;
    PyDosObj far *value;
    PyDosObj far *returned;

    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.max_stack = 1;
    function.closure_count = 1;
    function.flags = PBC_FUNC_GENERATOR;
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);
    captured = pydos_obj_new_int(64);
    cell = pydos_cell_new();
    pydos_cell_set(cell, captured);
    closure = pydos_list_new(1);
    none = pydos_list_append(closure, cell);
    PYDOS_DECREF(none);
    pydos_list_to_tuple(closure);
    callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"closure_generator");
    pydos_func_set_closure(callable, closure);
    pydos_func_set_arg_count(callable, 0);
    generator = pydos_obj_call(callable, 0, 0);
    PYDOS_DECREF(callable);
    PYDOS_DECREF(closure);
    PYDOS_DECREF(cell);
    PYDOS_DECREF(captured);

    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    ASSERT_EQ(value->v.int_val, 64);
    PYDOS_DECREF(value);
    value = pydos_gen_next(generator);
    ASSERT_NULL(value);
    ASSERT_TRUE(pydos_exc_matches(pydos_exc_current(),
                                  PYDOS_EXC_STOP_ITERATION));
    pydos_exc_clear();
    returned = pydos_gen_get_return_value(generator);
    ASSERT_NOT_NULL(returned);
    ASSERT_EQ(returned->v.int_val, 64);
    PYDOS_DECREF(returned);
    PYDOS_DECREF(generator);
}

TEST(vm_generator_frame_releases_cycles_for_gc)
{
    static const PBCU8 code[] = {
        PBC_OP_LOAD_NONE,
        PBC_OP_YIELD_VALUE,
        PBC_OP_POP_TOP,
        PBC_OP_RETURN_NONE
    };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosObj far *callable;
    PyDosObj far *generator;
    PyDosObj far *cycle;
    PyDosObj far *none;
    PyDosObj far *value;
    unsigned int collected;

    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.arg_count = 1;
    function.local_count = 1;
    function.max_stack = 1;
    function.flags = PBC_FUNC_GENERATOR;
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);
    callable = pydos_func_new_pbc(
        &module, 0, (const char far *)"cycle_generator");
    pydos_func_set_arg_count(callable, 1);
    cycle = pydos_list_new(1);
    none = pydos_list_append(cycle, cycle);
    PYDOS_DECREF(none);
    generator = pydos_obj_call(callable, 1, &cycle);
    PYDOS_DECREF(cycle);
    value = pydos_gen_next(generator);
    ASSERT_NOT_NULL(value);
    PYDOS_DECREF(value);
    pydos_gen_close(generator);
    ASSERT_FALSE(pydos_exc_pending());
    PYDOS_DECREF(generator);
    PYDOS_DECREF(callable);
    collected = pydos_gc_collect();
    ASSERT_TRUE(collected >= 1U);
}

TEST(vm_rejects_invalid_frames)
{
    static const PBCU8 code[] = { PBC_OP_RETURN_NONE };
    PyDosVMFunction function;
    PyDosVMModule module;
    PyDosVMResult report;
    PyDosObj far *value;
    _fmemset(&function, 0, sizeof(function));
    function.code = code;
    function.code_size = sizeof(code);
    function.arg_count = 1;
    function.local_count = 1;
    init_module(&module, 0, 0, 0, 0, &function, 1, 0);
    value = pydos_vm_execute(&module, 0, 0, 0, &report);
    ASSERT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_ARGUMENT_COUNT);
    value = pydos_vm_execute(&module, 2, 0, 0, &report);
    ASSERT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_FUNCTION_OUT_OF_RANGE);

    module.constant_count = 1;
    module.constants = 0;
    value = pydos_vm_execute(&module, 0, 1, &value, &report);
    ASSERT_NULL(value);
    ASSERT_EQ(report.status, PYDOS_VM_INVALID_ARGUMENT);
}

void run_vm_tests(void)
{
    SUITE("pydos_vm");
    RUN(vm_arithmetic_and_locals);
    RUN(vm_collection_and_iteration);
    RUN(vm_build_dictionary);
    RUN(vm_identity_and_membership);
    RUN(vm_branch_globals_and_native_call);
    RUN(vm_exception_regions);
    RUN(vm_reraise_preserves_the_active_exception);
    RUN(vm_handler_reraises_its_frame_active_exception);
    RUN(vm_exception_match_uses_the_runtime_hierarchy);
    RUN(vm_function_reference_and_nested_call);
    RUN(vm_closure_cells_survive_outer_frame);
    RUN(vm_function_supports_more_than_eight_arguments);
    RUN(vm_code_ref_method_binds_and_dispatches);
    RUN(vm_generator_suspends_sends_and_returns);
    RUN(vm_generator_routes_throw_through_exception_table);
    RUN(vm_coroutine_uses_same_persistent_frame);
    RUN(vm_generator_throw_and_close_terminal_paths);
    RUN(vm_generator_keeps_closure_across_suspension);
    RUN(vm_generator_frame_releases_cycles_for_gc);
    RUN(vm_rejects_invalid_frames);
}
