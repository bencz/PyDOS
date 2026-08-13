/*
 * pirbld.cpp - PIR Builder implementation (AST -> PIR)
 *
 * Mirrors IRGenerator (ir.cpp) but produces SSA-based PIR with
 * basic blocks, typed values, and explicit control flow edges.
 *
 * Variables use alloca/load/store (pre-mem2reg style).
 * Control flow uses branch/cond_branch to named blocks.
 */

#include "pirbld.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* Utility functions                                                 */
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

/* Return the runtime class named by a class-header base expression.  Generic
 * aliases retain the same runtime base as their origin, so dict[str, int]
 * must establish dict in the C3 MRO rather than silently falling back to
 * object. */
static const char *class_base_identifier(ASTNode *base)
{
    while (base && base->kind == AST_SUBSCRIPT)
        base = base->data.subscript.object;
    return base && base->kind == AST_NAME ? base->data.name.id : 0;
}

/* Build a unique symbol for a def nested inside another function.  The same
 * name may be defined by several scopes, and even twice in one scope through
 * separate branches, so the enclosing name is a prefix and not a guarantee. */
const char *PIRBuilder::qualified_nested_name(const char *enclosing,
                                              const char *py_name)
{
    char buf[256];
    char candidate[256];
    int attempt;

    if (!enclosing) return py_name;
    if ((int)(strlen(enclosing) + strlen(py_name) + 3) > (int)sizeof(buf))
        return py_name;
    strcpy(buf, enclosing);
    strcat(buf, "__");
    strcat(buf, py_name);

    strcpy(candidate, buf);
    for (attempt = 2; attempt < 1000; attempt++) {
        int taken = 0;
        int i;
        if (mod) {
            for (i = 0; i < mod->functions.size(); i++) {
                if (mod->functions[i]->name &&
                    strcmp(mod->functions[i]->name, candidate) == 0) {
                    taken = 1;
                    break;
                }
            }
        }
        if (!taken) return pir_str_dup(candidate);
        sprintf(candidate, "%s_%d", buf, attempt);
    }
    return pir_str_dup(candidate);
}

/* Find the innermost visible nested def for a Python name. */
PIRBuilder::NestedName *PIRBuilder::nested_entry(const char *py_name)
{
    NestedName *entry;
    if (!py_name) return 0;
    for (entry = nested_names; entry; entry = entry->outer) {
        if (strcmp(entry->py_name, py_name) == 0) return entry;
    }
    return 0;
}

static int pir_str_eq(const char *a, const char *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int type_has_decorated_method(TypeInfo *type, const char *name)
{
    ClassInfo *ci;
    int bi;
    if (!type || !name || !type->class_info) return 0;
    ci = type->class_info;
    {
        Symbol *member;
        for (member = ci->members; member; member = member->next) {
            if (member->name && strcmp(member->name, name) == 0)
                return member->is_decorated;
        }
    }
    for (bi = 0; bi < ci->num_bases; bi++) {
        TypeInfo base_type;
        memset(&base_type, 0, sizeof(base_type));
        base_type.class_info = ci->bases[bi];
        if (type_has_decorated_method(&base_type, name)) return 1;
    }
    return 0;
}

/* Compute semantic C3 order as ClassInfo pointers.  Argument binding must
 * select the same definition as runtime lookup and devirtualization. */
static int class_compute_c3(ClassInfo *ci, ClassInfo **result,
                            int max_result, int depth)
{
    ClassInfo *base_mros[8][32];
    int base_lengths[8];
    int positions[9];
    int result_count = 1;
    int i;

    if (!ci || !result || max_result < 1 || depth > 32) return 0;
    result[0] = ci;
    if (ci->num_bases <= 0) return result_count;
    if (ci->num_bases > 8) return 0;

    memset(positions, 0, sizeof(positions));
    for (i = 0; i < ci->num_bases; i++) {
        base_lengths[i] = class_compute_c3(ci->bases[i], base_mros[i],
                                           32, depth + 1);
        if (base_lengths[i] <= 0) return 0;
    }

    for (;;) {
        ClassInfo *candidate = 0;
        int any = 0;
        int found = 0;
        int si;
        for (si = 0; si <= ci->num_bases; si++) {
            int length = si < ci->num_bases
                         ? base_lengths[si] : ci->num_bases;
            if (positions[si] < length) any = 1;
        }
        if (!any) break;

        for (si = 0; si <= ci->num_bases && !found; si++) {
            int length = si < ci->num_bases
                         ? base_lengths[si] : ci->num_bases;
            int sj;
            int in_tail = 0;
            if (positions[si] >= length) continue;
            candidate = si < ci->num_bases
                        ? base_mros[si][positions[si]]
                        : ci->bases[positions[si]];
            for (sj = 0; sj <= ci->num_bases && !in_tail; sj++) {
                int other_length = sj < ci->num_bases
                                   ? base_lengths[sj] : ci->num_bases;
                int k;
                for (k = positions[sj] + 1; k < other_length; k++) {
                    ClassInfo *tail = sj < ci->num_bases
                                      ? base_mros[sj][k] : ci->bases[k];
                    if (tail == candidate) {
                        in_tail = 1;
                        break;
                    }
                }
            }
            if (!in_tail) found = 1;
        }
        if (!found || result_count >= max_result) return 0;
        result[result_count++] = candidate;
        for (si = 0; si <= ci->num_bases; si++) {
            int length = si < ci->num_bases
                         ? base_lengths[si] : ci->num_bases;
            if (positions[si] < length) {
                ClassInfo *head = si < ci->num_bases
                                  ? base_mros[si][positions[si]]
                                  : ci->bases[positions[si]];
                if (head == candidate) positions[si]++;
            }
        }
    }
    return result_count;
}

static ASTNode *class_method_definition(TypeInfo *type, const char *name)
{
    ClassInfo *mro[32];
    int mro_len;
    int i;
    if (!type || !type->class_info || !name) return 0;
    mro_len = class_compute_c3(type->class_info, mro, 32, 0);
    for (i = 0; i < mro_len; i++) {
        Symbol *member;
        for (member = mro[i]->members; member; member = member->next) {
            if (member->name && strcmp(member->name, name) == 0)
                return member->definition;
        }
    }
    return 0;
}

/* Map BinOp enum to PIROp (generic Python arithmetic) */
static PIROp binop_to_pirop(int op)
{
    switch (op) {
    case OP_ADD:      return PIR_PY_ADD;
    case OP_SUB:      return PIR_PY_SUB;
    case OP_MUL:      return PIR_PY_MUL;
    case OP_DIV:      return PIR_PY_DIV;
    case OP_FLOORDIV: return PIR_PY_FLOORDIV;
    case OP_MOD:      return PIR_PY_MOD;
    case OP_POW:      return PIR_PY_POW;
    case OP_LSHIFT:   return PIR_PY_LSHIFT;
    case OP_RSHIFT:   return PIR_PY_RSHIFT;
    case OP_BITOR:    return PIR_PY_BIT_OR;
    case OP_BITXOR:   return PIR_PY_BIT_XOR;
    case OP_BITAND:   return PIR_PY_BIT_AND;
    case OP_MATMUL:   return PIR_PY_MATMUL;
    default:          return PIR_PY_ADD;
    }
}

/* Map BinOp enum to inplace op index for pydos_obj_inplace() */
static int binop_to_inplace_idx(int op)
{
    switch (op) {
    case OP_ADD:      return 0;
    case OP_SUB:      return 1;
    case OP_MUL:      return 2;
    case OP_FLOORDIV: return 3;
    case OP_DIV:      return 4;
    case OP_MOD:      return 5;
    case OP_POW:      return 6;
    case OP_BITAND:   return 7;
    case OP_BITOR:    return 8;
    case OP_BITXOR:   return 9;
    case OP_LSHIFT:   return 10;
    case OP_RSHIFT:   return 11;
    case OP_MATMUL:   return 12;
    default:          return 0;
    }
}

/* Map CmpOp enum to PIROp (generic Python comparison) */
static PIROp cmpop_to_pirop(int op)
{
    switch (op) {
    case CMP_EQ:      return PIR_PY_CMP_EQ;
    case CMP_NE:      return PIR_PY_CMP_NE;
    case CMP_LT:      return PIR_PY_CMP_LT;
    case CMP_LE:      return PIR_PY_CMP_LE;
    case CMP_GT:      return PIR_PY_CMP_GT;
    case CMP_GE:      return PIR_PY_CMP_GE;
    case CMP_IS:      return PIR_PY_IS;
    case CMP_IS_NOT:  return PIR_PY_IS_NOT;
    case CMP_IN:      return PIR_PY_IN;
    case CMP_NOT_IN:  return PIR_PY_NOT_IN;
    default:          return PIR_PY_CMP_EQ;
    }
}

/* Map exception name to type code (must match runtime/pdos_exc.h enum) */
static int map_exc_name_to_code(const char *name, StdlibRegistry *reg)
{
    if (!name) return -1;
    /* All exception name→code mappings come from stdlib.idx */
    if (reg && reg->is_loaded()) {
        int code = reg->find_exc_code(name);
        if (code >= 0) return code;
    }
    return -1;
}

/* Check if a Param is a bare * separator (keyword-only marker, not *args) */
static int is_bare_star_sep(Param *p)
{
    return p->is_star && p->name && strcmp(p->name, "*") == 0;
}

/* Walk AST subtree looking for yield expressions (copied from ir.cpp) */
static int contains_yield(ASTNode *node)
{
    if (!node) return 0;
    if (node->kind == AST_YIELD_EXPR || node->kind == AST_YIELD_FROM_EXPR) {
        return 1;
    }
    /* Stop at nested function/class definitions (they have their own scope) */
    if (node->kind == AST_FUNC_DEF || node->kind == AST_CLASS_DEF ||
        node->kind == AST_LAMBDA) {
        /* But still check siblings (statements after this def) */
        return contains_yield(node->next);
    }

    /* Check children based on node kind */
    int found = 0;
    switch (node->kind) {
    case AST_IF:
        found = contains_yield(node->data.if_stmt.condition) ||
                contains_yield(node->data.if_stmt.body) ||
                contains_yield(node->data.if_stmt.else_body);
        break;
    case AST_WHILE:
        found = contains_yield(node->data.while_stmt.condition) ||
                contains_yield(node->data.while_stmt.body) ||
                contains_yield(node->data.while_stmt.else_body);
        break;
    case AST_FOR:
        found = contains_yield(node->data.for_stmt.iter) ||
                contains_yield(node->data.for_stmt.body) ||
                contains_yield(node->data.for_stmt.else_body);
        break;
    case AST_ASSIGN:
        found = contains_yield(node->data.assign.value) ||
                contains_yield(node->data.assign.targets);
        break;
    case AST_ANN_ASSIGN:
        found = contains_yield(node->data.ann_assign.value);
        break;
    case AST_AUG_ASSIGN:
        found = contains_yield(node->data.aug_assign.value);
        break;
    case AST_RETURN:
        found = contains_yield(node->data.ret.value);
        break;
    case AST_EXPR_STMT:
        found = contains_yield(node->data.expr_stmt.expr);
        break;
    case AST_RAISE:
        found = contains_yield(node->data.raise_stmt.exc);
        break;
    case AST_ASSERT:
        found = contains_yield(node->data.assert_stmt.test) ||
                contains_yield(node->data.assert_stmt.msg);
        break;
    case AST_TRY:
        found = contains_yield(node->data.try_stmt.body) ||
                contains_yield(node->data.try_stmt.finally_body);
        break;
    case AST_BINOP:
        found = contains_yield(node->data.binop.left) ||
                contains_yield(node->data.binop.right);
        break;
    case AST_UNARYOP:
        found = contains_yield(node->data.unaryop.operand);
        break;
    case AST_CALL:
        found = contains_yield(node->data.call.func) ||
                contains_yield(node->data.call.args);
        break;
    case AST_BOOLOP:
        found = contains_yield(node->data.boolop.values);
        break;
    case AST_COMPARE:
        found = contains_yield(node->data.compare.left) ||
                contains_yield(node->data.compare.comparators);
        break;
    default:
        break;
    }
    if (found) return 1;

    /* Check sibling chain (next) */
    return contains_yield(node->next);
}

/* --------------------------------------------------------------- */
/* Constructor / Destructor                                          */
/* --------------------------------------------------------------- */
PIRBuilder::PIRBuilder()
    : sema(0), mod(0), current_func(0), current_block(0),
      error_count(0), stdlib_reg_(0), var_map(0), cell_map(0), closure_map(0),
      nested_names(0), loop_depth(0),
      current_class_name(0), current_base_class_name(0),
      current_method_class_name(0), current_method_first_param(0),
      is_building_coroutine(0),
      gen_num_locals(0), gen_state_count(0), gen_local_count(0),
      gen_for_iter_count(0),
      synth_counter_(0),
      arg_top(0), return_cleanup_depth(0),
      exception_target_depth(0), exception_exit_block(0),
      suppress_exception_checks(0),
      handled_exception_depth(0),
      num_func_defs(0), num_decorated_classes(0),
      num_general_metaclass_classes(0)
{
    gen_val = pir_value_none();
    memset(break_targets, 0, sizeof(break_targets));
    memset(continue_targets, 0, sizeof(continue_targets));
    memset(loop_cleanup_depths, 0, sizeof(loop_cleanup_depths));
    memset(gen_state_blocks, 0, sizeof(gen_state_blocks));
    memset(gen_local_names, 0, sizeof(gen_local_names));
    memset(arg_vals, 0, sizeof(arg_vals));
    memset(return_cleanups, 0, sizeof(return_cleanups));
    memset(exception_targets, 0, sizeof(exception_targets));
    memset(handled_exceptions, 0, sizeof(handled_exceptions));
    memset(func_defs, 0, sizeof(func_defs));
    memset(decorated_class_names, 0, sizeof(decorated_class_names));
    memset(general_metaclass_class_names, 0,
           sizeof(general_metaclass_class_names));
}

PIRBuilder::~PIRBuilder()
{
    if (var_map) {
        delete var_map;
        var_map = 0;
    }
    if (cell_map) {
        delete cell_map;
        cell_map = 0;
    }
    if (closure_map) {
        delete closure_map;
        closure_map = 0;
    }
}

int PIRBuilder::is_decorated_class(const char *name) const
{
    int i;
    if (!name) return 0;
    for (i = 0; i < num_decorated_classes; i++) {
        if (decorated_class_names[i] &&
            pir_str_eq(decorated_class_names[i], name)) return 1;
    }
    return 0;
}

int PIRBuilder::uses_general_metaclass(const char *name) const
{
    int i;
    if (!name) return 0;
    for (i = 0; i < num_general_metaclass_classes; i++) {
        if (general_metaclass_class_names[i] &&
            pir_str_eq(general_metaclass_class_names[i], name)) return 1;
    }
    return 0;
}

void PIRBuilder::init(SemanticAnalyzer *s)
{
    sema = s;
}

void PIRBuilder::set_stdlib(StdlibRegistry *reg)
{
    stdlib_reg_ = reg;
}

int PIRBuilder::get_error_count() const
{
    return error_count;
}

/* --------------------------------------------------------------- */
/* Block management                                                  */
/* --------------------------------------------------------------- */
PIRBlock *PIRBuilder::new_block(const char *label)
{
    return pir_block_new(current_func, label);
}

void PIRBuilder::switch_to_block(PIRBlock *block)
{
    current_block = block;
}

int PIRBuilder::block_is_terminated() const
{
    if (!current_block || !current_block->last) return 0;
    PIROp op = current_block->last->op;
    return op == PIR_BRANCH || op == PIR_COND_BRANCH ||
           op == PIR_RETURN || op == PIR_RETURN_NONE ||
           op == PIR_RERAISE || op == PIR_RAISE;
}

/* --------------------------------------------------------------- */
/* Emit helpers                                                      */
/* --------------------------------------------------------------- */
PIRInst *PIRBuilder::emit(PIROp op)
{
    PIRInst *inst = pir_inst_new(op);
    if (current_block) {
        pir_block_append(current_block, inst);
    }
    if ((op == PIR_RAISE || op == PIR_RERAISE) && current_block) {
        inst->handler_block = current_exception_target();
        pir_block_add_edge(current_block, inst->handler_block);
    }
    if (op == PIR_FOR_ITER && current_block) {
        inst->false_block = current_exception_target();
        pir_block_add_edge(current_block, inst->false_block);
    }
    if (!suppress_exception_checks && op_may_raise(op) && current_block) {
        PIRBlock *target = current_exception_target();
        PIRInst *check = pir_inst_new(PIR_CHECK_EXCEPTION);
        check->handler_block = target;
        check->line = inst->line;
        pir_block_append(current_block, check);
        pir_block_add_edge(current_block, target);
    }
    return inst;
}

int PIRBuilder::op_may_raise(PIROp op) const
{
    switch (op) {
    case PIR_PY_ADD: case PIR_PY_SUB: case PIR_PY_MUL:
    case PIR_PY_DIV: case PIR_PY_FLOORDIV: case PIR_PY_MOD:
    case PIR_PY_POW: case PIR_PY_MATMUL: case PIR_PY_INPLACE:
    case PIR_PY_NEG: case PIR_PY_POS:
    case PIR_PY_CMP_EQ: case PIR_PY_CMP_NE:
    case PIR_PY_CMP_LT: case PIR_PY_CMP_LE:
    case PIR_PY_CMP_GT: case PIR_PY_CMP_GE:
    case PIR_PY_BIT_AND: case PIR_PY_BIT_OR: case PIR_PY_BIT_XOR:
    case PIR_PY_BIT_NOT: case PIR_PY_LSHIFT: case PIR_PY_RSHIFT:
    case PIR_PY_IN: case PIR_PY_NOT_IN:
    case PIR_LIST_APPEND: case PIR_DICT_SET: case PIR_TUPLE_SET:
    case PIR_SET_ADD:
    case PIR_SUBSCR_GET: case PIR_SUBSCR_SET: case PIR_DEL_SUBSCR:
    case PIR_SLICE: case PIR_GET_ATTR: case PIR_SET_ATTR:
    case PIR_DEL_ATTR: case PIR_DEL_NAME: case PIR_DEL_GLOBAL:
    case PIR_STR_CONCAT: case PIR_STR_FORMAT: case PIR_STR_JOIN:
    case PIR_CALL: case PIR_CALL_METHOD: case PIR_GUARDED_CALL_METHOD:
    case PIR_GET_ITER: case PIR_GEN_CHECK_THROW:
    case PIR_IMPORT: case PIR_IMPORT_FROM:
        return 1;
    default:
        return 0;
    }
}

PIRBlock *PIRBuilder::current_exception_target()
{
    if (exception_target_depth > 0)
        return exception_targets[exception_target_depth - 1];
    if (!exception_exit_block)
        exception_exit_block = pir_block_new(current_func, "exception_exit");
    return exception_exit_block;
}

PIRValue PIRBuilder::emit_const_int(long val)
{
    PIRValue v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_CONST_INT);
    inst->result = v;
    inst->int_val = val;
    return v;
}

PIRValue PIRBuilder::emit_const_float(double val)
{
    PIRValue v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_CONST_FLOAT);
    inst->result = v;
    /* Store float in constant pool */
    char buf[64];
    sprintf(buf, "%g", val);
    inst->str_val = pir_str_dup(buf);
    return v;
}

PIRValue PIRBuilder::emit_const_bool(int val)
{
    PIRValue v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_CONST_BOOL);
    inst->result = v;
    inst->int_val = val ? 1 : 0;
    return v;
}

PIRValue PIRBuilder::emit_const_str(const char *s, int len)
{
    PIRValue v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_CONST_STR);
    inst->result = v;
    /* Store string data */
    char *copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, s, len);
        copy[len] = '\0';
    }
    inst->str_val = copy;
    inst->int_val = len;
    return v;
}

PIRValue PIRBuilder::emit_const_none()
{
    PIRValue v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_CONST_NONE);
    inst->result = v;
    return v;
}

void PIRBuilder::emit_branch(PIRBlock *target)
{
    if (block_is_terminated()) return;
    PIRInst *inst = emit(PIR_BRANCH);
    inst->target_block = target;
    pir_block_add_edge(current_block, target);
    current_block->filled = 1;
}

void PIRBuilder::emit_cond_branch(PIRValue cond, PIRBlock *true_blk, PIRBlock *false_blk)
{
    if (block_is_terminated()) return;
    PIRInst *inst = emit(PIR_COND_BRANCH);
    inst->operands[0] = cond;
    inst->num_operands = 1;
    inst->target_block = true_blk;
    inst->false_block = false_blk;
    pir_block_add_edge(current_block, true_blk);
    pir_block_add_edge(current_block, false_blk);
    current_block->filled = 1;
}

void PIRBuilder::emit_return(PIRValue val)
{
    if (block_is_terminated()) return;
    PIRInst *inst = emit(PIR_RETURN);
    inst->operands[0] = val;
    inst->num_operands = 1;
    current_block->filled = 1;
}

void PIRBuilder::emit_return_none()
{
    if (block_is_terminated()) return;
    PIRInst *inst = emit(PIR_RETURN_NONE);
    (void)inst;
    current_block->filled = 1;
}

/* --------------------------------------------------------------- */
/* Constant pool                                                     */
/* --------------------------------------------------------------- */
int PIRBuilder::add_const_str(const char *data, int len)
{
    char *copy;
    int idx;
    /* Check for existing */
    int i;
    for (i = 0; i < mod->string_constants.size(); i++) {
        if (mod->string_constants[i] && strcmp(mod->string_constants[i], data) == 0) {
            return i;
        }
    }
    copy = (char *)malloc(len + 1);
    if (copy) {
        memcpy(copy, data, len);
        copy[len] = '\0';
    }
    idx = mod->string_constants.size();
    mod->string_constants.push_back(copy);
    return idx;
}

/* --------------------------------------------------------------- */
/* Variable access (alloca/load/store)                               */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::var_alloca(const char *name)
{
    PIRValue v = pir_func_alloc_value(current_func, PIR_TYPE_PTR);
    PIRInst *inst = emit(PIR_ALLOCA);
    inst->result = v;
    inst->str_val = pir_str_dup(name);
    var_map->put(name, v);
    current_func->num_locals++;
    return v;
}

PIRValue PIRBuilder::var_load(const char *name)
{
    PIRValue *alloca_val;
    PIRValue v;
    PIRInst *inst;

    /* Check if this variable is accessed through a cell (captured/nonlocal) */
    if (cell_map) {
        PIRValue *cell_val = cell_map->get(name);
        if (cell_val) {
            v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            inst = emit(PIR_CELL_GET);
            inst->result = v;
            inst->operands[0] = *cell_val;
            inst->num_operands = 1;
            return v;
        }
    }

    alloca_val = var_map->get(name);
    if (!alloca_val) {
        /* Try global */
        v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        inst = emit(PIR_LOAD_GLOBAL);
        inst->result = v;
        inst->str_val = pir_str_dup(name);
        return v;
    }

    v = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    inst = emit(PIR_LOAD);
    inst->result = v;
    inst->operands[0] = *alloca_val;
    inst->num_operands = 1;
    return v;
}

void PIRBuilder::var_store(const char *name, PIRValue val)
{
    PIRValue *alloca_val;
    PIRInst *inst;

    /* Check if this variable is accessed through a cell (captured/nonlocal) */
    if (cell_map) {
        PIRValue *cell_val = cell_map->get(name);
        if (cell_val) {
            inst = emit(PIR_CELL_SET);
            inst->operands[0] = *cell_val;
            inst->operands[1] = val;
            inst->num_operands = 2;
            return;
        }
    }

    alloca_val = var_map->get(name);
    if (!alloca_val) {
        /* If in init func (module level), store as global */
        if (in_module_init_context()) {
            inst = emit(PIR_STORE_GLOBAL);
            inst->operands[0] = val;
            inst->num_operands = 1;
            inst->str_val = pir_str_dup(name);
            return;
        }
        /* Auto-create local */
        var_alloca(name);
        alloca_val = var_map->get(name);
    }

    inst = emit(PIR_STORE);
    inst->operands[0] = *alloca_val;
    inst->operands[1] = val;
    inst->num_operands = 2;
}

int PIRBuilder::var_exists(const char *name)
{
    return var_map->has(name);
}

/* --------------------------------------------------------------- */
/* Function management                                               */
/* --------------------------------------------------------------- */
PIRFunction *PIRBuilder::begin_func(const char *name)
{
    PIRFunction *func = pir_func_new(name);
    mod->functions.push_back(func);
    current_func = func;

    /* Create entry block */
    PIRBlock *entry = pir_block_new(func, "entry");
    func->entry_block = entry;
    current_block = entry;

    /* Fresh variable map */
    if (var_map) delete var_map;
    var_map = new PdHashMap<const char *, PIRValue>(
        (PdHashMap<const char *, PIRValue>::HashFn)pd_hash_str,
        (PdHashMap<const char *, PIRValue>::EqFn)pd_eq_str);

    arg_top = 0;
    exception_target_depth = 0;
    exception_exit_block = 0;
    suppress_exception_checks = 0;
    handled_exception_depth = 0;
    memset(exception_targets, 0, sizeof(exception_targets));
    memset(handled_exceptions, 0, sizeof(handled_exceptions));
    return func;
}

int PIRBuilder::in_module_init_context() const
{
    return current_func != 0 &&
           ((mod != 0 && current_func == mod->init_func) ||
            (current_func->name != 0 &&
             strncmp(current_func->name, "__module_init_", 14) == 0));
}

void PIRBuilder::end_func()
{
    PIRBlock *normal_block = current_block;
    /* Auto-insert return None if block not terminated */
    if (current_block && !block_is_terminated()) {
        emit_return_none();
    }
    if (exception_exit_block) {
        PIRInst *ret;
        current_block = exception_exit_block;
        suppress_exception_checks = 1;
        if (mod && current_func == mod->init_func) {
            pir_block_append(current_block,
                             pir_inst_new(PIR_PANIC_EXCEPTION));
        }
        ret = pir_inst_new(PIR_RETURN);
        ret->operands[0] = pir_value_none();
        ret->num_operands = 1;
        pir_block_append(current_block, ret);
        current_block->filled = 1;
        suppress_exception_checks = 0;
    }
    current_block = normal_block;
    current_func = 0;
    current_block = 0;
}

/* --------------------------------------------------------------- */
/* Top-level build                                                   */
/* --------------------------------------------------------------- */
PIRModule *PIRBuilder::build(ASTNode *module_node)
{
    ASTNode *stmt;
    const char *helper_names[128];
    int helper_count = 0;
    int top_level_count = 0;
    const int statements_per_helper = 8;

    mod = pir_module_new();

    /* Resolve the flattened, source-linked module body first. */
    if (module_node && module_node->kind == AST_MODULE) {
        stmt = module_node->data.module.body;
    } else {
        stmt = module_node;
    }

    {
        ASTNode *counted;
        for (counted = stmt; counted; counted = counted->next)
            top_level_count++;
    }

    /* Small modules keep the compact traditional shape.  Besides avoiding
     * needless call overhead, this prevents internal init helpers from being
     * serialized as ordinary functions while building the stdlib index. */
    if (top_level_count <= 32) {
        PIRFunction *small_init = begin_func("__init__");
        mod->init_func = small_init;
        build_stmts(stmt);
        end_func();
        return mod;
    }

    /* A real-mode code segment cannot exceed 64 KiB.  Source-linked modules
     * can contain hundreds of top-level definitions, so compile bounded
     * groups into module-scope helpers.  Their stores remain global and the
     * root __init__ calls them in original source order. */
    while (stmt && helper_count < 128) {
        char helper_name[48];
        int built = 0;
        sprintf(helper_name, "__module_init_%d", helper_count);
        helper_names[helper_count] = pir_str_dup(helper_name);
        begin_func(helper_names[helper_count]);
        while (stmt && built < statements_per_helper) {
            ASTNode *next = stmt->next;
            build_stmt(stmt);
            stmt = next;
            built++;
        }
        end_func();
        helper_count++;
    }
    if (stmt) {
        report_error(stmt, "too many top-level initialization chunks");
    }

    /* The public init stays small and is the sole boundary that converts an
     * uncaught Python exception into the executable's panic path. */
    PIRFunction *init = begin_func("__init__");
    mod->init_func = init;
    {
        int i;
        for (i = 0; i < helper_count; i++) {
            PIRInst *call = emit(PIR_CALL);
            call->result = pir_func_alloc_value(current_func,
                                                 PIR_TYPE_PYOBJ);
            call->str_val = pir_str_dup(helper_names[i]);
            call->int_val = 0;
        }
    }
    end_func();

    return mod;
}

/* --------------------------------------------------------------- */
/* Statement dispatch                                                */
/* --------------------------------------------------------------- */
void PIRBuilder::build_stmts(ASTNode *first)
{
    ASTNode *n;
    for (n = first; n; n = n->next) {
        build_stmt(n);
    }
}

void PIRBuilder::build_stmt(ASTNode *node)
{
    if (!node) return;

    switch (node->kind) {
    case AST_FUNC_DEF:    build_funcdef(node); break;
    case AST_CLASS_DEF:   build_classdef(node); break;
    case AST_IF:          build_if(node); break;
    case AST_WHILE:       build_while(node); break;
    case AST_FOR:         build_for(node); break;
    case AST_ASSIGN:      build_assign(node); break;
    case AST_ANN_ASSIGN:  build_ann_assign(node); break;
    case AST_AUG_ASSIGN:  build_aug_assign(node); break;
    case AST_RETURN:      build_return(node); break;
    case AST_EXPR_STMT:   build_expr_stmt(node); break;
    case AST_TRY:         build_try(node); break;
    case AST_RAISE:       build_raise(node); break;
    case AST_BREAK:       build_break(node); break;
    case AST_CONTINUE:    build_continue(node); break;
    case AST_PASS:        build_pass(node); break;
    case AST_ASSERT:      build_assert(node); break;
    case AST_DELETE:      build_delete(node); break;
    case AST_WITH:        build_with(node); break;
    case AST_MATCH:       build_match(node); break;
    case AST_IMPORT:      build_import(node); break;
    case AST_IMPORT_FROM: break;  /* resolved at sema/link time */
    case AST_GLOBAL:
    case AST_NONLOCAL:    break;  /* scope declarations have no runtime op */
    case AST_TYPE_ALIAS:  build_type_alias(node); break;
    default:
        /* Try as expression statement */
        build_expr(node);
        break;
    }
}

