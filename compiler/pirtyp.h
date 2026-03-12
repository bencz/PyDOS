/*
 * pirtyp.h - PIR type inference pass
 *
 * Forward type propagation through SSA defs. Recovers type info
 * lost after mem2reg (phi nodes) and refines it using dataflow.
 *
 * Results stored in PIRFunction::type_info (FuncTypeResult).
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRTYP_H
#define PIRTYP_H

#include "pir.h"
#include "pirdom.h"
#include "stdscan.h"

/* Run type inference on a single function.
 * Requires dominance info (for RPO walk).
 * stdlib_reg may be NULL (falls back to hardcoded knowledge).
 * Allocates and stores FuncTypeResult in func->type_info. */
void pir_type_infer(PIRFunction *func, DomInfo *dom,
                    StdlibRegistry *stdlib_reg);

/* Print type inference results to file. */
void pir_dump_types(PIRFunction *func, FILE *out);

#endif /* PIRTYP_H */
