/*
 * iropt.cpp - IR optimization passes
 *
 * Implements constant folding, dead code elimination, copy propagation,
 * strength reduction, and peephole optimizations over the three-address
 * code IR.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL - arrays, linked lists, manual memory only.
 */

#include "iropt.h"

#include <stdlib.h>
#include <string.h>

/* ================================================================ */
/* Constructor / Destructor                                          */
/* ================================================================ */

IROptimizer::IROptimizer()
{
    num_temp_consts = 0;
    current_mod = 0;
    reset_const_tracking();
}

IROptimizer::~IROptimizer()
{
    /* nothing to free - all state is stack-allocated */
}

/* ================================================================ */
/* Top-level optimize: run all passes on every function              */
/* ================================================================ */

void IROptimizer::optimize(IRModule *mod)
{
    if (!mod) return;
    current_mod = mod;

    /* Process init function */
    if (mod->init_func) {
        constant_fold(mod->init_func);
        strength_reduce(mod->init_func);
        copy_propagate(mod->init_func);
        dead_code_eliminate(mod->init_func);
        peephole(mod->init_func);
    }

    /* Process all other functions */
    IRFunc *func = mod->functions;
    while (func) {
        if (func != mod->init_func) {
            constant_fold(func);
            strength_reduce(func);
            copy_propagate(func);
            dead_code_eliminate(func);
            peephole(func);
        }
        func = func->next;
    }
}

/* ================================================================ */
/* Helper: determine if an opcode is side-effect free (pure)         */
/* ================================================================ */

int IROptimizer::is_pure(IROp op)
{
    switch (op) {
    /* Constants */
    case IR_CONST_INT:
    case IR_CONST_STR:
    case IR_CONST_FLOAT:
    case IR_CONST_NONE:
    case IR_CONST_BOOL:
    /* Arithmetic (pure computation, no memory effects) */
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    /* IR_DIV, IR_FLOORDIV, IR_MOD omitted — can raise ZeroDivisionError */
    case IR_POW:
    case IR_NEG:
    case IR_POS:
    case IR_NOT:
    case IR_BITNOT:
    /* Bitwise */
    case IR_BITAND:
    case IR_BITOR:
    case IR_BITXOR:
    case IR_SHL:
    case IR_SHR:
    /* Comparison */
    case IR_CMP_EQ:
    case IR_CMP_NE:
    case IR_CMP_LT:
    case IR_CMP_LE:
    case IR_CMP_GT:
    case IR_CMP_GE:
    case IR_IS:
    case IR_IS_NOT:
    case IR_IN:
    case IR_NOT_IN:
    /* Typed i32 arithmetic (pure — no heap allocation, no side effects) */
    case IR_ADD_I32:
    case IR_SUB_I32:
    case IR_MUL_I32:
    case IR_NEG_I32:
    /* IR_DIV_I32, IR_MOD_I32 omitted — can raise ZeroDivisionError */
    /* Typed i32 comparison (pure) */
    case IR_CMP_I32_EQ:
    case IR_CMP_I32_NE:
    case IR_CMP_I32_LT:
    case IR_CMP_I32_LE:
    case IR_CMP_I32_GT:
    case IR_CMP_I32_GE:
    /* Unbox is pure (just a field read) */
    case IR_UNBOX_INT:
    /* Load local (reading a local is pure) */
    case IR_LOAD_LOCAL:
        return 1;

    /* NOT pure: BOX_INT/BOX_BOOL allocate heap objects */
    case IR_BOX_INT:
    case IR_BOX_BOOL:
    /* NOT pure: STR_JOIN allocates a heap string */
    case IR_STR_JOIN:
    /* NOT pure: arena scope operations */
    case IR_SCOPE_ENTER:
    case IR_SCOPE_TRACK:
    case IR_SCOPE_EXIT:
    /* NOT pure: side-effecting opcodes */
    case IR_MAKE_FUNCTION:
    case IR_MAKE_GENERATOR:
    case IR_GEN_LOAD_PC:
    case IR_GEN_SET_PC:
    case IR_GEN_LOAD_LOCAL:
    case IR_GEN_SAVE_LOCAL:
    case IR_GEN_CHECK_THROW:
    case IR_GEN_GET_SENT:

    default:
        return 0;
    }
}

/* ================================================================ */
/* Helper: check if a temp is read by an instruction                 */
/* ================================================================ */

