/*
 * pdos_vm.h - Portable PBC stack virtual machine
 *
 * The VM executes verified bytecode over the existing PyDosObj model.  A
 * module descriptor contains already materialized constants and symbols;
 * file loading and paging are deliberately separate concerns.
 */

#ifndef PDOS_VM_H
#define PDOS_VM_H

#include "pdos_obj.h"
#include "../common/pdospbc.h"

typedef struct PyDosVMException {
    PBCU16 start_offset;
    PBCU16 end_offset;
    PBCU16 handler_offset;
    PBCU16 stack_depth;
    PBCU16 match_symbol;
    PBCU16 flags;
} PyDosVMException;

typedef struct PyDosVMFunction {
    const PBCU8 far *code;
    PBCU16 code_size;
    PBCU16 arg_count;
    PBCU16 local_count;
    PBCU16 max_stack;
    PBCU16 name_symbol;
    PBCU16 flags;
    PBCU16 closure_count;
    const PyDosVMException far *exceptions;
    PBCU16 exception_count;
} PyDosVMFunction;

typedef struct PyDosVMModule {
    PyDosObj far * far *constants;
    PBCU16 constant_count;
    PyDosObj far * far *symbols;       /* owned elsewhere, all strings */
    PBCU16 symbol_count;
    const PyDosVMFunction far *functions;
    PBCU16 function_count;
    PyDosObj far *globals;             /* NULL selects pydos_globals */
} PyDosVMModule;

typedef enum PyDosVMStatus {
    PYDOS_VM_OK = 0,
    PYDOS_VM_INVALID_ARGUMENT,
    PYDOS_VM_FUNCTION_OUT_OF_RANGE,
    PYDOS_VM_ARGUMENT_COUNT,
    PYDOS_VM_FRAME_TOO_LARGE,
    PYDOS_VM_OUT_OF_MEMORY,
    PYDOS_VM_BAD_BYTECODE,
    PYDOS_VM_UNSUPPORTED_OPCODE,
    PYDOS_VM_PYTHON_EXCEPTION
} PyDosVMStatus;

typedef struct PyDosVMResult {
    PyDosVMStatus status;
    PBCU16 function_index;
    PBCU16 bytecode_offset;
    PBCU8 opcode;
} PyDosVMResult;

const char far * PYDOS_API pydos_vm_status_name(PyDosVMStatus status);

/* Returns a new reference, or NULL.  A NULL result with
 * PYDOS_VM_PYTHON_EXCEPTION leaves the Python exception pending. */
PyDosObj far * PYDOS_API pydos_vm_execute(
    const PyDosVMModule far *module,
    PBCU16 function_index,
    PBCU16 argc,
    PyDosObj far * far *argv,
    PyDosVMResult far *result);

/* Construct a suspended generator/coroutine for a PBC function.  Arguments
 * and closure cells are copied into GC-visible frame containers. */
PyDosObj far * PYDOS_API pydos_vm_create_suspended(
    const struct PyDosCodeRef far *reference,
    PBCU16 argc,
    PyDosObj far * far *argv,
    PyDosObj far *closure);

#endif /* PDOS_VM_H */
