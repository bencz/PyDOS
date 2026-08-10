/*
 * piresc.h - PIR escape analysis pass
 *
 * Determines which SSA values escape the current function scope.
 * Values that don't escape can use arena allocation and skip
 * reference counting.
 *
 * Results stored in PIRFunction::escape_info (FuncEscapeResult).
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRESC_H
#define PIRESC_H

#include "pir.h"

/* Run escape analysis on a single function.
 * Allocates and stores FuncEscapeResult in func->escape_info. */
void pir_escape_analyze(PIRFunction *func);

/* Print escape analysis results to file. */
void pir_dump_escape(PIRFunction *func, FILE *out);

#endif /* PIRESC_H */