void PIRBuilder::build_import(ASTNode *node)
{
    const char *module_name = node->data.import_stmt.module;
    const char *bound_name = node->data.import_stmt.alias;
    PIRValue name;
    PIRValue module;
    PIRInst *push;
    PIRInst *call;
    const char *dot;
    char root_name[128];

    if (!module_name) return;
    if (!bound_name) {
        dot = strchr(module_name, '.');
        if (dot) {
            int len = (int)(dot - module_name);
            if (len > 127) len = 127;
            memcpy(root_name, module_name, len);
            root_name[len] = '\0';
            bound_name = root_name;
        } else {
            bound_name = module_name;
        }
    }
    name = emit_const_str(module_name, (int)strlen(module_name));
    push = emit(PIR_PUSH_ARG);
    push->operands[0] = name;
    push->num_operands = 1;
    module = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    call = emit(PIR_CALL);
    call->result = module;
    call->str_val = pir_str_dup("pydos_import_module");
    call->int_val = 1;
    var_store(bound_name, module);
}

PIRValue PIRBuilder::attach_type_parameters(const char **names,
                                            ASTNode **bounds,
                                            unsigned char *kinds, int count,
                                            PIRValue owner)
{
    PIRValue parameters[16];
    int i;
    if (!names || count <= 0) {
        PIRValue empty_tuple = pir_func_alloc_value(current_func,
                                                     PIR_TYPE_PYOBJ);
        PIRInst *empty = emit(PIR_BUILD_TUPLE);
        empty->result = empty_tuple;
        empty->int_val = 0;
        return empty_tuple;
    }
    if (count > 16) count = 16;

    for (i = 0; i < count; i++) {
        PIRValue name = emit_const_str(names[i], (int)strlen(names[i]));
        PIRValue kind = emit_const_int(kinds ? kinds[i] : 0);
        PIRValue bound = emit_const_none();
        PIRValue constraints;
        PIRValue thunk = emit_const_none();
        ASTNode *bound_node = bounds ? bounds[i] : 0;

        if (bound_node && bound_node->kind == AST_TUPLE_EXPR) {
            constraints = build_expr(bound_node);
        } else {
            PIRInst *empty = emit(PIR_BUILD_TUPLE);
            constraints = pir_func_alloc_value(current_func,
                                                PIR_TYPE_PYOBJ);
            empty->result = constraints;
            empty->int_val = 0;
            if (bound_node) {
                char thunk_name[96];
                ASTNode *function;
                ASTNode *ret;
                static unsigned int thunk_serial = 0;
                sprintf(thunk_name, "__pydos_type_bound_%u",
                        ++thunk_serial);
                function = ast_alloc(AST_FUNC_DEF, bound_node->line,
                                     bound_node->col);
                ret = ast_alloc(AST_RETURN, bound_node->line,
                                bound_node->col);
                function->data.func_def.name = pir_str_dup(thunk_name);
                function->data.func_def.body = ret;
                ret->data.ret.value = bound_node;
                build_funcdef(function);
                thunk = var_load(thunk_name);
            }
        }

        {
            PIRInst *push = emit(PIR_PUSH_ARG);
            PIRInst *call;
            push->operands[0] = name;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = kind;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = bound;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = constraints;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = thunk;
            push->num_operands = 1;
            parameters[i] = pir_func_alloc_value(current_func,
                                                  PIR_TYPE_PYOBJ);
            call = emit(PIR_CALL);
            call->result = parameters[i];
            call->str_val = pir_str_dup("pydos_type_param_new");
            call->int_val = 5;
        }
    }

    for (i = 0; i < count; i++) {
        PIRInst *push = emit(PIR_PUSH_ARG);
        push->operands[0] = parameters[i];
        push->num_operands = 1;
    }
    {
        PIRValue tuple = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *build = emit(PIR_BUILD_TUPLE);
        PIRInst *set;
        build->result = tuple;
        build->int_val = count;
        if (pir_value_valid(owner)) {
            set = emit(PIR_SET_ATTR);
            set->operands[0] = owner;
            set->operands[1] = tuple;
            set->num_operands = 2;
            set->str_val = pir_str_dup("__type_params__");
        }
        return tuple;
    }
}

PIRValue PIRBuilder::build_runtime_type_expr(ASTNode *node,
                                             const char **param_names,
                                             PIRValue *param_values,
                                             int param_count)
{
    int i;
    if (!node) return emit_const_none();
    if (node->kind == AST_TYPE_NAME) {
        const char *name = node->data.type_name.tname;
        if (name && pir_str_eq(name, "None")) return emit_const_none();
        for (i = 0; i < param_count; i++)
            if (name && pir_str_eq(name, param_names[i]))
                return param_values[i];
        return var_load(name ? name : "object");
    }
    if (node->kind == AST_TYPE_GENERIC) {
        PIRValue origin = var_load(node->data.type_generic.gname);
        PIRValue args[32];
        PIRValue key;
        PIRValue result;
        ASTNode *arg;
        int count = 0;
        for (arg = node->data.type_generic.type_args;
             arg && count < 32; arg = arg->next)
            args[count++] = build_runtime_type_expr(
                arg, param_names, param_values, param_count);
        if (count == 1) {
            key = args[0];
        } else {
            for (i = 0; i < count; i++) {
                PIRInst *push = emit(PIR_PUSH_ARG);
                push->operands[0] = args[i];
                push->num_operands = 1;
            }
            key = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *tuple = emit(PIR_BUILD_TUPLE);
                tuple->result = key;
                tuple->int_val = count;
            }
        }
        result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *subscript = emit(PIR_SUBSCR_GET);
            subscript->result = result;
            subscript->operands[0] = origin;
            subscript->operands[1] = key;
            subscript->num_operands = 2;
        }
        return result;
    }
    if (node->kind == AST_TYPE_UNION || node->kind == AST_TYPE_TUPLE) {
        ASTNode *part = node->data.type_union.types;
        PIRValue parts[32];
        PIRValue tuple;
        int count = 0;
        for (; part && count < 32; part = part->next)
            parts[count++] = build_runtime_type_expr(
                part, param_names, param_values, param_count);
        for (i = 0; i < count; i++) {
            PIRInst *push = emit(PIR_PUSH_ARG);
            push->operands[0] = parts[i];
            push->num_operands = 1;
        }
        tuple = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *build = emit(PIR_BUILD_TUPLE);
            build->result = tuple;
            build->int_val = count;
        }
        if (node->kind == AST_TYPE_TUPLE) return tuple;
        {
            PIRValue marker = emit_const_str("Union", 5);
            PIRValue result = pir_func_alloc_value(current_func,
                                                    PIR_TYPE_PYOBJ);
            PIRInst *push = emit(PIR_PUSH_ARG);
            PIRInst *call;
            push->operands[0] = marker;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = tuple;
            push->num_operands = 1;
            call = emit(PIR_CALL);
            call->result = result;
            call->str_val = pir_str_dup("pydos_generic_alias_new");
            call->int_val = 2;
            return result;
        }
    }
    return build_expr(node);
}

void PIRBuilder::build_type_alias(ASTNode *node)
{
    int count = node->data.type_alias.num_type_params;
    PIRValue params;
    PIRValue alias;
    PIRValue values[16];
    PIRValue name;
    PIRValue value;
    int i;
    if (count > 16) count = 16;
    params = attach_type_parameters(
        node->data.type_alias.type_param_names,
        node->data.type_alias.type_param_bounds,
        node->data.type_alias.type_param_kinds, count,
        pir_value_none());
    name = emit_const_str(node->data.type_alias.name,
                          (int)strlen(node->data.type_alias.name));
    {
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = name;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = params;
        push->num_operands = 1;
        alias = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        call = emit(PIR_CALL);
        call->result = alias;
        call->str_val = pir_str_dup("pydos_type_alias_new");
        call->int_val = 2;
    }
    var_store(node->data.type_alias.name, alias);
    for (i = 0; i < count; i++) {
        PIRValue index = emit_const_int(i);
        PIRInst *get;
        values[i] = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        get = emit(PIR_SUBSCR_GET);
        get->result = values[i];
        get->operands[0] = params;
        get->operands[1] = index;
        get->num_operands = 2;
    }
    value = build_runtime_type_expr(node->data.type_alias.value,
                                    node->data.type_alias.type_param_names,
                                    values, count);
    {
        PIRValue updated = pir_func_alloc_value(current_func,
                                                PIR_TYPE_PYOBJ);
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = alias;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = value;
        push->num_operands = 1;
        call = emit(PIR_CALL);
        call->result = updated;
        call->str_val = pir_str_dup("pydos_type_alias_set_value");
        call->int_val = 2;
        var_store(node->data.type_alias.name, updated);
    }
}

/* --------------------------------------------------------------- */
/* Statement builders                                                */
/* --------------------------------------------------------------- */

/* Attach evaluated default arguments to a function object.
 *
 * Direct calls normalize omitted arguments from the AST, but decorators,
 * callbacks and dynamic dispatch go through pydos_obj_call() and need the
 * defaults on the object itself. */
void PIRBuilder::attach_function_defaults(ASTNode *node, PIRValue fobj)
{
    /* First-class functions must carry their evaluated defaults too.
     * Direct calls can normalize omitted arguments from the AST, but
     * decorators, callbacks and imported functions go through
     * pydos_obj_call().  Include the synthetic empty containers for
     * variadic positional/keyword parameters because they occupy
     * explicit slots in the
     * fixed DOS ABI. */
    {
        Param *dp;
        int optional_started = 0;
        int defaults_valid = 1;
        int default_count = 0;

        for (dp = node->data.func_def.params; dp; dp = dp->next) {
            if (is_bare_star_sep(dp)) continue;
            if (!optional_started &&
                (dp->default_val || dp->is_star || dp->is_double_star))
                optional_started = 1;
            if (optional_started) {
                if (!dp->default_val && !dp->is_star &&
                    !dp->is_double_star) {
                    defaults_valid = 0;
                    break;
                }
                default_count++;
            }
        }

        if (defaults_valid && default_count > 0) {
            PIRValue default_values[64];
            int default_index = 0;
            PIRValue defaults;
            PIRValue ignored;
            PIRInst *build_defaults;
            PIRInst *push_func;
            PIRInst *push_defaults;
            PIRInst *set_defaults;

            optional_started = 0;
            for (dp = node->data.func_def.params; dp; dp = dp->next) {
                PIRValue default_value;
                if (is_bare_star_sep(dp)) continue;
                if (!optional_started &&
                    (dp->default_val || dp->is_star ||
                     dp->is_double_star))
                    optional_started = 1;
                if (!optional_started) continue;

                if (dp->default_val) {
                    default_value = build_expr(dp->default_val);
                } else if (dp->is_double_star) {
                    PIRInst *empty_dict;
                    default_value = pir_func_alloc_value(
                        current_func, PIR_TYPE_PYOBJ);
                    empty_dict = emit(PIR_BUILD_DICT);
                    empty_dict->result = default_value;
                    empty_dict->int_val = 0;
                } else {
                    PIRInst *empty_tuple;
                    default_value = pir_func_alloc_value(
                        current_func, PIR_TYPE_PYOBJ);
                    empty_tuple = emit(PIR_BUILD_TUPLE);
                    empty_tuple->result = default_value;
                    empty_tuple->int_val = 0;
                }
                if (default_index < 64)
                    default_values[default_index++] = default_value;
            }

            /* PUSH_ARG is a lowering-side argument bundle.  Keep the
             * pushes adjacent to BUILD_TUPLE so evaluating a later
             * default (which may itself call/build a container) cannot
             * consume the earlier bundle. */
            for (default_index = 0;
                 default_index < default_count;
                 default_index++) {
                PIRInst *push_default = emit(PIR_PUSH_ARG);
                push_default->operands[0] =
                    default_values[default_index];
                push_default->num_operands = 1;
            }

            defaults = pir_func_alloc_value(current_func,
                                            PIR_TYPE_PYOBJ);
            build_defaults = emit(PIR_BUILD_TUPLE);
            build_defaults->result = defaults;
            build_defaults->int_val = default_count;

            push_func = emit(PIR_PUSH_ARG);
            push_func->operands[0] = fobj;
            push_func->num_operands = 1;
            push_defaults = emit(PIR_PUSH_ARG);
            push_defaults->operands[0] = defaults;
            push_defaults->num_operands = 1;
            ignored = pir_func_alloc_value(current_func,
                                           PIR_TYPE_PYOBJ);
            set_defaults = emit(PIR_CALL);
            set_defaults->result = ignored;
            set_defaults->str_val = pir_str_dup(
                "pydos_func_apply_defaults");
            set_defaults->int_val = 2;
        }
    }
}

void PIRBuilder::attach_code_metadata(ASTNode *node, PIRValue function,
                                      const char *python_name)
{
    PIRValue freevars;
    PIRValue consts;
    PIRValue name;
    PIRValue ignored;
    int i;
    for (i = 0; i < node->data.func_def.num_free_vars; i++) {
        const char *free_name = node->data.func_def.free_var_names[i];
        PIRValue item = emit_const_str(free_name, (int)strlen(free_name));
        PIRInst *push = emit(PIR_PUSH_ARG);
        push->operands[0] = item;
        push->num_operands = 1;
    }
    freevars = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *build = emit(PIR_BUILD_TUPLE);
        build->result = freevars;
        build->int_val = node->data.func_def.num_free_vars;
    }
    /* Nested functions are separately compiled PyDOS code objects.  Literal
     * constants do not require heap code objects, and comprehensions are
     * inlined, so the code-object subset of co_consts is initially empty. */
    consts = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *build = emit(PIR_BUILD_TUPLE);
        build->result = consts;
        build->int_val = 0;
    }
    name = emit_const_str(python_name, (int)strlen(python_name));
    {
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = function;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = name;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = freevars;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = consts;
        push->num_operands = 1;
        ignored = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        call = emit(PIR_CALL);
        call->result = ignored;
        call->str_val = pir_str_dup("pydos_func_set_code_metadata");
        call->int_val = 4;
    }
}

void PIRBuilder::attach_function_annotations(ASTNode *node,
                                              PIRValue function,
                                              PIRValue type_params)
{
    PIRValue type_param_values[16];
    PIRValue keys[65];
    PIRValue values[65];
    Param *param;
    int type_param_count = node->data.func_def.num_type_params;
    int count = 0;
    int i;

    if (type_param_count > 16) type_param_count = 16;
    for (i = 0; i < type_param_count; i++) {
        PIRValue index = emit_const_int((long)i);
        PIRInst *get;
        type_param_values[i] = pir_func_alloc_value(current_func,
                                                    PIR_TYPE_PYOBJ);
        get = emit(PIR_SUBSCR_GET);
        get->result = type_param_values[i];
        get->operands[0] = type_params;
        get->operands[1] = index;
        get->num_operands = 2;
    }

    for (param = node->data.func_def.params;
         param && count < 64; param = param->next) {
        if (is_bare_star_sep(param) || !param->annotation) continue;
        keys[count] = emit_const_str(param->name ? param->name : "",
            param->name ? (int)strlen(param->name) : 0);
        values[count] = build_runtime_type_expr(
            param->annotation,
            node->data.func_def.type_param_names,
            type_param_values, type_param_count);
        count++;
    }
    if (node->data.func_def.return_type && count < 65) {
        keys[count] = emit_const_str("return", 6);
        values[count] = build_runtime_type_expr(
            node->data.func_def.return_type,
            node->data.func_def.type_param_names,
            type_param_values, type_param_count);
        count++;
    }

    if (count == 0) return;

    for (i = 0; i < count; i++) {
        PIRInst *push = emit(PIR_PUSH_ARG);
        push->operands[0] = keys[i];
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = values[i];
        push->num_operands = 1;
    }
    {
        PIRValue annotations = pir_func_alloc_value(current_func,
                                                     PIR_TYPE_PYOBJ);
        PIRInst *build = emit(PIR_BUILD_DICT);
        PIRInst *set;
        build->result = annotations;
        build->int_val = count;
        set = emit(PIR_SET_ATTR);
        set->operands[0] = function;
        set->operands[1] = annotations;
        set->num_operands = 2;
        set->str_val = pir_str_dup("__annotations__");
    }
}

void PIRBuilder::attach_parameter_metadata(Param *params, PIRValue fobj)
{
    Param *param;
    int count = 0;
    int keyword_only = 0;
    int names_len = 0;
    unsigned long packed_flags = 0UL;
    char *packed_names;
    int name_pos = 0;
    PIRValue names;
    PIRValue flags;
    PIRValue ignored;
    PIRInst *push;
    PIRInst *call;

    for (param = params; param && count < 64; param = param->next) {
        int bits = 0;
        if (is_bare_star_sep(param)) {
            keyword_only = 1;
            continue;
        }
        if (param->is_star) {
            bits |= 1;
            keyword_only = 1;
        }
        if (param->is_double_star) bits |= 2;
        if (param->is_positional_only) bits |= 4;
        if (keyword_only && !param->is_star && !param->is_double_star)
            bits |= 8;
        if (count < 8)
            packed_flags |= ((unsigned long)bits & 0x0FUL) << (count * 4);
        names_len += param->name ? (int)strlen(param->name) : 0;
        if (count > 0) names_len++;
        count++;
    }
    packed_names = (char *)malloc((unsigned int)names_len + 1U);
    if (!packed_names) {
        report_error(0, "out of memory packing function parameters");
        return;
    }
    for (param = params; param; param = param->next) {
        int len;
        if (is_bare_star_sep(param)) continue;
        if (name_pos > 0) packed_names[name_pos++] = ',';
        len = param->name ? (int)strlen(param->name) : 0;
        if (len > 0) memcpy(packed_names + name_pos, param->name, len);
        name_pos += len;
    }
    packed_names[name_pos] = '\0';
    names = emit_const_str(packed_names, names_len);
    free(packed_names);
    flags = emit_const_int((long)packed_flags);

    push = emit(PIR_PUSH_ARG);
    push->operands[0] = fobj;
    push->num_operands = 1;
    push = emit(PIR_PUSH_ARG);
    push->operands[0] = names;
    push->num_operands = 1;
    push = emit(PIR_PUSH_ARG);
    push->operands[0] = flags;
    push->num_operands = 1;
    ignored = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    call = emit(PIR_CALL);
    call->result = ignored;
    call->str_val = pir_str_dup("pydos_func_set_param_spec");
    call->int_val = 3;
}