int IROptimizer::instr_reads_temp(IRInstr *ins, int temp)
{
    if (!ins || temp < 0) return 0;

    switch (ins->op) {
    /* Instructions that read src1 only */
    case IR_NEG:
    case IR_POS:
    case IR_NOT:
    case IR_BITNOT:
    case IR_NEG_I32:
    case IR_UNBOX_INT:
    case IR_BOX_INT:
    case IR_BOX_BOOL:
    case IR_SCOPE_TRACK:
    case IR_JUMP_IF_TRUE:
    case IR_JUMP_IF_FALSE:
    case IR_RETURN:
    case IR_PUSH_ARG:
    case IR_INCREF:
    case IR_DECREF:
    case IR_GET_ITER:
    case IR_RAISE:
    case IR_YIELD:
    case IR_YIELD_FROM:
    case IR_MAKE_FUNCTION:
        return ins->src1 == temp;

    /* Generator state machine: src1 is always the gen temp */
    case IR_GEN_LOAD_PC:
    case IR_GEN_SET_PC:
    case IR_GEN_LOAD_LOCAL:
    case IR_GEN_CHECK_THROW:
        return ins->src1 == temp;

    case IR_GEN_GET_SENT:
        return 0;  /* reads global, not a temp */

    case IR_GEN_SAVE_LOCAL:
        return ins->src1 == temp || ins->src2 == temp;

    case IR_MAKE_GENERATOR:
        return ins->src1 == temp;

    /* Instructions that read src1 and src2 */
    case IR_ADD:
    case IR_SUB:
    case IR_MUL:
    case IR_DIV:
    case IR_FLOORDIV:
    case IR_MOD:
    case IR_POW:
    case IR_ADD_I32:
    case IR_SUB_I32:
    case IR_MUL_I32:
    case IR_DIV_I32:
    case IR_MOD_I32:
    case IR_CMP_I32_EQ:
    case IR_CMP_I32_NE:
    case IR_CMP_I32_LT:
    case IR_CMP_I32_LE:
    case IR_CMP_I32_GT:
    case IR_CMP_I32_GE:
    case IR_BITAND:
    case IR_BITOR:
    case IR_BITXOR:
    case IR_SHL:
    case IR_SHR:
    case IR_CMP_EQ:
    case IR_CMP_NE:
    case IR_CMP_LT:
    case IR_CMP_LE:
    case IR_CMP_GT:
    case IR_CMP_GE:
    case IR_IS:
    case IR_IS_NOT:
    case IR_IN:
    case IR_NOT_IN:
    case IR_LOAD_SUBSCRIPT:
        return ins->src1 == temp || ins->src2 == temp;

    /* STORE_LOCAL: reads src1 as value */
    case IR_STORE_LOCAL:
        return ins->src1 == temp;

    /* STORE_GLOBAL: reads src1 as value */
    case IR_STORE_GLOBAL:
        return ins->src1 == temp;

    /* STORE_ATTR: reads src1 (object) and src2 (value) */
    case IR_STORE_ATTR:
        return ins->src1 == temp || ins->src2 == temp;

    /* STORE_SUBSCRIPT: reads dest (value), src1 (object), src2 (index) */
    case IR_STORE_SUBSCRIPT:
        return ins->dest == temp || ins->src1 == temp || ins->src2 == temp;

    /* LOAD_ATTR: reads src1 (object) */
    case IR_LOAD_ATTR:
        return ins->src1 == temp;

    /* CALL: reads src1 (function) */
    case IR_CALL:
        return ins->src1 == temp;

    /* CALL_METHOD: reads src1 (object) */
    case IR_CALL_METHOD:
        return ins->src1 == temp;

    /* FOR_ITER: reads src1 (iterator) */
    case IR_FOR_ITER:
        return ins->src1 == temp;

    /* LOAD_LOCAL: reads src1 as slot index, not a temp register.
     * However, when used as a copy (see boolop gen), src1 may be a temp.
     * We check it here for safety. */
    case IR_LOAD_LOCAL:
        return ins->src1 == temp;

    default:
        /* Conservative: check both src fields */
        return ins->src1 == temp || ins->src2 == temp;
    }
}

/* ================================================================ */
/* Helper: check if temp is used after a given instruction           */
/* ================================================================ */

int IROptimizer::is_temp_used_after(IRFunc *func, IRInstr *after, int temp)
{
    (void)func;
    if (temp < 0) return 0;
    IRInstr *ins = after ? after->next : 0;
    while (ins) {
        if (instr_reads_temp(ins, temp))
            return 1;
        ins = ins->next;
    }
    return 0;
}

int IROptimizer::is_temp_used(IRFunc *func, int temp)
{
    if (temp < 0) return 0;
    IRInstr *ins = func->first;
    while (ins) {
        if (instr_reads_temp(ins, temp))
            return 1;
        ins = ins->next;
    }
    return 0;
}

/* ================================================================ */
/* Helper: remove / NOP instructions                                 */
/* ================================================================ */

void IROptimizer::remove_instr(IRFunc *func, IRInstr *instr)
{
    if (!instr) return;
    if (instr->prev) {
        instr->prev->next = instr->next;
    } else {
        func->first = instr->next;
    }
    if (instr->next) {
        instr->next->prev = instr->prev;
    } else {
        func->last = instr->prev;
    }
    free(instr);
}

void IROptimizer::nop_out(IRInstr *instr)
{
    if (!instr) return;
    instr->op = IR_NOP;
    instr->dest = -1;
    instr->src1 = -1;
    instr->src2 = -1;
    instr->extra = 0;
    instr->type_hint = 0;
}

/* ================================================================ */
/* Constant tracking                                                 */
/* ================================================================ */

void IROptimizer::reset_const_tracking()
{
    for (int i = 0; i < MAX_TRACKED_TEMPS; i++) {
        temp_consts[i].is_const = 0;
    }
    num_temp_consts = 0;
}

