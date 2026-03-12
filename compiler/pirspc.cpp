/*
 * pirspc.cpp - PIR type-guided specialization pass
 *
 * When both operands of a Python arithmetic/comparison operation are
 * proven to be the same primitive type (TY_INT), this pass replaces
 * the generic boxed operation with an unbox-compute-box sequence:
 *
 *   %r = py_add %a, %b      (calls pydos_int_add_ runtime function)
 *       ->
 *   %u1 = unbox_int %a      (extract obj->v.int_val)
 *   %u2 = unbox_int %b
 *   %u3 = add_i32 %u1, %u2  (native 32-bit add, no function call)
 *   %r  = box_int %u3       (create new PyDosObj with int result)
 *
 * On 8086, this eliminates far calls and DS restores (~20 instr -> 6).
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirspc.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* str_dup (local copy, same pattern as pir.cpp / pirbld.cpp)        */
/* --------------------------------------------------------------- */
static char *pir_str_dup(const char *s)
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
/* Helpers                                                           */
/* --------------------------------------------------------------- */

/* Get the proven type of a value from the type info array */
static int get_proven_type(FuncTypeResult *ti, int value_id)
{
    if (!ti || value_id < 0 || value_id >= ti->count) return -1;
    return ti->values[value_id].proven_type;
}

/* Map a generic Python arith opcode to its typed i32 equivalent */
static PIROp py_arith_to_i32(PIROp op)
{
    switch (op) {
    case PIR_PY_ADD:      return PIR_ADD_I32;
    case PIR_PY_SUB:      return PIR_SUB_I32;
    case PIR_PY_MUL:      return PIR_MUL_I32;
    /* DIV and MOD need runtime zero-check — cannot specialize to raw CPU ops */
    case PIR_PY_NEG:      return PIR_NEG_I32;
    default:              return PIR_NOP;
    }
}

/* Map a generic Python comparison opcode to its typed i32 equivalent */
static PIROp py_cmp_to_i32(PIROp op)
{
    switch (op) {
    case PIR_PY_CMP_EQ: return PIR_CMP_I32_EQ;
    case PIR_PY_CMP_NE: return PIR_CMP_I32_NE;
    case PIR_PY_CMP_LT: return PIR_CMP_I32_LT;
    case PIR_PY_CMP_LE: return PIR_CMP_I32_LE;
    case PIR_PY_CMP_GT: return PIR_CMP_I32_GT;
    case PIR_PY_CMP_GE: return PIR_CMP_I32_GE;
    default:             return PIR_NOP;
    }
}

/* Check if an opcode is a binary arithmetic op */
static int is_binary_arith(PIROp op)
{
    switch (op) {
    case PIR_PY_ADD: case PIR_PY_SUB: case PIR_PY_MUL:
        return 1;
    default:
        return 0;
    }
}

/* Check if an opcode is a comparison op */
static int is_comparison(PIROp op)
{
    switch (op) {
    case PIR_PY_CMP_EQ: case PIR_PY_CMP_NE:
    case PIR_PY_CMP_LT: case PIR_PY_CMP_LE:
    case PIR_PY_CMP_GT: case PIR_PY_CMP_GE:
        return 1;
    default:
        return 0;
    }
}

/* Check if a value is already unboxed (i.e., produced by an i32 op) */
static int is_already_unboxed(PIRFunction *func, int value_id)
{
    int bi;
    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            if (inst->result.id == value_id) {
                switch (inst->op) {
                case PIR_UNBOX_INT:
                case PIR_ADD_I32: case PIR_SUB_I32:
                case PIR_MUL_I32: case PIR_DIV_I32:
                case PIR_MOD_I32: case PIR_NEG_I32:
                case PIR_CMP_I32_EQ: case PIR_CMP_I32_NE:
                case PIR_CMP_I32_LT: case PIR_CMP_I32_LE:
                case PIR_CMP_I32_GT: case PIR_CMP_I32_GE:
                    return 1;
                default:
                    return 0;
                }
            }
        }
    }
    return 0;
}

/* Insert a new instruction before `before` in `block`.
 * Returns the newly inserted instruction. */
static PIRInst *insert_before(PIRBlock *block, PIRInst *before, PIRInst *newinst)
{
    newinst->next = before;
    newinst->prev = before->prev;
    if (before->prev) {
        before->prev->next = newinst;
    } else {
        block->first = newinst;
    }
    before->prev = newinst;
    block->inst_count++;
    return newinst;
}