/* --- Function definition --- */
void PIRBuilder::build_funcdef(ASTNode *node)
{
    const char *py_name = node->data.func_def.name;
    const char *fname = py_name;
    int is_nested = current_func != 0 && current_class_name == 0 &&
                    mod != 0 && !in_module_init_context();
    NestedName *pushed_nested = 0;
    Param *param;
    int param_count = 0;
    int is_gen = 0;
    int i;
    PIRValue decorator_vals[32];
    int decorator_count = 0;

    /* A nested def is qualified by its enclosing function so that two scopes
     * may define the same inner name.  The entry is pushed before the body is
     * built so recursive calls resolve to the same symbol. */
    if (is_nested) {
        fname = qualified_nested_name(current_func->name, py_name);
        pushed_nested = (NestedName *)malloc(sizeof(NestedName));
        if (pushed_nested) {
            NestedName *previous = nested_entry(py_name);
            pushed_nested->py_name = py_name;
            pushed_nested->symbol = fname;
            pushed_nested->owner = current_func;
            pushed_nested->ambiguous =
                previous && previous->owner == current_func;
            pushed_nested->outer = nested_names;
            nested_names = pushed_nested;
        }
    }

    /* Decorator expressions are evaluated top-to-bottom in the defining
     * scope.  Their application happens bottom-up after MAKE_FUNCTION.
     * Methods require descriptor-aware class materialization and are left
     * for build_classdef rather than being silently applied incorrectly. */
    if (current_func && !current_class_name) {
        ASTNode *decorator;
        for (decorator = node->data.func_def.decorators;
             decorator && decorator_count < 32;
             decorator = decorator->next) {
            decorator_vals[decorator_count++] = build_expr(decorator);
        }
    }

    /* Count params (skip bare * separator) */
    for (param = node->data.func_def.params; param; param = param->next) {
        if (is_bare_star_sep(param)) continue;
        param_count++;
    }

    /* Check if generator (contains yield) or async def */
    int is_async = node->data.func_def.is_async;
    is_gen = contains_yield(node->data.func_def.body);

    if (is_async && is_gen) {
        /* async generators not supported yet */
        report_error(node, "async generators are not supported");
        return;
    }
    if (is_async) {
        /* async def uses same state machine as generators */
        is_gen = 1;
    }

    /* Save outer context */
    PIRFunction *outer_func = current_func;
    PIRBlock *outer_block = current_block;
    PdHashMap<const char *, PIRValue> *outer_var_map = var_map;
    PdHashMap<const char *, PIRValue> *outer_cell_map = cell_map;
    PdHashMap<const char *, PIRValue> *outer_closure_map = closure_map;
    NestedName *outer_nested_names = nested_names;
    var_map = 0; /* begin_func will create new */
    cell_map = 0;
    closure_map = 0;
    const char *outer_class = current_class_name;
    const char *outer_base = current_base_class_name;
    const char *outer_method_class = current_method_class_name;
    const char *outer_method_first = current_method_first_param;
    if (outer_class) {
        Param *first_param = node->data.func_def.params;
        while (first_param && is_bare_star_sep(first_param))
            first_param = first_param->next;
        current_method_class_name = outer_class;
        current_method_first_param = first_param ? first_param->name : 0;
    }
    /* A def nested in a method body is a local function, not a class member.
     * The base class name stays live for super(). */
    current_class_name = 0;
    PIRValue outer_gen_val = gen_val;
    int outer_gen_locals = gen_num_locals;
    int outer_gen_states = gen_state_count;
    int outer_gen_local_count = gen_local_count;
    int outer_gen_for_iter_count = gen_for_iter_count;
    int outer_loop_depth = loop_depth;
    int outer_return_cleanup_depth = return_cleanup_depth;
    int outer_exception_target_depth = exception_target_depth;
    PIRBlock *outer_exception_exit_block = exception_exit_block;
    int outer_suppress_exception_checks = suppress_exception_checks;
    int outer_handled_exception_depth = handled_exception_depth;
    int outer_is_building_coroutine = is_building_coroutine;
    PIRBlock *outer_break_targets[32];
    PIRBlock *outer_continue_targets[32];
    int outer_loop_cleanup_depths[32];
    ReturnCleanup outer_return_cleanups[32];
    PIRBlock *outer_exception_targets[32];
    PIRValue outer_handled_exceptions[32];
    PIRBlock *outer_gen_state_blocks[32];
    const char *outer_gen_local_names[64];
    memcpy(outer_break_targets, break_targets, sizeof(break_targets));
    memcpy(outer_continue_targets, continue_targets,
           sizeof(continue_targets));
    memcpy(outer_loop_cleanup_depths, loop_cleanup_depths,
           sizeof(loop_cleanup_depths));
    memcpy(outer_return_cleanups, return_cleanups,
           sizeof(return_cleanups));
    memcpy(outer_exception_targets, exception_targets,
           sizeof(exception_targets));
    memcpy(outer_handled_exceptions, handled_exceptions,
           sizeof(handled_exceptions));
    memcpy(outer_gen_state_blocks, gen_state_blocks, sizeof(gen_state_blocks));
    memcpy(outer_gen_local_names, gen_local_names, sizeof(gen_local_names));

    loop_depth = 0;
    return_cleanup_depth = 0;
    handled_exception_depth = 0;
    memset(break_targets, 0, sizeof(break_targets));
    memset(continue_targets, 0, sizeof(continue_targets));
    memset(loop_cleanup_depths, 0, sizeof(loop_cleanup_depths));
    memset(return_cleanups, 0, sizeof(return_cleanups));
    memset(handled_exceptions, 0, sizeof(handled_exceptions));
    gen_val = pir_value_none();
    gen_num_locals = 0;
    gen_state_count = 0;
    gen_local_count = 0;
    gen_for_iter_count = 0;
    is_building_coroutine = is_async;
    memset(gen_state_blocks, 0, sizeof(gen_state_blocks));
    memset(gen_local_names, 0, sizeof(gen_local_names));

    /* Record func def for default arg lookup */
    if (num_func_defs < 256) {
        func_defs[num_func_defs].name = fname;
        func_defs[num_func_defs].node = node;
        num_func_defs++;
    }

    if (is_gen) {
        /* --- Generator/Coroutine: wrapper + resume function --- */

        /* 1. Wrapper function */
        PIRFunction *wrapper = begin_func(fname);
        wrapper->num_params = param_count;

        /* Add params as locals (skip bare * separator) */
        for (param = node->data.func_def.params; param; param = param->next) {
            if (is_bare_star_sep(param)) continue;
            var_alloca(param->name);
            wrapper->params.push_back(
                pir_func_alloc_value(wrapper, PIR_TYPE_PYOBJ));
        }

        /* Store params into locals (skip bare * separator) */
        i = 0;
        for (param = node->data.func_def.params; param; param = param->next) {
            if (is_bare_star_sep(param)) continue;
            PIRValue pval = wrapper->params[i];
            var_store(param->name, pval);
            i++;
        }

        /* Create generator/coroutine object */
        char resume_name[256];
        sprintf(resume_name, is_async ? "_corresume_%s" : "_genresume_%s", fname);
        int name_ci = add_const_str(resume_name, (int)strlen(resume_name));

        PIRValue gen_obj = pir_func_alloc_value(wrapper, PIR_TYPE_PYOBJ);
        {
            PIROp make_op = is_async ? PIR_MAKE_COROUTINE : PIR_MAKE_GENERATOR;
            PIRInst *inst = emit(make_op);
            inst->result = gen_obj;
            inst->str_val = pir_str_dup(resume_name);
            inst->int_val = 32; /* max locals */
            (void)name_ci;
        }

        /* Save params into gen->locals (skip bare * separator) */
        i = 0;
        for (param = node->data.func_def.params; param; param = param->next) {
            if (is_bare_star_sep(param)) continue;
            PIRValue p = var_load(param->name);
            PIRInst *save = emit(PIR_GEN_SAVE_LOCAL);
            save->operands[0] = gen_obj;
            save->operands[1] = p;
            save->num_operands = 2;
            save->int_val = i;
            i++;
        }

        /* A generator resumes after the call frame that created it has
         * returned.  Snapshot the function closure into the generator object
         * instead of relying on the process-wide active-closure register. */
        if (node->data.func_def.num_free_vars > 0) {
            PIRValue active_closure = pir_func_alloc_value(
                wrapper, PIR_TYPE_PYOBJ);
            PIRInst *load_closure = emit(PIR_LOAD_CLOSURE);
            PIRInst *save_closure;
            load_closure->result = active_closure;
            save_closure = emit(PIR_GEN_SAVE_LOCAL);
            save_closure->operands[0] = gen_obj;
            save_closure->operands[1] = active_closure;
            save_closure->num_operands = 2;
            save_closure->int_val = param_count;
        }

        /* Return generator */
        emit_return(gen_obj);
        end_func();

        /* 2. Resume function */
        PIRFunction *resume = begin_func(resume_name);
        resume->is_generator = 1;
        resume->is_coroutine = is_async ? 1 : 0;
        resume->num_params = 1; /* __gen__ */

        /* __gen__ parameter */
        PIRValue gen_param = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
        resume->params.push_back(gen_param);
        var_alloca("__gen__");
        var_store("__gen__", gen_param);

        gen_val = var_load("__gen__");
        gen_num_locals = param_count +
            (node->data.func_def.num_free_vars > 0 ? 1 : 0);

        /* Initialize gen_local_names from params (skip bare * separator) */
        {
            Param *pp = node->data.func_def.params;
            int pi = 0;
            for (; pp; pp = pp->next) {
                if (is_bare_star_sep(pp)) continue;
                gen_local_names[pi] = pp->name;
                pi++;
            }
            gen_local_count = param_count;
            if (node->data.func_def.num_free_vars > 0) {
                gen_local_names[gen_local_count++] = "__pydos_closure__";
            }
        }

        /* Allocate state blocks */
        for (i = 0; i < 32; i++) {
            gen_state_blocks[i] = 0;
        }
        gen_state_count = 1; /* State 0 is initial entry */

        PIRBlock *dispatch_block = new_block("dispatch");
        PIRBlock *state0_block = new_block("state0");
        PIRBlock *exhausted_block = new_block("exhausted");

        gen_state_blocks[0] = state0_block;

        /* Entry → dispatch */
        emit_branch(dispatch_block);

        /* State 0: restore params and run body */
        switch_to_block(state0_block);

        /* Restore params from gen->locals (skip bare * separator) */
        i = 0;
        for (param = node->data.func_def.params; param; param = param->next) {
            if (is_bare_star_sep(param)) continue;
            var_alloca(param->name);
            PIRValue loaded = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
            PIRInst *ld = emit(PIR_GEN_LOAD_LOCAL);
            ld->result = loaded;
            ld->operands[0] = gen_val;
            ld->num_operands = 1;
            ld->int_val = i;
            var_store(param->name, loaded);
            i++;
        }

        /* Recreate the lexical cell map in the resume frame.  Free-variable
         * cells are borrowed from the closure saved by the wrapper; cell
         * variables owned by this generator are created once in state 0 and
         * then saved as ordinary generator locals across every yield. */
        if (node->data.func_def.num_free_vars > 0 ||
            node->data.func_def.num_cell_vars > 0) {
            cell_map = new PdHashMap<const char *, PIRValue>(
                (PdHashMap<const char *, PIRValue>::HashFn)pd_hash_str,
                (PdHashMap<const char *, PIRValue>::EqFn)pd_eq_str);
        }
        if (node->data.func_def.num_free_vars > 0) {
            int fv;
            PIRValue closure = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
            PIRInst *load_closure = emit(PIR_GEN_LOAD_LOCAL);
            load_closure->result = closure;
            load_closure->operands[0] = gen_val;
            load_closure->num_operands = 1;
            load_closure->int_val = param_count;
            var_alloca("__pydos_closure__");
            var_store("__pydos_closure__", closure);
            for (fv = 0; fv < node->data.func_def.num_free_vars; fv++) {
                const char *fvname = node->data.func_def.free_var_names[fv];
                PIRValue index = emit_const_int(fv);
                PIRValue cell = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
                PIRInst *get_cell = emit(PIR_SUBSCR_GET);
                get_cell->result = cell;
                get_cell->operands[0] = closure;
                get_cell->operands[1] = index;
                get_cell->num_operands = 2;
                cell_map->put(fvname, cell);
            }
        }
        if (node->data.func_def.num_cell_vars > 0) {
            int cv;
            for (cv = 0; cv < node->data.func_def.num_cell_vars; cv++) {
                const char *cvname = node->data.func_def.cell_var_names[cv];
                char hidden_buffer[160];
                const char *hidden_name;
                PIRValue current_value = pir_value_none();
                PIRValue cell;
                PIRInst *make_cell;
                int has_initial_value = var_map->has(cvname);
                if (has_initial_value) current_value = var_load(cvname);
                cell = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
                make_cell = emit(PIR_MAKE_CELL);
                make_cell->result = cell;
                if (has_initial_value) {
                    PIRInst *set_cell = emit(PIR_CELL_SET);
                    set_cell->operands[0] = cell;
                    set_cell->operands[1] = current_value;
                    set_cell->num_operands = 2;
                }
                sprintf(hidden_buffer, "__pydos_cell_%s", cvname);
                hidden_name = pir_str_dup(hidden_buffer);
                var_alloca(hidden_name);
                var_store(hidden_name, cell);
                cell_map->put(cvname, cell);
            }
        }

        /* Generate function body */
        build_stmts(node->data.func_def.body);

        /* Return NULL (generator exhausted — NOT None) */
        if (!block_is_terminated()) {
            emit_return(pir_value_none());
        }

        /* Dispatch block: switch on gen->pc */
        switch_to_block(dispatch_block);
        {
            PIRValue gen_temp = var_load("__gen__");
            PIRValue pc = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
            PIRInst *ld_pc = emit(PIR_GEN_LOAD_PC);
            ld_pc->result = pc;
            ld_pc->operands[0] = gen_temp;
            ld_pc->num_operands = 1;

            /* Check exhausted: pc < 0 */
            PIRValue zero = emit_const_int(0);
            PIRValue neg_check = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
            {
                PIRInst *cmp = emit(PIR_PY_CMP_LT);
                cmp->result = neg_check;
                cmp->operands[0] = pc;
                cmp->operands[1] = zero;
                cmp->num_operands = 2;
            }

            PIRBlock *not_exhausted = new_block("not_exhausted");
            emit_cond_branch(neg_check, exhausted_block, not_exhausted);

            /* State dispatch chain */
            switch_to_block(not_exhausted);
            for (i = 0; i < gen_state_count; i++) {
                if (!gen_state_blocks[i]) continue;
                PIRValue state_const = emit_const_int(i);
                PIRValue eq = pir_func_alloc_value(resume, PIR_TYPE_PYOBJ);
                {
                    PIRInst *cmp = emit(PIR_PY_CMP_EQ);
                    cmp->result = eq;
                    cmp->operands[0] = pc;
                    cmp->operands[1] = state_const;
                    cmp->num_operands = 2;
                }
                PIRBlock *next_check;
                if (i < gen_state_count - 1) {
                    next_check = new_block("state_check");
                } else {
                    next_check = exhausted_block;
                }
                emit_cond_branch(eq, gen_state_blocks[i], next_check);
                if (i < gen_state_count - 1) {
                    switch_to_block(next_check);
                }
            }
        }

        /* Exhausted block — return NULL (StopIteration) */
        switch_to_block(exhausted_block);
        emit_return(pir_value_none());

        end_func();
    } else {
        /* --- Regular function --- */
        PIRFunction *func = begin_func(fname);
        func->num_params = param_count;

        /* Add parameters (skip bare * separator) */
        for (param = node->data.func_def.params; param; param = param->next) {
            if (is_bare_star_sep(param)) continue;
            PIRValue pval = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
            func->params.push_back(pval);
            var_alloca(param->name);
            var_store(param->name, pval);
        }

        /* Set up cells from closure for nonlocal (free) variables */
        if (node->data.func_def.num_free_vars > 0) {
            int fv;
            PIRValue closure;
            PIRInst *lc;

            cell_map = new PdHashMap<const char *, PIRValue>(
                (PdHashMap<const char *, PIRValue>::HashFn)pd_hash_str,
                (PdHashMap<const char *, PIRValue>::EqFn)pd_eq_str);

            /* Load the active closure */
            closure = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
            lc = emit(PIR_LOAD_CLOSURE);
            lc->result = closure;

            /* Extract cells from the closure list by index */
            for (fv = 0; fv < node->data.func_def.num_free_vars; fv++) {
                const char *fvname = node->data.func_def.free_var_names[fv];
                PIRValue idx = emit_const_int(fv);
                PIRValue cell = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
                PIRInst *sg = emit(PIR_SUBSCR_GET);
                sg->result = cell;
                sg->operands[0] = closure;
                sg->operands[1] = idx;
                sg->num_operands = 2;
                cell_map->put(fvname, cell);
            }
        }

        /* Create cells for captured (cell) variables */
        if (node->data.func_def.num_cell_vars > 0) {
            int cv;

            if (!cell_map) {
                cell_map = new PdHashMap<const char *, PIRValue>(
                (PdHashMap<const char *, PIRValue>::HashFn)pd_hash_str,
                (PdHashMap<const char *, PIRValue>::EqFn)pd_eq_str);
            }

            for (cv = 0; cv < node->data.func_def.num_cell_vars; cv++) {
                const char *cvname = node->data.func_def.cell_var_names[cv];
                PIRValue cell;
                PIRInst *mk_cell;

                cell = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
                mk_cell = emit(PIR_MAKE_CELL);
                mk_cell->result = cell;

                /* If the cell var is a parameter, seed the cell with its value.
                   var_load goes through alloca since cell_map doesn't have cvname yet. */
                if (var_map->has(cvname)) {
                    PIRValue cur = var_load(cvname);
                    PIRInst *cs = emit(PIR_CELL_SET);
                    cs->operands[0] = cell;
                    cs->operands[1] = cur;
                    cs->num_operands = 2;
                }

                /* Register in cell_map — future accesses go through cell */
                cell_map->put(cvname, cell);
            }
        }

        /* Generate body */
        build_stmts(node->data.func_def.body);

        end_func();
    }

    /* Restore outer context */
    if (var_map) delete var_map;
    var_map = outer_var_map;
    if (cell_map) delete cell_map;
    cell_map = outer_cell_map;
    if (closure_map) delete closure_map;
    closure_map = outer_closure_map;
    nested_names = outer_nested_names;
    current_func = outer_func;
    current_block = outer_block;
    current_class_name = outer_class;
    current_base_class_name = outer_base;
    current_method_class_name = outer_method_class;
    current_method_first_param = outer_method_first;
    gen_val = outer_gen_val;
    gen_num_locals = outer_gen_locals;
    gen_state_count = outer_gen_states;
    gen_local_count = outer_gen_local_count;
    gen_for_iter_count = outer_gen_for_iter_count;
    loop_depth = outer_loop_depth;
    return_cleanup_depth = outer_return_cleanup_depth;
    exception_target_depth = outer_exception_target_depth;
    exception_exit_block = outer_exception_exit_block;
    suppress_exception_checks = outer_suppress_exception_checks;
    handled_exception_depth = outer_handled_exception_depth;
    is_building_coroutine = outer_is_building_coroutine;
    memcpy(break_targets, outer_break_targets, sizeof(break_targets));
    memcpy(continue_targets, outer_continue_targets,
           sizeof(continue_targets));
    memcpy(loop_cleanup_depths, outer_loop_cleanup_depths,
           sizeof(loop_cleanup_depths));
    memcpy(return_cleanups, outer_return_cleanups,
           sizeof(return_cleanups));
    memcpy(exception_targets, outer_exception_targets,
           sizeof(exception_targets));
    memcpy(handled_exceptions, outer_handled_exceptions,
           sizeof(handled_exceptions));
    memcpy(gen_state_blocks, outer_gen_state_blocks, sizeof(gen_state_blocks));
    memcpy(gen_local_names, outer_gen_local_names, sizeof(gen_local_names));

    /* In module init or enclosing function, create function object */
    if (current_func && !current_class_name) {
        int name_ci = add_const_str(py_name, (int)strlen(py_name));
        PIRValue closure_list = pir_value_none();

        /* If inner function has free vars, build closure list from outer cells */
        if (node->data.func_def.num_free_vars > 0 && cell_map) {
            int fv;
            PIRInst *nl;

            closure_list = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            nl = emit(PIR_LIST_NEW);
            nl->result = closure_list;

            for (fv = 0; fv < node->data.func_def.num_free_vars; fv++) {
                const char *fvname = node->data.func_def.free_var_names[fv];
                PIRValue *cv = cell_map->get(fvname);
                if (cv) {
                    PIRInst *ap = emit(PIR_LIST_APPEND);
                    ap->operands[0] = closure_list;
                    ap->operands[1] = *cv;
                    ap->num_operands = 2;
                }
            }
        }

        PIRValue fobj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRValue function_type_params = pir_value_none();
        PIRInst *mk = emit(PIR_MAKE_FUNCTION);
        mk->result = fobj;
        mk->str_val = pir_str_dup(fname);
        mk->int_val = name_ci;

        /* Attach closure to function object */
        if (pir_value_valid(closure_list)) {
            mk->operands[0] = closure_list;
            mk->num_operands = 1;

            /* Record in closure_map so build_call can emit SET_CLOSURE
               before direct calls to this function */
            if (!closure_map) {
                closure_map = new PdHashMap<const char *, PIRValue>(
                    (PdHashMap<const char *, PIRValue>::HashFn)pd_hash_str,
                    (PdHashMap<const char *, PIRValue>::EqFn)pd_eq_str);
            }
            closure_map->put(pir_str_dup(py_name), closure_list);
        }

        attach_function_defaults(node, fobj);
        attach_parameter_metadata(node->data.func_def.params, fobj);
        attach_code_metadata(node, fobj, py_name);
        if (node->data.func_def.num_type_params > 0)
            function_type_params = attach_type_parameters(
                node->data.func_def.type_param_names,
                node->data.func_def.type_param_bounds,
                node->data.func_def.type_param_kinds,
                node->data.func_def.num_type_params, fobj);
        attach_function_annotations(node, fobj, function_type_params);

        /* @top @bottom def f is top(bottom(f)).  Use an explicit callable
         * operand so the decorated object cannot be optimized back into a
         * direct call of the original code label. */
        for (i = decorator_count - 1; i >= 0; i--) {
            PIRValue decorated;
            PIRInst *pa = emit(PIR_PUSH_ARG);
            PIRInst *call;
            pa->operands[0] = fobj;
            pa->num_operands = 1;
            decorated = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            call = emit(PIR_CALL);
            call->result = decorated;
            call->operands[0] = decorator_vals[i];
            call->num_operands = 1;
            call->int_val = 1;
            fobj = decorated;
        }

        /* A definition binds its name in the current Python scope.  At
         * module level var_store emits STORE_GLOBAL; inside a function it
         * emits a local STORE or CELL_SET.  Keeping nested definitions in
         * the global namespace pins their closures forever and also gives
         * them incorrect Python name-resolution semantics. */
        var_store(py_name, fobj);
    }
}

/* A Python class body may redefine the same name (the canonical example is
 * @property followed by @name.setter and @name.deleter).  Assembly symbols
 * still need unique identities even though every definition targets the same
 * Python class-dictionary key. */
static void class_method_mangled_name(ASTNode *class_node,
                                      ASTNode *method_node,
                                      char *mangled)
{
    ASTNode *candidate;
    int occurrence = 0;
    const char *class_name = class_node->data.class_def.name;
    const char *method_name = method_node->data.func_def.name;
    for (candidate = class_node->data.class_def.body;
         candidate; candidate = candidate->next) {
        if (candidate->kind == AST_FUNC_DEF &&
            candidate->data.func_def.name != 0 &&
            pir_str_eq(candidate->data.func_def.name, method_name))
            occurrence++;
        if (candidate == method_node) break;
    }
    if (occurrence <= 1)
        sprintf(mangled, "%s__%s", class_name, method_name);
    else
        sprintf(mangled, "%s__%s__definition%d", class_name, method_name,
                occurrence);
}

/* Materialize one method at its exact class-body position.  Vtable code is
 * generated separately, but Python reflection and decorators observe the
 * insertion order of the namespace executed by the class body. */
void PIRBuilder::materialize_class_method(ASTNode *class_node,
                                          ASTNode *method_node,
                                          PIRValue class_obj)
{
    const char *method_name = method_node->data.func_def.name;
    char mangled[256];
    PIRValue method_obj;
    PIRValue method_type_params = pir_value_none();
    PIRInst *mk;
    PIRValue evaluated_decorators[8];
    int evaluated_count = 0;
    int decorator_index;
    ASTNode *decorator;

    class_method_mangled_name(class_node, method_node, mangled);
    method_obj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    mk = emit(PIR_MAKE_FUNCTION);
    mk->result = method_obj;
    mk->str_val = pir_str_dup(mangled);
    mk->int_val = add_const_str(method_name, (int)strlen(method_name));
    attach_function_defaults(method_node, method_obj);
    attach_parameter_metadata(method_node->data.func_def.params, method_obj);
    attach_code_metadata(method_node, method_obj, method_name);
    if (method_node->data.func_def.num_type_params > 0)
        method_type_params = attach_type_parameters(
            method_node->data.func_def.type_param_names,
            method_node->data.func_def.type_param_bounds,
            method_node->data.func_def.type_param_kinds,
            method_node->data.func_def.num_type_params, method_obj);
    attach_function_annotations(method_node, method_obj,
                                method_type_params);

    /* Decorator expressions are evaluated top-to-bottom, while application
     * is bottom-to-top.  Attribute decorators such as @value.setter resolve
     * the property already inserted by an earlier class-body statement. */
    for (decorator = method_node->data.func_def.decorators;
         decorator && evaluated_count < 8;
         decorator = decorator->next) {
        if (decorator->kind == AST_ATTR &&
            decorator->data.attribute.object != 0 &&
            decorator->data.attribute.object->kind == AST_NAME) {
            PIRValue namespace_value = pir_func_alloc_value(
                current_func, PIR_TYPE_PYOBJ);
            PIRValue decorator_value = pir_func_alloc_value(
                current_func, PIR_TYPE_PYOBJ);
            PIRInst *get_namespace = emit(PIR_GET_ATTR);
            PIRInst *get_decorator;
            get_namespace->result = namespace_value;
            get_namespace->operands[0] = class_obj;
            get_namespace->num_operands = 1;
            get_namespace->str_val = pir_str_dup(
                decorator->data.attribute.object->data.name.id);
            get_decorator = emit(PIR_GET_ATTR);
            get_decorator->result = decorator_value;
            get_decorator->operands[0] = namespace_value;
            get_decorator->num_operands = 1;
            get_decorator->str_val = pir_str_dup(
                decorator->data.attribute.attr);
            evaluated_decorators[evaluated_count++] = decorator_value;
        } else {
            evaluated_decorators[evaluated_count++] = build_expr(decorator);
        }
    }

    for (decorator_index = evaluated_count - 1;
         decorator_index >= 0; decorator_index--) {
        PIRValue decorated;
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = method_obj;
        push->num_operands = 1;
        decorated = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        call = emit(PIR_CALL);
        call->result = decorated;
        call->operands[0] = evaluated_decorators[decorator_index];
        call->num_operands = 1;
        call->int_val = 1;
        method_obj = decorated;
    }

    {
        PIRInst *set = emit(PIR_SET_ATTR);
        set->operands[0] = class_obj;
        set->operands[1] = method_obj;
        set->num_operands = 2;
        set->str_val = pir_str_dup(method_name);
    }
}

/* --- Class definition --- */
void PIRBuilder::build_classdef(ASTNode *node)
{
    const char *class_name = node->data.class_def.name;
    ASTNode *stmt;
    const char *base_name = 0;
    PIRValue decorator_vals[32];
    int decorator_count = 0;
    int di;
    int general_metaclass = 0;
    int materialize_class_namespace = 0;

    /* Match Python's class decorator evaluation/application ordering. */
    for (stmt = node->data.class_def.decorators;
         stmt && decorator_count < 32;
         stmt = stmt->next) {
        decorator_vals[decorator_count++] = build_expr(stmt);
    }
    if (decorator_count > 0 && num_decorated_classes < 64) {
        decorated_class_names[num_decorated_classes++] =
            pir_str_dup(class_name);
    }
    if (decorator_count > 0) materialize_class_namespace = 1;

    /* Ordinary class variables and decorated methods are observable Python
     * state.  A decorator replaces the value stored under the method name,
     * so leaving such a class on the vtable-only path would call the raw
     * compiled function and bypass property/classmethod/staticmethod or a
     * user-provided replacement entirely. */
    for (stmt = node->data.class_def.body; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_ASSIGN || stmt->kind == AST_ANN_ASSIGN ||
            stmt->kind == AST_CLASS_DEF) {
            materialize_class_namespace = 1;
            break;
        }
        if (stmt->kind == AST_FUNC_DEF &&
            stmt->data.func_def.decorators != 0) {
            materialize_class_namespace = 1;
            break;
        }
    }

    /* Parse base class */
    if (node->data.class_def.bases) {
        ASTNode *base = node->data.class_def.bases;
        base_name = class_base_identifier(base);
    }

    if (node->data.class_def.metaclass &&
        node->data.class_def.metaclass->kind == AST_NAME && sema) {
        Symbol *meta_symbol = sema->lookup(
            node->data.class_def.metaclass->data.name.id);
        TypeInfo *meta_type = meta_symbol ? meta_symbol->type : 0;
        if (class_method_definition(meta_type, "__prepare__") ||
            class_method_definition(meta_type, "__new__") ||
            class_method_definition(meta_type, "__init__"))
            general_metaclass = 1;
    }
    if (!general_metaclass && base_name &&
        uses_general_metaclass(base_name))
        general_metaclass = 1;
    if (node->data.class_def.keywords) general_metaclass = 1;
    if (general_metaclass && num_general_metaclass_classes < 64)
        general_metaclass_class_names[num_general_metaclass_classes++] =
            pir_str_dup(class_name);

    /* Emit comment */
    {
        PIRInst *c = emit(PIR_COMMENT);
        c->str_val = pir_str_dup(class_name);
    }

    /* Register vtable info in the PIR module (lowerer copies to IRModule) */
    int vt_idx = -1;
    if (mod->num_vtables < 128) {
        vt_idx = mod->num_vtables;
        PIRVTableInfo *vti = &mod->vtables[vt_idx];
        vti->class_name = pir_str_dup(class_name);
        vti->display_name = pir_str_dup(
            node->data.class_def.generic_origin
                ? node->data.class_def.generic_origin : class_name);
        vti->base_class_name = base_name ? pir_str_dup(base_name) : 0;
        vti->num_extra_bases = 0;

        /* Collect extra bases (multiple inheritance) */
        if (node->data.class_def.bases) {
            ASTNode *eb = node->data.class_def.bases->next;
            while (eb && vti->num_extra_bases < 7) {
                const char *extra_base = class_base_identifier(eb);
                if (extra_base) {
                    vti->extra_bases[vti->num_extra_bases++] =
                        pir_str_dup(extra_base);
                }
                eb = eb->next;
            }
        }

        /* Walk class body, collecting method names */
        vti->num_methods = 0;
        for (stmt = node->data.class_def.body; stmt; stmt = stmt->next) {
            if (stmt->kind == AST_FUNC_DEF && vti->num_methods < 64) {
                const char *mname = stmt->data.func_def.name;
                char mangled[256];
                class_method_mangled_name(node, stmt, mangled);
                vti->methods[vti->num_methods].python_name = pir_str_dup(mname);
                vti->methods[vti->num_methods].mangled_name = pir_str_dup(mangled);
                {
                    Param *mp;
                    int method_argc = 0;
                    for (mp = stmt->data.func_def.params; mp; mp = mp->next) {
                        if (!is_bare_star_sep(mp)) method_argc++;
                    }
                    vti->methods[vti->num_methods].arg_count = method_argc;
                }
                vti->num_methods++;
            }
        }
        mod->num_vtables++;
    }

    const char *outer_class = current_class_name;
    const char *outer_base = current_base_class_name;
    current_class_name = class_name;
    current_base_class_name = base_name;

    /* Generate methods */
    for (stmt = node->data.class_def.body; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_FUNC_DEF) {
            int first_generated = mod->functions.size();
            /* Mangle name: ClassName__methodname */
            const char *mname = stmt->data.func_def.name;
            char mangled[256];
            class_method_mangled_name(node, stmt, mangled);
            const char *orig = stmt->data.func_def.name;
            stmt->data.func_def.name = pir_str_dup(mangled);
            build_funcdef(stmt);
            stmt->data.func_def.name = orig;

            /* Tag both regular methods and any generator/coroutine helper
             * functions created by build_funcdef(). */
            if (node->data.class_def.generic_origin) {
                int fi;
                char gen_name[272];
                char cor_name[272];
                sprintf(gen_name, "_genresume_%s", mangled);
                sprintf(cor_name, "_corresume_%s", mangled);
                for (fi = first_generated; fi < mod->functions.size(); fi++) {
                    const char *generated_name = mod->functions[fi]->name;
                    if (generated_name &&
                        (pir_str_eq(generated_name, mangled) ||
                         pir_str_eq(generated_name, gen_name) ||
                         pir_str_eq(generated_name, cor_name))) {
                        mod->functions[fi]->generic_origin =
                            pir_str_dup(node->data.class_def.generic_origin);
                        mod->functions[fi]->generic_method_name =
                            pir_str_dup(mname);
                    }
                }
            }
        } else if (stmt->kind == AST_PASS) {
            /* skip */
        }
    }

    current_class_name = outer_class;
    current_base_class_name = outer_base;

    /* Emit vtable init with the proper index */
    {
        PIRInst *init = emit(PIR_INIT_VTABLE);
        init->str_val = pir_str_dup(class_name);
        init->int_val = vt_idx;
    }

    if (node->data.class_def.num_type_params > 0) {
        PIRValue class_obj = var_load(class_name);
        attach_type_parameters(node->data.class_def.type_param_names,
                               node->data.class_def.type_param_bounds,
                               node->data.class_def.type_param_kinds,
                               node->data.class_def.num_type_params,
                               class_obj);
    }

    /* Evaluate method defaults once when the class is created and attach
     * them to the compiled vtable entry.  Dynamic method calls can then pad
     * the fixed generated ABI exactly like statically bound calls do. */
    for (stmt = node->data.class_def.body; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_FUNC_DEF) {
            Param *param;
            int default_count = 0;
            for (param = stmt->data.func_def.params;
                 param; param = param->next) {
                if (!is_bare_star_sep(param) && param->default_val)
                    default_count++;
            }
            if (default_count > 0) {
                PIRValue class_obj = var_load(class_name);
                PIRValue method_name = emit_const_str(
                    stmt->data.func_def.name,
                    (int)strlen(stmt->data.func_def.name));
                PIRValue defaults;
                PIRValue result;
                PIRInst *build_defaults;
                PIRInst *push_class;
                PIRInst *push_name;
                PIRInst *push_defaults;
                PIRInst *call;

                for (param = stmt->data.func_def.params;
                     param; param = param->next) {
                    if (!is_bare_star_sep(param) && param->default_val) {
                        PIRValue default_value = build_expr(
                            param->default_val);
                        PIRInst *push_default = emit(PIR_PUSH_ARG);
                        push_default->operands[0] = default_value;
                        push_default->num_operands = 1;
                    }
                }
                defaults = pir_func_alloc_value(current_func,
                                                PIR_TYPE_PYOBJ);
                build_defaults = emit(PIR_BUILD_TUPLE);
                build_defaults->result = defaults;
                build_defaults->int_val = default_count;

                push_class = emit(PIR_PUSH_ARG);
                push_class->operands[0] = class_obj;
                push_class->num_operands = 1;
                push_name = emit(PIR_PUSH_ARG);
                push_name->operands[0] = method_name;
                push_name->num_operands = 1;
                push_defaults = emit(PIR_PUSH_ARG);
                push_defaults->operands[0] = defaults;
                push_defaults->num_operands = 1;
                result = pir_func_alloc_value(current_func,
                                              PIR_TYPE_PYOBJ);
                call = emit(PIR_CALL);
                call->result = result;
                call->str_val = pir_str_dup(
                    "pydos_class_set_method_defaults");
                call->int_val = 3;
            }
        }
    }

    /* A general metaclass receives a concrete namespace.  Materialize raw
     * methods and simple class-body assignments only for this path; ordinary
     * classes retain the compact vtable-only representation on DOS. */
    if (general_metaclass || materialize_class_namespace) {
        PIRValue class_obj = var_load(class_name);
        ASTNode *annotation_stmt;
        int annotation_count = 0;

        for (annotation_stmt = node->data.class_def.body;
             annotation_stmt; annotation_stmt = annotation_stmt->next) {
            ASTNode *target;
            ASTNode *annotation;
            PIRValue key;
            PIRValue annotation_value;
            PIRInst *push_key;
            PIRInst *push_value;
            if (annotation_stmt->kind != AST_ANN_ASSIGN) continue;
            target = annotation_stmt->data.ann_assign.target;
            annotation = annotation_stmt->data.ann_assign.annotation;
            if (!target || target->kind != AST_NAME) continue;

            key = emit_const_str(target->data.name.id,
                                 (int)strlen(target->data.name.id));
            if (annotation && annotation->kind == AST_TYPE_NAME &&
                annotation->data.type_name.tname) {
                if (pir_str_eq(annotation->data.type_name.tname, "None"))
                    annotation_value = emit_const_none();
                else
                    annotation_value = var_load(
                        annotation->data.type_name.tname);
            } else if (annotation && annotation->kind == AST_TYPE_GENERIC &&
                       annotation->data.type_generic.gname) {
                /* Until parameterized type objects exist at runtime, retain
                 * the concrete origin (list, dict, ...).  Dataclasses needs
                 * field order and identity; it must not require a compiler
                 * intrinsic for annotations. */
                annotation_value = var_load(
                    annotation->data.type_generic.gname);
            } else {
                annotation_value = emit_const_str("object", 6);
            }
            push_key = emit(PIR_PUSH_ARG);
            push_key->operands[0] = key;
            push_key->num_operands = 1;
            push_value = emit(PIR_PUSH_ARG);
            push_value->operands[0] = annotation_value;
            push_value->num_operands = 1;
            annotation_count++;
        }
        if (annotation_count > 0) {
            PIRValue annotations = pir_func_alloc_value(current_func,
                                                         PIR_TYPE_PYOBJ);
            PIRInst *build_annotations = emit(PIR_BUILD_DICT);
            PIRInst *set_annotations;
            build_annotations->result = annotations;
            build_annotations->int_val = annotation_count;
            set_annotations = emit(PIR_SET_ATTR);
            set_annotations->operands[0] = class_obj;
            set_annotations->operands[1] = annotations;
            set_annotations->num_operands = 2;
            set_annotations->str_val = pir_str_dup("__annotations__");
        }

        for (stmt = node->data.class_def.body; stmt; stmt = stmt->next) {
            if (stmt->kind == AST_FUNC_DEF) {
                materialize_class_method(node, stmt, class_obj);
            } else if (stmt->kind == AST_ASSIGN &&
                       stmt->data.assign.targets != 0 &&
                       stmt->data.assign.targets->kind == AST_NAME) {
                PIRValue value = build_expr(stmt->data.assign.value);
                PIRInst *set = emit(PIR_SET_ATTR);
                set->operands[0] = class_obj;
                set->operands[1] = value;
                set->num_operands = 2;
                set->str_val = pir_str_dup(
                    stmt->data.assign.targets->data.name.id);
            } else if (stmt->kind == AST_ANN_ASSIGN &&
                       stmt->data.ann_assign.value != 0 &&
                       stmt->data.ann_assign.target != 0 &&
                       stmt->data.ann_assign.target->kind == AST_NAME) {
                PIRValue value = build_expr(
                    stmt->data.ann_assign.value);
                PIRInst *set = emit(PIR_SET_ATTR);
                set->operands[0] = class_obj;
                set->operands[1] = value;
                set->num_operands = 2;
                set->str_val = pir_str_dup(
                    stmt->data.ann_assign.target->data.name.id);
            } else if (stmt->kind == AST_CLASS_DEF) {
                const char *inner_name = stmt->data.class_def.name;
                const char *old_origin = stmt->data.class_def.generic_origin;
                char qualified[256];
                PIRValue inner_class;
                PIRInst *set;

                /* Assembly and global symbols must be unique even when two
                 * enclosing classes use the same nested class name.  The
                 * runtime display name remains the lexical Python name. */
                sprintf(qualified, "%s__%s", class_name, inner_name);
                stmt->data.class_def.name = pir_str_dup(qualified);
                stmt->data.class_def.generic_origin = inner_name;
                build_classdef(stmt);
                stmt->data.class_def.name = inner_name;
                stmt->data.class_def.generic_origin = old_origin;

                inner_class = var_load(qualified);
                set = emit(PIR_SET_ATTR);
                set->operands[0] = class_obj;
                set->operands[1] = inner_class;
                set->num_operands = 2;
                set->str_val = pir_str_dup(inner_name);
            }
        }
    }

    /* A class without a metaclass path still needs type.__new__'s
     * descriptor finalization step.  Metaclass helpers perform the same
     * step at the correct point in their own construction protocol. */
    if (!node->data.class_def.metaclass &&
        !node->data.class_def.bases) {
        PIRValue class_obj = var_load(class_name);
        PIRValue set_names_result = pir_func_alloc_value(
            current_func, PIR_TYPE_PYOBJ);
        PIRInst *pa = emit(PIR_PUSH_ARG);
        PIRInst *call;
        pa->operands[0] = class_obj;
        pa->num_operands = 1;
        call = emit(PIR_CALL);
        call->result = set_names_result;
        call->str_val = pir_str_dup("pydos_class_set_names");
        call->int_val = 1;
    }

    if (node->data.class_def.metaclass) {
        PIRValue class_obj = var_load(class_name);
        PIRValue metaclass_obj = build_expr(node->data.class_def.metaclass);
        PIRValue keyword_dict;
        PIRValue hook_result = pir_func_alloc_value(current_func,
                                                     PIR_TYPE_PYOBJ);
        PIRInst *pa_class;
        PIRInst *pa_meta;
        PIRInst *pa_keywords;
        PIRInst *call;
        ASTNode *keyword;
        int keyword_count = 0;
        for (keyword = node->data.class_def.keywords;
             keyword; keyword = keyword->next) {
            PIRValue key = emit_const_str(
                keyword->data.keyword_arg.key,
                (int)strlen(keyword->data.keyword_arg.key));
            PIRValue value = build_expr(
                keyword->data.keyword_arg.kw_value);
            PIRInst *push_key = emit(PIR_PUSH_ARG);
            PIRInst *push_value;
            push_key->operands[0] = key;
            push_key->num_operands = 1;
            push_value = emit(PIR_PUSH_ARG);
            push_value->operands[0] = value;
            push_value->num_operands = 1;
            keyword_count++;
        }
        keyword_dict = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *build_keywords = emit(PIR_BUILD_DICT);
            build_keywords->result = keyword_dict;
            build_keywords->int_val = keyword_count;
        }
        pa_class = emit(PIR_PUSH_ARG);
        pa_class->operands[0] = class_obj;
        pa_class->num_operands = 1;
        pa_meta = emit(PIR_PUSH_ARG);
        pa_meta->operands[0] = metaclass_obj;
        pa_meta->num_operands = 1;
        pa_keywords = emit(PIR_PUSH_ARG);
        pa_keywords->operands[0] = keyword_dict;
        pa_keywords->num_operands = 1;
        call = emit(PIR_CALL);
        call->result = hook_result;
        call->str_val = pir_str_dup(
            "pydos_class_apply_metaclass_protocol");
        call->int_val = 3;
        var_store(class_name, hook_result);
    } else if (node->data.class_def.bases) {
        PIRValue class_obj = var_load(class_name);
        PIRValue hook_result = pir_func_alloc_value(current_func,
                                                     PIR_TYPE_PYOBJ);
        PIRInst *pa;
        PIRInst *call;
        PIRValue keyword_dict;
        ASTNode *keyword;
        int keyword_count = 0;

        if (general_metaclass) {
            for (keyword = node->data.class_def.keywords;
                 keyword; keyword = keyword->next) {
                PIRValue key = emit_const_str(
                    keyword->data.keyword_arg.key,
                    (int)strlen(keyword->data.keyword_arg.key));
                PIRValue value = build_expr(
                    keyword->data.keyword_arg.kw_value);
                PIRInst *push_key = emit(PIR_PUSH_ARG);
                PIRInst *push_value;
                push_key->operands[0] = key;
                push_key->num_operands = 1;
                push_value = emit(PIR_PUSH_ARG);
                push_value->operands[0] = value;
                push_value->num_operands = 1;
                keyword_count++;
            }
            keyword_dict = pir_func_alloc_value(current_func,
                                                 PIR_TYPE_PYOBJ);
            {
                PIRInst *build_keywords = emit(PIR_BUILD_DICT);
                build_keywords->result = keyword_dict;
                build_keywords->int_val = keyword_count;
            }
        }

        pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = class_obj;
        pa->num_operands = 1;
        if (general_metaclass) {
            PIRInst *pa_keywords = emit(PIR_PUSH_ARG);
            pa_keywords->operands[0] = keyword_dict;
            pa_keywords->num_operands = 1;
        }
        call = emit(PIR_CALL);
        call->result = hook_result;
        if (general_metaclass) {
            call->str_val = pir_str_dup(
                "pydos_class_apply_inherited_metaclass_protocol");
            call->int_val = 2;
            var_store(class_name, hook_result);
        } else {
            call->str_val = pir_str_dup(
                "pydos_class_apply_inherited_metaclass");
            call->int_val = 1;
        }
    }

    if (decorator_count > 0) {
        PIRValue class_obj = var_load(class_name);
        for (di = decorator_count - 1; di >= 0; di--) {
            PIRValue decorated;
            PIRInst *pa = emit(PIR_PUSH_ARG);
            PIRInst *call;
            pa->operands[0] = class_obj;
            pa->num_operands = 1;
            decorated = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            call = emit(PIR_CALL);
            call->result = decorated;
            call->operands[0] = decorator_vals[di];
            call->num_operands = 1;
            call->int_val = 1;
            class_obj = decorated;
        }
        var_store(class_name, class_obj);
    }
}