void IROptimizer::set_const(int temp, const IRConst *val)
{
    if (temp < 0 || temp >= MAX_TRACKED_TEMPS) return;
    temp_consts[temp].is_const = 1;
    temp_consts[temp].value = *val;
    if (temp >= num_temp_consts)
        num_temp_consts = temp + 1;
}

IROptimizer::ConstValue *IROptimizer::get_const(int temp)
{
    if (temp < 0 || temp >= MAX_TRACKED_TEMPS) return 0;
    if (temp_consts[temp].is_const)
        return &temp_consts[temp];
    return 0;
}

/* ================================================================ */
/* Helper: power of two detection                                    */
/* ================================================================ */

int IROptimizer::is_power_of_two(long val, int *shift_out)
{
    if (val <= 0) return 0;
    if ((val & (val - 1)) != 0) return 0;

    int shift = 0;
    long v = val;
    while (v > 1) {
        v >>= 1;
        shift++;
    }
    if (shift_out) *shift_out = shift;
    return 1;
}

/* ================================================================ */
/* PASS 1: Constant Folding                                          */
/* ================================================================ */

void IROptimizer::constant_fold(IRFunc *func)
{
    if (!func) return;
    reset_const_tracking();

    IRInstr *ins = func->first;
    while (ins) {
        IRInstr *next = ins->next;

        switch (ins->op) {
        /* Track integer constants */
        case IR_CONST_INT:
        {
            if (ins->dest >= 0 && ins->src1 >= 0 &&
                current_mod && ins->src1 < current_mod->num_constants) {
                IRConst *c = &current_mod->constants[ins->src1];
                if (c->kind == IRConst::CONST_INT) {
                    set_const(ins->dest, c);
                }
            }
            break;
        }

        /* Track bool constants (as integers) */
        case IR_CONST_BOOL:
        {
            if (ins->dest >= 0) {
                IRConst cv;
                cv.kind = IRConst::CONST_INT;
                cv.int_val = ins->src1;
                set_const(ins->dest, &cv);
            }
            break;
        }

        /* Fold binary integer arithmetic */
        case IR_ADD: case IR_SUB: case IR_MUL: case IR_DIV:
        case IR_FLOORDIV: case IR_MOD:
        case IR_BITAND: case IR_BITOR: case IR_BITXOR:
        case IR_SHL: case IR_SHR:
        {
            ConstValue *lc = get_const(ins->src1);
            ConstValue *rc = get_const(ins->src2);

            if (lc && rc &&
                lc->value.kind == IRConst::CONST_INT &&
                rc->value.kind == IRConst::CONST_INT) {

                long lv = lc->value.int_val;
                long rv = rc->value.int_val;
                long result = 0;
                int  valid  = 1;

                switch (ins->op) {
                case IR_ADD:      result = lv + rv; break;
                case IR_SUB:      result = lv - rv; break;
                case IR_MUL:      result = lv * rv; break;
                case IR_DIV:
                    if (rv == 0) { valid = 0; }
                    else { result = lv / rv; }
                    break;
                case IR_FLOORDIV:
                    if (rv == 0) { valid = 0; }
                    else { result = lv / rv; }
                    break;
                case IR_MOD:
                    if (rv == 0) { valid = 0; }
                    else { result = lv % rv; }
                    break;
                case IR_BITAND:   result = lv & rv; break;
                case IR_BITOR:    result = lv | rv; break;
                case IR_BITXOR:   result = lv ^ rv; break;
                case IR_SHL:
                    if (rv < 0 || rv > 31) { valid = 0; }
                    else { result = lv << rv; }
                    break;
                case IR_SHR:
                    if (rv < 0 || rv > 31) { valid = 0; }
                    else { result = lv >> rv; }
                    break;
                default: valid = 0; break;
                }

                if (valid) {
                    /* Replace instruction with CONST_INT */
                    int ci = -1;
                    if (current_mod) {
                        /* Search for existing constant or create new */
                        for (int i = 0; i < current_mod->num_constants; i++) {
                            if (current_mod->constants[i].kind == IRConst::CONST_INT &&
                                current_mod->constants[i].int_val == result) {
                                ci = i;
                                break;
                            }
                        }
                        if (ci < 0) {
                            /* Add new constant */
                            if (current_mod->num_constants < current_mod->max_constants) {
                                ci = current_mod->num_constants;
                                current_mod->constants[ci].kind = IRConst::CONST_INT;
                                current_mod->constants[ci].int_val = result;
                                current_mod->num_constants++;
                            }
                        }
                    }

                    if (ci >= 0) {
                        ins->op = IR_CONST_INT;
                        ins->src1 = ci;
                        ins->src2 = -1;
                        ins->extra = 0;

                        /* Track the folded result */
                        IRConst rv_const;
                        rv_const.kind = IRConst::CONST_INT;
                        rv_const.int_val = result;
                        set_const(ins->dest, &rv_const);
                    }
                }
            }
            break;
        }

        /* Fold unary negation */
        case IR_NEG:
        {
            ConstValue *oc = get_const(ins->src1);
            if (oc && oc->value.kind == IRConst::CONST_INT) {
                long result = -oc->value.int_val;
                int ci = -1;
                if (current_mod) {
                    for (int i = 0; i < current_mod->num_constants; i++) {
                        if (current_mod->constants[i].kind == IRConst::CONST_INT &&
                            current_mod->constants[i].int_val == result) {
                            ci = i;
                            break;
                        }
                    }
                    if (ci < 0 && current_mod->num_constants < current_mod->max_constants) {
                        ci = current_mod->num_constants;
                        current_mod->constants[ci].kind = IRConst::CONST_INT;
                        current_mod->constants[ci].int_val = result;
                        current_mod->num_constants++;
                    }
                }
                if (ci >= 0) {
                    ins->op = IR_CONST_INT;
                    ins->src1 = ci;
                    ins->src2 = -1;

                    IRConst rc;
                    rc.kind = IRConst::CONST_INT;
                    rc.int_val = result;
                    set_const(ins->dest, &rc);
                }
            }
            break;
        }

        /* Fold bitwise NOT */
        case IR_BITNOT:
        {
            ConstValue *oc = get_const(ins->src1);
            if (oc && oc->value.kind == IRConst::CONST_INT) {
                long result = ~oc->value.int_val;
                int ci = -1;
                if (current_mod) {
                    for (int i = 0; i < current_mod->num_constants; i++) {
                        if (current_mod->constants[i].kind == IRConst::CONST_INT &&
                            current_mod->constants[i].int_val == result) {
                            ci = i;
                            break;
                        }
                    }
                    if (ci < 0 && current_mod->num_constants < current_mod->max_constants) {
                        ci = current_mod->num_constants;
                        current_mod->constants[ci].kind = IRConst::CONST_INT;
                        current_mod->constants[ci].int_val = result;
                        current_mod->num_constants++;
                    }
                }
                if (ci >= 0) {
                    ins->op = IR_CONST_INT;
                    ins->src1 = ci;
                    ins->src2 = -1;

                    IRConst rc;
                    rc.kind = IRConst::CONST_INT;
                    rc.int_val = result;
                    set_const(ins->dest, &rc);
                }
            }
            break;
        }

        /* Fold logical NOT on known bool/int */
        case IR_NOT:
        {
            ConstValue *oc = get_const(ins->src1);
            if (oc && oc->value.kind == IRConst::CONST_INT) {
                int bool_result = (oc->value.int_val == 0) ? 1 : 0;
                ins->op = IR_CONST_BOOL;
                ins->src1 = bool_result;
                ins->src2 = -1;

                IRConst rc;
                rc.kind = IRConst::CONST_INT;
                rc.int_val = bool_result;
                set_const(ins->dest, &rc);
            }
            break;
        }

        /* Fold integer comparisons */
        case IR_CMP_EQ: case IR_CMP_NE:
        case IR_CMP_LT: case IR_CMP_LE:
        case IR_CMP_GT: case IR_CMP_GE:
        {
            ConstValue *lc = get_const(ins->src1);
            ConstValue *rc = get_const(ins->src2);

            if (lc && rc &&
                lc->value.kind == IRConst::CONST_INT &&
                rc->value.kind == IRConst::CONST_INT) {

                long lv = lc->value.int_val;
                long rv = rc->value.int_val;
                int  cmp_result = 0;

                switch (ins->op) {
                case IR_CMP_EQ: cmp_result = (lv == rv) ? 1 : 0; break;
                case IR_CMP_NE: cmp_result = (lv != rv) ? 1 : 0; break;
                case IR_CMP_LT: cmp_result = (lv <  rv) ? 1 : 0; break;
                case IR_CMP_LE: cmp_result = (lv <= rv) ? 1 : 0; break;
                case IR_CMP_GT: cmp_result = (lv >  rv) ? 1 : 0; break;
                case IR_CMP_GE: cmp_result = (lv >= rv) ? 1 : 0; break;
                default: break;
                }

                ins->op = IR_CONST_BOOL;
                ins->src1 = cmp_result;
                ins->src2 = -1;

                IRConst rc_val;
                rc_val.kind = IRConst::CONST_INT;
                rc_val.int_val = cmp_result;
                set_const(ins->dest, &rc_val);
            }
            break;
        }

        /* Positive unary on known const is identity */
        case IR_POS:
        {
            ConstValue *oc = get_const(ins->src1);
            if (oc && oc->value.kind == IRConst::CONST_INT) {
                set_const(ins->dest, &oc->value);
            }
            break;
        }

        /* Any instruction that defines a dest invalidates tracking
         * (unless already handled above) */
        default:
            if (ins->dest >= 0 && ins->dest < MAX_TRACKED_TEMPS) {
                temp_consts[ins->dest].is_const = 0;
            }
            break;
        }

        ins = next;
    }
}

