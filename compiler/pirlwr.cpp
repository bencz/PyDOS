/*
 * pirlwr.cpp - PIR Lowerer implementation (PIR -> flat IR)
 *
 * Converts SSA-based PIR back into the flat three-address code IR
 * consumed by IROpt and Codegen (cg8086/cg386).
 *
 * Key transformations:
 *   1. Block linearization: PIR blocks -> IR_LABEL + IR_JUMP sequences
 *   2. SSA value -> temp mapping (PIR value IDs reused as temps)
 *   3. Alloca -> local slot (PIR_ALLOCA -> IRFunc local slots)
 *   4. PIR opcodes -> flat IR opcodes (mostly 1:1)
 *   5. Constant pool creation (PIR inline values -> IRConst pool entries)
 *   6. Initial param->alloca stores skipped (params already in slots)
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirlwr.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

/* --------------------------------------------------------------- */
/* Utility                                                           */
/* --------------------------------------------------------------- */
static char *lower_str_dup(const char *s)
{
    int len;
    char *d;
    if (!s) return 0;
    len = (int)strlen(s);
    d = (char *)malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* --------------------------------------------------------------- */
/* Constructor / Destructor                                          */
/* --------------------------------------------------------------- */
PIRLowerer::PIRLowerer()
    : ir_mod(0), pir_mod(0), current_func(0),
      error_count(0), block_labels(0), block_labels_cap(0),
      next_label(0), alloca_slots(0), alloca_slots_cap(0),
      num_param_ids(0), next_temp(0),
      phi_pending(0), phi_pending_cap(0),
      num_phi_pending(0), current_pir_block_id(-1)
{
    block_labels_cap = 128;
    block_labels = (int *)malloc(sizeof(int) * block_labels_cap);
    if (block_labels) memset(block_labels, 0, sizeof(int) * block_labels_cap);

    alloca_slots_cap = 128;
    alloca_slots = (int *)malloc(sizeof(int) * alloca_slots_cap);
    if (alloca_slots) memset(alloca_slots, -1, sizeof(int) * alloca_slots_cap);

    phi_pending_cap = 128;
    phi_pending = (PhiPendingStore *)malloc(sizeof(PhiPendingStore) * phi_pending_cap);
    if (phi_pending) memset(phi_pending, 0, sizeof(PhiPendingStore) * phi_pending_cap);

    memset(param_ids, -1, sizeof(param_ids));
}

PIRLowerer::~PIRLowerer()
{
    if (block_labels) free(block_labels);
    if (alloca_slots) free(alloca_slots);
    if (phi_pending) free(phi_pending);
}

void PIRLowerer::ensure_block_labels(int needed)
{
    if (needed <= block_labels_cap) return;
    int new_cap = block_labels_cap * 2;
    while (new_cap < needed) new_cap *= 2;
    int *np = (int *)malloc(sizeof(int) * new_cap);
    if (!np) { fprintf(stderr, "pirlwr: out of memory (block_labels)\n"); exit(1); }
    memset(np, 0, sizeof(int) * new_cap);
    if (block_labels) {
        memcpy(np, block_labels, sizeof(int) * block_labels_cap);
        free(block_labels);
    }
    block_labels = np;
    block_labels_cap = new_cap;
}

void PIRLowerer::ensure_alloca_slots(int needed)
{
    if (needed <= alloca_slots_cap) return;
    int new_cap = alloca_slots_cap * 2;
    while (new_cap < needed) new_cap *= 2;
    int *np = (int *)malloc(sizeof(int) * new_cap);
    if (!np) { fprintf(stderr, "pirlwr: out of memory (alloca_slots)\n"); exit(1); }
    memset(np, -1, sizeof(int) * new_cap);
    if (alloca_slots) {
        memcpy(np, alloca_slots, sizeof(int) * alloca_slots_cap);
        free(alloca_slots);
    }
    alloca_slots = np;
    alloca_slots_cap = new_cap;
}

void PIRLowerer::ensure_phi_pending(int needed)
{
    if (needed <= phi_pending_cap) return;
    int new_cap = phi_pending_cap * 2;
    while (new_cap < needed) new_cap *= 2;
    PhiPendingStore *np = (PhiPendingStore *)malloc(sizeof(PhiPendingStore) * new_cap);
    if (!np) { fprintf(stderr, "pirlwr: out of memory (phi_pending)\n"); exit(1); }
    memset(np, 0, sizeof(PhiPendingStore) * new_cap);
    if (phi_pending) {
        memcpy(np, phi_pending, sizeof(PhiPendingStore) * num_phi_pending);
        free(phi_pending);
    }
    phi_pending = np;
    phi_pending_cap = new_cap;
}

int PIRLowerer::get_error_count() const
{
    return error_count;
}

void PIRLowerer::report_error(const char *msg)
{
    fprintf(stderr, "PIR lowerer error: %s\n", msg);
    error_count++;
}

/* --------------------------------------------------------------- */
/* Temp / label accessors                                            */
/* --------------------------------------------------------------- */
int PIRLowerer::val_temp(PIRValue v)
{
    if (!pir_value_valid(v)) return -1;
    return v.id;
}

int PIRLowerer::block_label(PIRBlock *block)
{
    if (!block) return -1;
    if (block->id < 0 || block->id >= block_labels_cap) return -1;
    return block_labels[block->id];
}

int PIRLowerer::alloc_temp()
{
    return next_temp++;
}

int PIRLowerer::is_param_id(int id)
{
    int i;
    for (i = 0; i < num_param_ids; i++) {
        if (param_ids[i] == id) return 1;
    }
    return 0;
}

/* --------------------------------------------------------------- */
/* Constant pool management                                          */
/* --------------------------------------------------------------- */
static void grow_constants(IRModule *mod)
{
    int new_max;
    IRConst *new_arr;
    if (mod->max_constants == 0) {
        new_max = 64;
    } else {
        new_max = mod->max_constants * 2;
    }
    new_arr = (IRConst *)malloc(new_max * sizeof(IRConst));
    if (!new_arr) {
        fprintf(stderr, "pir_lower: out of memory for constants\n");
        exit(1);
    }
    if (mod->constants) {
        memcpy(new_arr, mod->constants, mod->num_constants * sizeof(IRConst));
        free(mod->constants);
    }
    mod->constants = new_arr;
    mod->max_constants = new_max;
}

int PIRLowerer::add_const_int(long val)
{
    int i;
    /* Check for existing */
    for (i = 0; i < ir_mod->num_constants; i++) {
        if (ir_mod->constants[i].kind == IRConst::CONST_INT &&
            ir_mod->constants[i].int_val == val) {
            return i;
        }
    }
    if (ir_mod->num_constants >= ir_mod->max_constants) {
        grow_constants(ir_mod);
    }
    i = ir_mod->num_constants++;
    ir_mod->constants[i].kind = IRConst::CONST_INT;
    ir_mod->constants[i].int_val = val;
    return i;
}

int PIRLowerer::add_const_float(double val)
{
    if (ir_mod->num_constants >= ir_mod->max_constants) {
        grow_constants(ir_mod);
    }
    int i = ir_mod->num_constants++;
    ir_mod->constants[i].kind = IRConst::CONST_FLOAT;
    ir_mod->constants[i].float_val = val;
    return i;
}

int PIRLowerer::add_const_str(const char *data, int len)
{
    int i;
    /* Check for existing */
    for (i = 0; i < ir_mod->num_constants; i++) {
        if (ir_mod->constants[i].kind == IRConst::CONST_STR &&
            ir_mod->constants[i].str_val.data &&
            strcmp(ir_mod->constants[i].str_val.data, data) == 0) {
            return i;
        }
    }
    if (ir_mod->num_constants >= ir_mod->max_constants) {
        grow_constants(ir_mod);
    }
    i = ir_mod->num_constants++;
    ir_mod->constants[i].kind = IRConst::CONST_STR;
    ir_mod->constants[i].str_val.data = lower_str_dup(data);
    ir_mod->constants[i].str_val.len = len;
    return i;
}

/* --------------------------------------------------------------- */
/* Emit a flat IR instruction                                        */
/* --------------------------------------------------------------- */
IRInstr *PIRLowerer::emit(IROp op, int dest, int src1, int src2, int extra)
{
    IRInstr *instr = (IRInstr *)malloc(sizeof(IRInstr));
    if (!instr) {
        fprintf(stderr, "pir_lower: out of memory for instruction\n");
        exit(1);
    }
    memset(instr, 0, sizeof(IRInstr));
    instr->op = op;
    instr->dest = dest;
    instr->src1 = src1;
    instr->src2 = src2;
    instr->extra = extra;
    instr->type_hint = 0;
    instr->next = 0;
    instr->prev = current_func->last;

    if (current_func->last) {
        current_func->last->next = instr;
    } else {
        current_func->first = instr;
    }
    current_func->last = instr;

    return instr;
}

/* --------------------------------------------------------------- */
/* Pre-scan: assign labels to blocks, alloca slots to locals         */
/* --------------------------------------------------------------- */
void PIRLowerer::prescan_function(PIRFunction *pir_func)
{
    int bi;
    int slot_counter = 0;
    PIRInst *inst;

    /* Reset state */
    memset(block_labels, 0, sizeof(int) * block_labels_cap);
    memset(alloca_slots, -1, sizeof(int) * alloca_slots_cap);
    num_param_ids = 0;
    memset(param_ids, -1, sizeof(param_ids));
    next_temp = pir_func->next_value_id;

    /* Assign label IDs to all blocks */
    for (bi = 0; bi < pir_func->blocks.size(); bi++) {
        PIRBlock *block = pir_func->blocks[bi];
        if (block->id >= 0) {
            ensure_block_labels(block->id + 1);
            block_labels[block->id] = next_label++;
        }
    }

    /* Record param value IDs */
    {
        int pi;
        for (pi = 0; pi < pir_func->params.size() && pi < 64; pi++) {
            param_ids[pi] = pir_func->params[pi].id;
            num_param_ids++;
        }
    }

    /* Scan all blocks for allocas — assign local slots.
     * Param allocas should come first (they're created first in entry block),
     * giving them slots 0..num_params-1. */
    for (bi = 0; bi < pir_func->blocks.size(); bi++) {
        PIRBlock *block = pir_func->blocks[bi];
        for (inst = block->first; inst; inst = inst->next) {
            if (inst->op == PIR_ALLOCA) {
                int vid = inst->result.id;
                if (vid >= 0) {
                    ensure_alloca_slots(vid + 1);
                    alloca_slots[vid] = slot_counter++;

                    /* Register in IRFunc locals */
                    if (current_func->locals_count < 256) {
                        int li = current_func->locals_count++;
                        current_func->locals[li].name = inst->str_val;
                        current_func->locals[li].type = 0;
                        current_func->locals[li].slot = alloca_slots[vid];
                    }
                }
            }
        }
    }

    current_func->num_locals = slot_counter;
}

/* --------------------------------------------------------------- */
/* Pre-process phi nodes: allocate slots, record pending stores      */
/* --------------------------------------------------------------- */
void PIRLowerer::prescan_phis(PIRFunction *pir_func)
{
    int bi;
    PIRInst *inst;

    num_phi_pending = 0;

    for (bi = 0; bi < pir_func->blocks.size(); bi++) {
        PIRBlock *block = pir_func->blocks[bi];
        for (inst = block->first; inst; inst = inst->next) {
            if (inst->op != PIR_PHI) continue;

            /* Allocate a local slot for this phi */
            int phi_slot = current_func->num_locals++;
            if (current_func->locals_count < 256) {
                int li = current_func->locals_count++;
                current_func->locals[li].name = "__phi";
                current_func->locals[li].type = 0;
                current_func->locals[li].slot = phi_slot;
            }

            /* Store the phi_slot in int_val so lower_inst can find it */
            inst->int_val = phi_slot;

            /* Record a pending store for each predecessor */
            int pi;
            for (pi = 0; pi < inst->extra.phi.count; pi++) {
                PIRValue val = inst->extra.phi.entries[pi].value;
                PIRBlock *pred = inst->extra.phi.entries[pi].block;
                if (!pir_value_valid(val) || !pred) continue;

                ensure_phi_pending(num_phi_pending + 1);
                phi_pending[num_phi_pending].pred_block_id = pred->id;
                phi_pending[num_phi_pending].phi_slot = phi_slot;
                phi_pending[num_phi_pending].value_id = val.id;
                num_phi_pending++;
            }
        }
    }
}

/* --------------------------------------------------------------- */
/* Emit deferred phi stores for the current block                    */
/* Called just before emitting the block's terminator.               */
/* --------------------------------------------------------------- */
void PIRLowerer::emit_phi_stores_for_block(int block_id)
{
    int i;
    for (i = 0; i < num_phi_pending; i++) {
        if (phi_pending[i].pred_block_id == block_id) {
            emit(IR_STORE_LOCAL, phi_pending[i].phi_slot,
                 phi_pending[i].value_id, -1, 0);
        }
    }
}

/* --------------------------------------------------------------- */
/* Lower a single PIR instruction                                    */
/* --------------------------------------------------------------- */
void PIRLowerer::lower_inst(PIRInst *inst, PIRFunction * /*pir_func*/)
{
    int dest;
    int ci;

    dest = val_temp(inst->result);

    switch (inst->op) {
    /* ----- Constants ----- */
    case PIR_CONST_INT:
        ci = add_const_int(inst->int_val);
        emit(IR_CONST_INT, dest, ci, -1, 0);
        break;

    case PIR_CONST_FLOAT:
        {
            double fval = 0.0;
            if (inst->str_val) fval = atof(inst->str_val);
            ci = add_const_float(fval);
            emit(IR_CONST_FLOAT, dest, ci, -1, 0);
        }
        break;

    case PIR_CONST_BOOL:
        emit(IR_CONST_BOOL, dest, inst->int_val, -1, 0);
        break;

    case PIR_CONST_STR:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->int_val > 0 ? inst->int_val : 0);
        emit(IR_CONST_STR, dest, ci, -1, 0);
        break;

    case PIR_CONST_NONE:
        emit(IR_CONST_NONE, dest, -1, -1, 0);
        break;

    /* ----- Variables ----- */
    case PIR_ALLOCA:
        /* No-op: alloca handled in prescan */
        break;

    case PIR_LOAD:
        {
            /* operands[0] is the alloca value */
            int alloca_id = inst->operands[0].id;
            int slot = -1;
            if (alloca_id >= 0 && alloca_id < alloca_slots_cap) {
                slot = alloca_slots[alloca_id];
            }
            if (slot < 0) {
                report_error("PIR_LOAD: unknown alloca");
                break;
            }
            emit(IR_LOAD_LOCAL, dest, slot, -1, 0);
        }
        break;

    case PIR_STORE:
        {
            /* operands[0] = alloca, operands[1] = value */
            int alloca_id = inst->operands[0].id;
            int val_id = inst->operands[1].id;
            int slot = -1;

            if (alloca_id >= 0 && alloca_id < alloca_slots_cap) {
                slot = alloca_slots[alloca_id];
            }
            if (slot < 0) {
                report_error("PIR_STORE: unknown alloca");
                break;
            }

            /* Skip stores of param values into their slots —
             * params are already in the correct slots per
             * flat IR convention */
            if (is_param_id(val_id)) {
                /* Check if this is the initial param->alloca store.
                 * We verify by checking if the slot index matches
                 * the param index. */
                int pi;
                for (pi = 0; pi < num_param_ids; pi++) {
                    if (param_ids[pi] == val_id && slot == pi) {
                        goto skip_store;
                    }
                }
            }

            emit(IR_STORE_LOCAL, slot, val_temp(inst->operands[1]), -1, 0);
            skip_store: ;
        }
        break;

    case PIR_LOAD_GLOBAL:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_LOAD_GLOBAL, dest, ci, -1, 0);
        break;

    case PIR_STORE_GLOBAL:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_STORE_GLOBAL, ci, val_temp(inst->operands[0]), -1, 0);
        break;

    /* ----- Generic Python arithmetic ----- */
    case PIR_PY_ADD:
        emit(IR_ADD, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_SUB:
        emit(IR_SUB, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_MUL:
        emit(IR_MUL, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_DIV:
        emit(IR_DIV, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_FLOORDIV:
        emit(IR_FLOORDIV, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_MOD:
        emit(IR_MOD, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_POW:
        emit(IR_POW, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_MATMUL:
        emit(IR_MATMUL, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_INPLACE:
        emit(IR_INPLACE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), (int)inst->int_val);
        break;
    case PIR_PY_NEG:
        emit(IR_NEG, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_PY_POS:
        emit(IR_POS, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_PY_NOT:
        emit(IR_NOT, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    /* ----- Bitwise ----- */
    case PIR_PY_BIT_AND:
        emit(IR_BITAND, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_BIT_OR:
        emit(IR_BITOR, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_BIT_XOR:
        emit(IR_BITXOR, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_BIT_NOT:
        emit(IR_BITNOT, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_PY_LSHIFT:
        emit(IR_SHL, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_RSHIFT:
        emit(IR_SHR, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    /* ----- Comparison ----- */
    case PIR_PY_CMP_EQ:
        emit(IR_CMP_EQ, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_CMP_NE:
        emit(IR_CMP_NE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_CMP_LT:
        emit(IR_CMP_LT, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_CMP_LE:
        emit(IR_CMP_LE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_CMP_GT:
        emit(IR_CMP_GT, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_CMP_GE:
        emit(IR_CMP_GE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_IS:
        emit(IR_IS, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_IS_NOT:
        emit(IR_IS_NOT, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_IN:
        emit(IR_IN, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_PY_NOT_IN:
        emit(IR_NOT_IN, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    /* ----- Typed i32 arithmetic (native, no runtime call) ----- */
    case PIR_ADD_I32:
        emit(IR_ADD_I32, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_SUB_I32:
        emit(IR_SUB_I32, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_MUL_I32:
        emit(IR_MUL_I32, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_DIV_I32:
        emit(IR_DIV_I32, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_MOD_I32:
        emit(IR_MOD_I32, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_NEG_I32:
        emit(IR_NEG_I32, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    /* ----- Typed f64 arithmetic (future) ----- */
    case PIR_ADD_F64:
        emit(IR_ADD, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_SUB_F64:
        emit(IR_SUB, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_MUL_F64:
        emit(IR_MUL, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_DIV_F64:
        emit(IR_DIV, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_NEG_F64:
        emit(IR_NEG, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    /* ----- Typed i32 comparison (native, no runtime call) ----- */
    case PIR_CMP_I32_EQ:
        emit(IR_CMP_I32_EQ, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_CMP_I32_NE:
        emit(IR_CMP_I32_NE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_CMP_I32_LT:
        emit(IR_CMP_I32_LT, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_CMP_I32_LE:
        emit(IR_CMP_I32_LE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_CMP_I32_GT:
        emit(IR_CMP_I32_GT, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_CMP_I32_GE:
        emit(IR_CMP_I32_GE, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    /* ----- Box/Unbox ----- */
    case PIR_BOX_INT:
        emit(IR_BOX_INT, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_BOX_BOOL:
        emit(IR_BOX_BOOL, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_UNBOX_INT:
        emit(IR_UNBOX_INT, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_BOX_FLOAT:
    case PIR_UNBOX_FLOAT:
        /* Float box/unbox not yet implemented — pass through as NOP */
        emit(IR_NOP, -1, -1, -1, 0);
        break;

    /* ----- String operations ----- */
    case PIR_STR_CONCAT:
        emit(IR_ADD, dest, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    case PIR_STR_JOIN:
        /* Lower as IR_STR_JOIN — args already pushed via IR_PUSH_ARG */
        emit(IR_STR_JOIN, dest, -1, -1, inst->int_val);
        break;

    case PIR_STR_FORMAT:
        /* Lower as call to str(): PUSH_ARG + LOAD_GLOBAL + CALL */
        {
            emit(IR_PUSH_ARG, -1, val_temp(inst->operands[0]), -1, 0);
            int ci_str = add_const_str("str", 3);
            int func_temp = alloc_temp();
            emit(IR_LOAD_GLOBAL, func_temp, ci_str, -1, 0);
            emit(IR_CALL, dest, func_temp, -1, 1);
        }
        break;

    /* ----- Collection builders (via pushed args) ----- */
    case PIR_BUILD_LIST:
        emit(IR_BUILD_LIST, dest, -1, -1, inst->int_val);
        break;
    case PIR_BUILD_DICT:
        emit(IR_BUILD_DICT, dest, -1, -1, inst->int_val);
        break;
    case PIR_BUILD_TUPLE:
        emit(IR_BUILD_TUPLE, dest, -1, -1, inst->int_val);
        break;
    case PIR_BUILD_SET:
        emit(IR_BUILD_SET, dest, -1, -1, inst->int_val);
        break;

    case PIR_LIST_NEW:
        /* Create empty list = BUILD_LIST with 0 args */
        emit(IR_BUILD_LIST, dest, -1, -1, 0);
        break;
    case PIR_DICT_NEW:
        emit(IR_BUILD_DICT, dest, -1, -1, 0);
        break;
    case PIR_SET_NEW:
        emit(IR_BUILD_SET, dest, -1, -1, 0);
        break;
    case PIR_TUPLE_NEW:
        emit(IR_BUILD_TUPLE, dest, -1, -1, inst->int_val);
        break;

    case PIR_LIST_APPEND:
        /* list.append(item) -> PUSH_ARG + CALL_METHOD "append" */
        emit(IR_PUSH_ARG, -1, val_temp(inst->operands[1]), -1, 0);
        ci = add_const_str("append", 6);
        emit(IR_CALL_METHOD, dest >= 0 ? dest : -1,
             val_temp(inst->operands[0]), ci, 1);
        break;

    case PIR_DICT_SET:
        /* dict[key] = value -> STORE_SUBSCRIPT */
        emit(IR_STORE_SUBSCRIPT,
             val_temp(inst->operands[2]),
             val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    case PIR_TUPLE_SET:
        /* tuple[idx] = value -> STORE_SUBSCRIPT with index const */
        {
            int idx_temp = dest; /* Reuse result temp for index */
            int idx_ci = add_const_int(inst->int_val);
            emit(IR_CONST_INT, idx_temp, idx_ci, -1, 0);
            emit(IR_STORE_SUBSCRIPT,
                 val_temp(inst->operands[1]),
                 val_temp(inst->operands[0]),
                 idx_temp, 0);
        }
        break;

    case PIR_SET_ADD:
        /* set.add(item) -> PUSH_ARG + CALL_METHOD "add" */
        emit(IR_PUSH_ARG, -1, val_temp(inst->operands[1]), -1, 0);
        ci = add_const_str("add", 3);
        emit(IR_CALL_METHOD, dest >= 0 ? dest : -1,
             val_temp(inst->operands[0]), ci, 1);
        break;

    /* ----- Subscript / attribute ----- */
    case PIR_SUBSCR_GET:
        emit(IR_LOAD_SUBSCRIPT, dest,
             val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    case PIR_SUBSCR_SET:
        emit(IR_STORE_SUBSCRIPT,
             val_temp(inst->operands[2]),
             val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    case PIR_DEL_SUBSCR:
        emit(IR_DEL_SUBSCRIPT, -1,
             val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    case PIR_DEL_NAME: {
        /* del local variable: DECREF + zero the stack slot */
        int alloca_id = inst->operands[0].id;
        int slot = -1;
        if (alloca_id >= 0 && alloca_id < alloca_slots_cap) {
            slot = alloca_slots[alloca_id];
        }
        if (slot < 0) {
            report_error("PIR_DEL_NAME: unknown alloca");
            break;
        }
        emit(IR_DEL_LOCAL, -1, slot, -1, 0);
        break;
    }

    case PIR_DEL_GLOBAL: {
        /* del global variable: DECREF + zero the data segment slot */
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_DEL_GLOBAL, ci, -1, -1, 0);
        break;
    }

    case PIR_DEL_ATTR: {
        /* del obj.attr: call runtime pydos_obj_del_attr */
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_DEL_ATTR, ci, val_temp(inst->operands[0]), -1, 0);
        break;
    }

    case PIR_SLICE:
        /* PUSH_ARGs for start, stop, step already lowered before this.
         * Just emit IR_LOAD_SLICE with the object.
         * type_hint propagated automatically at end of lower_inst(). */
        emit(IR_LOAD_SLICE, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_GET_ATTR:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_LOAD_ATTR, dest, val_temp(inst->operands[0]), ci, 0);
        break;

    case PIR_SET_ATTR:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_STORE_ATTR, ci, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    /* ----- Calls ----- */
    case PIR_PUSH_ARG:
        emit(IR_PUSH_ARG, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_CALL:
        {
            /* The codegen expects IR_CALL src1 = a temp loaded from
             * a global/local (not a const pool index). Mirror the
             * legacy IRGenerator::gen_name() pattern: check locals
             * first, then fall back to LOAD_GLOBAL. */
            ci = add_const_str(
                inst->str_val ? inst->str_val : "",
                inst->str_val ? (int)strlen(inst->str_val) : 0);
            int func_temp = alloc_temp();

            /* Check if callee is a local variable (first-class function) */
            int local_slot = -1;
            if (inst->str_val) {
                int li;
                for (li = 0; li < current_func->locals_count; li++) {
                    if (current_func->locals[li].name &&
                        strcmp(current_func->locals[li].name,
                               inst->str_val) == 0) {
                        local_slot = current_func->locals[li].slot;
                        break;
                    }
                }
            }

            if (local_slot >= 0) {
                emit(IR_LOAD_LOCAL, func_temp, local_slot, -1, 0);
            } else {
                emit(IR_LOAD_GLOBAL, func_temp, ci, -1, 0);
            }
            emit(IR_CALL, dest, func_temp, -1, inst->int_val);
        }
        break;

    case PIR_CALL_METHOD:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_CALL_METHOD, dest, val_temp(inst->operands[0]),
             ci, inst->int_val);
        break;

    /* ----- Control flow ----- */
    case PIR_BRANCH:
        emit_phi_stores_for_block(current_pir_block_id);
        emit(IR_JUMP, -1, -1, -1, block_label(inst->target_block));
        break;

    case PIR_COND_BRANCH:
        /* if cond: goto true_block, else: goto false_block
         * Lower as: phi stores, then JUMP_IF_TRUE to true, then JUMP to false */
        emit_phi_stores_for_block(current_pir_block_id);
        emit(IR_JUMP_IF_TRUE, -1, val_temp(inst->operands[0]), -1,
             block_label(inst->target_block));
        emit(IR_JUMP, -1, -1, -1, block_label(inst->false_block));
        break;

    case PIR_RETURN:
        emit_phi_stores_for_block(current_pir_block_id);
        emit(IR_RETURN, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_RETURN_NONE: {
        emit_phi_stores_for_block(current_pir_block_id);
        int none_tmp = alloc_temp();
        emit(IR_CONST_NONE, none_tmp, -1, -1, 0);
        emit(IR_RETURN, -1, none_tmp, -1, 0);
        break;
    }

    /* ----- SSA (phi nodes — inserted by mem2reg) ----- */
    case PIR_PHI: {
        /* Phi stores were pre-recorded by prescan_phis() and are
         * emitted by emit_phi_stores_for_block() before each
         * block's terminator. Here we just load the phi slot. */
        int phi_slot = inst->int_val; /* set by prescan_phis */
        emit(IR_LOAD_LOCAL, dest, phi_slot, -1, 0);
        break;
    }

    /* ----- Object operations ----- */
    case PIR_ALLOC_OBJ:
        emit(IR_ALLOC_OBJ, dest, -1, -1, inst->int_val);
        break;

    case PIR_INIT_VTABLE:
        emit(IR_INIT_VTABLE, -1, -1, -1, inst->int_val);
        break;

    case PIR_SET_VTABLE:
    {
        /* Resolve vtable index by name at lowering time, since all
         * class vtables are registered by now (PIR builder may have
         * set int_val=-1 for forward-referenced classes). */
        int vt_idx = inst->int_val;
        if (vt_idx < 0 && inst->str_val && pir_mod) {
            int vi;
            for (vi = 0; vi < pir_mod->num_vtables; vi++) {
                if (pir_mod->vtables[vi].class_name &&
                    inst->str_val &&
                    strcmp(pir_mod->vtables[vi].class_name,
                           inst->str_val) == 0) {
                    vt_idx = vi;
                    break;
                }
            }
        }
        emit(IR_SET_VTABLE, -1, val_temp(inst->operands[0]), -1, vt_idx);
        break;
    }

    case PIR_CHECK_VTABLE:
    {
        /* Resolve vtable index by name (same as PIR_SET_VTABLE) */
        int vt_idx = inst->int_val;
        if (vt_idx < 0 && inst->str_val && pir_mod) {
            int vi;
            for (vi = 0; vi < pir_mod->num_vtables; vi++) {
                if (pir_mod->vtables[vi].class_name &&
                    inst->str_val &&
                    strcmp(pir_mod->vtables[vi].class_name,
                           inst->str_val) == 0) {
                    vt_idx = vi;
                    break;
                }
            }
        }
        emit(IR_CHECK_VTABLE, val_temp(inst->result),
             val_temp(inst->operands[0]), -1, vt_idx);
        break;
    }

    case PIR_INCREF:
        emit(IR_INCREF, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_DECREF:
        emit(IR_DECREF, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    /* ----- Functions ----- */
    case PIR_MAKE_FUNCTION:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        /* src2 = closure temp if operands[0] is valid, -1 otherwise */
        emit(IR_MAKE_FUNCTION, dest, ci,
             (inst->num_operands > 0 && pir_value_valid(inst->operands[0]))
                 ? val_temp(inst->operands[0]) : -1,
             inst->int_val);
        break;

    case PIR_MAKE_GENERATOR:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_MAKE_GENERATOR, dest, ci, -1, inst->int_val);
        break;

    case PIR_MAKE_COROUTINE:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_MAKE_COROUTINE, dest, ci, -1, inst->int_val);
        break;

    case PIR_COR_SET_RESULT:
        emit(IR_COR_SET_RESULT, -1, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;

    /* ----- Exception handling ----- */
    case PIR_SETUP_TRY:
        emit(IR_SETUP_TRY, -1, -1, -1,
             block_label(inst->handler_block));
        break;

    case PIR_POP_TRY:
        emit(IR_POP_TRY, -1, -1, -1, 0);
        break;

    case PIR_RAISE:
        emit(IR_RAISE, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_RERAISE:
        emit(IR_RERAISE, -1, -1, -1, 0);
        break;

    case PIR_GET_EXCEPTION:
        emit(IR_GET_EXCEPTION, dest, -1, -1, 0);
        break;

    case PIR_EXC_MATCH:
        /* src2 = raw exception type code (not a temp), extra unused */
        emit(IR_EXC_MATCH, dest, val_temp(inst->operands[0]),
             inst->int_val, 0);
        break;

    /* ----- Iteration ----- */
    case PIR_GET_ITER:
        emit(IR_GET_ITER, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_FOR_ITER:
        /* PIR: result = next(iter), branch to handler on StopIteration
         * Flat IR: IR_FOR_ITER dest, src1=iter, extra=end_label */
        emit(IR_FOR_ITER, dest, val_temp(inst->operands[0]), -1,
             block_label(inst->handler_block));
        break;

    /* ----- Generators ----- */
    case PIR_GEN_LOAD_PC:
        emit(IR_GEN_LOAD_PC, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_GEN_SET_PC:
        emit(IR_GEN_SET_PC, -1, val_temp(inst->operands[0]), -1,
             inst->int_val);
        break;

    case PIR_GEN_LOAD_LOCAL:
        emit(IR_GEN_LOAD_LOCAL, dest, val_temp(inst->operands[0]), -1,
             inst->int_val);
        break;

    case PIR_GEN_SAVE_LOCAL:
        emit(IR_GEN_SAVE_LOCAL, -1, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), inst->int_val);
        break;

    case PIR_YIELD:
        emit(IR_YIELD, dest, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_GEN_CHECK_THROW:
        emit(IR_GEN_CHECK_THROW, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    case PIR_GEN_GET_SENT:
        emit(IR_GEN_GET_SENT, dest, -1, -1, 0);
        break;

    /* ----- Cell objects (closures) ----- */
    case PIR_MAKE_CELL:
        emit(IR_MAKE_CELL, dest, -1, -1, 0);
        break;
    case PIR_CELL_GET:
        emit(IR_CELL_GET, dest, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_CELL_SET:
        emit(IR_CELL_SET, -1, val_temp(inst->operands[0]),
             val_temp(inst->operands[1]), 0);
        break;
    case PIR_LOAD_CLOSURE:
        emit(IR_LOAD_CLOSURE, dest, -1, -1, 0);
        break;
    case PIR_SET_CLOSURE:
        emit(IR_SET_CLOSURE, -1, val_temp(inst->operands[0]), -1, 0);
        break;

    /* ----- Arena Scope ----- */
    case PIR_SCOPE_ENTER:
        emit(IR_SCOPE_ENTER, -1, -1, -1, 0);
        break;
    case PIR_SCOPE_TRACK:
        emit(IR_SCOPE_TRACK, -1, val_temp(inst->operands[0]), -1, 0);
        break;
    case PIR_SCOPE_EXIT:
        emit(IR_SCOPE_EXIT, -1, -1, -1, 0);
        break;

    /* ----- Import (no flat IR equivalents yet) ----- */
    case PIR_IMPORT:
    case PIR_IMPORT_FROM:
        /* TODO: add flat IR opcodes when import is implemented */
        emit(IR_NOP, -1, -1, -1, 0);
        break;

    /* ----- Misc ----- */
    case PIR_NOP:
        emit(IR_NOP, -1, -1, -1, 0);
        break;

    case PIR_COMMENT:
        ci = add_const_str(
            inst->str_val ? inst->str_val : "",
            inst->str_val ? (int)strlen(inst->str_val) : 0);
        emit(IR_COMMENT, -1, ci, -1, 0);
        break;

    default:
        report_error("unknown PIR opcode in lowering");
        break;
    }

    /* Propagate type_hint from PIR instruction to the last emitted flat IR instruction. */
    if (inst->type_hint && current_func->last) {
        current_func->last->type_hint = inst->type_hint;
    }
}

/* --------------------------------------------------------------- */
/* Lower a single PIR function → IRFunc                              */
/* --------------------------------------------------------------- */
void PIRLowerer::lower_function(PIRFunction *pir_func)
{
    IRFunc *func;
    int bi;
    PIRInst *inst;
    int max_temp;

    /* Create IRFunc */
    func = (IRFunc *)malloc(sizeof(IRFunc));
    if (!func) {
        report_error("out of memory for IRFunc");
        return;
    }
    memset(func, 0, sizeof(IRFunc));
    func->name = lower_str_dup(pir_func->name);
    func->num_params = pir_func->num_params;
    func->is_generator = pir_func->is_generator;
    func->is_coroutine = pir_func->is_coroutine;
    func->ret_type = 0;
    func->first = 0;
    func->last = 0;
    func->next = 0;

    current_func = func;

    /* Pre-scan: assign labels, alloca slots */
    prescan_function(pir_func);

    /* Pre-scan phi nodes: allocate slots, record pending stores */
    prescan_phis(pir_func);

    /* Walk blocks in order and emit flat IR */
    for (bi = 0; bi < pir_func->blocks.size(); bi++) {
        PIRBlock *block = pir_func->blocks[bi];
        current_pir_block_id = block->id;

        /* Emit label for this block */
        {
            IRInstr *lbl = emit(IR_LABEL, -1, -1, -1, block_label(block));
            (void)lbl;
        }

        /* Lower each instruction in the block */
        for (inst = block->first; inst; inst = inst->next) {
            lower_inst(inst, pir_func);
        }
    }

    current_pir_block_id = -1;

    /* Calculate num_temps (includes any extra temps allocated during lowering) */
    max_temp = next_temp;
    func->num_temps = max_temp;

    /* Link into IR module */
    if (ir_mod->functions) {
        /* Append to end of linked list */
        IRFunc *last = ir_mod->functions;
        while (last->next) last = last->next;
        last->next = func;
    } else {
        ir_mod->functions = func;
    }

    /* Mark init func */
    if (pir_mod->init_func == pir_func) {
        ir_mod->init_func = func;
    }

    current_func = 0;
}

/* --------------------------------------------------------------- */
/* Lower entire PIR module → IRModule                                */
/* --------------------------------------------------------------- */
IRModule *PIRLowerer::lower(PIRModule *pm)
{
    int fi;

    pir_mod = pm;

    /* Allocate IRModule */
    ir_mod = (IRModule *)malloc(sizeof(IRModule));
    if (!ir_mod) {
        report_error("out of memory for IRModule");
        return 0;
    }
    memset(ir_mod, 0, sizeof(IRModule));

    /* Copy module qualification fields */
    ir_mod->module_name = pm->module_name;
    ir_mod->is_main_module = pm->is_main_module;
    ir_mod->has_main_func = pm->has_main_func;
    ir_mod->entry_func = pm->entry_func;

    /* Pre-allocate constant pool */
    grow_constants(ir_mod);

    /* Copy vtable info from PIR module to IR module */
    {
        int vi, mi;
        ir_mod->num_class_vtables = pm->num_vtables;
        for (vi = 0; vi < pm->num_vtables && vi < 32; vi++) {
            PIRVTableInfo *src = &pm->vtables[vi];
            ClassVTableInfo *dst = &ir_mod->class_vtables[vi];
            dst->class_name = src->class_name;
            dst->base_class_name = src->base_class_name;
            dst->num_extra_bases = src->num_extra_bases;
            for (mi = 0; mi < src->num_extra_bases && mi < 7; mi++) {
                dst->extra_bases[mi] = src->extra_bases[mi];
            }
            dst->num_methods = src->num_methods;
            for (mi = 0; mi < src->num_methods && mi < 64; mi++) {
                dst->methods[mi].python_name = src->methods[mi].python_name;
                dst->methods[mi].mangled_name = src->methods[mi].mangled_name;
            }
        }
    }

    /* Lower each PIR function */
    for (fi = 0; fi < pm->functions.size(); fi++) {
        lower_function(pm->functions[fi]);
    }

    return ir_mod;
}