/* --------------------------------------------------------------- */
/* Main specialization pass                                          */
/* --------------------------------------------------------------- */

int pir_specialize(PIRFunction *func)
{
    int specialized = 0;
    int bi;
    FuncTypeResult *ti;

    if (!func || func->blocks.size() == 0) return 0;
    ti = func->type_info;
    if (!ti) return 0;

    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst, *next;

        for (inst = block->first; inst; inst = next) {
            next = inst->next;

            /* Binary arithmetic: PIR_PY_ADD etc. */
            if (is_binary_arith(inst->op)) {
                int t0 = get_proven_type(ti, inst->operands[0].id);
                int t1 = get_proven_type(ti, inst->operands[1].id);
                PIROp i32_op = py_arith_to_i32(inst->op);

                if (t0 == TY_INT && t1 == TY_INT && i32_op != PIR_NOP) {
                    /* Create: %u0 = unbox_int %operands[0] */
                    PIRValue u0;
                    PIRInst *unbox0;
                    PIRValue u1;
                    PIRInst *unbox1;
                    PIRValue i32_result;
                    PIRValue old_result;

                    if (!is_already_unboxed(func, inst->operands[0].id)) {
                        u0 = pir_func_alloc_value(func, PIR_TYPE_I32);
                        unbox0 = pir_inst_new(PIR_UNBOX_INT);
                        unbox0->result = u0;
                        unbox0->operands[0] = inst->operands[0];
                        unbox0->num_operands = 1;
                        unbox0->line = inst->line;
                        insert_before(block, inst, unbox0);
                    } else {
                        u0 = inst->operands[0];
                    }

                    /* Create: %u1 = unbox_int %operands[1] */
                    if (!is_already_unboxed(func, inst->operands[1].id)) {
                        u1 = pir_func_alloc_value(func, PIR_TYPE_I32);
                        unbox1 = pir_inst_new(PIR_UNBOX_INT);
                        unbox1->result = u1;
                        unbox1->operands[0] = inst->operands[1];
                        unbox1->num_operands = 1;
                        unbox1->line = inst->line;
                        insert_before(block, inst, unbox1);
                    } else {
                        u1 = inst->operands[1];
                    }

                    /* Replace instruction in-place with i32 op */
                    i32_result = pir_func_alloc_value(func, PIR_TYPE_I32);
                    old_result = inst->result;

                    inst->op = i32_op;
                    inst->result = i32_result;
                    inst->operands[0] = u0;
                    inst->operands[1] = u1;

                    /* Create: %old_result = box_int %i32_result */
                    {
                        PIRInst *box_inst = pir_inst_new(PIR_BOX_INT);
                        box_inst->result = old_result;
                        box_inst->operands[0] = i32_result;
                        box_inst->num_operands = 1;
                        box_inst->line = inst->line;

                        /* Insert after the i32 op */
                        box_inst->prev = inst;
                        box_inst->next = inst->next;
                        if (inst->next) {
                            inst->next->prev = box_inst;
                        } else {
                            block->last = box_inst;
                        }
                        inst->next = box_inst;
                        block->inst_count++;

                        next = box_inst->next;
                    }

                    specialized++;
                }
            }
            /* Unary negation: PIR_PY_NEG */
            else if (inst->op == PIR_PY_NEG) {
                int t0 = get_proven_type(ti, inst->operands[0].id);

                if (t0 == TY_INT) {
                    PIRValue u0;
                    PIRInst *unbox0;
                    PIRValue i32_result;
                    PIRValue old_result;

                    if (!is_already_unboxed(func, inst->operands[0].id)) {
                        u0 = pir_func_alloc_value(func, PIR_TYPE_I32);
                        unbox0 = pir_inst_new(PIR_UNBOX_INT);
                        unbox0->result = u0;
                        unbox0->operands[0] = inst->operands[0];
                        unbox0->num_operands = 1;
                        unbox0->line = inst->line;
                        insert_before(block, inst, unbox0);
                    } else {
                        u0 = inst->operands[0];
                    }

                    i32_result = pir_func_alloc_value(func, PIR_TYPE_I32);
                    old_result = inst->result;

                    inst->op = PIR_NEG_I32;
                    inst->result = i32_result;
                    inst->operands[0] = u0;

                    {
                        PIRInst *box_inst = pir_inst_new(PIR_BOX_INT);
                        box_inst->result = old_result;
                        box_inst->operands[0] = i32_result;
                        box_inst->num_operands = 1;
                        box_inst->line = inst->line;
                        box_inst->prev = inst;
                        box_inst->next = inst->next;
                        if (inst->next) inst->next->prev = box_inst;
                        else block->last = box_inst;
                        inst->next = box_inst;
                        block->inst_count++;
                        next = box_inst->next;
                    }

                    specialized++;
                }
            }
            /* Comparisons */
            else if (is_comparison(inst->op)) {
                int t0 = get_proven_type(ti, inst->operands[0].id);
                int t1 = get_proven_type(ti, inst->operands[1].id);
                PIROp i32_op = py_cmp_to_i32(inst->op);

                if (t0 == TY_INT && t1 == TY_INT && i32_op != PIR_NOP) {
                    PIRValue u0;
                    PIRInst *unbox0;
                    PIRValue u1;
                    PIRInst *unbox1;
                    PIRValue cmp_result;
                    PIRValue old_result;

                    if (!is_already_unboxed(func, inst->operands[0].id)) {
                        u0 = pir_func_alloc_value(func, PIR_TYPE_I32);
                        unbox0 = pir_inst_new(PIR_UNBOX_INT);
                        unbox0->result = u0;
                        unbox0->operands[0] = inst->operands[0];
                        unbox0->num_operands = 1;
                        unbox0->line = inst->line;
                        insert_before(block, inst, unbox0);
                    } else {
                        u0 = inst->operands[0];
                    }

                    if (!is_already_unboxed(func, inst->operands[1].id)) {
                        u1 = pir_func_alloc_value(func, PIR_TYPE_I32);
                        unbox1 = pir_inst_new(PIR_UNBOX_INT);
                        unbox1->result = u1;
                        unbox1->operands[0] = inst->operands[1];
                        unbox1->num_operands = 1;
                        unbox1->line = inst->line;
                        insert_before(block, inst, unbox1);
                    } else {
                        u1 = inst->operands[1];
                    }

                    /* Comparisons produce a bool that is used for branching.
                     * The result is already treated as a truth value by
                     * COND_BRANCH, so we just replace in-place without
                     * box/unbox — the lowerer will handle CMP_I32 -> IR_CMP. */
                    cmp_result = pir_func_alloc_value(func, PIR_TYPE_BOOL);
                    old_result = inst->result;

                    inst->op = i32_op;
                    inst->result = cmp_result;
                    inst->operands[0] = u0;
                    inst->operands[1] = u1;

                    /* Create: %old_result = box_bool %cmp_result
                     * so downstream uses of the comparison result still work */
                    {
                        PIRInst *box_inst = pir_inst_new(PIR_BOX_BOOL);
                        box_inst->result = old_result;
                        box_inst->operands[0] = cmp_result;
                        box_inst->num_operands = 1;
                        box_inst->line = inst->line;
                        box_inst->prev = inst;
                        box_inst->next = inst->next;
                        if (inst->next) inst->next->prev = box_inst;
                        else block->last = box_inst;
                        inst->next = box_inst;
                        block->inst_count++;
                        next = box_inst->next;
                    }

                    specialized++;
                }
            }
        }
    }

    return specialized;
}