/* --- If statement --- */
void PIRBuilder::build_if(ASTNode *node)
{
    PIRBlock *then_block = new_block("then");
    PIRBlock *else_block = new_block("else");
    PIRBlock *merge_block = new_block("if_merge");

    PIRValue cond = build_expr(node->data.if_stmt.condition);
    emit_cond_branch(cond, then_block, else_block);

    /* Then */
    switch_to_block(then_block);
    build_stmts(node->data.if_stmt.body);
    if (!block_is_terminated()) emit_branch(merge_block);

    /* Else */
    switch_to_block(else_block);
    if (node->data.if_stmt.else_body) {
        build_stmts(node->data.if_stmt.else_body);
    }
    if (!block_is_terminated()) emit_branch(merge_block);

    switch_to_block(merge_block);
}

/* --- While statement --- */
void PIRBuilder::build_while(ASTNode *node)
{
    PIRBlock *cond_block = new_block("while_cond");
    PIRBlock *body_block = new_block("while_body");
    PIRBlock *else_block = node->data.while_stmt.else_body
                           ? new_block("while_else") : 0;
    PIRBlock *end_block = new_block("while_end");

    /* Push loop targets */
    break_targets[loop_depth] = end_block;
    continue_targets[loop_depth] = cond_block;
    loop_cleanup_depths[loop_depth] = return_cleanup_depth;
    loop_depth++;

    emit_branch(cond_block);

    /* Condition */
    switch_to_block(cond_block);
    PIRValue cond = build_expr(node->data.while_stmt.condition);
    emit_cond_branch(cond, body_block, else_block ? else_block : end_block);

    /* Body */
    switch_to_block(body_block);
    build_stmts(node->data.while_stmt.body);
    if (!block_is_terminated()) emit_branch(cond_block);

    /* Else */
    if (else_block) {
        switch_to_block(else_block);
        build_stmts(node->data.while_stmt.else_body);
        if (!block_is_terminated()) emit_branch(end_block);
    }

    loop_depth--;
    switch_to_block(end_block);
}

/* Constant folding of a range() step so the loop can pick its comparison.
 * Returns 0 when the step is not a compile-time integer. */
static int range_step_constant(ASTNode *step, long *out)
{
    if (!step) { *out = 1; return 1; }
    if (step->kind == AST_INT_LIT) {
        *out = step->data.int_lit.value;
        return 1;
    }
    if (step->kind == AST_UNARYOP && step->data.unaryop.operand &&
        step->data.unaryop.operand->kind == AST_INT_LIT) {
        long value = step->data.unaryop.operand->data.int_lit.value;
        if (step->data.unaryop.op == UNARY_NEG) { *out = -value; return 1; }
        if (step->data.unaryop.op == UNARY_POS) { *out = value; return 1; }
    }
    return 0;
}

/* --- For statement: detect range() pattern --- */
static int is_range_call(ASTNode *iter)
{
    if (!iter) return 0;
    if (iter->kind != AST_CALL) return 0;
    if (!iter->data.call.func) return 0;
    if (iter->data.call.func->kind != AST_NAME) return 0;
    if (strcmp(iter->data.call.func->data.name.id, "range") != 0) return 0;
    /* 1-3 args, no keyword args */
    if (iter->data.call.num_args < 1 || iter->data.call.num_args > 3) return 0;
    return 1;
}

/* The fast path needs a known non-zero step. */
static int range_step_uses_fast_path(ASTNode *iter)
{
    ASTNode *arg;
    long step = 1;
    int index = 0;
    if (iter->data.call.num_args < 3) return 1;
    for (arg = iter->data.call.args; arg; arg = arg->next, index++) {
        if (index == 2) return range_step_constant(arg, &step) && step != 0;
    }
    return 0;
}

/* --- For statement --- */
void PIRBuilder::build_for(ASTNode *node)
{
    ASTNode *iter_node = node->data.for_stmt.iter;
    ASTNode *target_node = node->data.for_stmt.target;

    /* Optimized range() loop: for simple_name in range(...).  The direction
     * of the loop test comes from the step, so a step that is not a known
     * non-zero constant uses the general iterator instead. */
    if (is_range_call(iter_node) && target_node->kind == AST_NAME &&
        range_step_uses_fast_path(iter_node)) {
        int nargs = iter_node->data.call.num_args;
        ASTNode *arg1 = iter_node->data.call.args;
        ASTNode *arg2 = arg1 ? arg1->next : 0;
        ASTNode *arg3 = arg2 ? arg2->next : 0;
        long step_value = 1;

        PIRValue range_start, range_stop, range_step;

        range_step_constant(nargs == 3 ? arg3 : 0, &step_value);

        if (nargs == 1) {
            /* range(stop): start=0, step=1 */
            range_start = emit_const_int(0);
            range_stop  = build_expr(arg1);
            range_step  = emit_const_int(1);
        } else if (nargs == 2) {
            /* range(start, stop): step=1 */
            range_start = build_expr(arg1);
            range_stop  = build_expr(arg2);
            range_step  = emit_const_int(1);
        } else {
            /* range(start, stop, step) */
            range_start = build_expr(arg1);
            range_stop  = build_expr(arg2);
            range_step  = build_expr(arg3);
        }

        PIRBlock *check_block = new_block("range_check");
        PIRBlock *body_block  = new_block("range_body");
        PIRBlock *incr_block  = new_block("range_incr");
        PIRBlock *else_block  = node->data.for_stmt.else_body
                                 ? new_block("range_else") : 0;
        PIRBlock *end_block   = new_block("range_end");

        /* Initialize counter = start */
        const char *var_name = target_node->data.name.id;
        const char *stop_name = 0;
        const char *step_name = 0;
        if (!var_exists(var_name)) {
            var_alloca(var_name);
        }
        var_store(var_name, range_start);

        /* Inside a generator only named locals survive a yield, so the loop
           bounds need allocas of their own; the counter already has one. */
        if (pir_value_valid(gen_val)) {
            char bound_name[64];
            sprintf(bound_name, "__forstop_%d__", gen_for_iter_count);
            stop_name = pir_str_dup(bound_name);
            sprintf(bound_name, "__forstep_%d__", gen_for_iter_count);
            step_name = pir_str_dup(bound_name);
            gen_for_iter_count++;
            var_alloca(stop_name);
            var_store(stop_name, range_stop);
            var_alloca(step_name);
            var_store(step_name, range_step);
        }

        /* Push loop targets: break->end, continue->incr */
        break_targets[loop_depth] = end_block;
        continue_targets[loop_depth] = incr_block;
        loop_cleanup_depths[loop_depth] = return_cleanup_depth;
        loop_depth++;

        emit_branch(check_block);

        /* Check: counter < stop */
        switch_to_block(check_block);
        {
            PIRValue counter = var_load(var_name);
            PIRValue stop_val = stop_name ? var_load(stop_name) : range_stop;
            PIRValue cmp_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *cmp = emit(step_value < 0 ? PIR_PY_CMP_GT : PIR_PY_CMP_LT);
            cmp->result = cmp_result;
            cmp->operands[0] = counter;
            cmp->operands[1] = stop_val;
            cmp->num_operands = 2;
            emit_cond_branch(cmp_result, body_block,
                             else_block ? else_block : end_block);
        }

        /* Body */
        switch_to_block(body_block);
        build_stmts(node->data.for_stmt.body);
        if (!block_is_terminated()) emit_branch(incr_block);

        /* Increment: counter += step */
        switch_to_block(incr_block);
        {
            PIRValue cur = var_load(var_name);
            PIRValue step_val = step_name ? var_load(step_name) : range_step;
            PIRValue next_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *add = emit(PIR_PY_ADD);
            add->result = next_val;
            add->operands[0] = cur;
            add->operands[1] = step_val;
            add->num_operands = 2;
            var_store(var_name, next_val);
        }
        emit_branch(check_block);

        /* Else (for/else: runs on normal completion) */
        if (else_block) {
            switch_to_block(else_block);
            build_stmts(node->data.for_stmt.else_body);
            if (!block_is_terminated()) emit_branch(end_block);
        }

        loop_depth--;
        switch_to_block(end_block);
        return;
    }

    /* Generic iterator-based for loop */
    PIRBlock *loop_block = new_block("for_loop");
    PIRBlock *body_block = new_block("for_body");
    PIRBlock *else_block = node->data.for_stmt.else_body
                           ? new_block("for_else") : 0;
    PIRBlock *end_block = new_block("for_end");

    /* Get iterator */
    PIRValue iter_src = build_expr(node->data.for_stmt.iter);
    PIRValue iter_obj;
    const char *iter_alloca_name = 0;

    if (pir_value_valid(gen_val)) {
        /* Generator: store iterator in named alloca so build_yield()
           discovers it via var_map and saves/restores across yields */
        char iname[64];
        sprintf(iname, "__foriter_%d__", gen_for_iter_count++);
        iter_alloca_name = pir_str_dup(iname);
        var_alloca(iter_alloca_name);
        PIRValue raw = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *gi = emit(PIR_GET_ITER);
            gi->result = raw;
            gi->operands[0] = iter_src;
            gi->num_operands = 1;
        }
        var_store(iter_alloca_name, raw);
        iter_obj = pir_value_none(); /* placeholder, loaded in loop_block */
    } else {
        /* Non-generator: anonymous temp (unchanged) */
        iter_obj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *gi = emit(PIR_GET_ITER);
            gi->result = iter_obj;
            gi->operands[0] = iter_src;
            gi->num_operands = 1;
        }
    }

    /* Push loop targets */
    break_targets[loop_depth] = end_block;
    continue_targets[loop_depth] = loop_block;
    loop_cleanup_depths[loop_depth] = return_cleanup_depth;
    loop_depth++;

    emit_branch(loop_block);

    /* Loop: get next item */
    switch_to_block(loop_block);

    /* In generator, reload iterator from alloca (may have been restored
       from gen.locals after a yield resume) */
    if (iter_alloca_name) {
        iter_obj = var_load(iter_alloca_name);
    }

    PIRValue item = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *fi = emit(PIR_FOR_ITER);
        fi->result = item;
        fi->operands[0] = iter_obj;
        fi->num_operands = 1;
        fi->handler_block = else_block ? else_block : end_block;
        /* FOR_ITER: branch to handler_block on StopIteration, fall through otherwise */
        pir_block_add_edge(loop_block, else_block ? else_block : end_block);
    }
    /* Store item to target variable */
    build_store(node->data.for_stmt.target, item);
    emit_branch(body_block);

    /* Body */
    switch_to_block(body_block);
    build_stmts(node->data.for_stmt.body);
    if (!block_is_terminated()) emit_branch(loop_block);

    /* Else */
    if (else_block) {
        switch_to_block(else_block);
        build_stmts(node->data.for_stmt.else_body);
        if (!block_is_terminated()) emit_branch(end_block);
    }

    loop_depth--;
    switch_to_block(end_block);
}

/* --- Assignment --- */
void PIRBuilder::build_assign(ASTNode *node)
{
    PIRValue val = build_expr(node->data.assign.value);
    ASTNode *target;
    for (target = node->data.assign.targets; target; target = target->next) {
        build_store(target, val);
    }
}

void PIRBuilder::build_ann_assign(ASTNode *node)
{
    if (node->data.ann_assign.value) {
        PIRValue val = build_expr(node->data.ann_assign.value);
        build_store(node->data.ann_assign.target, val);
    }
}

void PIRBuilder::build_aug_assign(ASTNode *node)
{
    /* target op= value  →  target = inplace(target, value, op_idx) */
    PIRValue left = build_expr(node->data.aug_assign.target);
    PIRValue right = build_expr(node->data.aug_assign.value);

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_PY_INPLACE);
    inst->result = result;
    inst->operands[0] = left;
    inst->operands[1] = right;
    inst->num_operands = 2;
    inst->int_val = (long)binop_to_inplace_idx(node->data.aug_assign.op);

    build_store(node->data.aug_assign.target, result);
}

void PIRBuilder::build_return(ASTNode *node)
{
    PIRValue return_value = pir_value_none();
    int has_value = node->data.ret.value != 0;

    /* Python evaluates the return expression before running finally. */
    if (has_value) return_value = build_expr(node->data.ret.value);

    if (emit_cleanups_to_depth(0)) return;

    if (pir_value_valid(gen_val)) {
        /* Generator/coroutine resume function: return signals exhaustion.
         * Must return NULL (not None object) so the runtime treats it
         * as StopIteration. */
        if (has_value) {
            /* Generator return <value> becomes StopIteration.value;
             * coroutine completion uses the same state field. */
            PIRInst *sr = emit(PIR_COR_SET_RESULT);
            sr->operands[0] = gen_val;
            sr->operands[1] = return_value;
            sr->num_operands = 2;
        }
        emit_return(pir_value_none()); /* pir_value_none() → IR_RETURN -1 → NULL */
        return;
    }

    if (has_value) {
        emit_return(return_value);
    } else {
        emit_return_none();
    }
}

int PIRBuilder::emit_cleanups_to_depth(int target_depth)
{
    int cleanup_count = return_cleanup_depth;
    int cleanup_index;

    if (target_depth < 0) target_depth = 0;
    if (target_depth > cleanup_count) target_depth = cleanup_count;

    /* Inline the active cleanup chain.  This avoids heap-allocating a
     * pending control-flow record on 8086 and is shared by return, break and
     * continue. */
    for (cleanup_index = cleanup_count - 1;
         cleanup_index >= target_depth; cleanup_index--) {
        ReturnCleanup cleanup = return_cleanups[cleanup_index];
        int pop_index;
        for (pop_index = 0; pop_index < cleanup.pop_count; pop_index++)
            emit(PIR_POP_TRY);
        if (cleanup.manager_name[0] != '\0') {
            PIRValue none_value = emit_const_none();
            emit_context_exit(cleanup.manager_name, none_value, none_value,
                              none_value);
        } else if (cleanup.finally_body) {
            return_cleanup_depth = cleanup_index;
            build_stmts(cleanup.finally_body);
            if (block_is_terminated()) {
                return_cleanup_depth = cleanup_count;
                return 1;
            }
        }
    }
    return_cleanup_depth = cleanup_count;
    return 0;
}

void PIRBuilder::build_expr_stmt(ASTNode *node)
{
    if (node->data.expr_stmt.expr) {
        build_expr(node->data.expr_stmt.expr);
    }
}

/* --- Try/except --- */
void PIRBuilder::build_try(ASTNode *node)
{
    PIRBlock *handler_block = new_block("except");
    PIRBlock *end_block = new_block("try_end");
    PIRBlock *finally_block = node->data.try_stmt.finally_body
                              ? new_block("finally") : 0;
    PIRBlock *finally_guard = finally_block
                              ? new_block("finally_guard") : 0;
    int cleanup_base = return_cleanup_depth;
    int exception_base = exception_target_depth;

    /* Setup try (outer finally guard if present) */
    if (finally_guard) {
        PIRInst *st = emit(PIR_SETUP_TRY);
        st->handler_block = finally_guard;
        pir_block_add_edge(current_block, finally_guard);
        if (exception_target_depth < 32)
            exception_targets[exception_target_depth++] = finally_guard;
    }

    /* Setup try (inner except handlers) */
    {
        PIRInst *st = emit(PIR_SETUP_TRY);
        st->handler_block = handler_block;
        pir_block_add_edge(current_block, handler_block);
        if (exception_target_depth < 32)
            exception_targets[exception_target_depth++] = handler_block;
    }

    /* Try body */
    if (return_cleanup_depth < 32) {
        return_cleanups[return_cleanup_depth].finally_body =
            node->data.try_stmt.finally_body;
        return_cleanups[return_cleanup_depth].pop_count = finally_block ? 2 : 1;
        return_cleanups[return_cleanup_depth].manager_name[0] = '\0';
        return_cleanup_depth++;
    }
    build_stmts(node->data.try_stmt.body);
    return_cleanup_depth = cleanup_base;
    if (exception_target_depth > exception_base + (finally_guard ? 1 : 0))
        exception_target_depth--;

    /* Pop inner try */
    if (!block_is_terminated()) emit(PIR_POP_TRY);

    /* The else suite runs only after a normal try exit.  At this point the
     * except frame is gone, but an outer finally guard is still active. */
    if (!block_is_terminated() && node->data.try_stmt.else_body) {
        if (finally_block && return_cleanup_depth < 32) {
            return_cleanups[return_cleanup_depth].finally_body =
                node->data.try_stmt.finally_body;
            return_cleanups[return_cleanup_depth].pop_count = 1;
            return_cleanups[return_cleanup_depth].manager_name[0] = '\0';
            return_cleanup_depth++;
        }
        build_stmts(node->data.try_stmt.else_body);
        return_cleanup_depth = cleanup_base;
    }

    /* Normal exit: jump to finally or end */
    if (!block_is_terminated()) {
        emit_branch(finally_block ? finally_block : end_block);
    }

    /* Exception handlers */
    switch_to_block(handler_block);
    emit(PIR_POP_TRY);
    if (finally_block && return_cleanup_depth < 32) {
        return_cleanups[return_cleanup_depth].finally_body =
            node->data.try_stmt.finally_body;
        return_cleanups[return_cleanup_depth].pop_count = 1;
        return_cleanups[return_cleanup_depth].manager_name[0] = '\0';
        return_cleanup_depth++;
    }

    /* Check if any handler uses except* */
    {
        int has_star = 0;
        ASTNode *handler;
        for (handler = node->data.try_stmt.handlers; handler; handler = handler->next) {
            if (handler->kind == AST_EXCEPT_HANDLER && handler->data.handler.is_star) {
                has_star = 1;
                break;
            }
        }

        if (!has_star) {
            /* Regular except handlers */
            for (handler = node->data.try_stmt.handlers; handler; handler = handler->next) {
                if (handler->kind != AST_EXCEPT_HANDLER) continue;

                if (handler->data.handler.type) {
                    /* Typed handler: check exception match */
                    PIRBlock *handler_body = new_block("handler_body");
                    PIRBlock *next_handler = handler->next ? new_block("next_handler")
                                                           : new_block("unhandled");

                    PIRValue exc = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    {
                        PIRInst *ge = emit(PIR_GET_EXCEPTION);
                        ge->result = exc;
                    }

                    const char *type_name = 0;
                    if (handler->data.handler.type->kind == AST_NAME) {
                        type_name = handler->data.handler.type->data.name.id;
                    }
                    int code = map_exc_name_to_code(type_name, stdlib_reg_);

                    PIRValue match = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    {
                        PIRInst *em = emit(PIR_EXC_MATCH);
                        em->result = match;
                        em->operands[0] = exc;
                        em->num_operands = 1;
                        em->int_val = code;
                    }

                    emit_cond_branch(match, handler_body, next_handler);

                    switch_to_block(handler_body);

                    emit(PIR_CLEAR_EXCEPTION);

                    /* Bind exception name if present */
                    if (handler->data.handler.name) {
                        var_store(handler->data.handler.name, exc);
                    }

                    if (handled_exception_depth < 32)
                        handled_exceptions[handled_exception_depth++] = exc;
                    build_stmts(handler->data.handler.body);
                    if (handled_exception_depth > 0)
                        handled_exception_depth--;
                    if (!block_is_terminated()) {
                        emit_branch(finally_block ? finally_block : end_block);
                    }

                    switch_to_block(next_handler);
                } else {
                    /* Bare except */
                    PIRValue exc = pir_func_alloc_value(current_func,
                                                        PIR_TYPE_PYOBJ);
                    {
                        PIRInst *ge = emit(PIR_GET_EXCEPTION);
                        ge->result = exc;
                    }
                    emit(PIR_CLEAR_EXCEPTION);
                    if (handler->data.handler.name) {
                        var_store(handler->data.handler.name, exc);
                    }
                    if (handled_exception_depth < 32)
                        handled_exceptions[handled_exception_depth++] = exc;
                    build_stmts(handler->data.handler.body);
                    if (handled_exception_depth > 0)
                        handled_exception_depth--;
                    if (!block_is_terminated()) {
                        emit_branch(finally_block ? finally_block : end_block);
                    }
                }
            }
        } else {
            /* except* handlers — exception group splitting */
            /* Store a synthetic variable holding the current remainder */
            char remainder_name[48];
            int rem_id = synth_counter_++;
            sprintf(remainder_name, "__excg_rem_%d__", rem_id);
            var_alloca(remainder_name);

            /* Start with current exception as the remainder */
            PIRValue exc_init = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *ge = emit(PIR_GET_EXCEPTION);
                ge->result = exc_init;
            }
            emit(PIR_CLEAR_EXCEPTION);
            var_store(remainder_name, exc_init);

            for (handler = node->data.try_stmt.handlers; handler; handler = handler->next) {
                if (handler->kind != AST_EXCEPT_HANDLER) continue;
                if (!handler->data.handler.is_star) continue;
                if (!handler->data.handler.type) continue;

                PIRBlock *star_body = new_block("star_body");
                PIRBlock *star_next = handler->next ? new_block("star_next")
                                                     : new_block("star_end");

                /* Load current remainder */
                PIRValue rem = var_load(remainder_name);

                /* Check if remainder is None (all matched already) */
                PIRValue none_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRInst *ci = emit(PIR_CONST_NONE);
                    ci->result = none_val;
                }
                PIRValue rem_is_none = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRInst *cmp = emit(PIR_PY_IS);
                    cmp->result = rem_is_none;
                    cmp->operands[0] = rem;
                    cmp->operands[1] = none_val;
                    cmp->num_operands = 2;
                }

                PIRBlock *rem_not_none = new_block("rem_notnone");
                emit_cond_branch(rem_is_none, star_next, rem_not_none);
                switch_to_block(rem_not_none);

                /* Call pydos_excgroup_match(exc, type_code) */
                const char *type_name = 0;
                if (handler->data.handler.type->kind == AST_NAME) {
                    type_name = handler->data.handler.type->data.name.id;
                }
                int code = map_exc_name_to_code(type_name, stdlib_reg_);

                /* Push args: remainder, type_code */
                {
                    PIRValue rem2 = var_load(remainder_name);
                    PIRInst *pa1 = emit(PIR_PUSH_ARG);
                    pa1->operands[0] = rem2;
                    pa1->num_operands = 1;
                }
                {
                    PIRValue code_val = emit_const_int((long)code);
                    PIRInst *pa2 = emit(PIR_PUSH_ARG);
                    pa2->operands[0] = code_val;
                    pa2->num_operands = 1;
                }

                PIRValue match_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRInst *call = emit(PIR_CALL);
                    call->result = match_result;
                    call->str_val = pir_str_dup("pydos_excgroup_match");
                    call->int_val = 2;
                }

                /* match_result is a list [matched_or_none, remainder_or_none] */
                /* matched = match_result[0] */
                PIRValue matched = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRValue idx0 = emit_const_int(0L);
                    PIRInst *sg = emit(PIR_SUBSCR_GET);
                    sg->result = matched;
                    sg->operands[0] = match_result;
                    sg->operands[1] = idx0;
                    sg->num_operands = 2;
                }

                /* new_remainder = match_result[1] */
                PIRValue new_rem = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRValue idx1 = emit_const_int(1L);
                    PIRInst *sg = emit(PIR_SUBSCR_GET);
                    sg->result = new_rem;
                    sg->operands[0] = match_result;
                    sg->operands[1] = idx1;
                    sg->num_operands = 2;
                }

                /* Update remainder */
                var_store(remainder_name, new_rem);

                /* Check if matched is not None */
                PIRValue matched_none = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRInst *cmp = emit(PIR_PY_IS);
                    cmp->result = matched_none;
                    cmp->operands[0] = matched;
                    cmp->operands[1] = none_val;
                    cmp->num_operands = 2;
                }

                emit_cond_branch(matched_none, star_next, star_body);

                switch_to_block(star_body);

                /* Bind exception name if present */
                if (handler->data.handler.name) {
                    if (!var_exists(handler->data.handler.name)) {
                        var_alloca(handler->data.handler.name);
                    }
                    var_store(handler->data.handler.name, matched);
                }

                if (handled_exception_depth < 32)
                    handled_exceptions[handled_exception_depth++] = matched;
                build_stmts(handler->data.handler.body);
                if (handled_exception_depth > 0)
                    handled_exception_depth--;
                if (!block_is_terminated()) {
                    emit_branch(star_next);
                }

                switch_to_block(star_next);
            }

            /* After all except* handlers: if remainder is not None, re-raise it */
            PIRValue final_rem = var_load(remainder_name);
            PIRValue final_none = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *ci = emit(PIR_CONST_NONE);
                ci->result = final_none;
            }
            PIRValue final_is_none = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *cmp = emit(PIR_PY_IS);
                cmp->result = final_is_none;
                cmp->operands[0] = final_rem;
                cmp->operands[1] = final_none;
                cmp->num_operands = 2;
            }

            PIRBlock *all_handled = new_block("all_handled");
            PIRBlock *reraise_rem = new_block("reraise_rem");
            emit_cond_branch(final_is_none, all_handled, reraise_rem);

            switch_to_block(reraise_rem);
            {
                PIRInst *raise = emit(PIR_RAISE);
                raise->operands[0] = final_rem;
                raise->num_operands = 1;
            }

            switch_to_block(all_handled);
            if (!block_is_terminated()) {
                emit_branch(finally_block ? finally_block : end_block);
            }
        }
    }

    /* Re-raise if no handler matched */
    if (!block_is_terminated()) {
        emit(PIR_RERAISE);
    }
    return_cleanup_depth = cleanup_base;

    /* Normal finally and the exceptional finally guard propagate to the
     * enclosing handler, not back into the same guard. */
    exception_target_depth = exception_base;

    /* Finally block (normal path) */
    if (finally_block) {
        switch_to_block(finally_block);
        if (finally_guard) emit(PIR_POP_TRY);
        build_stmts(node->data.try_stmt.finally_body);
        if (!block_is_terminated()) emit_branch(end_block);

        /* Finally guard (exception path) */
        switch_to_block(finally_guard);
        emit(PIR_POP_TRY);
        {
            PIRValue pending = pir_func_alloc_value(current_func,
                                                     PIR_TYPE_PYOBJ);
            PIRInst *ge = emit(PIR_GET_EXCEPTION);
            ge->result = pending;
            emit(PIR_CLEAR_EXCEPTION);
            build_stmts(node->data.try_stmt.finally_body);
            if (!block_is_terminated()) {
                PIRInst *raise = emit(PIR_RAISE);
                raise->operands[0] = pending;
                raise->num_operands = 1;
                raise->handler_block = current_exception_target();
            }
        }
    }

    switch_to_block(end_block);
}

