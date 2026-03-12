/*
 * pirutil.h - PIR utility functions
 *
 * Helper functions for manipulating PIR instructions and values:
 * instruction removal, use replacement, use counting, classification.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRUTIL_H
#define PIRUTIL_H

#include "pir.h"

/* Remove an instruction from its block */
void pir_inst_remove(PIRBlock *block, PIRInst *inst);

/* Replace all uses of old_val with new_val in the function */
void pir_replace_all_uses(PIRFunction *func, PIRValue old_val, PIRValue new_val);

/* Does val have any uses in the function? */
int pir_value_has_uses(PIRFunction *func, PIRValue val);

/* Count uses of val in the function */
int pir_value_use_count(PIRFunction *func, PIRValue val);

/* Is this opcode a block terminator? */
int pir_is_terminator(PIROp op);

/* Does this instruction have side effects? (calls, stores, raises, branches) */
int pir_has_side_effects(PIROp op);

/* Is this instruction a pure computation (no side effects, safe to remove)? */
int pir_is_pure(PIROp op);

/* Get the block containing a given block ID */
PIRBlock *pir_func_get_block(PIRFunction *func, int block_id);

#endif /* PIRUTIL_H */
