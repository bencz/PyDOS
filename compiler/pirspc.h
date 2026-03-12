/*
 * pirspc.h - PIR type-guided specialization pass
 *
 * Replaces boxed Python operations with unboxed typed operations
 * when type inference proves operand types. For example:
 *   PIR_PY_ADD (int, int) -> PIR_UNBOX_INT + PIR_ADD_I32 + PIR_BOX_INT
 *
 * Requires FuncTypeResult from pirtyp.cpp (Phase 1).
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRSPC_H
#define PIRSPC_H

#include "pir.h"

/* Run specialization on a single function.
 * Requires func->type_info to be populated (by pir_type_infer).
 * Returns number of instructions specialized. */
int pir_specialize(PIRFunction *func);

/* Run devirtualization on a single function.
 * Replaces PIR_CALL_METHOD with direct PIR_CALL when the object's
 * class type is known and the method exists in the vtable.
 * Returns number of calls devirtualized. */
int pir_devirtualize(PIRFunction *func, PIRModule *mod);

#endif /* PIRSPC_H */