void PIRBuilder::build_raise(ASTNode *node)
{
    if (node->data.raise_stmt.exc) {
        PIRValue exc = build_expr(node->data.raise_stmt.exc);
        PIRInst *inst = emit(PIR_RAISE);
        inst->operands[0] = exc;
        inst->num_operands = 1;
        inst->handler_block = current_exception_target();
    } else {
        PIRInst *inst;
        if (handled_exception_depth > 0) {
            inst = emit(PIR_RAISE);
            inst->operands[0] =
                handled_exceptions[handled_exception_depth - 1];
            inst->num_operands = 1;
        } else {
            inst = emit(PIR_RERAISE);
        }
        inst->handler_block = current_exception_target();
    }
}

void PIRBuilder::build_break(ASTNode *node)
{
    (void)node;
    if (loop_depth > 0) {
        int depth = loop_cleanup_depths[loop_depth - 1];
        if (!emit_cleanups_to_depth(depth))
            emit_branch(break_targets[loop_depth - 1]);
    }
}

void PIRBuilder::build_continue(ASTNode *node)
{
    (void)node;
    if (loop_depth > 0) {
        int depth = loop_cleanup_depths[loop_depth - 1];
        if (!emit_cleanups_to_depth(depth))
            emit_branch(continue_targets[loop_depth - 1]);
    }
}

void PIRBuilder::build_pass(ASTNode *node)
{
    (void)node;
    /* No-op */
}

void PIRBuilder::build_assert(ASTNode *node)
{
    PIRBlock *ok_block = new_block("assert_ok");
    PIRBlock *fail_block = new_block("assert_fail");

    PIRValue cond = build_expr(node->data.assert_stmt.test);
    emit_cond_branch(cond, ok_block, fail_block);

    switch_to_block(fail_block);
    /* Raise AssertionError */
    PIRValue exc_name = emit_const_str("AssertionError", 14);
    PIRInst *push = emit(PIR_PUSH_ARG);
    push->operands[0] = exc_name;
    push->num_operands = 1;

    PIRValue exc = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *call = emit(PIR_CALL);
    call->result = exc;
    call->str_val = "AssertionError";
    call->int_val = 1;

    PIRInst *raise = emit(PIR_RAISE);
    raise->operands[0] = exc;
    raise->num_operands = 1;

    switch_to_block(ok_block);
}

void PIRBuilder::build_delete(ASTNode *node)
{
    ASTNode *target = node->data.del_stmt.targets;
    for (; target; target = target->next) {
        if (target->kind == AST_SUBSCRIPT) {
            PIRValue obj = build_expr(target->data.subscript.object);
            PIRValue idx = build_expr(target->data.subscript.index);
            PIRInst *inst = emit(PIR_DEL_SUBSCR);
            inst->operands[0] = obj;
            inst->operands[1] = idx;
            inst->num_operands = 2;
        } else if (target->kind == AST_NAME) {
            const char *name = target->data.name.id;
            PIRValue *alloca_val = var_map->get(name);
            if (alloca_val) {
                /* Local variable: emit PIR_DEL_NAME */
                PIRInst *inst = emit(PIR_DEL_NAME);
                inst->operands[0] = *alloca_val;
                inst->num_operands = 1;
                inst->str_val = pir_str_dup(name);
            } else {
                /* Global variable: emit PIR_DEL_GLOBAL */
                PIRInst *inst = emit(PIR_DEL_GLOBAL);
                inst->num_operands = 0;
                inst->str_val = pir_str_dup(name);
            }
        } else if (target->kind == AST_ATTR) {
            PIRValue obj = build_expr(target->data.attribute.object);
            PIRInst *inst = emit(PIR_DEL_ATTR);
            inst->operands[0] = obj;
            inst->num_operands = 1;
            inst->str_val = pir_str_dup(target->data.attribute.attr);
        } else {
            report_error(target, "unsupported delete target");
        }
    }
}

/* --------------------------------------------------------------- */
/* Expression dispatch                                               */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::build_expr(ASTNode *node)
{
    if (!node) return emit_const_none();

    switch (node->kind) {
    case AST_INT_LIT:     return build_int_lit(node);
    case AST_FLOAT_LIT:   return build_float_lit(node);
    case AST_COMPLEX_LIT: return build_complex_lit(node);
    case AST_STR_LIT:     return build_str_lit(node);
    case AST_BOOL_LIT:    return build_bool_lit(node);
    case AST_NONE_LIT:    return build_none_lit(node);
    case AST_NAME:        return build_name(node);
    case AST_BINOP:       return build_binop(node);
    case AST_UNARYOP:     return build_unaryop(node);
    case AST_COMPARE:     return build_compare(node);
    case AST_BOOLOP:      return build_boolop(node);
    case AST_CALL:        return build_call(node);
    case AST_ATTR:        return build_attr(node);
    case AST_SUBSCRIPT:   return build_subscript(node);
    case AST_LIST_EXPR:   return build_list_expr(node);
    case AST_DICT_EXPR:   return build_dict_expr(node);
    case AST_TUPLE_EXPR:  return build_tuple_expr(node);
    case AST_SET_EXPR:    return build_set_expr(node);
    case AST_FSTRING:     return build_fstring(node);
    case AST_LISTCOMP:    return build_listcomp(node);
    case AST_DICTCOMP:    return build_dictcomp(node);
    case AST_SETCOMP:     return build_setcomp(node);
    case AST_GENEXPR:     return build_genexpr(node);
    case AST_WALRUS:      return build_walrus(node);
    case AST_LAMBDA:      return build_lambda(node);
    case AST_IFEXPR:      return build_ifexpr(node);
    case AST_YIELD_EXPR:      return build_yield(node);
    case AST_YIELD_FROM_EXPR: return build_yield_from(node);
    case AST_AWAIT: {
        if (!pir_value_valid(gen_val) || !is_building_coroutine) {
            report_error(node, "'await' outside async function");
            return emit_const_none();
        }
        PIRValue awaitable = build_expr(node->data.starred.value);
        return emit_yield_point(awaitable);
    }
    default:
        report_error(node, "unsupported expression in PIR builder");
        return emit_const_none();
    }
}

/* --------------------------------------------------------------- */
/* Expression builders                                               */
/* --------------------------------------------------------------- */

PIRValue PIRBuilder::build_int_lit(ASTNode *node)
{
    return emit_const_int(node->data.int_lit.value);
}

PIRValue PIRBuilder::build_float_lit(ASTNode *node)
{
    return emit_const_float(node->data.float_lit.value);
}

PIRValue PIRBuilder::build_complex_lit(ASTNode *node)
{
    /* Complex literal Nj -> complex(0.0, imag)
     * Use the Python name "complex" so codegen recognizes it as a builtin
     * and uses the argc/argv calling convention (sub sp; mov si,sp; ...).
     * Args must be emitted via PIR_PUSH_ARG (not operands[]) because the
     * PIR lowerer only processes PUSH_ARG to set up the call stack. */
    PIRValue real_v = emit_const_float(0.0);
    PIRValue imag_v = emit_const_float(node->data.complex_lit.imag);

    {
        PIRInst *pa0 = emit(PIR_PUSH_ARG);
        pa0->operands[0] = real_v;
        pa0->num_operands = 1;
    }
    {
        PIRInst *pa1 = emit(PIR_PUSH_ARG);
        pa1->operands[0] = imag_v;
        pa1->num_operands = 1;
    }

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *call = emit(PIR_CALL);
        call->result = result;
        call->str_val = pir_str_dup("complex");
        call->int_val = 2;
        call->num_operands = 0;
    }
    return result;
}

PIRValue PIRBuilder::build_str_lit(ASTNode *node)
{
    return emit_const_str(node->data.str_lit.value,
                          node->data.str_lit.len);
}

PIRValue PIRBuilder::build_bool_lit(ASTNode *node)
{
    return emit_const_bool(node->data.bool_lit.value);
}

PIRValue PIRBuilder::build_none_lit(ASTNode *node)
{
    (void)node;
    return emit_const_none();
}

PIRValue PIRBuilder::build_name(ASTNode *node)
{
    if (node->data.name.id &&
        pir_str_eq(node->data.name.id, "NotImplemented")) {
        PIRValue result = pir_func_alloc_value(current_func,
                                               PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL);
        call->result = result;
        call->str_val = pir_str_dup("pydos_obj_new_notimplemented");
        call->int_val = 0;
        return result;
    }
    PIRValue v = var_load(node->data.name.id);
    /* Attach type_hint from sema */
    if (sema && current_block && current_block->last) {
        TypeInfo *t = sema->get_expr_type(node);
        if (t) current_block->last->type_hint = t;
    }
    return v;
}

PIRValue PIRBuilder::build_binop(ASTNode *node)
{
    PIRValue left = build_expr(node->data.binop.left);
    PIRValue right = build_expr(node->data.binop.right);
    PIROp op = binop_to_pirop(node->data.binop.op);

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(op);
    inst->result = result;
    inst->operands[0] = left;
    inst->operands[1] = right;
    inst->num_operands = 2;

    /* Attach type_hint for codegen dispatch (e.g. str_concat vs obj_add) */
    if (sema) {
        TypeInfo *res_type = sema->get_expr_type(node);
        /* For ADD: mixed int+str must use generic obj_add, not str_concat */
        if (op == PIR_PY_ADD && res_type && res_type->kind == TY_STR) {
            TypeInfo *ltype = sema->get_expr_type(node->data.binop.left);
            TypeInfo *rtype = sema->get_expr_type(node->data.binop.right);
            if (ltype && rtype &&
                !(ltype->kind == TY_STR && rtype->kind == TY_STR)) {
                res_type = 0;
            }
        }
        inst->type_hint = res_type;
    }
    return result;
}

PIRValue PIRBuilder::build_unaryop(ASTNode *node)
{
    PIRValue operand = build_expr(node->data.unaryop.operand);
    PIROp op;

    switch (node->data.unaryop.op) {
    case UNARY_NEG:    op = PIR_PY_NEG; break;
    case UNARY_POS:    op = PIR_PY_POS; break;
    case UNARY_NOT:    op = PIR_PY_NOT; break;
    case UNARY_BITNOT: op = PIR_PY_BIT_NOT; break;
    default:           op = PIR_PY_NEG; break;
    }

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(op);
    inst->result = result;
    inst->operands[0] = operand;
    inst->num_operands = 1;
    return result;
}

PIRValue PIRBuilder::build_compare(ASTNode *node)
{
    ASTNode *left_node = node->data.compare.left;
    ASTNode *comp = node->data.compare.comparators;
    CmpOp *ops = node->data.compare.ops;
    int num_ops = node->data.compare.num_ops;

    PIRValue left = build_expr(left_node);

    if (num_ops == 1) {
        /* Simple: a op b */
        PIRValue right = build_expr(comp);
        PIROp pirop = cmpop_to_pirop(ops[0]);
        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *inst = emit(pirop);
        inst->result = result;
        inst->operands[0] = left;
        inst->operands[1] = right;
        inst->num_operands = 2;
        return result;
    }

    /* Chained: a op1 b op2 c ...
     *
     * Alloca-based merge, same pattern as build_boolop: every path
     * stores into one slot before reaching the end block, so the loaded
     * result is defined regardless of which comparison short-circuited.
     * (Returning the last comparison's SSA value looked right when the
     * chain was used alone, but its defining instruction never runs on
     * the short-circuit path — inside an and/or that surfaced as a null
     * result at runtime.) */
    PIRBlock *end_block = new_block("cmp_end");

    char merge_name[32];
    sprintf(merge_name, "__cmpchain_%d", current_func->next_value_id);
    PIRValue merge_addr = pir_func_alloc_value(current_func, PIR_TYPE_PTR);
    PIRInst *alloca_inst = emit(PIR_ALLOCA);
    alloca_inst->result = merge_addr;
    alloca_inst->str_val = pir_str_dup(merge_name);
    current_func->num_locals++;

    int i;
    ASTNode *c = comp;
    for (i = 0; i < num_ops; i++) {
        PIRValue right = build_expr(c);
        PIROp pirop = cmpop_to_pirop(ops[i]);
        PIRValue cmp_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *inst = emit(pirop);
        inst->result = cmp_result;
        inst->operands[0] = left;
        inst->operands[1] = right;
        inst->num_operands = 2;

        if (i < num_ops - 1) {
            PIRBlock *next_block = new_block("cmp_next");
            PIRBlock *false_block = new_block("cmp_false");
            emit_cond_branch(cmp_result, next_block, false_block);

            switch_to_block(false_block);
            /* Short circuit: result is false */
            {
                PIRValue false_val = emit_const_bool(0);
                PIRInst *st = emit(PIR_STORE);
                st->operands[0] = merge_addr;
                st->operands[1] = false_val;
                st->num_operands = 2;
            }
            emit_branch(end_block);

            switch_to_block(next_block);
        } else {
            PIRInst *st = emit(PIR_STORE);
            st->operands[0] = merge_addr;
            st->operands[1] = cmp_result;
            st->num_operands = 2;
        }

        left = right;
        if (c) c = c->next;
    }

    if (!block_is_terminated()) emit_branch(end_block);
    switch_to_block(end_block);

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *ld = emit(PIR_LOAD);
    ld->result = result;
    ld->operands[0] = merge_addr;
    ld->num_operands = 1;
    return result;
}

PIRValue PIRBuilder::build_boolop(ASTNode *node)
{
    ASTNode *left_node = node->data.boolop.values;
    ASTNode *right_node = node->data.boolop.values->next;
    int is_and = (node->data.boolop.op == BOOL_AND);

    /* Alloca-based merge: mirrors legacy IR_POS pattern.
       Both branches store their result into the same alloca slot
       so the merge block always loads a valid value. */
    char merge_name[32];
    sprintf(merge_name, "__boolop_%d", current_func->next_value_id);
    PIRValue merge_addr = pir_func_alloc_value(current_func, PIR_TYPE_PTR);
    PIRInst *alloca_inst = emit(PIR_ALLOCA);
    alloca_inst->result = merge_addr;
    alloca_inst->str_val = pir_str_dup(merge_name);
    current_func->num_locals++;

    PIRValue left = build_expr(left_node);

    /* Store left into merge variable */
    PIRInst *st1 = emit(PIR_STORE);
    st1->operands[0] = merge_addr;
    st1->operands[1] = left;
    st1->num_operands = 2;

    PIRBlock *eval_right = new_block(is_and ? "and_right" : "or_right");
    PIRBlock *merge = new_block("bool_merge");

    if (is_and) {
        emit_cond_branch(left, eval_right, merge);
    } else {
        emit_cond_branch(left, merge, eval_right);
    }

    switch_to_block(eval_right);
    PIRValue right = build_expr(right_node);

    /* Store right into merge variable (overwrites left) */
    if (!block_is_terminated()) {
        PIRInst *st2 = emit(PIR_STORE);
        st2->operands[0] = merge_addr;
        st2->operands[1] = right;
        st2->num_operands = 2;
        emit_branch(merge);
    }

    switch_to_block(merge);
    /* Load the merge result — always valid regardless of path */
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *ld = emit(PIR_LOAD);
    ld->result = result;
    ld->operands[0] = merge_addr;
    ld->num_operands = 1;
    return result;
}

/* Compact signature parser for Python-backed stdlib functions.  The stdlib
 * index stores the source parameter list (for example
 * "iterable, key=None, reverse=False") in the otherwise-unused C symbol
 * field.  Keeping this metadata with the PIR function lets independent user
 * modules obey Python's keyword/default argument rules. */
struct StdlibCallParam {
    char name[48];
    char default_text[64];
    int has_default;
};

static void copy_trimmed_text(const char *begin, const char *end,
                              char *out, int out_size)
{
    int len;
    while (begin < end && (*begin == ' ' || *begin == '\t')) begin++;
    while (end > begin && (end[-1] == ' ' || end[-1] == '\t')) end--;
    len = (int)(end - begin);
    if (len >= out_size) len = out_size - 1;
    if (len > 0) memcpy(out, begin, len);
    out[len] = '\0';
}

static int parse_stdlib_signature(const char *sig, StdlibCallParam *params,
                                  int max_params)
{
    const char *token_start;
    const char *p;
    int count = 0;
    int depth = 0;
    char quote = 0;

    if (!sig || !*sig) return 0;
    token_start = sig;
    for (p = sig; ; p++) {
        char ch = *p;
        if (quote) {
            if (ch == quote && (p == sig || p[-1] != '\\')) quote = 0;
        } else if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '(' || ch == '[' || ch == '{') {
            depth++;
        } else if (ch == ')' || ch == ']' || ch == '}') {
            if (depth > 0) depth--;
        }

        if ((ch == ',' && depth == 0 && !quote) || ch == '\0') {
            const char *start = token_start;
            const char *end = p;
            const char *q;
            const char *colon = 0;
            const char *equals = 0;
            int inner_depth = 0;
            char inner_quote = 0;

            while (start < end && (*start == ' ' || *start == '\t')) start++;
            while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;
            if (start < end && !(end - start == 1 &&
                                 (*start == '*' || *start == '/')) &&
                count < max_params) {
                while (start < end && *start == '*') start++;
                for (q = start; q < end; q++) {
                    char qc = *q;
                    if (inner_quote) {
                        if (qc == inner_quote && (q == start || q[-1] != '\\'))
                            inner_quote = 0;
                    } else if (qc == '\'' || qc == '"') {
                        inner_quote = qc;
                    } else if (qc == '(' || qc == '[' || qc == '{') {
                        inner_depth++;
                    } else if (qc == ')' || qc == ']' || qc == '}') {
                        if (inner_depth > 0) inner_depth--;
                    } else if (inner_depth == 0 && qc == ':' && !colon && !equals) {
                        colon = q;
                    } else if (inner_depth == 0 && qc == '=' && !equals) {
                        equals = q;
                    }
                }

                memset(&params[count], 0, sizeof(params[count]));
                copy_trimmed_text(start, colon ? colon : (equals ? equals : end),
                                  params[count].name,
                                  (int)sizeof(params[count].name));
                if (equals) {
                    params[count].has_default = 1;
                    copy_trimmed_text(equals + 1, end,
                                      params[count].default_text,
                                      (int)sizeof(params[count].default_text));
                }
                if (params[count].name[0]) count++;
            }
            if (ch == '\0') break;
            token_start = p + 1;
        }
    }
    return count;
}

PIRValue PIRBuilder::emit_signature_default(const char *text, ASTNode *call,
                                            int *ok)
{
    const char *start = text;
    const char *end;
    char number[64];
    char *number_end;
    long int_value;
    double float_value;

    if (ok) *ok = 1;
    if (!start) start = "";
    while (*start == ' ' || *start == '\t') start++;
    end = start + strlen(start);
    while (end > start && (end[-1] == ' ' || end[-1] == '\t')) end--;

    if ((end - start) == 4 && strncmp(start, "None", 4) == 0)
        return emit_const_none();
    if ((end - start) == 4 && strncmp(start, "True", 4) == 0)
        return emit_const_bool(1);
    if ((end - start) == 5 && strncmp(start, "False", 5) == 0)
        return emit_const_bool(0);
    if (end - start >= 2 &&
        ((start[0] == '\'' && end[-1] == '\'') ||
         (start[0] == '"' && end[-1] == '"'))) {
        return emit_const_str(start + 1, (int)(end - start - 2));
    }

    copy_trimmed_text(start, end, number, (int)sizeof(number));
    int_value = strtol(number, &number_end, 10);
    if (*number && *number_end == '\0') return emit_const_int(int_value);
    float_value = strtod(number, &number_end);
    if (*number && *number_end == '\0') return emit_const_float(float_value);

    report_error(call, "unsupported default value in stdlib PIR signature");
    if (ok) *ok = 0;
    return emit_const_none();
}

int PIRBuilder::normalize_stdlib_pir_call(ASTNode *call,
                                         const char *func_name,
                                         PIRValue *evaluated,
                                         int evaluated_count,
                                         PIRValue *bound,
                                         int *bound_count)
{
    const char *sig;

    if (!stdlib_reg_ || !func_name || !bound_count) return 0;
    sig = stdlib_reg_->find_pir_signature(func_name);
    if (!sig) return 0;
    return normalize_stdlib_signature_call(call, sig, evaluated,
                                           evaluated_count, bound,
                                           bound_count);
}

int PIRBuilder::normalize_stdlib_signature_call(ASTNode *call,
                                                const char *sig,
                                                PIRValue *evaluated,
                                                int evaluated_count,
                                                PIRValue *bound,
                                                int *bound_count)
{
    StdlibCallParam params[16];
    int assigned[16];
    ASTNode *arg;
    int param_count;
    int eval_index = 0;
    int positional_index = 0;
    int i;

    if (!sig || !*sig || !bound_count) return 0;
    param_count = parse_stdlib_signature(sig, params, 16);
    if (param_count <= 0) return 0;

    memset(assigned, 0, sizeof(assigned));
    for (arg = call->data.call.args; arg && eval_index < evaluated_count;
         arg = arg->next, eval_index++) {
        int slot = -1;
        if (arg->kind == AST_KEYWORD_ARG) {
            const char *key = arg->data.keyword_arg.key;
            if (key) {
                for (i = 0; i < param_count; i++) {
                    if (strcmp(params[i].name, key) == 0) {
                        slot = i;
                        break;
                    }
                }
                if (slot < 0) report_error(call, "unexpected keyword argument");
            } else {
                report_error(call, "**kwargs is not supported for this stdlib call");
            }
        } else {
            while (positional_index < param_count && assigned[positional_index])
                positional_index++;
            if (positional_index < param_count) slot = positional_index++;
            else report_error(call, "too many positional arguments");
        }

        if (slot >= 0) {
            if (assigned[slot]) {
                report_error(call, "multiple values for stdlib argument");
            } else {
                bound[slot] = evaluated[eval_index];
                assigned[slot] = 1;
            }
        }
    }

    for (i = 0; i < param_count; i++) {
        if (!assigned[i]) {
            if (params[i].has_default) {
                int ok = 0;
                bound[i] = emit_signature_default(params[i].default_text,
                                                   call, &ok);
                assigned[i] = 1;
            } else {
                report_error(call, "missing required stdlib argument");
                bound[i] = emit_const_none();
            }
        }
    }
    *bound_count = param_count;
    return 1;
}

/* Bind a statically resolved ordinary instance method to its generated C
 * ABI.  The first declared parameter is supplied by descriptor binding.
 * Invalid arity is deliberately left unnormalized: runtime signature
 * metadata then raises TypeError at the actual call site, so try/except
 * retains Python semantics instead of becoming a compiler diagnostic. */
int PIRBuilder::normalize_user_call(ASTNode *call,
                                    ASTNode *definition,
                                    PIRValue *evaluated,
                                    int evaluated_count,
                                    PIRValue *bound,
                                    int *bound_count,
                                    int skip_first_param)
{
    Param *params[64];
    int keyword_only[64];
    int assigned[64];
    PIRValue values[64];
    PIRValue extra_pos[64];
    PIRValue extra_kw_keys[64];
    PIRValue extra_kw_vals[64];
    Param *p;
    ASTNode *arg;
    int param_count = 0;
    int skipped_self = skip_first_param ? 0 : 1;
    int after_star = 0;
    int star_index = -1;
    int dstar_index = -1;
    int positional_cursor = 0;
    int extra_pos_count = 0;
    int extra_kw_count = 0;
    int eval_index = 0;
    int i;

    if (!call || !definition || definition->kind != AST_FUNC_DEF ||
        !bound_count)
        return 0;
    memset(assigned, 0, sizeof(assigned));
    memset(keyword_only, 0, sizeof(keyword_only));

    for (p = definition->data.func_def.params; p; p = p->next) {
        if (is_bare_star_sep(p)) {
            after_star = 1;
            continue;
        }
        if (!skipped_self) {
            skipped_self = 1;
            continue;
        }
        if (param_count >= 64) return 0;
        params[param_count] = p;
        keyword_only[param_count] = after_star &&
                                    !p->is_star && !p->is_double_star;
        if (p->is_star) {
            star_index = param_count;
            after_star = 1;
        } else if (p->is_double_star) {
            dstar_index = param_count;
        }
        param_count++;
    }

    for (arg = call->data.call.args;
         arg && eval_index < evaluated_count;
         arg = arg->next, eval_index++) {
        if (arg->kind == AST_KEYWORD_ARG) {
            const char *key = arg->data.keyword_arg.key;
            int slot = -1;
            if (!key) return 0; /* dynamic **mapping needs runtime binding */
            for (i = 0; i < param_count; i++) {
                if (!params[i]->is_star && !params[i]->is_double_star &&
                    params[i]->name && strcmp(params[i]->name, key) == 0) {
                    slot = i;
                    break;
                }
            }
            if (slot >= 0) {
                if (params[slot]->is_positional_only || assigned[slot])
                    return 0;
                values[slot] = evaluated[eval_index];
                assigned[slot] = 1;
            } else if (dstar_index >= 0) {
                if (extra_kw_count >= 64) return 0;
                extra_kw_keys[extra_kw_count] = emit_const_str(
                    key, (int)strlen(key));
                extra_kw_vals[extra_kw_count] = evaluated[eval_index];
                extra_kw_count++;
            } else {
                return 0;
            }
        } else {
            int slot = -1;
            while (positional_cursor < param_count) {
                if (!params[positional_cursor]->is_star &&
                    !params[positional_cursor]->is_double_star &&
                    !keyword_only[positional_cursor] &&
                    !assigned[positional_cursor]) {
                    slot = positional_cursor++;
                    break;
                }
                positional_cursor++;
            }
            if (slot >= 0) {
                values[slot] = evaluated[eval_index];
                assigned[slot] = 1;
            } else {
                if (star_index < 0 || extra_pos_count >= 64) return 0;
                extra_pos[extra_pos_count++] = evaluated[eval_index];
            }
        }
    }

    /* Required parameters must be present.  Defaults are materialized only
     * after validation, avoiding partial binding side effects on failure. */
    for (i = 0; i < param_count; i++) {
        if (i == star_index || i == dstar_index) continue;
        if (!assigned[i] && !params[i]->default_val) return 0;
    }
    for (i = 0; i < param_count; i++) {
        if (i == star_index || i == dstar_index) continue;
        if (!assigned[i]) {
            values[i] = build_expr(params[i]->default_val);
            assigned[i] = 1;
        }
    }

    if (star_index >= 0) {
        PIRValue tuple_value;
        for (i = 0; i < extra_pos_count; i++) {
            PIRInst *push = emit(PIR_PUSH_ARG);
            push->operands[0] = extra_pos[i];
            push->num_operands = 1;
        }
        tuple_value = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *tuple = emit(PIR_BUILD_TUPLE);
            tuple->result = tuple_value;
            tuple->int_val = extra_pos_count;
        }
        values[star_index] = tuple_value;
        assigned[star_index] = 1;
    }
    if (dstar_index >= 0) {
        PIRValue dict_value;
        for (i = 0; i < extra_kw_count; i++) {
            PIRInst *push_key = emit(PIR_PUSH_ARG);
            PIRInst *push_value;
            push_key->operands[0] = extra_kw_keys[i];
            push_key->num_operands = 1;
            push_value = emit(PIR_PUSH_ARG);
            push_value->operands[0] = extra_kw_vals[i];
            push_value->num_operands = 1;
        }
        dict_value = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *dict = emit(PIR_BUILD_DICT);
            dict->result = dict_value;
            dict->int_val = extra_kw_count;
        }
        values[dstar_index] = dict_value;
        assigned[dstar_index] = 1;
    }

    for (i = 0; i < param_count; i++) bound[i] = values[i];
    *bound_count = param_count;
    return 1;
}

