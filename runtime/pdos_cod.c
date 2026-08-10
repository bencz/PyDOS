/* pdos_cod.c - Stable executable-code references and call gateway. */

#include "pdos_cod.h"
#include "pdos_vm.h"
#include "pdos_mem.h"
#include "pdos_exc.h"
#include "pdos_rt.h"
#include <limits.h>

PyDosCodeRef far * PYDOS_API pydos_code_ref_new_native(
    void (far *entry)(void), PyDosCodeKind kind)
{
    PyDosCodeRef far *reference;
    if (kind != PYDOS_CODE_NATIVE && kind != PYDOS_CODE_BUILTIN) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"invalid native code kind");
        return (PyDosCodeRef far *)0;
    }
    reference = (PyDosCodeRef far *)pydos_mem_alloc(
        PYDOS_MEM_METADATA, (unsigned long)sizeof(PyDosCodeRef));
    if (reference == (PyDosCodeRef far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate code reference");
        return (PyDosCodeRef far *)0;
    }
    reference->refcount = 1;
    reference->kind = (unsigned char)kind;
    reference->flags = 0;
    reference->target.native = entry;
    return reference;
}

PyDosCodeRef far * PYDOS_API pydos_code_ref_new_pbc(
    const struct PyDosVMModule far *module, PBCU16 function_index)
{
    PyDosCodeRef far *reference;
    if (module == (const struct PyDosVMModule far *)0 ||
        function_index >= module->function_count) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"invalid PBC code reference");
        return (PyDosCodeRef far *)0;
    }
    reference = (PyDosCodeRef far *)pydos_mem_alloc(
        PYDOS_MEM_METADATA, (unsigned long)sizeof(PyDosCodeRef));
    if (reference == (PyDosCodeRef far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate code reference");
        return (PyDosCodeRef far *)0;
    }
    reference->refcount = 1;
    reference->kind = PYDOS_CODE_PBC;
    reference->flags = 0;
    reference->target.pbc.module = module;
    reference->target.pbc.function_index = function_index;
    return reference;
}

void PYDOS_API pydos_code_ref_retain(PyDosCodeRef far *reference)
{
    if (reference != (PyDosCodeRef far *)0 &&
        reference->refcount < REFCOUNT_MAX)
        reference->refcount++;
}

void PYDOS_API pydos_code_ref_release(PyDosCodeRef far *reference)
{
    if (reference == (PyDosCodeRef far *)0) return;
    if (reference->refcount != 0) reference->refcount--;
    if (reference->refcount == 0) pydos_far_free(reference);
}

PyDosCodeKind PYDOS_API pydos_code_ref_kind(
    const PyDosCodeRef far *reference)
{
    if (reference == (const PyDosCodeRef far *)0)
        return (PyDosCodeKind)0;
    return (PyDosCodeKind)reference->kind;
}

void (far * PYDOS_API pydos_code_ref_native_entry(
    const PyDosCodeRef far *reference))(void)
{
    if (reference == (const PyDosCodeRef far *)0 ||
        (reference->kind != PYDOS_CODE_NATIVE &&
         reference->kind != PYDOS_CODE_BUILTIN))
        return (void (far *)(void))0;
    return reference->target.native;
}

static PyDosObj far *call_fixed_native(void (far *entry)(void),
                                       unsigned int argc,
                                       PyDosObj far * far *argv)
{
    typedef PyDosObj far * (PYDOS_API far *NativeFn)(
        PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *,
        PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
    PyDosObj far *args[8];
    PyDosObj far *none_value;
    unsigned int i;
    if (entry == (void (far *)(void))0 || argc > 8U) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid native function call");
        return (PyDosObj far *)0;
    }
    none_value = pydos_obj_new_none();
    for (i = 0; i < 8U; i++)
        args[i] = i < argc ? argv[i] : none_value;
    return ((NativeFn)entry)(args[0], args[1], args[2], args[3],
                             args[4], args[5], args[6], args[7]);
}

/* A Python-callable code reference has one result convention.  A successful
 * call returns an owned object and leaves no exception pending.  A failed
 * call returns null and leaves an exception pending.  Keeping this invariant
 * at the code-reference boundary also protects callers from legacy builtin
 * implementations while those implementations are migrated. */
static PyDosObj far *normalize_call_result(PyDosObj far *result)
{
    if (result != (PyDosObj far *)0) {
        if (!pydos_exc_pending()) return result;
        PYDOS_DECREF(result);
        return (PyDosObj far *)0;
    }
    if (!pydos_exc_pending())
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"call returned null without exception");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_code_ref_call(
    const PyDosCodeRef far *reference, unsigned int argc,
    PyDosObj far * far *argv)
{
    if (reference == (const PyDosCodeRef far *)0 ||
        (argc != 0 && argv == (PyDosObj far * far *)0)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid function reference");
        return (PyDosObj far *)0;
    }
    if (reference->kind == PYDOS_CODE_BUILTIN) {
        typedef PyDosObj far * (PYDOS_API far *BuiltinFn)(
            int, PyDosObj far * far *);
        if (reference->target.native == (void (far *)(void))0) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"invalid builtin function");
            return (PyDosObj far *)0;
        }
        return normalize_call_result(
            ((BuiltinFn)reference->target.native)((int)argc, argv));
    }
    if (reference->kind == PYDOS_CODE_NATIVE)
        return normalize_call_result(
            call_fixed_native(reference->target.native, argc, argv));
    if (reference->kind == PYDOS_CODE_PBC) {
        const PyDosVMFunction far *function;
        PyDosVMResult result;
        PyDosObj far *value;
#if UINT_MAX > 65535U
        if (argc > 65535U) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"too many PBC arguments");
            return (PyDosObj far *)0;
        }
#endif
        function = &reference->target.pbc.module->functions[
            reference->target.pbc.function_index];
        if ((function->flags & (PBC_FUNC_GENERATOR |
                                PBC_FUNC_COROUTINE)) != 0)
            return normalize_call_result(pydos_vm_create_suspended(
                reference, (PBCU16)argc, argv, pydos_active_closure));
        value = pydos_vm_execute(
            reference->target.pbc.module,
            reference->target.pbc.function_index,
            (PBCU16)argc, argv, &result);
        if (value == (PyDosObj far *)0 && !pydos_exc_pending())
            pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                            pydos_vm_status_name(result.status));
        return normalize_call_result(value);
    }
    pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                    (const char far *)"unknown function code kind");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_func_apply_defaults(
    PyDosObj far *function, PyDosObj far *defaults)
{
    if (function == (PyDosObj far *)0 ||
        (PyDosType)function->type != PYDT_FUNCTION ||
        (defaults != (PyDosObj far *)0 &&
         (PyDosType)defaults->type != PYDT_TUPLE)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid function defaults");
        return (PyDosObj far *)0;
    }
    pydos_func_set_defaults(function, defaults);
    return pydos_obj_new_none();
}