/* ================================================================ */
/* PASS 2: Dead Code Elimination                                     */
/* ================================================================ */

void IROptimizer::dead_code_eliminate(IRFunc *func)
{
    if (!func) return;

    int changed = 1;
    int iterations = 0;
    int max_iterations = 20; /* safety limit */

    while (changed && iterations < max_iterations) {
        changed = 0;
        iterations++;

        /* ---- Phase A: Remove unreachable code after unconditional jumps ---- */
        IRInstr *ins = func->first;
        while (ins) {
            IRInstr *next = ins->next;

            if (ins->op == IR_JUMP || ins->op == IR_RETURN ||
                ins->op == IR_RAISE || ins->op == IR_RERAISE) {
                /* Everything between this instruction and the next label
                 * (exclusive) is unreachable. */
                IRInstr *dead = ins->next;
                while (dead && dead->op != IR_LABEL) {
                    IRInstr *dead_next = dead->next;
                    if (dead->op != IR_NOP) {
                        nop_out(dead);
                        changed = 1;
                    }
                    dead = dead_next;
                }
            }

            ins = next;
        }

        /* ---- Phase B: Remove pure instructions whose dest is never read ---- */
        ins = func->first;
        while (ins) {
            IRInstr *next = ins->next;

            if (ins->op != IR_NOP && is_pure(ins->op) && ins->dest >= 0) {
                if (!is_temp_used(func, ins->dest)) {
                    nop_out(ins);
                    changed = 1;
                }
            }

            ins = next;
        }

        /* ---- Phase C: Fold constant conditional jumps ---- */
        ins = func->first;
        while (ins) {
            IRInstr *next = ins->next;

            if (ins->op == IR_JUMP_IF_TRUE || ins->op == IR_JUMP_IF_FALSE) {
                ConstValue *cv = get_const(ins->src1);
                if (cv && cv->value.kind == IRConst::CONST_INT) {
                    int truthy = (cv->value.int_val != 0) ? 1 : 0;

                    if ((ins->op == IR_JUMP_IF_TRUE && truthy) ||
                        (ins->op == IR_JUMP_IF_FALSE && !truthy)) {
                        /* Condition is always taken: replace with unconditional jump */
                        ins->op = IR_JUMP;
                        ins->src1 = -1;
                        changed = 1;
                    } else {
                        /* Condition is never taken: remove the jump */
                        nop_out(ins);
                        changed = 1;
                    }
                }
            }

            ins = next;
        }
    }

    /* ---- Final: strip out NOP instructions ---- */
    IRInstr *ins = func->first;
    while (ins) {
        IRInstr *next = ins->next;
        if (ins->op == IR_NOP) {
            remove_instr(func, ins);
        }
        ins = next;
    }
}