PIRValue PIRBuilder::build_call_ex(ASTNode *node)
{
    PIRValue callable = build_expr(node->data.call.func);
    PIRValue positional = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRValue keywords = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    ASTNode *arg;
    PIRInst *build;

    build = emit(PIR_BUILD_LIST);
    build->result = positional;
    build->int_val = 0;
    build = emit(PIR_BUILD_DICT);
    build->result = keywords;
    build->int_val = 0;

    for (arg = node->data.call.args; arg; arg = arg->next) {
        PIRValue value;
        PIRValue ignored;
        PIRInst *push;
        PIRInst *call;
        const char *helper;

        if (arg->kind == AST_STARRED) {
            value = build_expr(arg->data.starred.value);
            helper = "pydos_call_pos_extend";
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = positional;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = value;
            push->num_operands = 1;
            ignored = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            call = emit(PIR_CALL);
            call->result = ignored;
            call->str_val = pir_str_dup(helper);
            call->int_val = 2;
        } else if (arg->kind == AST_KEYWORD_ARG) {
            value = build_expr(arg->data.keyword_arg.kw_value);
            if (arg->data.keyword_arg.key) {
                PIRValue name = emit_const_str(
                    arg->data.keyword_arg.key,
                    (int)strlen(arg->data.keyword_arg.key));
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = keywords;
                push->num_operands = 1;
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = name;
                push->num_operands = 1;
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = value;
                push->num_operands = 1;
                ignored = pir_func_alloc_value(current_func,
                                                PIR_TYPE_PYOBJ);
                call = emit(PIR_CALL);
                call->result = ignored;
                call->str_val = pir_str_dup("pydos_call_kw_set");
                call->int_val = 3;
            } else {
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = keywords;
                push->num_operands = 1;
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = value;
                push->num_operands = 1;
                ignored = pir_func_alloc_value(current_func,
                                                PIR_TYPE_PYOBJ);
                call = emit(PIR_CALL);
                call->result = ignored;
                call->str_val = pir_str_dup("pydos_call_kw_update");
                call->int_val = 2;
            }
        } else {
            value = build_expr(arg);
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = positional;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = value;
            push->num_operands = 1;
            ignored = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            call = emit(PIR_CALL);
            call->result = ignored;
            call->str_val = pir_str_dup("pydos_call_pos_append");
            call->int_val = 2;
        }
    }

    {
        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = callable;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = positional;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = keywords;
        push->num_operands = 1;
        call = emit(PIR_CALL);
        call->result = result;
        call->str_val = pir_str_dup("pydos_obj_call_ex");
        call->int_val = 3;
        return result;
    }
}

PIRValue PIRBuilder::build_call(ASTNode *node)
{
    ASTNode *func_node = node->data.call.func;
    ASTNode *arg_node;
    int i;
    int force_indirect = 0;

    if (func_node && func_node->kind == AST_NAME &&
        func_node->data.name.id &&
        pir_str_eq(func_node->data.name.id, "locals") &&
        node->data.call.args == 0 && !var_exists("locals") &&
        !(cell_map && cell_map->get("locals")))
        return build_locals_dict();

    if (func_node && func_node->kind == AST_NAME &&
        func_node->data.name.id &&
        pir_str_eq(func_node->data.name.id, "super") &&
        node->data.call.args == 0 && !var_exists("super") &&
        !(cell_map && cell_map->get("super")) &&
        current_method_class_name && current_method_first_param) {
        PIRValue start_type = var_load(current_method_class_name);
        PIRValue bound_obj = var_load(current_method_first_param);
        PIRValue result = pir_func_alloc_value(current_func,
                                               PIR_TYPE_PYOBJ);
        PIRInst *push = emit(PIR_PUSH_ARG);
        push->operands[0] = start_type;
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = bound_obj;
        push->num_operands = 1;
        PIRInst *call = emit(PIR_CALL);
        call->result = result;
        call->str_val = pir_str_dup("pydos_super_new");
        call->int_val = 2;
        return result;
    }

    for (arg_node = node->data.call.args; arg_node;
         arg_node = arg_node->next) {
        if (arg_node->kind == AST_STARRED ||
            (arg_node->kind == AST_KEYWORD_ARG &&
             arg_node->data.keyword_arg.key == 0))
            return build_call_ex(node);
    }

    PIRValue arg_temps[64];
    i = 0;
    for (arg_node = node->data.call.args; arg_node; arg_node = arg_node->next) {
        if (arg_node->kind == AST_KEYWORD_ARG) {
            arg_temps[i] = build_expr(arg_node->data.keyword_arg.kw_value);
        } else {
            arg_temps[i] = build_expr(arg_node);
        }
        i++;
    }
    int total_args = i;

    /* Check for method call: obj.method(args) */
    if (func_node->kind == AST_ATTR) {
        ASTNode *attr_obj = func_node->data.attribute.object;
        const char *method = func_node->data.attribute.attr;

        /* Check for super().method(args) pattern */
        int is_super_call = 0;
        if (attr_obj && attr_obj->kind == AST_CALL &&
            attr_obj->data.call.func &&
            attr_obj->data.call.func->kind == AST_NAME &&
            attr_obj->data.call.func->data.name.id &&
            pir_str_eq(attr_obj->data.call.func->data.name.id, "super") &&
            current_method_class_name && current_method_first_param) {
            is_super_call = 1;
        }

        if (is_super_call) {
            PIRValue start_type = var_load(current_method_class_name);
            PIRValue bound_obj = var_load(current_method_first_param);
            PIRValue super_obj = pir_func_alloc_value(
                current_func, PIR_TYPE_PYOBJ);
            PIRInst *push = emit(PIR_PUSH_ARG);
            push->operands[0] = start_type;
            push->num_operands = 1;
            push = emit(PIR_PUSH_ARG);
            push->operands[0] = bound_obj;
            push->num_operands = 1;
            PIRInst *make_super = emit(PIR_CALL);
            make_super->result = super_obj;
            make_super->str_val = pir_str_dup("pydos_super_new");
            make_super->int_val = 2;

            for (i = 0; i < total_args; i++) {
                PIRInst *pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = arg_temps[i];
                pa->num_operands = 1;
            }

            PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *call = emit(PIR_CALL_METHOD);
            call->result = result;
            call->operands[0] = super_obj;
            call->num_operands = 1;
            call->str_val = pir_str_dup(method);
            call->int_val = total_args;
            return result;
        }

        PIRValue obj = build_expr(func_node->data.attribute.object);

        /* Check for PIR-backed method: emit PIR_CALL with self prepended */
        if (stdlib_reg_ && sema) {
            TypeInfo *obj_type = sema->get_expr_type(func_node->data.attribute.object);
            if (obj_type && obj_type->kind != TY_ERROR && obj_type->kind != TY_ANY) {
                char pir_name[48];
                if (stdlib_reg_->is_pir_method(obj_type->kind, method, pir_name)) {
                    const BuiltinMethodEntry *method_entry =
                        stdlib_reg_->find_method(obj_type->kind, method);
                    PIRValue method_args[64];
                    int method_argc = total_args;
                    int normalized = 0;

                    for (i = 0; i < total_args; i++) {
                        method_args[i] = arg_temps[i];
                    }
                    if (method_entry && method_entry->asm_name[0] != '\0') {
                        PIRValue bound_method_args[64];
                        int bound_method_count = 0;
                        normalized = normalize_stdlib_signature_call(
                            node, method_entry->asm_name,
                            arg_temps, total_args,
                            bound_method_args, &bound_method_count);
                        if (normalized) {
                            method_argc = bound_method_count;
                            for (i = 0; i < method_argc; i++) {
                                method_args[i] = bound_method_args[i];
                            }
                        }
                    }

                    /* Push self as first arg, then the normalized user args. */
                    PIRInst *pa_self = emit(PIR_PUSH_ARG);
                    pa_self->operands[0] = obj;
                    pa_self->num_operands = 1;
                    for (i = 0; i < method_argc; i++) {
                        PIRInst *pa = emit(PIR_PUSH_ARG);
                        pa->operands[0] = method_args[i];
                        pa->num_operands = 1;
                    }

                    /* Dynamic dispatch pads omitted parameters with None.
                     * Do the same for direct PIR calls so the Python body can
                     * materialize its declared defaults consistently. */
                    int bound_args = method_argc;
                    if (!normalized && method_entry &&
                        method_entry->num_params > bound_args) {
                        for (i = bound_args; i < method_entry->num_params; i++) {
                            PIRValue default_none = emit_const_none();
                            PIRInst *pa = emit(PIR_PUSH_ARG);
                            pa->operands[0] = default_none;
                            pa->num_operands = 1;
                        }
                        bound_args = method_entry->num_params;
                    }
                    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    PIRInst *call = emit(PIR_CALL);
                    call->result = result;
                    call->str_val = pir_str_dup(pir_name);
                    call->int_val = bound_args + 1; /* +1 for self */
                    return result;
                }
            }
        }

        /* Ordinary user methods use the definition selected by semantic C3
         * to bind names, defaults and variadic args into the fixed ABI. */
        PIRValue user_args[64];
        int user_argc = total_args;
        TypeInfo *user_obj_type = sema
            ? sema->get_expr_type(func_node->data.attribute.object) : 0;
        ASTNode *user_method_def = class_method_definition(user_obj_type,
                                                            method);
        for (i = 0; i < total_args; i++) user_args[i] = arg_temps[i];
        if (user_method_def &&
            !type_has_decorated_method(user_obj_type, method)) {
            PIRValue normalized_args[64];
            int normalized_count = 0;
            if (normalize_user_call(node, user_method_def,
                                    arg_temps, total_args,
                                    normalized_args,
                                    &normalized_count, 1)) {
                user_argc = normalized_count;
                for (i = 0; i < user_argc; i++)
                    user_args[i] = normalized_args[i];
            }
        }

        /* Push bound user arguments. */
        for (i = 0; i < user_argc; i++) {
            PIRInst *pa = emit(PIR_PUSH_ARG);
            pa->operands[0] = user_args[i];
            pa->num_operands = 1;
        }

        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL_METHOD);
        call->result = result;
        call->operands[0] = obj;
        call->num_operands = 1;
        call->str_val = pir_str_dup(method);
        call->int_val = user_argc;
        /* Attach object type for typed method dispatch */
        if (sema) {
            TypeInfo *obj_type = user_obj_type;
            if (obj_type && obj_type->kind != TY_ERROR &&
                !type_has_decorated_method(obj_type, method))
                call->type_hint = obj_type;
        }
        return result;
    }

    /* Special-case isinstance(obj, TypeName):
     * Replace second arg with CONST_INT of PYDT_* type tag.
     * Type tags are looked up from the stdlib registry (.idx). */
    if (func_node->kind == AST_NAME && func_node->data.name.id &&
        pir_str_eq(func_node->data.name.id, "isinstance") && total_args >= 2) {
        ASTNode *type_arg = node->data.call.args;
        if (type_arg) type_arg = type_arg->next; /* second arg */
        if (type_arg && type_arg->kind == AST_NAME && type_arg->data.name.id) {
            int tag = -1;
            if (stdlib_reg_)
                tag = stdlib_reg_->find_runtime_type_tag(type_arg->data.name.id);
            if (tag >= 0)
                arg_temps[1] = emit_const_int((long)tag);
        }
    }

    /* Builtin type names are not runtime class objects yet, so resolve that
     * narrow form here.  User-defined classes now flow to the runtime as
     * real PYDT_CLASS objects. */
    if (func_node->kind == AST_NAME && func_node->data.name.id &&
        pir_str_eq(func_node->data.name.id, "issubclass") && total_args >= 2) {
        ASTNode *cls_arg = node->data.call.args;
        ASTNode *base_arg = cls_arg ? cls_arg->next : 0;
        if (cls_arg && cls_arg->kind == AST_NAME && cls_arg->data.name.id &&
            base_arg && base_arg->kind == AST_NAME && base_arg->data.name.id) {
            const char *cls_name = cls_arg->data.name.id;
            const char *base_name = base_arg->data.name.id;
            int cls_tag = stdlib_reg_
                          ? stdlib_reg_->find_runtime_type_tag(cls_name) : -1;
            int base_tag = stdlib_reg_
                           ? stdlib_reg_->find_runtime_type_tag(base_name) : -1;
            if (cls_tag >= 0 && base_tag >= 0) {
                int is_sub = base_tag == 10 ||
                             cls_tag == base_tag ||
                             (cls_tag == 1 && base_tag == 2);
                PIRValue bval = pir_func_alloc_value(current_func,
                                                      PIR_TYPE_PYOBJ);
                PIRInst *cb = emit(PIR_CONST_BOOL);
                cb->result = bval;
                cb->int_val = is_sub;
                return bval;
            }
        }
    }

    /* Special-case iter(x): emit PIR_GET_ITER */
    if (func_node->kind == AST_NAME && func_node->data.name.id &&
        pir_str_eq(func_node->data.name.id, "iter") && total_args == 1) {
        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *gi = emit(PIR_GET_ITER);
        gi->result = result;
        gi->operands[0] = arg_temps[0];
        gi->num_operands = 1;
        return result;
    }

    /* Look up callee definition for defaults and varargs */
    if (func_node->kind == AST_NAME && func_node->data.name.id) {
        const char *callee_name = func_node->data.name.id;
        ASTNode *callee_def = 0;
        int has_star = 0, has_dstar = 0, num_regular = 0;
        int user_normalized = 0;
        int instance_callee = 0;
        int fi;

        /* A name whose type is a class is either the class itself or a
         * variable holding an instance.  Only the first is a constructor
         * call; the scope lookup cannot tell them apart here because
         * function locals are gone by the time PIR is built, so compare the
         * name against the class it denotes. */
        if (sema) {
            TypeInfo *callee_type = sema->get_expr_type(func_node);
            if (callee_type && callee_type->kind == TY_CLASS &&
                callee_type->class_info && callee_type->class_info->name) {
                instance_callee =
                    strcmp(callee_name, callee_type->class_info->name) != 0;
            }
        }

        for (fi = 0; fi < num_func_defs; fi++) {
            if (pir_str_eq(func_defs[fi].name, callee_name)) {
                callee_def = func_defs[fi].node;
                break;
            }
        }

        /* Bind ordinary Python calls by parameter name as well as by
         * position.  This is shared with method binding and is needed
         * for source-linked stdlib functions such as field(compare=...). */
        if (callee_def) {
            PIRValue normalized_args[64];
            int normalized_count = 0;
            if (normalize_user_call(node, callee_def,
                                    arg_temps, total_args,
                                    normalized_args,
                                    &normalized_count, 0)) {
                for (i = 0; i < normalized_count; i++)
                    arg_temps[i] = normalized_args[i];
                total_args = normalized_count;
                user_normalized = 1;
            }
        } else if (sema && !instance_callee) {
            /* A class call binds user arguments against __init__ (excluding
             * self).  Previously keyword names were discarded and values
             * reached the constructor in source order.  A variable holding an
             * instance has the same class type but calls __call__, so it must
             * not be bound against the constructor. */
            TypeInfo *callee_type = sema->get_expr_type(func_node);
            ASTNode *constructor_def = class_method_definition(
                callee_type, "__init__");
            if (constructor_def &&
                !type_has_decorated_method(callee_type, "__init__")) {
                PIRValue normalized_args[64];
                int normalized_count = 0;
                if (normalize_user_call(node, constructor_def,
                                        arg_temps, total_args,
                                        normalized_args,
                                        &normalized_count, 1)) {
                    for (i = 0; i < normalized_count; i++)
                        arg_temps[i] = normalized_args[i];
                    total_args = normalized_count;
                    user_normalized = 1;
                }
            }
        }

        if (callee_def) {
            if (callee_def->data.func_def.decorators)
                force_indirect = 1;
            Param *p = callee_def->data.func_def.params;
            while (p) {
                if (is_bare_star_sep(p))
                    ; /* skip bare * separator */
                else if (p->is_star && p->name && strcmp(p->name, "*") != 0)
                    has_star = 1;
                else if (p->is_double_star)
                    has_dstar = 1;
                else
                    num_regular++;
                p = p->next;
            }

            /* Validate keyword args don't target positional-only params */
            {
                ASTNode *ca;
                for (ca = node->data.call.args; ca; ca = ca->next) {
                    if (ca->kind == AST_KEYWORD_ARG && ca->data.keyword_arg.key) {
                        const char *kw = ca->data.keyword_arg.key;
                        Param *pp;
                        for (pp = callee_def->data.func_def.params; pp; pp = pp->next) {
                            if (pp->is_positional_only && pp->name &&
                                pir_str_eq(pp->name, kw)) {
                                char errbuf[128];
                                sprintf(errbuf,
                                    "positional-only argument '%s' passed as keyword",
                                    kw);
                                report_error(node, errbuf);
                                break;
                            }
                        }
                    }
                }
            }
        }

        if ((has_star || has_dstar) && !user_normalized) {
            /* === Varargs call path === */
            PIRValue pos_temps[64];
            PIRValue kw_key_temps[64];
            PIRValue kw_val_temps[64];
            int pos_count = 0, kw_count = 0;
            int idx = 0;
            ASTNode *ca;

            /* Separate positional and keyword args */
            ca = node->data.call.args;
            while (ca && idx < total_args) {
                if (ca->kind == AST_KEYWORD_ARG && ca->data.keyword_arg.key) {
                    kw_val_temps[kw_count] = arg_temps[idx];
                    kw_key_temps[kw_count] = emit_const_str(
                        ca->data.keyword_arg.key,
                        (int)strlen(ca->data.keyword_arg.key));
                    kw_count++;
                } else {
                    pos_temps[pos_count] = arg_temps[idx];
                    pos_count++;
                }
                idx++;
                ca = ca->next;
            }

            /* Split positional: first num_regular go as regular params */
            {
                int regular_count = pos_count < num_regular
                                    ? pos_count : num_regular;
                int extra_pos = pos_count > num_regular
                                ? pos_count - num_regular : 0;
                PIRValue reg_temps[64];
                int reg_total = regular_count;
                int ri, ei, ki;
                PIRValue star_temp;
                PIRValue dstar_temp;
                int call_argc = 0;

                star_temp = pir_value_none();
                dstar_temp = pir_value_none();

                for (ri = 0; ri < regular_count; ri++) {
                    reg_temps[ri] = pos_temps[ri];
                }

                /* Fill defaults for missing regular params */
                if (callee_def && reg_total < num_regular) {
                    Param *p = callee_def->data.func_def.params;
                    int pidx = 0;
                    while (p) {
                        if (!p->is_star && !p->is_double_star) {
                            if (pidx >= reg_total && p->default_val) {
                                reg_temps[reg_total] = build_expr(p->default_val);
                                reg_total++;
                            }
                            pidx++;
                        }
                        p = p->next;
                    }
                }

                /* Build *args tuple from excess positional args */
                if (has_star) {
                    for (ei = 0; ei < extra_pos; ei++) {
                        PIRInst *pa = emit(PIR_PUSH_ARG);
                        pa->operands[0] = pos_temps[num_regular + ei];
                        pa->num_operands = 1;
                    }
                    star_temp = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    {
                        PIRInst *bt = emit(PIR_BUILD_TUPLE);
                        bt->result = star_temp;
                        bt->int_val = extra_pos;
                    }
                }

                /* Build **kwargs dict from keyword args */
                if (has_dstar) {
                    for (ki = 0; ki < kw_count; ki++) {
                        PIRInst *pk = emit(PIR_PUSH_ARG);
                        pk->operands[0] = kw_key_temps[ki];
                        pk->num_operands = 1;
                        PIRInst *pv = emit(PIR_PUSH_ARG);
                        pv->operands[0] = kw_val_temps[ki];
                        pv->num_operands = 1;
                    }
                    dstar_temp = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    {
                        PIRInst *bd = emit(PIR_BUILD_DICT);
                        bd->result = dstar_temp;
                        bd->int_val = kw_count;
                    }
                }

                /* Push: [regular_args, star_tuple, dstar_dict] */
                for (ri = 0; ri < reg_total; ri++) {
                    PIRInst *pa = emit(PIR_PUSH_ARG);
                    pa->operands[0] = reg_temps[ri];
                    pa->num_operands = 1;
                    call_argc++;
                }
                if (has_star) {
                    PIRInst *pa = emit(PIR_PUSH_ARG);
                    pa->operands[0] = star_temp;
                    pa->num_operands = 1;
                    call_argc++;
                }
                if (has_dstar) {
                    PIRInst *pa = emit(PIR_PUSH_ARG);
                    pa->operands[0] = dstar_temp;
                    pa->num_operands = 1;
                    call_argc++;
                }

                /* Emit call */
                {
                    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    PIRInst *call_inst = emit(PIR_CALL);
                    call_inst->result = result;
                    call_inst->str_val = pir_str_dup(callee_name);
                    call_inst->int_val = call_argc;
                    if (sema) {
                        TypeInfo *rt = sema->get_expr_type(node);
                        if (rt) call_inst->type_hint = rt;
                    }
                    return result;
                }
            }
        }

        /* Fill default parameters for regular function calls */
        if (callee_def && !user_normalized) {
            Param *p = callee_def->data.func_def.params;
            int num_params = 0;
            Param *pp;
            for (pp = p; pp; pp = pp->next) {
                if (is_bare_star_sep(pp)) continue;
                num_params++;
            }

            if (total_args < num_params) {
                p = callee_def->data.func_def.params;
                {
                    int pidx = 0;
                    while (p) {
                        if (is_bare_star_sep(p)) {
                            p = p->next;
                            continue;
                        }
                        if (pidx >= total_args && p->default_val) {
                            arg_temps[total_args] = build_expr(p->default_val);
                            total_args++;
                        }
                        pidx++;
                        p = p->next;
                    }
                }
            }
        } else if (stdlib_reg_) {
            PIRValue bound_args[64];
            int bound_count = 0;
            if (normalize_stdlib_pir_call(node, callee_name,
                                          arg_temps, total_args,
                                          bound_args, &bound_count)) {
                int bi;
                for (bi = 0; bi < bound_count; bi++) {
                    arg_temps[bi] = bound_args[bi];
                }
                total_args = bound_count;
            }
        }
    }

    /* Check if callee is a class instance variable — emit __call__ dispatch */
    if (func_node->kind == AST_NAME && sema) {
        const char *callee_id = func_node->data.name.id;
        Symbol *sym = sema->lookup(callee_id);
        if (sym && sym->kind == SYM_VAR && sym->type &&
            sym->type->kind == TY_CLASS && sym->type->class_info) {
            /* Instance variable: obj(args) → obj.__call__(args) */
            PIRValue obj = var_load(callee_id);
            for (i = 0; i < total_args; i++) {
                PIRInst *pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = arg_temps[i];
                pa->num_operands = 1;
            }
            PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *call = emit(PIR_CALL_METHOD);
            call->result = result;
            call->operands[0] = obj;
            call->num_operands = 1;
            call->str_val = pir_str_dup("__call__");
            call->int_val = total_args;
            return result;
        }
    }

    /* Regular function call */
    {
        const char *fname = 0;
        PIRValue callee_val = pir_value_none();
        int callee_built = 0;
        if (func_node->kind == AST_NAME) {
            fname = func_node->data.name.id;
        }

        /* A captured name lives in a cell, not in the module namespace, so
         * calling it by name would resolve to a function that does not
         * exist.  A nested def binds a variable that separate branches may
         * rebind.  Both cases call the value, never a fixed symbol. */
        NestedName *nested = fname ? nested_entry(fname) : 0;
        if (fname && (var_exists(fname) ||
                      (cell_map && cell_map->get(fname)) ||
                      (nested && nested->ambiguous))) {
            force_indirect = 1;
        }
        if (fname && sema) {
            Symbol *callee_symbol = sema->lookup(fname);
            if (callee_symbol && callee_symbol->kind == SYM_VAR)
                force_indirect = 1;
        }

        /* A callee that is itself an expression is evaluated before the
         * arguments are pushed: building it afterwards would run a nested
         * call while this call's arguments already sit on the arg stack. */
        if (!fname) {
            callee_val = build_expr(func_node);
            force_indirect = 1;
            callee_built = 1;
        }

        for (i = 0; i < total_args; i++) {
            PIRInst *pa = emit(PIR_PUSH_ARG);
            pa->operands[0] = arg_temps[i];
            pa->num_operands = 1;
        }

        /* If calling a closure function, set active closure before call */
        if (fname && closure_map) {
            PIRValue *clo = closure_map->get(fname);
            if (clo) {
                PIRInst *sc = emit(PIR_SET_CLOSURE);
                sc->operands[0] = *clo;
                sc->num_operands = 1;
            }
        }

        PIRValue callable = callee_val;
        PIRValue result;
        PIRInst *call;
        if (force_indirect && fname) {
            callable = var_load(fname);
        } else if (!fname && !callee_built) {
            callable = build_expr(func_node);
            force_indirect = 1;
        }
        result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        call = emit(PIR_CALL);
        call->result = result;
        if (force_indirect) {
            call->operands[0] = callable;
            call->num_operands = 1;
        } else {
            const char *target = nested ? nested->symbol : fname;
            call->str_val = target ? pir_str_dup(target) : 0;
        }
        call->int_val = total_args;
        /* Attach return type hint if available */
        if (sema) {
            TypeInfo *rt = sema->get_expr_type(node);
            if (rt) call->type_hint = rt;
        }

        return result;
    }
}

PIRValue PIRBuilder::build_locals_dict(void)
{
    PIRValue keys[64];
    PIRValue values[64];
    const char *names[64];
    int count = 0;
    int i;
    if (var_map) {
        for (i = 0; i < var_map->capacity() && count < 64; i++) {
            const char *name;
            if (!var_map->slot_occupied(i)) continue;
            name = var_map->slot_key(i);
            if (!name || strncmp(name, "__pydos_", 8) == 0 ||
                pir_str_eq(name, "__gen__")) continue;
            names[count] = name;
            count++;
        }
    }
    if (cell_map) {
        for (i = 0; i < cell_map->capacity() && count < 64; i++) {
            const char *name;
            int duplicate = 0;
            int j;
            if (!cell_map->slot_occupied(i)) continue;
            name = cell_map->slot_key(i);
            if (!name || strncmp(name, "__pydos_", 8) == 0) continue;
            for (j = 0; j < count; j++)
                if (pir_str_eq(names[j], name)) duplicate = 1;
            if (!duplicate) names[count++] = name;
        }
    }
    for (i = 0; i < count; i++) {
        keys[i] = emit_const_str(names[i], (int)strlen(names[i]));
        values[i] = var_load(names[i]);
    }
    for (i = 0; i < count; i++) {
        PIRInst *push = emit(PIR_PUSH_ARG);
        push->operands[0] = keys[i];
        push->num_operands = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = values[i];
        push->num_operands = 1;
    }
    {
        PIRValue result = pir_func_alloc_value(current_func,
                                               PIR_TYPE_PYOBJ);
        PIRInst *build = emit(PIR_BUILD_DICT);
        build->result = result;
        build->int_val = count;
        return result;
    }
}

PIRValue PIRBuilder::build_attr(ASTNode *node)
{
    PIRValue obj = build_expr(node->data.attribute.object);
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_GET_ATTR);
    inst->result = result;
    inst->operands[0] = obj;
    inst->num_operands = 1;
    inst->str_val = pir_str_dup(node->data.attribute.attr);
    /* Attach object type for typed dispatch in codegen */
    if (sema) {
        TypeInfo *obj_type = sema->get_expr_type(node->data.attribute.object);
        if (obj_type && obj_type->kind != TY_ERROR &&
            !type_has_decorated_method(obj_type,
                                       node->data.attribute.attr))
            inst->type_hint = obj_type;
    }
    return result;
}

PIRValue PIRBuilder::build_subscript(ASTNode *node)
{
    PIRValue obj = build_expr(node->data.subscript.object);

    /* Check for slice */
    if (node->data.subscript.index &&
        node->data.subscript.index->kind == AST_SLICE) {
        ASTNode *sl = node->data.subscript.index;

        /* A slice is a first-class Python object.  It must flow through
         * __getitem__ for user classes instead of bypassing the data model
         * through the legacy primitive slicing opcode. */
        PIRValue low = sl->data.slice.lower
                       ? build_expr(sl->data.slice.lower)
                       : emit_const_none();
        PIRValue high = sl->data.slice.upper
                        ? build_expr(sl->data.slice.upper)
                        : emit_const_none();
        PIRValue step = sl->data.slice.step
                        ? build_expr(sl->data.slice.step)
                        : emit_const_none();

        /* Push start, stop, step as args before PIR_SLICE */
        PIRInst *pa0 = emit(PIR_PUSH_ARG);
        pa0->operands[0] = low;
        pa0->num_operands = 1;

        PIRInst *pa1 = emit(PIR_PUSH_ARG);
        pa1->operands[0] = high;
        pa1->num_operands = 1;

        PIRInst *pa2 = emit(PIR_PUSH_ARG);
        pa2->operands[0] = step;
        pa2->num_operands = 1;

        PIRValue slice = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *make_slice = emit(PIR_CALL);
        make_slice->result = slice;
        make_slice->str_val = pir_str_dup("pydos_obj_new_slice");
        make_slice->int_val = 3;

        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *inst = emit(PIR_SUBSCR_GET);
        inst->result = result;
        inst->operands[0] = obj;
        inst->operands[1] = slice;
        inst->num_operands = 2;
        /* The key is a slice object, so integer-only typed getitem helpers
         * are not valid even when the receiver is statically a string or a
         * list.  The generic runtime dispatch selects the primitive slice
         * path or invokes a user-defined __getitem__. */
        return result;
    }

    PIRValue index = build_expr(node->data.subscript.index);
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_SUBSCR_GET);
    inst->result = result;
    inst->operands[0] = obj;
    inst->operands[1] = index;
    inst->num_operands = 2;
    /* Attach object type for typed dispatch (str_index vs list_get vs obj_getitem) */
    if (sema) {
        TypeInfo *obj_type = sema->get_expr_type(node->data.subscript.object);
        TypeInfo *index_type = sema->get_expr_type(node->data.subscript.index);
        /* Primitive index helpers accept only an integer.  A union such as
         * int | slice must retain generic dispatch because its runtime value
         * can be a first-class slice object. */
        if (obj_type && obj_type->kind != TY_ERROR && index_type &&
            (index_type->kind == TY_INT || index_type->kind == TY_BOOL))
            inst->type_hint = obj_type;
    }
    return result;
}

