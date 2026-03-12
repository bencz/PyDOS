/*
 * pirlwr.h - PIR Lowerer (PIR -> flat IR)
 *
 * Converts SSA-based PIR back to the flat three-address code IR
 * that the existing IROpt and Codegen pipeline consumes.
 *
 * Key transformations:
 *   - Block linearization (PIR blocks -> IR labels + jumps)
 *   - SSA value -> temp register mapping
 *   - Alloca -> local slot mapping
 *   - PIR opcodes -> flat IR opcodes
 *   - Constant pool creation
 *   - Phi elimination via deferred parallel copies
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRLWR_H
#define PIRLWR_H

#include "pir.h"
#include "ir.h"

/* Deferred phi store: inserted before the terminator of pred_block */
struct PhiPendingStore {
    int pred_block_id;   /* PIR block that should emit this store */
    int phi_slot;        /* local slot allocated for the phi */
    int value_id;        /* PIR value to store (temp number) */
};

class PIRLowerer {
public:
    PIRLowerer();
    ~PIRLowerer();

    IRModule *lower(PIRModule *pir_mod);

    int get_error_count() const;

private:
    IRModule  *ir_mod;
    PIRModule *pir_mod;
    IRFunc    *current_func;
    int        error_count;

    /* Block -> label ID mapping (dynamic) */
    int *block_labels;
    int  block_labels_cap;
    int  next_label;

    /* Alloca value ID -> local slot mapping (dynamic) */
    int *alloca_slots;
    int  alloca_slots_cap;

    /* Param value IDs (to skip initial stores) */
    int param_ids[64];
    int num_param_ids;

    /* Extra temp allocation (continues from PIR's next_value_id) */
    int next_temp;
    int alloc_temp();

    /* Deferred phi stores — emitted before block terminators (dynamic) */
    PhiPendingStore *phi_pending;
    int phi_pending_cap;
    int num_phi_pending;

    /* Grow helpers */
    void ensure_block_labels(int needed);
    void ensure_alloca_slots(int needed);
    void ensure_phi_pending(int needed);

    /* Current PIR block being lowered (for phi store emission) */
    int current_pir_block_id;

    /* Constant pool management */
    int add_const_int(long val);
    int add_const_float(double val);
    int add_const_str(const char *data, int len);

    /* Emit a flat IR instruction into current_func */
    IRInstr *emit(IROp op, int dest, int src1, int src2, int extra);

    /* Lower a single PIR function */
    void lower_function(PIRFunction *pir_func);

    /* Pre-scan: assign labels, alloca slots, and phi stores */
    void prescan_function(PIRFunction *pir_func);

    /* Pre-process phi nodes: allocate slots, record pending stores */
    void prescan_phis(PIRFunction *pir_func);

    /* Emit deferred phi stores for the current block */
    void emit_phi_stores_for_block(int block_id);

    /* Lower a single PIR instruction */
    void lower_inst(PIRInst *inst, PIRFunction *pir_func);

    /* Get the flat IR temp number for a PIR value */
    int val_temp(PIRValue v);

    /* Get the label ID for a PIR block */
    int block_label(PIRBlock *block);

    /* Check if a value ID is a parameter */
    int is_param_id(int id);

    /* Error reporting */
    void report_error(const char *msg);
};

#endif /* PIRLWR_H */