/* ================================================================ */
/* PASS 3: Copy Propagation                                          */
/* ================================================================ */

void IROptimizer::copy_propagate(IRFunc *func)
{
    if (!func) return;

    /*
     * Track "copy" relationships: if we see a pure move-like instruction
     * where dest = src (e.g., IR_POS dest, src or IR_LOAD_LOCAL dest, src
     * used as a temp-to-temp copy), replace later uses of dest with src.
     *
     * We use a simple forward scan with a copy table.
     */

    /* Copy table: copy_src[t] = the temp that t is a copy of, or -1 */
    int copy_src[1024];
    for (int i = 0; i < 1024; i++) {
        copy_src[i] = -1;
    }

    IRInstr *ins = func->first;
    while (ins) {
        IRInstr *next = ins->next;

        /* Detect copy-like instructions:
         *   IR_POS dest, src    (identity: +x = x)
         *   IR_LOAD_LOCAL dest, src_temp  (when used as temp-to-temp copy
         *                                  in boolop codegen) */
        if (ins->op == IR_POS && ins->dest >= 0 && ins->src1 >= 0) {
            if (ins->dest < 1024 && ins->src1 < 1024) {
                /* Follow the copy chain: if src1 is itself a copy, follow it */
                int ultimate_src = ins->src1;
                while (ultimate_src >= 0 && ultimate_src < 1024 &&
                       copy_src[ultimate_src] >= 0) {
                    ultimate_src = copy_src[ultimate_src];
                }
                copy_src[ins->dest] = ultimate_src;
            }
        }

        /* Substitute copies in source operands.
         * IMPORTANT: For several instructions, src1 is a constant pool index
         * or slot index, NOT a temp register.  Do NOT apply copy propagation
         * to src1 for these.  Similarly, for LOAD_ATTR, STORE_ATTR,
         * CALL_METHOD, LOAD_GLOBAL, STORE_GLOBAL, the src2 field is a
         * constant pool index — do NOT propagate into src2 for those. */
        {
            int src1_is_temp = 1;
            if (ins->op == IR_CONST_INT || ins->op == IR_CONST_STR ||
                ins->op == IR_CONST_FLOAT || ins->op == IR_CONST_BOOL ||
                ins->op == IR_CONST_NONE ||
                ins->op == IR_LOAD_GLOBAL || ins->op == IR_LOAD_LOCAL ||
                ins->op == IR_DEL_LOCAL) {
                src1_is_temp = 0;
            }
            if (src1_is_temp &&
                ins->src1 >= 0 && ins->src1 < 1024 && copy_src[ins->src1] >= 0) {
                ins->src1 = copy_src[ins->src1];
            }
        }
        {
            int src2_is_temp = 1;
            if (ins->op == IR_LOAD_ATTR || ins->op == IR_STORE_ATTR ||
                ins->op == IR_CALL_METHOD ||
                ins->op == IR_LOAD_GLOBAL || ins->op == IR_STORE_GLOBAL) {
                src2_is_temp = 0;
            }
            if (src2_is_temp &&
                ins->src2 >= 0 && ins->src2 < 1024 && copy_src[ins->src2] >= 0) {
                ins->src2 = copy_src[ins->src2];
            }
        }
        /* For STORE_SUBSCRIPT, dest is also a read operand */
        if (ins->op == IR_STORE_SUBSCRIPT &&
            ins->dest >= 0 && ins->dest < 1024 && copy_src[ins->dest] >= 0) {
            ins->dest = copy_src[ins->dest];
        }

        /* Any non-copy instruction that writes to dest invalidates that copy */
        if (ins->dest >= 0 && ins->dest < 1024) {
            if (ins->op != IR_POS) {
                copy_src[ins->dest] = -1;
            }
            /* Also invalidate any copies that pointed TO this dest,
             * since its value just changed */
            for (int i = 0; i < 1024; i++) {
                if (copy_src[i] == ins->dest && i != ins->dest) {
                    copy_src[i] = -1;
                }
            }
        }

        /* Labels and jumps invalidate all copies (control flow merge point) */
        if (ins->op == IR_LABEL || ins->op == IR_JUMP ||
            ins->op == IR_JUMP_IF_TRUE || ins->op == IR_JUMP_IF_FALSE) {
            for (int i = 0; i < 1024; i++) {
                copy_src[i] = -1;
            }
        }

        ins = next;
    }
}

