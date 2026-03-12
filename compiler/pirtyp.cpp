/*
 * pirtyp.cpp - PIR type inference pass
 *
 * Walks PIR instructions in RPO order and propagates proven types
 * through SSA definitions. After mem2reg, phi nodes may lose type
 * info; this pass recovers it by examining all operands.
 *
 * Type lattice: TY_INT, TY_FLOAT, TY_STR, TY_BOOL, TY_NONE, -1 (unknown)
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "pirtyp.h"
#include "types.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Unknown type sentinel */
#define TYPE_UNKNOWN (-1)

/* --------------------------------------------------------------- */
/* Helpers                                                           */
/* --------------------------------------------------------------- */

static int pir_type_to_proven(PIRTypeKind tk)
{
    switch (tk) {
    case PIR_TYPE_I32:  return TY_INT;
    case PIR_TYPE_F64:  return TY_FLOAT;
    case PIR_TYPE_BOOL: return TY_BOOL;
    default:            return TYPE_UNKNOWN;
    }
}

/* Merge two proven types: same -> that type, different -> unknown */
static int merge_types(int a, int b)
{
    if (a == TYPE_UNKNOWN) return b;
    if (b == TYPE_UNKNOWN) return a;
    if (a == b) return a;
    return TYPE_UNKNOWN;
}

/* Return type name for debug output */
static const char *proven_type_name(int t)
{
    switch (t) {
    case TY_INT:   return "int";
    case TY_FLOAT: return "float";
    case TY_STR:   return "str";
    case TY_BOOL:  return "bool";
    case TY_NONE:  return "None";
    case TY_LIST:  return "list";
    case TY_DICT:  return "dict";
    default:       return "?";
    }
}

/* Check if a call target is a known builtin with a known return type.
 * Uses stdlib registry if available, otherwise returns TYPE_UNKNOWN. */
static int builtin_return_type(const char *name, StdlibRegistry *reg)
{
    if (!name) return TYPE_UNKNOWN;
    if (reg && reg->is_loaded()) {
        const BuiltinFuncEntry *f = reg->find_builtin(name);
        if (f) return f->ret_type_kind;
    }
    return TYPE_UNKNOWN;
}

/* Infer type from a type_hint (SemanticAnalyzer's TypeInfo) */
static int type_from_hint(TypeInfo *hint)
{
    if (!hint) return TYPE_UNKNOWN;
    switch (hint->kind) {
    case TY_INT:   return TY_INT;
    case TY_FLOAT: return TY_FLOAT;
    case TY_STR:   return TY_STR;
    case TY_BOOL:  return TY_BOOL;
    case TY_NONE:  return TY_NONE;
    case TY_LIST:  return TY_LIST;
    case TY_DICT:  return TY_DICT;
    default:       return TYPE_UNKNOWN;
    }
}

/* --------------------------------------------------------------- */
/* Main type inference                                               */
/* --------------------------------------------------------------- */

