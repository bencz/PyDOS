/*
 * pirmrg.h - PIR Stdlib Merge
 *
 * Merges pre-compiled stdlib PIR functions into a user's PIRModule.
 * Called between PIRBuilder and PIROptimizer in the pipeline.
 *
 * Algorithm: scan all PIR_CALL instructions for Python-backed stdlib
 * functions, deserialize their PIR from the StdlibRegistry, and add
 * them to the module's function list. Transitive closure ensures
 * that stdlib functions calling other stdlib functions are also merged.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRMRG_H
#define PIRMRG_H

#include "pir.h"
#include "stdscan.h"

/* Merge all required Python-backed stdlib functions into the PIR module.
 * Scans PIR_CALL instructions, finds Python-backed callees in the
 * registry, deep-copies their PIR, and adds to pir_mod->functions.
 * Returns number of functions merged. */
int pir_merge_stdlib(PIRModule *pir_mod, StdlibRegistry *reg);

#endif /* PIRMRG_H */