/* --------------------------------------------------------------- */
/* Devirtualization Pass                                             */
/* --------------------------------------------------------------- */

/* Find a vtable entry by class name. Returns index or -1. */
static int find_vtable(PIRModule *mod, const char *class_name)
{
    int i;
    if (!mod || !class_name) return -1;
    for (i = 0; i < mod->num_vtables; i++) {
        if (mod->vtables[i].class_name &&
            strcmp(mod->vtables[i].class_name, class_name) == 0) {
            return i;
        }
    }
    return -1;
}

/* Find a method in a vtable by python name. Returns mangled name or 0. */
static const char *find_method(PIRVTableInfo *vt, const char *method_name)
{
    int i;
    if (!vt || !method_name) return 0;
    for (i = 0; i < vt->num_methods; i++) {
        if (vt->methods[i].python_name &&
            strcmp(vt->methods[i].python_name, method_name) == 0) {
            return vt->methods[i].mangled_name;
        }
    }
    return 0;
}

/* Find a method traversing the inheritance chain. */
static const char *find_method_inherited(PIRModule *mod, const char *class_name,
                                         const char *method_name)
{
    const char *mangled;
    int vti;

    vti = find_vtable(mod, class_name);
    if (vti < 0) return 0;

    /* Check this class first */
    mangled = find_method(&mod->vtables[vti], method_name);
    if (mangled) return mangled;

    /* Check base class */
    if (mod->vtables[vti].base_class_name) {
        return find_method_inherited(mod, mod->vtables[vti].base_class_name,
                                     method_name);
    }
    return 0;
}