/* ================================================================ */
/* PASS 4: Strength Reduction                                        */
/* ================================================================ */

void IROptimizer::strength_reduce(IRFunc *func)
{
    if (!func || !current_mod) return;
    reset_const_tracking();

    /* First pass: collect known constants */
    IRInstr *ins = func->first;
    while (ins) {
        if (ins->op == IR_CONST_INT && ins->dest >= 0 &&
            ins->src1 >= 0 && ins->src1 < current_mod->num_constants) {
            IRConst *c = &current_mod->constants[ins->src1];
            if (c->kind == IRConst::CONST_INT) {
                set_const(ins->dest, c);
            }
        }
        if (ins->op == IR_CONST_BOOL && ins->dest >= 0) {
            IRConst cv;
            cv.kind = IRConst::CONST_INT;
            cv.int_val = ins->src1;
            set_const(ins->dest, &cv);
        }
        ins = ins->next;
    }

    /* Second pass: apply strength reductions */
    ins = func->first;
    while (ins) {
        IRInstr *next = ins->next;

        if (ins->op == IR_MUL) {
            ConstValue *lc = get_const(ins->src1);
            ConstValue *rc = get_const(ins->src2);

            /* MUL by 0 -> CONST_INT 0 */
            if (rc && rc->value.kind == IRConst::CONST_INT &&
                rc->value.int_val == 0) {
                int ci = -1;
                for (int i = 0; i < current_mod->num_constants; i++) {
                    if (current_mod->constants[i].kind == IRConst::CONST_INT &&
                        current_mod->constants[i].int_val == 0) {
                        ci = i;
                        break;
                    }
                }
                if (ci < 0 && current_mod->num_constants < current_mod->max_constants) {
                    ci = current_mod->num_constants;
                    current_mod->constants[ci].kind = IRConst::CONST_INT;
                    current_mod->constants[ci].int_val = 0;
                    current_mod->num_constants++;
                }
                if (ci >= 0) {
                    ins->op = IR_CONST_INT;
                    ins->src1 = ci;
                    ins->src2 = -1;
                }
            }
            /* MUL by 0 (left operand) */
            else if (lc && lc->value.kind == IRConst::CONST_INT &&
                     lc->value.int_val == 0) {
                int ci = -1;
                for (int i = 0; i < current_mod->num_constants; i++) {
                    if (current_mod->constants[i].kind == IRConst::CONST_INT &&
                        current_mod->constants[i].int_val == 0) {
                        ci = i;
                        break;
                    }
                }
                if (ci < 0 && current_mod->num_constants < current_mod->max_constants) {
                    ci = current_mod->num_constants;
                    current_mod->constants[ci].kind = IRConst::CONST_INT;
                    current_mod->constants[ci].int_val = 0;
                    current_mod->num_constants++;
                }
                if (ci >= 0) {
                    ins->op = IR_CONST_INT;
                    ins->src1 = ci;
                    ins->src2 = -1;
                }
            }
            /* MUL by 1 -> identity (copy src to dest via POS) */
            else if (rc && rc->value.kind == IRConst::CONST_INT &&
                     rc->value.int_val == 1) {
                ins->op = IR_POS;
                ins->src2 = -1;
            }
            else if (lc && lc->value.kind == IRConst::CONST_INT &&
                     lc->value.int_val == 1) {
                ins->op = IR_POS;
                ins->src1 = ins->src2;
                ins->src2 = -1;
            }
            /* MUL by 2 -> ADD src, src */
            else if (rc && rc->value.kind == IRConst::CONST_INT &&
                     rc->value.int_val == 2) {
                ins->op = IR_ADD;
                ins->src2 = ins->src1;
            }
            else if (lc && lc->value.kind == IRConst::CONST_INT &&
                     lc->value.int_val == 2) {
                ins->op = IR_ADD;
                ins->src1 = ins->src2;
                ins->src2 = ins->src1;
            }
            /* MUL by power of 2 -> SHL */
            else if (rc && rc->value.kind == IRConst::CONST_INT) {
                int shift;
                if (is_power_of_two(rc->value.int_val, &shift) && shift > 1) {
                    /* Create a const for the shift amount */
                    int ci = -1;
                    for (int i = 0; i < current_mod->num_constants; i++) {
                        if (current_mod->constants[i].kind == IRConst::CONST_INT &&
                            current_mod->constants[i].int_val == (long)shift) {
                            ci = i;
                            break;
                        }
                    }
                    if (ci < 0 && current_mod->num_constants < current_mod->max_constants) {
                        ci = current_mod->num_constants;
                        current_mod->constants[ci].kind = IRConst::CONST_INT;
                        current_mod->constants[ci].int_val = (long)shift;
                        current_mod->num_constants++;
                    }
                    if (ci >= 0) {
                        /* Check if src2 temp is used elsewhere — if so,
                         * patching its CONST_INT definition would corrupt
                         * those other uses (e.g. list literal elements). */
                        int safe_to_patch = 1;
                        {
                            IRInstr *chk;
                            for (chk = func->first; chk; chk = chk->next) {
                                if (chk != ins && instr_reads_temp(chk, ins->src2)) {
                                    safe_to_patch = 0;
                                    break;
                                }
                            }
                        }
                        if (safe_to_patch) {
                            ins->op = IR_SHL;
                            /* Walk backwards to patch src2's CONST_INT def */
                            IRInstr *def = ins->prev;
                            while (def) {
                                if (def->dest == ins->src2 &&
                                    def->op == IR_CONST_INT) {
                                    int sci = -1;
                                    for (int si = 0; si < current_mod->num_constants; si++) {
                                        if (current_mod->constants[si].kind == IRConst::CONST_INT &&
                                            current_mod->constants[si].int_val == (long)shift) {
                                            sci = si;
                                            break;
                                        }
                                    }
                                    if (sci >= 0) {
                                        def->src1 = sci;
                                    }
                                    break;
                                }
                                def = def->prev;
                            }
                        }
                        /* If not safe, leave as IR_MUL */
                    }
                }
            }
        }

        /* ADD by 0 -> identity */
        if (ins->op == IR_ADD) {
            ConstValue *lc = get_const(ins->src1);
            ConstValue *rc = get_const(ins->src2);

            if (rc && rc->value.kind == IRConst::CONST_INT &&
                rc->value.int_val == 0) {
                ins->op = IR_POS;
                ins->src2 = -1;
            }
            else if (lc && lc->value.kind == IRConst::CONST_INT &&
                     lc->value.int_val == 0) {
                ins->op = IR_POS;
                ins->src1 = ins->src2;
                ins->src2 = -1;
            }
        }

        /* SUB 0 -> identity */
        if (ins->op == IR_SUB) {
            ConstValue *rc = get_const(ins->src2);
            if (rc && rc->value.kind == IRConst::CONST_INT &&
                rc->value.int_val == 0) {
                ins->op = IR_POS;
                ins->src2 = -1;
            }
        }

        /* DIV by power of 2 -> SHR (only for known non-negative dividend
         * to be safe; for now, we do it only when the dividend is a known
         * non-negative constant) */
        if (ins->op == IR_FLOORDIV) {
            ConstValue *rc = get_const(ins->src2);
            ConstValue *lc = get_const(ins->src1);
            if (rc && rc->value.kind == IRConst::CONST_INT) {
                int shift;
                if (is_power_of_two(rc->value.int_val, &shift)) {
                    /* Only convert if we know dividend is non-negative */
                    if (lc && lc->value.kind == IRConst::CONST_INT &&
                        lc->value.int_val >= 0) {
                        /* Patch the constant temp to hold the shift amount */
                        IRInstr *def = ins->prev;
                        while (def) {
                            if (def->dest == ins->src2 &&
                                def->op == IR_CONST_INT) {
                                int sci = -1;
                                for (int si = 0; si < current_mod->num_constants; si++) {
                                    if (current_mod->constants[si].kind == IRConst::CONST_INT &&
                                        current_mod->constants[si].int_val == (long)shift) {
                                        sci = si;
                                        break;
                                    }
                                }
                                if (sci < 0 && current_mod->num_constants < current_mod->max_constants) {
                                    sci = current_mod->num_constants;
                                    current_mod->constants[sci].kind = IRConst::CONST_INT;
                                    current_mod->constants[sci].int_val = (long)shift;
                                    current_mod->num_constants++;
                                }
                                if (sci >= 0) {
                                    def->src1 = sci;
                                    ins->op = IR_SHR;
                                }
                                break;
                            }
                            def = def->prev;
                        }
                    }
                }
            }
        }

        ins = next;
    }
}

