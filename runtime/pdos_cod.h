/*
 * pdos_cod.h - Stable executable-code references
 *
 * A Python function owns a reference to this descriptor instead of assuming
 * that every implementation is a permanently resident C function pointer.
 */

#ifndef PDOS_COD_H
#define PDOS_COD_H

#include "pdos_obj.h"
#include "../common/pdospbc.h"

struct PyDosVMModule;

typedef enum PyDosCodeKind {
    PYDOS_CODE_NATIVE = 1,
    PYDOS_CODE_BUILTIN = 2,
    PYDOS_CODE_PBC = 3
} PyDosCodeKind;

typedef struct PyDosCodeRef {
    unsigned int refcount;
    unsigned char kind;
    unsigned char flags;
    union {
        void (far *native)(void);
        struct {
            const struct PyDosVMModule far *module;
            PBCU16 function_index;
        } pbc;
    } target;
} PyDosCodeRef;

PyDosCodeRef far * PYDOS_API pydos_code_ref_new_native(
    void (far *entry)(void), PyDosCodeKind kind);
PyDosCodeRef far * PYDOS_API pydos_code_ref_new_pbc(
    const struct PyDosVMModule far *module, PBCU16 function_index);
void PYDOS_API pydos_code_ref_retain(PyDosCodeRef far *reference);
void PYDOS_API pydos_code_ref_release(PyDosCodeRef far *reference);
PyDosCodeKind PYDOS_API pydos_code_ref_kind(
    const PyDosCodeRef far *reference);
void (far * PYDOS_API pydos_code_ref_native_entry(
    const PyDosCodeRef far *reference))(void);
PyDosObj far * PYDOS_API pydos_code_ref_call(
    const PyDosCodeRef far *reference, unsigned int argc,
    PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_func_apply_defaults(
    PyDosObj far *function, PyDosObj far *defaults);

#endif /* PDOS_COD_H */