void pir_type_infer(PIRFunction *func, DomInfo *dom,
                    StdlibRegistry *stdlib_reg)
{
    int num_values, i, bi, changed, iteration;
    FuncTypeResult *result;
    ValueTypeInfo *vt;

    if (!func || func->blocks.size() == 0) return;

    num_values = func->next_value_id;
    if (num_values <= 0) return;

    /* Allocate result */
    result = (FuncTypeResult *)malloc(sizeof(FuncTypeResult));
    if (!result) return;
    vt = (ValueTypeInfo *)malloc(sizeof(ValueTypeInfo) * num_values);
    if (!vt) { free(result); return; }

    /* Initialize all values as unknown */
    for (i = 0; i < num_values; i++) {
        vt[i].value_id = i;
        vt[i].proven_type = TYPE_UNKNOWN;
        vt[i].is_constant = 0;
    }

    /* Initialize parameter types from type_hints */
    for (i = 0; i < func->params.size(); i++) {
        PIRValue p = func->params[i];
        if (p.id >= 0 && p.id < num_values) {
            vt[p.id].proven_type = pir_type_to_proven(p.type);
        }
    }

    /* Iterative dataflow: walk blocks in RPO, repeat until stable */
    changed = 1;
    iteration = 0;
    while (changed && iteration < 10) {
        changed = 0;
        iteration++;

        for (bi = 0; bi < dom->num_blocks; bi++) {
            int block_id = dom->rpo_order[bi];
            PIRBlock *block = 0;
            PIRInst *inst;
            int bj;

            /* Find block by id */
            for (bj = 0; bj < func->blocks.size(); bj++) {
                if (func->blocks[bj]->id == block_id) {
                    block = func->blocks[bj];
                    break;
                }
            }
            if (!block) continue;

            for (inst = block->first; inst; inst = inst->next) {
                int new_type = TYPE_UNKNOWN;
                int rid = inst->result.id;

                if (rid < 0 || rid >= num_values) continue;

                switch (inst->op) {
                /* Constants have known types */
                case PIR_CONST_INT:
                    new_type = TY_INT;
                    vt[rid].is_constant = 1;
                    break;
                case PIR_CONST_FLOAT:
                    new_type = TY_FLOAT;
                    vt[rid].is_constant = 1;
                    break;
                case PIR_CONST_BOOL:
                    new_type = TY_BOOL;
                    vt[rid].is_constant = 1;
                    break;
                case PIR_CONST_STR:
                    new_type = TY_STR;
                    vt[rid].is_constant = 1;
                    break;
                case PIR_CONST_NONE:
                    new_type = TY_NONE;
                    vt[rid].is_constant = 1;
                    break;

                /* Typed arithmetic always produces known types */
                case PIR_ADD_I32: case PIR_SUB_I32:
                case PIR_MUL_I32: case PIR_DIV_I32:
                case PIR_MOD_I32: case PIR_NEG_I32:
                    new_type = TY_INT;
                    break;
                case PIR_ADD_F64: case PIR_SUB_F64:
                case PIR_MUL_F64: case PIR_DIV_F64:
                case PIR_NEG_F64:
                    new_type = TY_FLOAT;
                    break;

                /* Typed comparisons produce bool */
                case PIR_CMP_I32_EQ: case PIR_CMP_I32_NE:
                case PIR_CMP_I32_LT: case PIR_CMP_I32_LE:
                case PIR_CMP_I32_GT: case PIR_CMP_I32_GE:
                    new_type = TY_BOOL;
                    break;

                /* Box operations produce known types */
                case PIR_BOX_INT:   new_type = TY_INT;   break;
                case PIR_BOX_FLOAT: new_type = TY_FLOAT; break;
                case PIR_BOX_BOOL:  new_type = TY_BOOL;  break;

                /* Unbox operations produce known types */
                case PIR_UNBOX_INT:   new_type = TY_INT;   break;
                case PIR_UNBOX_FLOAT: new_type = TY_FLOAT; break;

                /* Generic Python arithmetic: if both operands are same numeric type */
                case PIR_PY_ADD: case PIR_PY_SUB:
                case PIR_PY_MUL: {
                    int t0 = TYPE_UNKNOWN, t1 = TYPE_UNKNOWN;
                    if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                        t0 = vt[inst->operands[0].id].proven_type;
                    if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                        t1 = vt[inst->operands[1].id].proven_type;
                    if (t0 == TY_INT && t1 == TY_INT)
                        new_type = TY_INT;
                    else if (t0 == TY_FLOAT && t1 == TY_FLOAT)
                        new_type = TY_FLOAT;
                    else if ((t0 == TY_INT && t1 == TY_FLOAT) ||
                             (t0 == TY_FLOAT && t1 == TY_INT))
                        new_type = TY_FLOAT;
                    else if (inst->op == PIR_PY_ADD && t0 == TY_STR && t1 == TY_STR)
                        new_type = TY_STR;
                    break;
                }

                case PIR_PY_DIV:
                    /* Python / always returns float */
                    new_type = TY_FLOAT;
                    break;

                case PIR_PY_FLOORDIV: case PIR_PY_MOD: {
                    int t0 = TYPE_UNKNOWN, t1 = TYPE_UNKNOWN;
                    if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                        t0 = vt[inst->operands[0].id].proven_type;
                    if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                        t1 = vt[inst->operands[1].id].proven_type;
                    if (t0 == TY_INT && t1 == TY_INT)
                        new_type = TY_INT;
                    break;
                }

                case PIR_PY_POW: {
                    int t0 = TYPE_UNKNOWN, t1 = TYPE_UNKNOWN;
                    if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                        t0 = vt[inst->operands[0].id].proven_type;
                    if (inst->operands[1].id >= 0 && inst->operands[1].id < num_values)
                        t1 = vt[inst->operands[1].id].proven_type;
                    if (t0 == TY_INT && t1 == TY_INT)
                        new_type = TY_INT;
                    break;
                }

                case PIR_PY_NEG: case PIR_PY_POS: {
                    int t0 = TYPE_UNKNOWN;
                    if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                        t0 = vt[inst->operands[0].id].proven_type;
                    if (t0 == TY_INT) new_type = TY_INT;
                    else if (t0 == TY_FLOAT) new_type = TY_FLOAT;
                    break;
                }

                case PIR_PY_NOT:
                    new_type = TY_BOOL;
                    break;

                /* Comparisons always produce bool */
                case PIR_PY_CMP_EQ: case PIR_PY_CMP_NE:
                case PIR_PY_CMP_LT: case PIR_PY_CMP_LE:
                case PIR_PY_CMP_GT: case PIR_PY_CMP_GE:
                case PIR_PY_IS:     case PIR_PY_IS_NOT:
                case PIR_PY_IN:     case PIR_PY_NOT_IN:
                    new_type = TY_BOOL;
                    break;

                /* Bitwise ops on int produce int */
                case PIR_PY_BIT_AND: case PIR_PY_BIT_OR:
                case PIR_PY_BIT_XOR: case PIR_PY_BIT_NOT:
                case PIR_PY_LSHIFT:  case PIR_PY_RSHIFT: {
                    int t0 = TYPE_UNKNOWN;
                    if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                        t0 = vt[inst->operands[0].id].proven_type;
                    if (t0 == TY_INT) new_type = TY_INT;
                    break;
                }

                /* String operations */
                case PIR_STR_CONCAT:
                case PIR_STR_FORMAT:
                    new_type = TY_STR;
                    break;

                /* Collection builders */
                case PIR_LIST_NEW:
                case PIR_BUILD_LIST:
                    new_type = TY_LIST;
                    break;
                case PIR_DICT_NEW:
                case PIR_BUILD_DICT:
                    new_type = TY_DICT;
                    break;

                /* Phi: merge all incoming types */
                case PIR_PHI: {
                    int merged = TYPE_UNKNOWN;
                    int first = 1;
                    int ei;
                    for (ei = 0; ei < inst->extra.phi.count; ei++) {
                        int vid = inst->extra.phi.entries[ei].value.id;
                        if (vid >= 0 && vid < num_values) {
                            int et = vt[vid].proven_type;
                            if (first) {
                                merged = et;
                                first = 0;
                            } else {
                                merged = merge_types(merged, et);
                            }
                        }
                    }
                    new_type = merged;
                    break;
                }

                /* Calls: check for known builtins */
                case PIR_CALL:
                    new_type = builtin_return_type(inst->str_val, stdlib_reg);
                    /* Fall back to type_hint from sema */
                    if (new_type == TYPE_UNKNOWN)
                        new_type = type_from_hint(inst->type_hint);
                    break;

                /* Method calls: use type_hint */
                case PIR_CALL_METHOD:
                    new_type = type_from_hint(inst->type_hint);
                    break;

                /* Load: inherit from type_hint if available */
                case PIR_LOAD:
                case PIR_LOAD_GLOBAL:
                    new_type = type_from_hint(inst->type_hint);
                    break;

                /* Iteration */
                case PIR_FOR_ITER:
                    new_type = type_from_hint(inst->type_hint);
                    break;

                /* Exception */
                case PIR_EXC_MATCH:
                    new_type = TY_BOOL;
                    break;

                /* Slice produces same type as input (str->str, list->list) */
                case PIR_SLICE: {
                    int t0 = TYPE_UNKNOWN;
                    if (inst->operands[0].id >= 0 && inst->operands[0].id < num_values)
                        t0 = vt[inst->operands[0].id].proven_type;
                    new_type = t0;
                    break;
                }

                default:
                    /* Try type_hint as fallback */
                    new_type = type_from_hint(inst->type_hint);
                    break;
                }

                /* Update if changed */
                if (new_type != vt[rid].proven_type) {
                    /* Only allow refinement: unknown -> known, never regress */
                    if (vt[rid].proven_type == TYPE_UNKNOWN ||
                        new_type == TYPE_UNKNOWN) {
                        int final_type = merge_types(vt[rid].proven_type, new_type);
                        if (final_type != vt[rid].proven_type) {
                            vt[rid].proven_type = final_type;
                            changed = 1;
                        }
                    }
                }
            }
        }
    }

    result->values = vt;
    result->count = num_values;

    /* Free previous result if any */
    if (func->type_info) {
        free(func->type_info->values);
        free(func->type_info);
    }
    func->type_info = result;
}

/* --------------------------------------------------------------- */
/* Debug dump                                                        */
/* --------------------------------------------------------------- */

void pir_dump_types(PIRFunction *func, FILE *out)
{
    int i;
    FuncTypeResult *ti;

    if (!func || !out) return;
    ti = func->type_info;
    if (!ti) {
        fprintf(out, "  (no type info)\n");
        return;
    }

    fprintf(out, "function @%s type inference (%d values):\n",
            func->name ? func->name : "?", ti->count);

    for (i = 0; i < ti->count; i++) {
        if (ti->values[i].proven_type != TYPE_UNKNOWN) {
            fprintf(out, "  %%%d: %s%s\n", i,
                    proven_type_name(ti->values[i].proven_type),
                    ti->values[i].is_constant ? " (const)" : "");
        }
    }
    fprintf(out, "\n");
}