/* Check if any class that inherits from class_name overrides method_name.
 * Walks the full vtable list to find subclasses (direct and transitive). */
static int method_overridden_in_subclass(PIRModule *mod,
                                          const char *class_name,
                                          const char *method_name)
{
    int i;
    if (!mod || !class_name || !method_name) return 0;

    for (i = 0; i < mod->num_vtables; i++) {
        PIRVTableInfo *vt = &mod->vtables[i];

        /* Check direct inheritance */
        if (vt->base_class_name &&
            strcmp(vt->base_class_name, class_name) == 0) {
            /* Direct subclass — check if it overrides the method */
            if (find_method(vt, method_name)) return 1;
            /* Also check transitive subclasses */
            if (method_overridden_in_subclass(mod, vt->class_name, method_name))
                return 1;
        }

        /* Check extra bases (multiple inheritance) */
        {
            int eb;
            for (eb = 0; eb < vt->num_extra_bases; eb++) {
                if (vt->extra_bases[eb] &&
                    strcmp(vt->extra_bases[eb], class_name) == 0) {
                    if (find_method(vt, method_name)) return 1;
                    if (method_overridden_in_subclass(mod, vt->class_name, method_name))
                        return 1;
                }
            }
        }
    }
    return 0;
}

int pir_devirtualize(PIRFunction *func, PIRModule *mod)
{
    int devirtualized = 0;
    int bi;

    if (!func || !mod || func->blocks.size() == 0) return 0;

    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst, *next;

        for (inst = block->first; inst; inst = next) {
            next = inst->next;

            if (inst->op != PIR_CALL_METHOD) continue;
            if (!inst->type_hint) continue;
            if (inst->type_hint->kind != TY_CLASS) continue;
            if (!inst->type_hint->name) continue;
            if (!inst->str_val) continue;

            {
                const char *class_name = inst->type_hint->name;
                const char *method_name = inst->str_val;
                const char *mangled;
                int argc = (int)inst->int_val;
                PIRInst *self_push;
                PIRInst *insert_point;
                int args_found;
                PIRInst *scan;

                mangled = find_method_inherited(mod, class_name, method_name);
                if (!mangled) continue;

                /* Don't devirtualize if any subclass overrides this method */
                if (method_overridden_in_subclass(mod, class_name, method_name))
                    continue;

                /* Find where to insert PUSH_ARG for self.
                 * Walk backward to find the first PUSH_ARG for this call. */
                insert_point = inst;
                args_found = 0;
                scan = inst->prev;
                while (scan && args_found < argc) {
                    if (scan->op == PIR_PUSH_ARG) {
                        args_found++;
                        insert_point = scan;
                    }
                    scan = scan->prev;
                }

                /* Insert PUSH_ARG self before the first existing PUSH_ARG
                 * (or before the CALL_METHOD itself if argc==0) */
                self_push = pir_inst_new(PIR_PUSH_ARG);
                self_push->operands[0] = inst->operands[0];
                self_push->num_operands = 1;
                self_push->line = inst->line;

                self_push->next = insert_point;
                self_push->prev = insert_point->prev;
                if (insert_point->prev) {
                    insert_point->prev->next = self_push;
                } else {
                    block->first = self_push;
                }
                insert_point->prev = self_push;
                block->inst_count++;

                /* Replace CALL_METHOD with PIR_CALL */
                inst->op = PIR_CALL;
                inst->str_val = pir_str_dup(mangled);
                inst->int_val = argc + 1;  /* +1 for self */
                inst->num_operands = 0;

                devirtualized++;
            }
        }
    }

    return devirtualized;
}