/* A display containing a starred element cannot use the count-based
 * BUILD_LIST: "[1, *rest]" splices an iterable at that position.  Build the
 * sequence incrementally instead, appending items and extending packs. */
PIRValue PIRBuilder::build_spliced_sequence(ASTNode *first)
{
    ASTNode *elem;
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *mk = emit(PIR_BUILD_LIST);
    mk->result = result;
    mk->int_val = 0;

    for (elem = first; elem; elem = elem->next) {
        if (elem->kind == AST_STARRED) {
            PIRValue pack = build_expr(elem->data.starred.value);
            PIRValue ignored = pir_func_alloc_value(current_func,
                                                    PIR_TYPE_PYOBJ);
            PIRInst *push = emit(PIR_PUSH_ARG);
            PIRInst *call;
            push->operands[0] = pack;
            push->num_operands = 1;
            call = emit(PIR_CALL_METHOD);
            call->result = ignored;
            call->operands[0] = result;
            call->num_operands = 1;
            call->str_val = pir_str_dup("extend");
            call->int_val = 1;
        } else {
            PIRValue item = build_expr(elem);
            PIRInst *append = emit(PIR_LIST_APPEND);
            append->operands[0] = result;
            append->operands[1] = item;
            append->num_operands = 2;
        }
    }
    return result;
}

/* Does this display splice an iterable into itself? */
static int display_has_star(ASTNode *first)
{
    ASTNode *elem;
    for (elem = first; elem; elem = elem->next) {
        if (elem->kind == AST_STARRED) return 1;
    }
    return 0;
}

PIRValue PIRBuilder::build_list_expr(ASTNode *node)
{
    ASTNode *elem;
    int count = 0;
    PIRValue elems[64];
    int i;

    if (display_has_star(node->data.collection.elts))
        return build_spliced_sequence(node->data.collection.elts);

    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        count++;
    }

    /* Evaluate all elements into temps first to avoid push_arg interleaving
       with sub-expression calls (e.g. [func(a), func(b)]) */
    i = 0;
    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        elems[i++] = build_expr(elem);
    }

    /* Now push all element temps — no interleaving with sub-calls */
    for (i = 0; i < count; i++) {
        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elems[i];
        pa->num_operands = 1;
    }

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_LIST);
    inst->result = result;
    inst->int_val = count;
    return result;
}

PIRValue PIRBuilder::build_dict_expr(ASTNode *node)
{
    ASTNode *key, *val;
    int count = 0;
    PIRValue keys[64], vals[64];
    int i;

    for (key = node->data.dict.keys; key; key = key->next) {
        count++;
    }

    /* Evaluate all key-value expressions into temps first to avoid
       push_arg interleaving with sub-expression calls */
    i = 0;
    key = node->data.dict.keys;
    val = node->data.dict.values;
    while (key && val) {
        keys[i] = build_expr(key);
        vals[i] = build_expr(val);
        i++;
        key = key->next;
        val = val->next;
    }

    /* Now push all key-value temps — no interleaving with sub-calls */
    for (i = 0; i < count; i++) {
        PIRInst *pak = emit(PIR_PUSH_ARG);
        pak->operands[0] = keys[i];
        pak->num_operands = 1;

        PIRInst *pav = emit(PIR_PUSH_ARG);
        pav->operands[0] = vals[i];
        pav->num_operands = 1;
    }

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_DICT);
    inst->result = result;
    inst->int_val = count;
    return result;
}

PIRValue PIRBuilder::build_tuple_expr(ASTNode *node)
{
    ASTNode *elem;
    int count = 0;
    PIRValue elems[64];
    int i;

    if (display_has_star(node->data.collection.elts)) {
        PIRValue spliced = build_spliced_sequence(node->data.collection.elts);
        PIRValue converted = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = spliced;
        push->num_operands = 1;
        call = emit(PIR_CALL);
        call->result = converted;
        /* tuple() builds a fresh object.  Retagging the list in place would
         * hand the same reference to the arena twice. */
        call->str_val = pir_str_dup("tuple");
        call->int_val = 1;
        return converted;
    }

    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        count++;
    }

    /* Evaluate all elements into temps first to avoid push_arg interleaving */
    i = 0;
    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        elems[i++] = build_expr(elem);
    }

    for (i = 0; i < count; i++) {
        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elems[i];
        pa->num_operands = 1;
    }

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_TUPLE);
    inst->result = result;
    inst->int_val = count;
    return result;
}

PIRValue PIRBuilder::build_set_expr(ASTNode *node)
{
    ASTNode *elem;
    int count = 0;
    PIRValue elems[64];
    int i;

    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        count++;
    }

    /* Evaluate all elements into temps first to avoid push_arg interleaving */
    i = 0;
    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        elems[i++] = build_expr(elem);
    }

    for (i = 0; i < count; i++) {
        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elems[i];
        pa->num_operands = 1;
    }

    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_SET);
    inst->result = result;
    inst->int_val = count;
    return result;
}

PIRValue PIRBuilder::build_fstring(ASTNode *node)
{
    ASTNode *part;
    PIRValue result = pir_value_none();
    int first = 1;
    int num_parts = 0;

    /* Count parts to decide: pairwise concat (<=2) or batched join (>=3) */
    for (part = node->data.fstring.parts; part; part = part->next) {
        num_parts++;
    }

    if (num_parts >= 3) {
        /* Batched join: evaluate all parts first, THEN push for join.
         * This avoids interleaving PUSH_ARG with CALL (str() conversion),
         * which would corrupt the arg accumulator in codegen. */
        PIRValue part_vals[64];
        int count = 0;

        for (part = node->data.fstring.parts; part; part = part->next) {
            if (count >= 64) break;

            if (part->kind == AST_STR_LIT) {
                part_vals[count] = build_str_lit(part);
            } else {
                PIRValue expr_val = build_expr(part);
                PIRInst *pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = expr_val;
                pa->num_operands = 1;

                PIRValue str_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                PIRInst *call = emit(PIR_CALL);
                call->result = str_val;
                call->str_val = pir_str_dup("str");
                call->int_val = 1;

                part_vals[count] = str_val;
            }
            count++;
        }

        /* Now push all parts consecutively - no intervening CALLs */
        {
            int i;
            for (i = 0; i < count; i++) {
                PIRInst *pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = part_vals[i];
                pa->num_operands = 1;
            }
        }

        result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *join = emit(PIR_STR_JOIN);
        join->result = result;
        join->int_val = count;
        return result;
    }

    /* Pairwise concat for 1-2 parts (no benefit from join) */
    for (part = node->data.fstring.parts; part; part = part->next) {
        PIRValue part_val;

        if (part->kind == AST_STR_LIT) {
            part_val = build_str_lit(part);
        } else {
            PIRValue expr_val = build_expr(part);
            PIRInst *pa = emit(PIR_PUSH_ARG);
            pa->operands[0] = expr_val;
            pa->num_operands = 1;

            part_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *call = emit(PIR_CALL);
            call->result = part_val;
            call->str_val = pir_str_dup("str");
            call->int_val = 1;
        }

        if (first) {
            result = part_val;
            first = 0;
        } else {
            PIRValue concat = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *add = emit(PIR_PY_ADD);
            add->result = concat;
            add->operands[0] = result;
            add->operands[1] = part_val;
            add->num_operands = 2;
            add->type_hint = type_str;
            result = concat;
        }
    }

    if (!pir_value_valid(result)) {
        result = emit_const_str("", 0);
    }
    return result;
}

static void collect_comprehension_target_names(ASTNode *target,
                                               const char **names,
                                               int *count)
{
    ASTNode *element;
    int i;
    if (!target || *count >= 32) return;
    if (target->kind == AST_NAME) {
        for (i = 0; i < *count; i++)
            if (pir_str_eq(names[i], target->data.name.id)) return;
        names[(*count)++] = target->data.name.id;
        return;
    }
    if (target->kind == AST_STARRED) {
        collect_comprehension_target_names(target->data.starred.value,
                                           names, count);
        return;
    }
    if (target->kind == AST_TUPLE_EXPR || target->kind == AST_LIST_EXPR)
        for (element = target->data.collection.elts; element;
             element = element->next)
            collect_comprehension_target_names(element, names, count);
}

int PIRBuilder::begin_comprehension_scope(ASTNode *generators,
                                          const char **names,
                                          PIRValue *saved,
                                          unsigned char *existed)
{
    ASTNode *generator;
    int count = 0;
    int i;
    for (generator = generators; generator; generator = generator->next)
        collect_comprehension_target_names(generator->data.comp_gen.target,
                                           names, &count);
    for (i = 0; i < count; i++) {
        existed[i] = (unsigned char)(
            (var_map && var_map->get(names[i])) ||
            (cell_map && cell_map->get(names[i])));
        if (existed[i]) saved[i] = var_load(names[i]);
    }
    return count;
}

void PIRBuilder::end_comprehension_scope(int count, const char **names,
                                         PIRValue *saved,
                                         unsigned char *existed)
{
    int i;
    for (i = 0; i < count; i++) {
        if (existed[i]) {
            var_store(names[i], saved[i]);
        } else if (var_map) {
            PIRValue *alloca_value = var_map->get(names[i]);
            if (alloca_value) {
                PIRInst *del = emit(PIR_DEL_NAME);
                del->operands[0] = *alloca_value;
                del->num_operands = 1;
                var_map->remove(names[i]);
            }
        }
    }
}

PIRValue PIRBuilder::build_listcomp(ASTNode *node)
{
    const char *scope_names[32];
    PIRValue saved[32];
    unsigned char existed[32];
    int scope_count = begin_comprehension_scope(
        node->data.listcomp.generators, scope_names, saved, existed);
    /* Create empty list */
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_LIST);
    inst->result = result;
    inst->int_val = 0;

    /* Process generators */
    build_listcomp_loop(node, node->data.listcomp.generators, result);

    end_comprehension_scope(scope_count, scope_names, saved, existed);

    return result;
}

void PIRBuilder::build_listcomp_loop(ASTNode *comp_node, ASTNode *gen, PIRValue result_val)
{
    if (!gen) {
        /* Base case: evaluate element and append */
        PIRValue elt = build_expr(comp_node->data.listcomp.elt);

        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elt;
        pa->num_operands = 1;

        PIRValue append_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL_METHOD);
        call->result = append_result;
        call->operands[0] = result_val;
        call->num_operands = 1;
        call->str_val = pir_str_dup("append");
        call->int_val = 1;
        return;
    }

    /* Get iterator */
    PIRValue iter_src = build_expr(gen->data.comp_gen.iter);
    PIRValue iter_obj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *gi = emit(PIR_GET_ITER);
        gi->result = iter_obj;
        gi->operands[0] = iter_src;
        gi->num_operands = 1;
    }

    PIRBlock *loop_block = new_block("comp_loop");
    PIRBlock *end_block = new_block("comp_end");

    emit_branch(loop_block);

    switch_to_block(loop_block);
    PIRValue item = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *fi = emit(PIR_FOR_ITER);
        fi->result = item;
        fi->operands[0] = iter_obj;
        fi->num_operands = 1;
        fi->handler_block = end_block;
        pir_block_add_edge(loop_block, end_block);
    }

    build_store(gen->data.comp_gen.target, item);

    /* Apply filters */
    ASTNode *filter;
    PIRBlock *filter_skip = 0;
    for (filter = gen->data.comp_gen.ifs; filter; filter = filter->next) {
        PIRValue filt_val = build_expr(filter);
        if (!filter_skip) {
            filter_skip = new_block("comp_skip");
        }
        PIRBlock *pass = new_block("comp_pass");
        emit_cond_branch(filt_val, pass, filter_skip);
        switch_to_block(pass);
    }

    /* Recurse or emit element */
    if (gen->next) {
        build_listcomp_loop(comp_node, gen->next, result_val);
    } else {
        /* Base case */
        PIRValue elt = build_expr(comp_node->data.listcomp.elt);
        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elt;
        pa->num_operands = 1;
        PIRValue append_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL_METHOD);
        call->result = append_result;
        call->operands[0] = result_val;
        call->num_operands = 1;
        call->str_val = pir_str_dup("append");
        call->int_val = 1;
    }

    if (filter_skip) {
        if (!block_is_terminated()) emit_branch(filter_skip);
        switch_to_block(filter_skip);
    }

    if (!block_is_terminated()) emit_branch(loop_block);

    switch_to_block(end_block);
}

PIRValue PIRBuilder::build_lambda(ASTNode *node)
{
    static int lambda_counter = 0;
    char name[64];
    sprintf(name, "_lambda_%d", lambda_counter++);

    /* Save context */
    PIRFunction *outer_func = current_func;
    PIRBlock *outer_block = current_block;
    PdHashMap<const char *, PIRValue> *outer_var_map = var_map;
    PdHashMap<const char *, PIRValue> *outer_cell_map = cell_map;
    PdHashMap<const char *, PIRValue> *outer_closure_map = closure_map;
    var_map = 0;
    cell_map = 0;
    closure_map = 0;
    int outer_loop = loop_depth;
    int outer_exception_target_depth = exception_target_depth;
    PIRBlock *outer_exception_exit_block = exception_exit_block;
    int outer_suppress_exception_checks = suppress_exception_checks;
    int outer_handled_exception_depth = handled_exception_depth;
    PIRBlock *outer_exception_targets[32];
    PIRValue outer_handled_exceptions[32];
    memcpy(outer_exception_targets, exception_targets,
           sizeof(exception_targets));
    memcpy(outer_handled_exceptions, handled_exceptions,
           sizeof(handled_exceptions));
    loop_depth = 0;
    handled_exception_depth = 0;
    memset(handled_exceptions, 0, sizeof(handled_exceptions));

    /* Create lambda function */
    PIRFunction *func = begin_func(name);

    /* Add params */
    Param *param;
    int param_count = 0;
    for (param = node->data.lambda.params; param; param = param->next) {
        if (is_bare_star_sep(param)) continue;
        PIRValue pval = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
        func->params.push_back(pval);
        var_alloca(param->name);
        var_store(param->name, pval);
        param_count++;
    }
    func->num_params = param_count;

    if (node->data.lambda.num_free_vars > 0) {
        int fv;
        PIRValue closure = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
        PIRInst *lc = emit(PIR_LOAD_CLOSURE);
        lc->result = closure;
        cell_map = new PdHashMap<const char *, PIRValue>(
            (PdHashMap<const char *, PIRValue>::HashFn)pd_hash_str,
            (PdHashMap<const char *, PIRValue>::EqFn)pd_eq_str);
        for (fv = 0; fv < node->data.lambda.num_free_vars; fv++) {
            PIRValue idx = emit_const_int(fv);
            PIRValue cell = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
            PIRInst *sg = emit(PIR_SUBSCR_GET);
            sg->result = cell;
            sg->operands[0] = closure;
            sg->operands[1] = idx;
            sg->num_operands = 2;
            cell_map->put(node->data.lambda.free_var_names[fv], cell);
        }
    }

    /* Body is a single expression */
    PIRValue body_val = build_expr(node->data.lambda.body);
    emit_return(body_val);

    end_func();

    /* Restore context */
    if (var_map) delete var_map;
    var_map = outer_var_map;
    if (cell_map) delete cell_map;
    cell_map = outer_cell_map;
    if (closure_map) delete closure_map;
    closure_map = outer_closure_map;
    current_func = outer_func;
    current_block = outer_block;
    loop_depth = outer_loop;
    exception_target_depth = outer_exception_target_depth;
    exception_exit_block = outer_exception_exit_block;
    suppress_exception_checks = outer_suppress_exception_checks;
    handled_exception_depth = outer_handled_exception_depth;
    memcpy(exception_targets, outer_exception_targets,
           sizeof(exception_targets));
    memcpy(handled_exceptions, outer_handled_exceptions,
           sizeof(handled_exceptions));

    /* Create function object */
    PIRValue closure_list = pir_value_none();
    if (node->data.lambda.num_free_vars > 0 && cell_map) {
        int fv;
        PIRInst *nl;
        closure_list = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        nl = emit(PIR_LIST_NEW);
        nl->result = closure_list;
        for (fv = 0; fv < node->data.lambda.num_free_vars; fv++) {
            PIRValue *cell = cell_map->get(
                node->data.lambda.free_var_names[fv]);
            if (cell) {
                PIRInst *ap = emit(PIR_LIST_APPEND);
                ap->operands[0] = closure_list;
                ap->operands[1] = *cell;
                ap->num_operands = 2;
            }
        }
    }

    PIRValue fobj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *mk = emit(PIR_MAKE_FUNCTION);
    mk->result = fobj;
    mk->str_val = pir_str_dup(name);
    mk->int_val = add_const_str("<lambda>", 8);
    if (pir_value_valid(closure_list)) {
        mk->operands[0] = closure_list;
        mk->num_operands = 1;
    }
    attach_parameter_metadata(node->data.lambda.params, fobj);
    return fobj;
}

PIRValue PIRBuilder::build_ifexpr(ASTNode *node)
{
    PIRBlock *then_block = new_block("ternary_then");
    PIRBlock *else_block = new_block("ternary_else");
    PIRBlock *merge_block = new_block("ternary_merge");

    PIRValue cond = build_expr(node->data.ifexpr.test);
    emit_cond_branch(cond, then_block, else_block);

    /* Then */
    switch_to_block(then_block);
    PIRValue then_val = build_expr(node->data.ifexpr.body);
    /* Store to a temp variable so both paths merge */
    const char *tmp_name = "__ternary_tmp__";
    if (!var_exists(tmp_name)) var_alloca(tmp_name);
    var_store(tmp_name, then_val);
    emit_branch(merge_block);

    /* Else */
    switch_to_block(else_block);
    PIRValue else_val = build_expr(node->data.ifexpr.else_body);
    var_store(tmp_name, else_val);
    emit_branch(merge_block);

    /* Merge */
    switch_to_block(merge_block);
    return var_load(tmp_name);
}

PIRValue PIRBuilder::build_yield(ASTNode *node)
{
    if (!pir_value_valid(gen_val)) {
        report_error(node, "yield outside generator function");
        return emit_const_none();
    }

    /* Evaluate yield value */
    PIRValue val;
    if (node->data.yield_expr.value) {
        val = build_expr(node->data.yield_expr.value);
    } else {
        val = emit_const_none();
    }

    return emit_yield_point(val);
}

/* --------------------------------------------------------------- */
/* emit_yield_point — Core yield state-machine logic                 */
/*                                                                   */
/* Saves generator state, returns val to caller, creates a resume    */
/* block that restores state + checks throw + reads sent value.      */
/* Returns the sent value (result of yield expression).              */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::emit_yield_point(PIRValue val)
{
    /* Allocate new state */
    int state_num = gen_state_count;
    gen_state_count++;
    PIRBlock *resume_block = new_block("yield_resume");
    if (state_num < 32) {
        gen_state_blocks[state_num] = resume_block;
    }

    /* Collect locals to save (all var_map entries except __gen__) */
    const char *save_names[64];
    int save_indices[64];
    int save_count = 0;
    {
        int si;
        for (si = 0; si < var_map->capacity(); si++) {
            int gi;
            int idx;
            const char *vname;

            if (!var_map->slot_occupied(si)) continue;
            vname = var_map->slot_key(si);
            if (pir_str_eq(vname, "__gen__")) continue;
            /* Captured names live inside the hidden cell object.  Saving the
             * value separately would restore through an uninitialized cell
             * before the hidden cell itself has been loaded. */
            if (cell_map && cell_map->get(vname)) continue;

            /* Find or assign gen-local index */
            idx = -1;
            for (gi = 0; gi < gen_local_count; gi++) {
                if (pir_str_eq(gen_local_names[gi], vname)) {
                    idx = gi;
                    break;
                }
            }
            if (idx < 0 && gen_local_count < 64) {
                idx = gen_local_count;
                gen_local_names[gen_local_count] = vname;
                gen_local_count++;
            }

            if (save_count < 64) {
                save_names[save_count] = vname;
                save_indices[save_count] = idx;
                save_count++;
            }
        }
    }

    /* Save locals to generator object */
    PIRValue gen_temp = var_load("__gen__");
    {
        int si;
        for (si = 0; si < save_count; si++) {
            PIRValue local_val = var_load(save_names[si]);
            PIRInst *sv = emit(PIR_GEN_SAVE_LOCAL);
            sv->operands[0] = gen_temp;
            sv->operands[1] = local_val;
            sv->num_operands = 2;
            sv->int_val = save_indices[si];
        }
    }

    /* Set PC to resume state */
    {
        PIRInst *sp = emit(PIR_GEN_SET_PC);
        sp->operands[0] = gen_temp;
        sp->num_operands = 1;
        sp->int_val = state_num;
    }

    /* Return yielded value */
    emit_return(val);

    /* Resume block */
    switch_to_block(resume_block);

    /* Restore locals from generator object */
    {
        PIRValue gen_temp2 = var_load("__gen__");
        int si;
        for (si = 0; si < save_count; si++) {
            PIRValue loaded = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *ld = emit(PIR_GEN_LOAD_LOCAL);
            ld->result = loaded;
            ld->operands[0] = gen_temp2;
            ld->num_operands = 1;
            ld->int_val = save_indices[si];
            var_store(save_names[si], loaded);
            if (cell_map &&
                strncmp(save_names[si], "__pydos_cell_", 13) == 0) {
                cell_map->put(save_names[si] + 13, loaded);
            }
        }
    }

    /* Check for a pending throw and branch through explicit exception flow. */
    {
        PIRValue gen_t2 = var_load("__gen__");
        PIRInst *ct = emit(PIR_GEN_CHECK_THROW);
        ct->operands[0] = gen_t2;
        ct->num_operands = 1;
    }

    /* Read sent value — this is the result of the yield expression.
     * send(val) stores val in pydos_gen_sent; next() stores None. */
    {
        PIRValue sent = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *gs = emit(PIR_GEN_GET_SENT);
        gs->result = sent;
        gs->num_operands = 0;
        return sent;
    }
}

/* --------------------------------------------------------------- */
/* yield from delegation                                             */
/*                                                                   */
/* Desugars `yield from EXPR` into:                                  */
/*   _iter = get_iter(EXPR)                                          */
/*   loop:                                                           */
/*     _item = for_iter(_iter)  -> on StopIteration, jump to end     */
/*     yield _item                                                   */
/*     goto loop                                                     */
/*   end:                                                            */
/*     result = StopIteration.value                                  */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::build_yield_from(ASTNode *node)
{
    if (!pir_value_valid(gen_val)) {
        report_error(node, "yield from outside generator function");
        return emit_const_none();
    }

    /* Evaluate sub-iterable */
    PIRValue sub_expr = build_expr(node->data.yield_expr.value);

    /* Get iterator — store in named alloca for generator save/restore */
    char iname[64];
    sprintf(iname, "__foriter_%d__", gen_for_iter_count++);
    const char *iter_alloca_name = pir_str_dup(iname);
    var_alloca(iter_alloca_name);
    {
        PIRValue raw = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *gi = emit(PIR_GET_ITER);
        gi->result = raw;
        gi->operands[0] = sub_expr;
        gi->num_operands = 1;
        var_store(iter_alloca_name, raw);
    }

    PIRBlock *loop_block = new_block("yieldfrom_loop");
    PIRBlock *end_block = new_block("yieldfrom_end");

    emit_branch(loop_block);

    /* Loop: get next item from sub-iterator */
    switch_to_block(loop_block);
    PIRValue iter_obj = var_load(iter_alloca_name);
    PIRValue item = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *fi = emit(PIR_FOR_ITER);
        fi->result = item;
        fi->operands[0] = iter_obj;
        fi->num_operands = 1;
        fi->handler_block = end_block;
        pir_block_add_edge(loop_block, end_block);
    }

    /* Yield the item — emit_yield_point handles the full save/restore cycle */
    emit_yield_point(item);
    /* Sent value is discarded (basic yield from, no forwarding) */

    /* Branch back to loop */
    if (!block_is_terminated()) {
        emit_branch(loop_block);
    }

    /* End block: recover the return expression carried by the exhausted
     * generator instead of silently replacing StopIteration.value by None. */
    switch_to_block(end_block);
    {
        PIRValue exhausted_iter = var_load(iter_alloca_name);
        PIRValue result = pir_func_alloc_value(current_func,
                                               PIR_TYPE_PYOBJ);
        PIRInst *push = emit(PIR_PUSH_ARG);
        PIRInst *call;
        push->operands[0] = exhausted_iter;
        push->num_operands = 1;
        call = emit(PIR_CALL);
        call->result = result;
        call->str_val = pir_str_dup("pydos_gen_get_return_value");
        call->int_val = 1;
        return result;
    }
}

/* --------------------------------------------------------------- */
/* Store to target                                                   */
/* --------------------------------------------------------------- */
void PIRBuilder::build_store(ASTNode *target, PIRValue val)
{
    if (!target) return;

    switch (target->kind) {
    case AST_NAME:
        var_store(target->data.name.id, val);
        break;

    case AST_ATTR: {
        PIRValue obj = build_expr(target->data.attribute.object);
        PIRInst *inst = emit(PIR_SET_ATTR);
        inst->operands[0] = obj;
        inst->operands[1] = val;
        inst->num_operands = 2;
        inst->str_val = pir_str_dup(target->data.attribute.attr);
        break;
    }

    case AST_SUBSCRIPT: {
        PIRValue obj = build_expr(target->data.subscript.object);
        PIRValue index = build_expr(target->data.subscript.index);
        PIRInst *inst = emit(PIR_SUBSCR_SET);
        inst->operands[0] = obj;
        inst->operands[1] = index;
        inst->operands[2] = val;
        inst->num_operands = 3;
        break;
    }

    case AST_TUPLE_EXPR:
    case AST_LIST_EXPR: {
        /* Tuple/list unpacking — check for starred element */
        ASTNode *elem = target->data.collection.elts;
        int has_star = 0;
        int star_pos = 0;
        int total_elts = 0;
        int before_count = 0;
        int after_count = 0;
        {
            ASTNode *e;
            int pos = 0;
            for (e = elem; e; e = e->next) {
                if (e->kind == AST_STARRED) {
                    if (has_star) {
                        report_error(target,
                            "multiple starred expressions in assignment");
                        break;
                    }
                    has_star = 1;
                    star_pos = pos;
                }
                pos++;
                total_elts++;
            }
        }

        if (has_star) {
            /* Star unpacking: call pydos_unpack_ex_(seq, before, after) */
            before_count = star_pos;
            after_count = total_elts - star_pos - 1;
            PIRValue before_val = emit_const_int(before_count);
            PIRValue after_val = emit_const_int(after_count);

            /* Push args: seq, before, after */
            {
                PIRInst *pa;
                pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = val;
                pa->num_operands = 1;
                pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = before_val;
                pa->num_operands = 1;
                pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = after_val;
                pa->num_operands = 1;
            }

            /* Call pydos_unpack_ex_ */
            PIRValue unpack_result = pir_func_alloc_value(current_func,
                                                          PIR_TYPE_PYOBJ);
            {
                PIRInst *call = emit(PIR_CALL);
                call->result = unpack_result;
                call->str_val = pir_str_dup("pydos_unpack_ex");
                call->int_val = 3;
            }

            /* Extract each element from the result list by index */
            {
                int idx = 0;
                ASTNode *e;
                for (e = elem; e; e = e->next) {
                    PIRValue idx_val = emit_const_int(idx);
                    PIRValue item = pir_func_alloc_value(current_func,
                                                         PIR_TYPE_PYOBJ);
                    PIRInst *sub = emit(PIR_SUBSCR_GET);
                    sub->result = item;
                    sub->operands[0] = unpack_result;
                    sub->operands[1] = idx_val;
                    sub->num_operands = 2;

                    if (e->kind == AST_STARRED) {
                        /* Store to the starred target's inner name */
                        build_store(e->data.starred.value, item);
                    } else {
                        build_store(e, item);
                    }
                    idx++;
                }
            }
        } else {
            /* Simple unpacking (no star) — existing path */
            int idx = 0;
            while (elem) {
                PIRValue idx_val = emit_const_int(idx);
                PIRValue item = pir_func_alloc_value(current_func,
                                                     PIR_TYPE_PYOBJ);
                PIRInst *sub = emit(PIR_SUBSCR_GET);
                sub->result = item;
                sub->operands[0] = val;
                sub->operands[1] = idx_val;
                sub->num_operands = 2;

                build_store(elem, item);
                elem = elem->next;
                idx++;
            }
        }
        break;
    }

    default:
        report_error(target, "unsupported assignment target in PIR builder");
        break;
    }
}

/* --------------------------------------------------------------- */
/* Walrus operator :=                                                */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::build_walrus(ASTNode *node)
{
    PIRValue val = build_expr(node->data.walrus.value);
    build_store(node->data.walrus.target, val);
    return val;
}