/* ================================================================ */
/* PASS 5: Peephole Optimizations                                    */
/* ================================================================ */

void IROptimizer::peephole(IRFunc *func)
{
    if (!func) return;

    int changed = 1;
    int iterations = 0;
    int max_iterations = 10;

    while (changed && iterations < max_iterations) {
        changed = 0;
        iterations++;

        IRInstr *ins = func->first;
        while (ins) {
            IRInstr *next = ins->next;

            /* Pattern 1: INCREF immediately followed by DECREF of same temp
             * -> remove both */
            if (ins->op == IR_INCREF && next && next->op == IR_DECREF &&
                ins->src1 == next->src1 && ins->src1 >= 0) {
                IRInstr *after_decref = next->next;
                remove_instr(func, next);
                remove_instr(func, ins);
                ins = after_decref;
                changed = 1;
                continue;
            }

            /* Pattern 2: DECREF immediately followed by INCREF of same temp
             * -> remove both (less common, but valid if no intervening use) */
            if (ins->op == IR_DECREF && next && next->op == IR_INCREF &&
                ins->src1 == next->src1 && ins->src1 >= 0) {
                IRInstr *after_incref = next->next;
                remove_instr(func, next);
                remove_instr(func, ins);
                ins = after_incref;
                changed = 1;
                continue;
            }

            /* Pattern 3: STORE_LOCAL slot, tX followed by LOAD_LOCAL tY, slot
             * where tX and tY are only connected through this slot
             * -> replace uses of tY with tX (handled by copy propagation,
             *    but we can also just patch the LOAD to use tX directly) */
            if (ins->op == IR_STORE_LOCAL && next &&
                next->op == IR_LOAD_LOCAL &&
                ins->dest == next->src1 && ins->dest >= 0) {
                /* Replace the LOAD_LOCAL with a POS (identity copy) */
                next->op = IR_POS;
                next->src1 = ins->src1;
                next->src2 = -1;
                changed = 1;
            }

            /* Pattern 4: JUMP to the immediately following label -> remove jump */
            if (ins->op == IR_JUMP && next && next->op == IR_LABEL &&
                ins->extra == next->extra) {
                IRInstr *after_label = next;
                remove_instr(func, ins);
                ins = after_label;
                changed = 1;
                continue;
            }

            /* Pattern 5: Two consecutive identical labels -> merge them.
             * Redirect all references from the second label to the first. */
            if (ins->op == IR_LABEL && next && next->op == IR_LABEL) {
                int old_label = next->extra;
                int new_label = ins->extra;

                /* Walk all instructions and replace references to old_label */
                IRInstr *scan = func->first;
                while (scan) {
                    if (scan != next) {
                        if ((scan->op == IR_JUMP ||
                             scan->op == IR_JUMP_IF_TRUE ||
                             scan->op == IR_JUMP_IF_FALSE ||
                             scan->op == IR_FOR_ITER ||
                             scan->op == IR_SETUP_TRY) &&
                            scan->extra == old_label) {
                            scan->extra = new_label;
                        }
                    }
                    scan = scan->next;
                }

                /* Remove the second label */
                IRInstr *after_second = next->next;
                remove_instr(func, next);
                next = after_second;
                changed = 1;
                /* Don't advance ins; check if another label follows */
                continue;
            }

            /* Pattern 6: CONST_BOOL followed by JUMP_IF_TRUE/FALSE where
             * the const is the condition -> replace with JUMP or NOP.
             * (Mostly caught by dead_code_eliminate, but catches more here.) */
            if (ins->op == IR_CONST_BOOL && next &&
                (next->op == IR_JUMP_IF_TRUE || next->op == IR_JUMP_IF_FALSE) &&
                next->src1 == ins->dest) {
                int truthy = ins->src1;

                if ((next->op == IR_JUMP_IF_TRUE && truthy) ||
                    (next->op == IR_JUMP_IF_FALSE && !truthy)) {
                    /* Jump is always taken */
                    next->op = IR_JUMP;
                    next->src1 = -1;
                    changed = 1;
                } else {
                    /* Jump is never taken */
                    IRInstr *after_jmp = next->next;
                    remove_instr(func, next);
                    next = after_jmp;
                    changed = 1;
                }
            }

            /* Pattern 7: POS dest, src where dest == src -> remove (identity) */
            if (ins->op == IR_POS && ins->dest == ins->src1) {
                IRInstr *after = ins->next;
                remove_instr(func, ins);
                ins = after;
                changed = 1;
                continue;
            }

            /* Pattern 8: Double negation: NEG dest1, src; NEG dest2, dest1
             * -> replace dest2 with POS of src */
            if (ins->op == IR_NEG && next && next->op == IR_NEG &&
                next->src1 == ins->dest) {
                next->op = IR_POS;
                next->src1 = ins->src1;
                /* Remove first NEG if dest1 is not used elsewhere */
                if (!is_temp_used_after(func, next, ins->dest)) {
                    IRInstr *after = next;
                    remove_instr(func, ins);
                    ins = after;
                    changed = 1;
                    continue;
                }
                changed = 1;
            }

            /* Pattern 9: Double NOT: NOT dest1, src; NOT dest2, dest1
             * -> replace dest2 with copy of src */
            if (ins->op == IR_NOT && next && next->op == IR_NOT &&
                next->src1 == ins->dest) {
                next->op = IR_POS;
                next->src1 = ins->src1;
                if (!is_temp_used_after(func, next, ins->dest)) {
                    IRInstr *after = next;
                    remove_instr(func, ins);
                    ins = after;
                    changed = 1;
                    continue;
                }
                changed = 1;
            }

            ins = next;
        }
    }
}