/* --------------------------------------------------------------- */
/* Dict comprehension {k: v for ...}                                 */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::build_dictcomp(ASTNode *node)
{
    const char *scope_names[32];
    PIRValue saved[32];
    unsigned char existed[32];
    int scope_count = begin_comprehension_scope(
        node->data.dictcomp.generators, scope_names, saved, existed);
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_DICT);
    inst->result = result;
    inst->int_val = 0;

    build_dictcomp_loop(node, node->data.dictcomp.generators, result);
    end_comprehension_scope(scope_count, scope_names, saved, existed);
    return result;
}

void PIRBuilder::build_dictcomp_loop(ASTNode *comp_node, ASTNode *gen,
                                      PIRValue result_val)
{
    if (!gen) {
        /* Base case: evaluate key+value and store in dict */
        PIRValue key = build_expr(comp_node->data.dictcomp.key);
        PIRValue val = build_expr(comp_node->data.dictcomp.value);

        PIRInst *ss = emit(PIR_SUBSCR_SET);
        ss->operands[0] = result_val;
        ss->operands[1] = key;
        ss->operands[2] = val;
        ss->num_operands = 3;
        return;
    }

    /* Get iterator */
    PIRValue iter_src = build_expr(gen->data.comp_gen.iter);
    PIRValue iter_obj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *gi = emit(PIR_GET_ITER);
        gi->result = iter_obj;
        gi->operands[0] = iter_src;
        gi->num_operands = 1;
    }

    PIRBlock *loop_block = new_block("dcomp_loop");
    PIRBlock *end_block = new_block("dcomp_end");

    emit_branch(loop_block);

    switch_to_block(loop_block);
    PIRValue item = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *fi = emit(PIR_FOR_ITER);
        fi->result = item;
        fi->operands[0] = iter_obj;
        fi->num_operands = 1;
        fi->handler_block = end_block;
        pir_block_add_edge(loop_block, end_block);
    }

    build_store(gen->data.comp_gen.target, item);

    /* Apply filters */
    ASTNode *filter;
    PIRBlock *filter_skip = 0;
    for (filter = gen->data.comp_gen.ifs; filter; filter = filter->next) {
        PIRValue filt_val = build_expr(filter);
        if (!filter_skip) filter_skip = new_block("dcomp_skip");
        PIRBlock *pass = new_block("dcomp_pass");
        emit_cond_branch(filt_val, pass, filter_skip);
        switch_to_block(pass);
    }

    /* Recurse or emit base case */
    if (gen->next) {
        build_dictcomp_loop(comp_node, gen->next, result_val);
    } else {
        PIRValue key = build_expr(comp_node->data.dictcomp.key);
        PIRValue val = build_expr(comp_node->data.dictcomp.value);

        PIRInst *ss = emit(PIR_SUBSCR_SET);
        ss->operands[0] = result_val;
        ss->operands[1] = key;
        ss->operands[2] = val;
        ss->num_operands = 3;
    }

    if (filter_skip) {
        if (!block_is_terminated()) emit_branch(filter_skip);
        switch_to_block(filter_skip);
    }

    if (!block_is_terminated()) emit_branch(loop_block);
    switch_to_block(end_block);
}

/* --------------------------------------------------------------- */
/* Set comprehension {x for ...}                                     */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::build_setcomp(ASTNode *node)
{
    const char *scope_names[32];
    PIRValue saved[32];
    unsigned char existed[32];
    int scope_count = begin_comprehension_scope(
        node->data.listcomp.generators, scope_names, saved, existed);
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_SET);
    inst->result = result;
    inst->int_val = 0;

    /* Uses listcomp struct (elt + generators) */
    build_setcomp_loop(node, node->data.listcomp.generators, result);
    end_comprehension_scope(scope_count, scope_names, saved, existed);
    return result;
}

void PIRBuilder::build_setcomp_loop(ASTNode *comp_node, ASTNode *gen,
                                     PIRValue result_val)
{
    if (!gen) {
        PIRValue elt = build_expr(comp_node->data.listcomp.elt);

        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elt;
        pa->num_operands = 1;

        PIRValue add_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL_METHOD);
        call->result = add_result;
        call->operands[0] = result_val;
        call->num_operands = 1;
        call->str_val = pir_str_dup("add");
        call->int_val = 1;
        return;
    }

    PIRValue iter_src = build_expr(gen->data.comp_gen.iter);
    PIRValue iter_obj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *gi = emit(PIR_GET_ITER);
        gi->result = iter_obj;
        gi->operands[0] = iter_src;
        gi->num_operands = 1;
    }

    PIRBlock *loop_block = new_block("scomp_loop");
    PIRBlock *end_block = new_block("scomp_end");

    emit_branch(loop_block);
    switch_to_block(loop_block);

    PIRValue item = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    {
        PIRInst *fi = emit(PIR_FOR_ITER);
        fi->result = item;
        fi->operands[0] = iter_obj;
        fi->num_operands = 1;
        fi->handler_block = end_block;
        pir_block_add_edge(loop_block, end_block);
    }

    build_store(gen->data.comp_gen.target, item);

    ASTNode *filter;
    PIRBlock *filter_skip = 0;
    for (filter = gen->data.comp_gen.ifs; filter; filter = filter->next) {
        PIRValue filt_val = build_expr(filter);
        if (!filter_skip) filter_skip = new_block("scomp_skip");
        PIRBlock *pass = new_block("scomp_pass");
        emit_cond_branch(filt_val, pass, filter_skip);
        switch_to_block(pass);
    }

    if (gen->next) {
        build_setcomp_loop(comp_node, gen->next, result_val);
    } else {
        PIRValue elt = build_expr(comp_node->data.listcomp.elt);
        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = elt;
        pa->num_operands = 1;
        PIRValue add_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL_METHOD);
        call->result = add_result;
        call->operands[0] = result_val;
        call->num_operands = 1;
        call->str_val = pir_str_dup("add");
        call->int_val = 1;
    }

    if (filter_skip) {
        if (!block_is_terminated()) emit_branch(filter_skip);
        switch_to_block(filter_skip);
    }

    if (!block_is_terminated()) emit_branch(loop_block);
    switch_to_block(end_block);
}

/* --------------------------------------------------------------- */
/* Generator expression (eager materialization as list)              */
/* --------------------------------------------------------------- */
PIRValue PIRBuilder::build_genexpr(ASTNode *node)
{
    const char *scope_names[32];
    PIRValue saved[32];
    unsigned char existed[32];
    int scope_count = begin_comprehension_scope(
        node->data.listcomp.generators, scope_names, saved, existed);
    /* Eagerly materialize as a list (same as list comprehension).
     * PyDOS has no lazy iteration infrastructure, so this is
     * semantically equivalent for bounded iterables. */
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_LIST);
    inst->result = result;
    inst->int_val = 0;

    /* AST_GENEXPR reuses listcomp struct */
    build_listcomp_loop(node, node->data.listcomp.generators, result);
    end_comprehension_scope(scope_count, scope_names, saved, existed);
    return result;
}

/* --------------------------------------------------------------- */
/* With statement                                                    */
/* --------------------------------------------------------------- */
void PIRBuilder::build_with(ASTNode *node)
{
    build_with_items(node->data.with_stmt.items,
                     node->data.with_stmt.body);
}

PIRValue PIRBuilder::emit_context_exit(const char *manager_name,
                                       PIRValue exc_type,
                                       PIRValue exc_value,
                                       PIRValue traceback)
{
    PIRValue manager = var_load(manager_name);
    PIRValue result;
    PIRInst *push;
    PIRInst *call;

    push = emit(PIR_PUSH_ARG);
    push->operands[0] = exc_type;
    push->num_operands = 1;
    push = emit(PIR_PUSH_ARG);
    push->operands[0] = exc_value;
    push->num_operands = 1;
    push = emit(PIR_PUSH_ARG);
    push->operands[0] = traceback;
    push->num_operands = 1;

    result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    call = emit(PIR_CALL_METHOD);
    call->result = result;
    call->operands[0] = manager;
    call->num_operands = 1;
    call->str_val = pir_str_dup("__exit__");
    call->int_val = 3;
    return result;
}

void PIRBuilder::build_with_items(ASTNode *item, ASTNode *body)
{
    char manager_name[48];
    PIRValue manager;
    PIRValue enter_result;
    PIRInst *call;
    PIRBlock *try_block;
    PIRBlock *handler_block;
    PIRBlock *normal_exit_block;
    PIRBlock *reraise_block;
    PIRBlock *end_block;
    int cleanup_base = return_cleanup_depth;
    int exception_base = exception_target_depth;
    PIRValue caught_exception = pir_value_none();

    if (!item) {
        build_stmts(body);
        return;
    }

    manager = build_expr(item->data.with_item.context_expr);
    sprintf(manager_name, "__with_mgr_%d__", synth_counter_++);
    var_alloca(manager_name);
    var_store(manager_name, manager);

    enter_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    call = emit(PIR_CALL_METHOD);
    call->result = enter_result;
    call->operands[0] = manager;
    call->num_operands = 1;
    call->str_val = pir_str_dup("__enter__");
    call->int_val = 0;

    try_block = new_block("with_try");
    handler_block = new_block("with_exc");
    normal_exit_block = new_block("with_exit");
    reraise_block = new_block("with_reraise");
    end_block = new_block("with_end");

    {
        PIRInst *setup = emit(PIR_SETUP_TRY);
        setup->handler_block = handler_block;
        pir_block_add_edge(current_block, handler_block);
    }
    if (exception_target_depth < 32)
        exception_targets[exception_target_depth++] = handler_block;
    emit_branch(try_block);
    switch_to_block(try_block);

    /* Target binding is protected too: a destructuring failure must invoke
     * __exit__ after __enter__ has succeeded. */
    if (item->data.with_item.optional_vars)
        build_store(item->data.with_item.optional_vars, enter_result);

    if (return_cleanup_depth >= 32) {
        report_error(item, "too many nested cleanup scopes (max 32)");
    } else {
        ReturnCleanup *cleanup = &return_cleanups[return_cleanup_depth++];
        cleanup->finally_body = 0;
        cleanup->pop_count = 1;
        strncpy(cleanup->manager_name, manager_name,
                sizeof(cleanup->manager_name) - 1);
        cleanup->manager_name[sizeof(cleanup->manager_name) - 1] = '\0';
    }

    if (item->next)
        build_with_items(item->next, body);
    else
        build_stmts(body);
    return_cleanup_depth = cleanup_base;
    exception_target_depth = exception_base;

    if (!block_is_terminated()) {
        emit(PIR_POP_TRY);
        emit_branch(normal_exit_block);
    }

    switch_to_block(normal_exit_block);
    {
        PIRValue none_value = emit_const_none();
        emit_context_exit(manager_name, none_value, none_value, none_value);
    }
    emit_branch(end_block);

    switch_to_block(handler_block);
    emit(PIR_POP_TRY);
    {
        PIRValue exception_value = pir_func_alloc_value(current_func,
                                                         PIR_TYPE_PYOBJ);
        PIRValue exception_type = pir_func_alloc_value(current_func,
                                                        PIR_TYPE_PYOBJ);
        PIRValue traceback = pir_func_alloc_value(current_func,
                                                   PIR_TYPE_PYOBJ);
        PIRValue exit_result;
        PIRInst *get_exception = emit(PIR_GET_EXCEPTION);
        PIRInst *push;
        PIRInst *type_call;

        get_exception->result = exception_value;
        caught_exception = exception_value;
        emit(PIR_CLEAR_EXCEPTION);
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = exception_value;
        push->num_operands = 1;
        type_call = emit(PIR_CALL);
        type_call->result = traceback;
        type_call->str_val = pir_str_dup("pydos_exc_get_traceback");
        type_call->int_val = 1;
        push = emit(PIR_PUSH_ARG);
        push->operands[0] = exception_value;
        push->num_operands = 1;
        type_call = emit(PIR_CALL);
        type_call->result = exception_type;
        type_call->str_val = pir_str_dup("type");
        type_call->int_val = 1;

        exit_result = emit_context_exit(manager_name, exception_type,
                                        exception_value, traceback);
        emit_cond_branch(exit_result, end_block, reraise_block);
    }

    switch_to_block(reraise_block);
    {
        PIRInst *raise = emit(PIR_RAISE);
        raise->operands[0] = caught_exception;
        raise->num_operands = 1;
    }

    switch_to_block(end_block);
}

/* --------------------------------------------------------------- */
/* Match/case statement                                              */
/* --------------------------------------------------------------- */
void PIRBuilder::build_match(ASTNode *node)
{
    PIRValue subject = build_expr(node->data.match_stmt.subject);

    /* Store subject for repeated access (unique name for nesting) */
    char match_subj_name[48];
    int match_id = synth_counter_++;
    sprintf(match_subj_name, "__match_subj_%d__", match_id);
    var_alloca(match_subj_name);
    var_store(match_subj_name, subject);

    PIRBlock *end_block = new_block("match_end");

    ASTNode *case_node;
    int case_idx = 0;
    for (case_node = node->data.match_stmt.cases; case_node;
         case_node = case_node->next, case_idx++) {
        PIRBlock *body_block = new_block("case_body");
        PIRBlock *next_block = case_node->next
                                ? new_block("case_next")
                                : end_block;

        PIRValue subj = var_load(match_subj_name);

        /* Build pattern matching */
        build_pattern_match(case_node->data.match_case.pattern,
                           subj, body_block, next_block);

        /* Build case body */
        switch_to_block(body_block);

        /* Apply guard if present */
        if (case_node->data.match_case.guard) {
            PIRBlock *guarded_body = new_block("case_guarded");
            PIRValue guard = build_expr(case_node->data.match_case.guard);
            emit_cond_branch(guard, guarded_body, next_block);
            switch_to_block(guarded_body);
        }

        build_stmts(case_node->data.match_case.body);
        if (!block_is_terminated()) {
            emit_branch(end_block);
        }

        if (case_node->next) {
            switch_to_block(next_block);
        }
    }

    switch_to_block(end_block);
}

void PIRBuilder::build_pattern_match(ASTNode *pattern, PIRValue subject,
                                      PIRBlock *match_block,
                                      PIRBlock *fail_block)
{
    if (!pattern) {
        /* No pattern = wildcard, always matches */
        emit_branch(match_block);
        return;
    }

    switch (pattern->kind) {
    case AST_NAME: {
        const char *name = pattern->data.name.id;
        if (strcmp(name, "_") == 0) {
            /* Wildcard: always matches */
            emit_branch(match_block);
        } else {
            /* Capture: bind variable and match */
            if (!var_exists(name)) var_alloca(name);
            var_store(name, subject);
            emit_branch(match_block);
        }
        break;
    }

    case AST_INT_LIT:
    case AST_STR_LIT:
    case AST_BOOL_LIT:
    case AST_NONE_LIT:
    case AST_FLOAT_LIT:
    case AST_COMPLEX_LIT: {
        /* Literal pattern: compare equality */
        PIRValue lit = build_expr(pattern);

        PIRValue eq = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *cmp = emit(PIR_PY_CMP_EQ);
            cmp->result = eq;
            cmp->operands[0] = subject;
            cmp->operands[1] = lit;
            cmp->num_operands = 2;
        }

        emit_cond_branch(eq, match_block, fail_block);
        break;
    }

    case AST_BINOP: {
        if (pattern->data.binop.op == OP_BITOR) {
            /* OR pattern: a | b */
            PIRBlock *try_right = new_block("or_right");
            build_pattern_match(pattern->data.binop.left, subject,
                               match_block, try_right);
            switch_to_block(try_right);
            build_pattern_match(pattern->data.binop.right, subject,
                               match_block, fail_block);
        } else {
            /* Not an OR pattern — evaluate and compare */
            PIRValue val = build_expr(pattern);
            PIRValue eq = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *cmp = emit(PIR_PY_CMP_EQ);
                cmp->result = eq;
                cmp->operands[0] = subject;
                cmp->operands[1] = val;
                cmp->num_operands = 2;
            }
            emit_cond_branch(eq, match_block, fail_block);
        }
        break;
    }

    case AST_LIST_EXPR:
    case AST_TUPLE_EXPR: {
        /* Sequence pattern [a, b, c] or [a, *rest, b] */
        ASTNode *elts = pattern->data.collection.elts;
        int count = 0;
        int star_idx = -1;
        int si;
        ASTNode *e;

        /* Count elements and find starred element */
        si = 0;
        for (e = elts; e; e = e->next, si++) {
            if (e->kind == AST_STARRED) star_idx = si;
            count++;
        }

        /* Structural sequence patterns exclude mappings and scalar
         * iterables such as str/bytes.  Testing len() alone allowed a dict
         * to reach starred-unpack before its later mapping case. */
        {
            PIRValue sequence_ok = pir_func_alloc_value(
                current_func, PIR_TYPE_PYOBJ);
            PIRInst *push = emit(PIR_PUSH_ARG);
            PIRInst *call;
            PIRBlock *sequence_body = new_block("seq_type_ok");
            push->operands[0] = subject;
            push->num_operands = 1;
            call = emit(PIR_CALL);
            call->result = sequence_ok;
            call->str_val = pir_str_dup("pydos_match_sequence");
            call->int_val = 1;
            emit_cond_branch(sequence_ok, sequence_body, fail_block);
            switch_to_block(sequence_body);
        }

        /* Get subject length */
        PIRInst *pa_len = emit(PIR_PUSH_ARG);
        pa_len->operands[0] = subject;
        pa_len->num_operands = 1;
        PIRValue len_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *len_call = emit(PIR_CALL);
        len_call->result = len_val;
        len_call->str_val = pir_str_dup("len");
        len_call->int_val = 1;

        if (star_idx < 0) {
            /* No star: exact length check */
            PIRValue expected_len = emit_const_int((long)count);
            PIRValue len_eq = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *cmp = emit(PIR_PY_CMP_EQ);
                cmp->result = len_eq;
                cmp->operands[0] = len_val;
                cmp->operands[1] = expected_len;
                cmp->num_operands = 2;
            }

            PIRBlock *check_elts = new_block("seq_check");
            emit_cond_branch(len_eq, check_elts, fail_block);
            switch_to_block(check_elts);

            /* Check each element by index */
            int idx = 0;
            for (e = elts; e; e = e->next, idx++) {
                PIRValue idx_val = emit_const_int((long)idx);
                PIRValue elem = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                PIRInst *sg = emit(PIR_SUBSCR_GET);
                sg->result = elem;
                sg->operands[0] = subject;
                sg->operands[1] = idx_val;
                sg->num_operands = 2;

                PIRBlock *next_elt = e->next
                                      ? new_block("seq_next")
                                      : match_block;

                build_pattern_match(e, elem, next_elt, fail_block);
                if (e->next) {
                    switch_to_block(next_elt);
                }
            }
            if (count == 0) {
                /* The exact-length check is the whole empty-sequence
                 * pattern.  Without an explicit edge this block was left
                 * unterminated and execution fell into a later case. */
                emit_branch(match_block);
            }
        } else {
            /* Star pattern: [a, *rest, b] */
            int before = star_idx;
            int after = count - star_idx - 1;
            int min_len = before + after;

            /* Check len(subject) >= min_len */
            PIRValue min_val = emit_const_int((long)min_len);
            PIRValue len_ok = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *cmp = emit(PIR_PY_CMP_GE);
                cmp->result = len_ok;
                cmp->operands[0] = len_val;
                cmp->operands[1] = min_val;
                cmp->num_operands = 2;
            }

            PIRBlock *star_check = new_block("star_check");
            emit_cond_branch(len_ok, star_check, fail_block);
            switch_to_block(star_check);

            /* Call pydos_unpack_ex(subject, before, after) to get parts */
            {
                PIRInst *pa1 = emit(PIR_PUSH_ARG);
                pa1->operands[0] = subject;
                pa1->num_operands = 1;
            }
            {
                PIRValue before_val = emit_const_int((long)before);
                PIRInst *pa2 = emit(PIR_PUSH_ARG);
                pa2->operands[0] = before_val;
                pa2->num_operands = 1;
            }
            {
                PIRValue after_val = emit_const_int((long)after);
                PIRInst *pa3 = emit(PIR_PUSH_ARG);
                pa3->operands[0] = after_val;
                pa3->num_operands = 1;
            }
            PIRValue parts = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *call = emit(PIR_CALL);
                call->result = parts;
                call->str_val = pir_str_dup("pydos_unpack_ex");
                call->int_val = 3;
            }

            /* parts = [elem0, ..., elem_{before-1}, [star_list], elem_{before+1}, ...] */
            /* Match each element */
            int idx = 0;
            for (e = elts; e; e = e->next, idx++) {
                PIRValue idx_val = emit_const_int((long)idx);
                PIRValue elem = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                PIRInst *sg = emit(PIR_SUBSCR_GET);
                sg->result = elem;
                sg->operands[0] = parts;
                sg->operands[1] = idx_val;
                sg->num_operands = 2;

                if (e->kind == AST_STARRED) {
                    /* Bind starred variable: *rest */
                    ASTNode *star_target = e->data.starred.value;
                    if (star_target && star_target->kind == AST_NAME) {
                        const char *name = star_target->data.name.id;
                        if (strcmp(name, "_") != 0) {
                            if (!var_exists(name)) var_alloca(name);
                            var_store(name, elem);
                        }
                    }

                    PIRBlock *next_elt = e->next
                                          ? new_block("star_next")
                                          : match_block;
                    emit_branch(next_elt);
                    if (e->next) {
                        switch_to_block(next_elt);
                    }
                } else {
                    PIRBlock *next_elt = e->next
                                          ? new_block("star_next")
                                          : match_block;
                    build_pattern_match(e, elem, next_elt, fail_block);
                    if (e->next) {
                        switch_to_block(next_elt);
                    }
                }
            }
        }
        break;
    }

    case AST_DICT_EXPR: {
        /* Mapping pattern {"key": value, ...} */
        ASTNode *key_node = pattern->data.dict.keys;
        ASTNode *val_node = pattern->data.dict.values;

        {
            PIRValue mapping_ok = pir_func_alloc_value(
                current_func, PIR_TYPE_PYOBJ);
            PIRInst *push = emit(PIR_PUSH_ARG);
            PIRInst *call;
            PIRBlock *mapping_body = new_block("map_type_ok");
            push->operands[0] = subject;
            push->num_operands = 1;
            call = emit(PIR_CALL);
            call->result = mapping_ok;
            call->str_val = pir_str_dup("pydos_match_mapping");
            call->int_val = 1;
            emit_cond_branch(mapping_ok, mapping_body, fail_block);
            switch_to_block(mapping_body);
        }

        /* Walk key-value pairs; for each: check key in subject, extract, match */
        while (key_node && val_node) {
            /* Check if key exists in subject: key in subject */
            PIRValue key = build_expr(key_node);
            PIRValue in_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *in_op = emit(PIR_PY_IN);
                in_op->result = in_result;
                in_op->operands[0] = key;
                in_op->operands[1] = subject;
                in_op->num_operands = 2;
            }

            PIRBlock *key_found = new_block("map_found");
            emit_cond_branch(in_result, key_found, fail_block);
            switch_to_block(key_found);

            /* Extract value: subject[key] */
            PIRValue extracted = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *sg = emit(PIR_SUBSCR_GET);
                sg->result = extracted;
                sg->operands[0] = subject;
                sg->operands[1] = key;
                sg->num_operands = 2;
            }

            /* Recursively match value pattern */
            int is_last = (key_node->next == 0);
            PIRBlock *next_pair = is_last ? match_block : new_block("map_next");

            build_pattern_match(val_node, extracted, next_pair, fail_block);

            if (!is_last) {
                switch_to_block(next_pair);
            }

            key_node = key_node->next;
            val_node = val_node->next;
        }

        /* If dict pattern was empty {}, always matches */
        if (!pattern->data.dict.keys) {
            emit_branch(match_block);
        }
        break;
    }

    case AST_CALL: {
        /* Class pattern: ClassName(x=a, y=b) — vtable-based isinstance check + attr matching */
        ASTNode *func_node = pattern->data.call.func;

        /* Resolve the source-level class to one or more implementation
         * vtables.  Generic classes are currently emitted as internal
         * specializations sharing display_name; a Python class pattern must
         * accept every specialization of that one source-level class. */
        const char *cls_name = 0;
        if (func_node->kind == AST_NAME) {
            cls_name = func_node->data.name.id;
        }

        int vt_indices[128];
        int vt_count = 0;
        if (cls_name) {
            int vi;
            for (vi = 0; vi < mod->num_vtables; vi++) {
                if (pir_str_eq(mod->vtables[vi].class_name, cls_name)) {
                    vt_indices[vt_count++] = vi;
                    break;
                }
            }
            if (vt_count == 0) {
                for (vi = 0; vi < mod->num_vtables && vt_count < 128; vi++) {
                    if (mod->vtables[vi].display_name &&
                        pir_str_eq(mod->vtables[vi].display_name, cls_name))
                        vt_indices[vt_count++] = vi;
                }
            }
        }

        PIRBlock *cls_body = new_block("cls_match");
        if (vt_count == 0) {
            int builtin_tag = cls_name && stdlib_reg_
                              ? stdlib_reg_->find_runtime_type_tag(cls_name)
                              : -1;
            PIRValue is_inst = pir_func_alloc_value(
                current_func, PIR_TYPE_PYOBJ);
            if (builtin_tag >= 0) {
                PIRValue tag = emit_const_int((long)builtin_tag);
                PIRInst *push = emit(PIR_PUSH_ARG);
                PIRInst *call;
                push->operands[0] = subject;
                push->num_operands = 1;
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = tag;
                push->num_operands = 1;
                call = emit(PIR_CALL);
                call->result = is_inst;
                call->str_val = pir_str_dup("isinstance");
                call->int_val = 2;
            } else {
                PIRInst *chk = emit(PIR_CHECK_VTABLE);
                chk->result = is_inst;
                chk->operands[0] = subject;
                chk->num_operands = 1;
                chk->str_val = cls_name ? pir_str_dup(cls_name) : 0;
                chk->int_val = -1;
            }
            emit_cond_branch(is_inst, cls_body, fail_block);
        } else {
            int vi;
            for (vi = 0; vi < vt_count; vi++) {
                PIRValue is_inst = pir_func_alloc_value(current_func,
                                                         PIR_TYPE_PYOBJ);
                PIRInst *chk = emit(PIR_CHECK_VTABLE);
                PIRBlock *next_check = vi + 1 < vt_count
                                       ? new_block("cls_type_next")
                                       : fail_block;
                chk->result = is_inst;
                chk->operands[0] = subject;
                chk->num_operands = 1;
                chk->str_val = cls_name ? pir_str_dup(cls_name) : 0;
                chk->int_val = vt_indices[vi];
                emit_cond_branch(is_inst, cls_body, next_check);
                if (vi + 1 < vt_count)
                    switch_to_block(next_check);
            }
        }
        switch_to_block(cls_body);

        /* Match positional arguments through __match_args__, and keyword
         * arguments through direct attribute lookup. */
        ASTNode *arg = pattern->data.call.args;
        int positional_index = 0;
        while (arg) {
            PIRValue attr_val;
            ASTNode *val_pattern;
            if (arg->kind == AST_KEYWORD_ARG) {
                const char *attr_name = arg->data.keyword_arg.key;
                val_pattern = arg->data.keyword_arg.kw_value;

                /* Extract subject.attr_name */
                attr_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRInst *ga = emit(PIR_GET_ATTR);
                    ga->result = attr_val;
                    ga->operands[0] = subject;
                    ga->num_operands = 1;
                    ga->str_val = pir_str_dup(attr_name);
                }
            } else {
                PIRValue index = emit_const_int(positional_index++);
                PIRInst *push = emit(PIR_PUSH_ARG);
                PIRInst *call;
                push->operands[0] = subject;
                push->num_operands = 1;
                push = emit(PIR_PUSH_ARG);
                push->operands[0] = index;
                push->num_operands = 1;
                attr_val = pir_func_alloc_value(current_func,
                                                 PIR_TYPE_PYOBJ);
                call = emit(PIR_CALL);
                call->result = attr_val;
                call->str_val = pir_str_dup("pydos_match_class_arg");
                call->int_val = 2;
                val_pattern = arg;
            }

            {
                int is_last_arg = (arg->next == 0);
                PIRBlock *next_arg = is_last_arg
                                     ? match_block : new_block("cls_next");
                build_pattern_match(val_pattern, attr_val, next_arg,
                                    fail_block);
                if (!is_last_arg) switch_to_block(next_arg);
            }
            arg = arg->next;
        }

        /* No args = just isinstance check */
        if (!pattern->data.call.args) {
            emit_branch(match_block);
        }
        break;
    }

    case AST_ATTR: {
        /* Value pattern like Color.RED — evaluate and compare */
        PIRValue val = build_expr(pattern);
        PIRValue eq = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *cmp = emit(PIR_PY_CMP_EQ);
            cmp->result = eq;
            cmp->operands[0] = subject;
            cmp->operands[1] = val;
            cmp->num_operands = 2;
        }
        emit_cond_branch(eq, match_block, fail_block);
        break;
    }

    default:
        /* Fallback: evaluate pattern and compare equality */
        {
            PIRValue val = build_expr(pattern);
            PIRValue eq = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            {
                PIRInst *cmp = emit(PIR_PY_CMP_EQ);
                cmp->result = eq;
                cmp->operands[0] = subject;
                cmp->operands[1] = val;
                cmp->num_operands = 2;
            }
            emit_cond_branch(eq, match_block, fail_block);
        }
        break;
    }
}

/* --------------------------------------------------------------- */
/* Error reporting                                                   */
/* --------------------------------------------------------------- */
void PIRBuilder::report_error(ASTNode *node, const char *msg)
{
    int line = node ? node->line : 0;
    fprintf(stderr, "pir_build: line %d: %s\n", line, msg);
    error_count++;
}
