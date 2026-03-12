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

static int pir_str_eq(const char *a, const char *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
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
      error_count(0), stdlib_reg_(0), var_map(0), cell_map(0), closure_map(0), loop_depth(0),
      current_class_name(0), current_base_class_name(0),
      is_building_coroutine(0),
      gen_num_locals(0), gen_state_count(0), gen_local_count(0),
      gen_for_iter_count(0),
      synth_counter_(0),
      arg_top(0), num_func_defs(0)
{
    gen_val = pir_value_none();
    memset(break_targets, 0, sizeof(break_targets));
    memset(continue_targets, 0, sizeof(continue_targets));
    memset(gen_state_blocks, 0, sizeof(gen_state_blocks));
    memset(gen_local_names, 0, sizeof(gen_local_names));
    memset(arg_vals, 0, sizeof(arg_vals));
    memset(func_defs, 0, sizeof(func_defs));
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
    return inst;
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
        if (current_func == mod->init_func) {
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
    return func;
}

void PIRBuilder::end_func()
{
    /* Auto-insert return None if block not terminated */
    if (current_block && !block_is_terminated()) {
        emit_return_none();
    }
    current_func = 0;
    current_block = 0;
}

/* --------------------------------------------------------------- */
/* Top-level build                                                   */
/* --------------------------------------------------------------- */
PIRModule *PIRBuilder::build(ASTNode *module_node)
{
    ASTNode *stmt;

    mod = pir_module_new();

    /* Create init function (module-level code) */
    PIRFunction *init = begin_func("__init__");
    mod->init_func = init;

    /* Generate all module-level statements */
    if (module_node && module_node->kind == AST_MODULE) {
        stmt = module_node->data.module.body;
    } else {
        stmt = module_node;
    }

    build_stmts(stmt);

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
    case AST_IMPORT:
    case AST_IMPORT_FROM: break;  /* resolved at sema/link time */
    case AST_GLOBAL:
    case AST_NONLOCAL:
    case AST_TYPE_ALIAS:  break;  /* handled by sema, no codegen needed */
    default:
        /* Try as expression statement */
        build_expr(node);
        break;
    }
}

/* --------------------------------------------------------------- */
/* Statement builders                                                */
/* --------------------------------------------------------------- */

/* --- Function definition --- */
void PIRBuilder::build_funcdef(ASTNode *node)
{
    const char *fname = node->data.func_def.name;
    Param *param;
    int param_count = 0;
    int is_gen = 0;
    int i;

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
    var_map = 0; /* begin_func will create new */
    cell_map = 0;
    closure_map = 0;
    const char *outer_class = current_class_name;
    const char *outer_base = current_base_class_name;
    PIRValue outer_gen_val = gen_val;
    int outer_gen_locals = gen_num_locals;
    int outer_gen_states = gen_state_count;
    int outer_gen_local_count = gen_local_count;
    int outer_gen_for_iter_count = gen_for_iter_count;
    int outer_loop_depth = loop_depth;
    int outer_is_building_coroutine = is_building_coroutine;
    PIRBlock *outer_gen_state_blocks[32];
    const char *outer_gen_local_names[64];
    memcpy(outer_gen_state_blocks, gen_state_blocks, sizeof(gen_state_blocks));
    memcpy(outer_gen_local_names, gen_local_names, sizeof(gen_local_names));

    loop_depth = 0;
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
        gen_num_locals = param_count;

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
    current_func = outer_func;
    current_block = outer_block;
    current_class_name = outer_class;
    current_base_class_name = outer_base;
    gen_val = outer_gen_val;
    gen_num_locals = outer_gen_locals;
    gen_state_count = outer_gen_states;
    gen_local_count = outer_gen_local_count;
    gen_for_iter_count = outer_gen_for_iter_count;
    loop_depth = outer_loop_depth;
    is_building_coroutine = outer_is_building_coroutine;
    memcpy(gen_state_blocks, outer_gen_state_blocks, sizeof(gen_state_blocks));
    memcpy(gen_local_names, outer_gen_local_names, sizeof(gen_local_names));

    /* In module init or enclosing function, create function object */
    if (current_func && !current_class_name) {
        int name_ci = add_const_str(fname, (int)strlen(fname));
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
            closure_map->put(pir_str_dup(fname), closure_list);
        }

        PIRInst *st = emit(PIR_STORE_GLOBAL);
        st->operands[0] = fobj;
        st->num_operands = 1;
        st->str_val = pir_str_dup(fname);
    }
}

/* --- Class definition --- */
void PIRBuilder::build_classdef(ASTNode *node)
{
    const char *class_name = node->data.class_def.name;
    ASTNode *stmt;
    const char *base_name = 0;

    /* Parse base class */
    if (node->data.class_def.bases) {
        ASTNode *base = node->data.class_def.bases;
        if (base->kind == AST_NAME) {
            base_name = base->data.name.id;
        }
    }

    /* Emit comment */
    {
        PIRInst *c = emit(PIR_COMMENT);
        c->str_val = pir_str_dup(class_name);
    }

    /* Register vtable info in the PIR module (lowerer copies to IRModule) */
    int vt_idx = -1;
    if (mod->num_vtables < 32) {
        vt_idx = mod->num_vtables;
        PIRVTableInfo *vti = &mod->vtables[vt_idx];
        vti->class_name = pir_str_dup(class_name);
        vti->base_class_name = base_name ? pir_str_dup(base_name) : 0;
        vti->num_extra_bases = 0;

        /* Collect extra bases (multiple inheritance) */
        if (node->data.class_def.bases) {
            ASTNode *eb = node->data.class_def.bases->next;
            while (eb && vti->num_extra_bases < 7) {
                if (eb->kind == AST_NAME && eb->data.name.id) {
                    vti->extra_bases[vti->num_extra_bases++] =
                        pir_str_dup(eb->data.name.id);
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
                sprintf(mangled, "%s__%s", class_name, mname);
                vti->methods[vti->num_methods].python_name = pir_str_dup(mname);
                vti->methods[vti->num_methods].mangled_name = pir_str_dup(mangled);
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
            /* Mangle name: ClassName__methodname */
            const char *mname = stmt->data.func_def.name;
            char mangled[256];
            sprintf(mangled, "%s__%s", class_name, mname);
            const char *orig = stmt->data.func_def.name;
            stmt->data.func_def.name = pir_str_dup(mangled);
            build_funcdef(stmt);
            stmt->data.func_def.name = orig;
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

/* --- For statement --- */
void PIRBuilder::build_for(ASTNode *node)
{
    ASTNode *iter_node = node->data.for_stmt.iter;
    ASTNode *target_node = node->data.for_stmt.target;

    /* Optimized range() loop: for simple_name in range(...) */
    if (is_range_call(iter_node) && target_node->kind == AST_NAME) {
        int nargs = iter_node->data.call.num_args;
        ASTNode *arg1 = iter_node->data.call.args;
        ASTNode *arg2 = arg1 ? arg1->next : 0;
        ASTNode *arg3 = arg2 ? arg2->next : 0;

        PIRValue range_start, range_stop, range_step;

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
        if (!var_exists(var_name)) {
            var_alloca(var_name);
        }
        var_store(var_name, range_start);

        /* Push loop targets: break->end, continue->incr */
        break_targets[loop_depth] = end_block;
        continue_targets[loop_depth] = incr_block;
        loop_depth++;

        emit_branch(check_block);

        /* Check: counter < stop */
        switch_to_block(check_block);
        {
            PIRValue counter = var_load(var_name);
            PIRValue cmp_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *cmp = emit(PIR_PY_CMP_LT);
            cmp->result = cmp_result;
            cmp->operands[0] = counter;
            cmp->operands[1] = range_stop;
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
            PIRValue next_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *add = emit(PIR_PY_ADD);
            add->result = next_val;
            add->operands[0] = cur;
            add->operands[1] = range_step;
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
    if (pir_value_valid(gen_val)) {
        /* Generator/coroutine resume function: return signals exhaustion.
         * Must return NULL (not None object) so the runtime treats it
         * as StopIteration. */
        if (is_building_coroutine && node->data.ret.value) {
            /* Coroutine return <value>: store result in gen->state
             * before exhaustion so async_run can retrieve it. */
            PIRValue val = build_expr(node->data.ret.value);
            PIRInst *sr = emit(PIR_COR_SET_RESULT);
            sr->operands[0] = gen_val;
            sr->operands[1] = val;
            sr->num_operands = 2;
        }
        emit_return(pir_value_none()); /* pir_value_none() → IR_RETURN -1 → NULL */
        return;
    }

    if (node->data.ret.value) {
        PIRValue val = build_expr(node->data.ret.value);
        emit_return(val);
    } else {
        emit_return_none();
    }
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

    /* Setup try (outer finally guard if present) */
    if (finally_guard) {
        PIRInst *st = emit(PIR_SETUP_TRY);
        st->handler_block = finally_guard;
        pir_block_add_edge(current_block, finally_guard);
    }

    /* Setup try (inner except handlers) */
    {
        PIRInst *st = emit(PIR_SETUP_TRY);
        st->handler_block = handler_block;
        pir_block_add_edge(current_block, handler_block);
    }

    /* Try body */
    build_stmts(node->data.try_stmt.body);

    /* Pop inner try */
    emit(PIR_POP_TRY);

    /* Normal exit: jump to finally or end */
    if (!block_is_terminated()) {
        emit_branch(finally_block ? finally_block : end_block);
    }

    /* Exception handlers */
    switch_to_block(handler_block);
    emit(PIR_POP_TRY);

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

                    /* Bind exception name if present */
                    if (handler->data.handler.name) {
                        var_store(handler->data.handler.name, exc);
                    }

                    build_stmts(handler->data.handler.body);
                    if (!block_is_terminated()) {
                        emit_branch(finally_block ? finally_block : end_block);
                    }

                    switch_to_block(next_handler);
                } else {
                    /* Bare except */
                    if (handler->data.handler.name) {
                        PIRValue exc = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                        PIRInst *ge = emit(PIR_GET_EXCEPTION);
                        ge->result = exc;
                        var_store(handler->data.handler.name, exc);
                    }
                    build_stmts(handler->data.handler.body);
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

                build_stmts(handler->data.handler.body);
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

    /* Finally block (normal path) */
    if (finally_block) {
        switch_to_block(finally_block);
        if (finally_guard) emit(PIR_POP_TRY);
        build_stmts(node->data.try_stmt.finally_body);
        if (!block_is_terminated()) emit_branch(end_block);

        /* Finally guard (exception path) */
        switch_to_block(finally_guard);
        emit(PIR_POP_TRY);
        build_stmts(node->data.try_stmt.finally_body);
        if (!block_is_terminated()) emit(PIR_RERAISE);
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
    } else {
        emit(PIR_RERAISE);
    }
}

void PIRBuilder::build_break(ASTNode *node)
{
    (void)node;
    if (loop_depth > 0) {
        emit_branch(break_targets[loop_depth - 1]);
    }
}

void PIRBuilder::build_continue(ASTNode *node)
{
    (void)node;
    if (loop_depth > 0) {
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

    /* Chained: a op1 b op2 c ... */
    PIRBlock *end_block = new_block("cmp_end");
    PIRValue result_val = emit_const_bool(1);

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
            result_val = emit_const_bool(0);
            emit_branch(end_block);

            switch_to_block(next_block);
        } else {
            result_val = cmp_result;
        }

        left = right;
        if (c) c = c->next;
    }

    if (!block_is_terminated()) emit_branch(end_block);
    switch_to_block(end_block);

    /* Note: without proper phi nodes here, the result from short-circuit
       path won't merge correctly. For now, we just return the last
       comparison result. Proper phi insertion is a future enhancement
       (or handled by mem2reg). */
    return result_val;
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

PIRValue PIRBuilder::build_call(ASTNode *node)
{
    ASTNode *func_node = node->data.call.func;
    ASTNode *arg_node;
    int argc = 0;
    int i;

    /* Count and generate arguments */
    for (arg_node = node->data.call.args; arg_node; arg_node = arg_node->next) {
        if (arg_node->kind == AST_KEYWORD_ARG) continue;
        argc++;
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
            current_base_class_name) {
            is_super_call = 1;
        }

        if (is_super_call) {
            /* super().method(args) -> direct call to BaseClass__method(self, args) */
            char mangled[256];
            sprintf(mangled, "%s__%s", current_base_class_name, method);

            /* self is always local[0] in a method */
            PIRValue self_val = var_load("self");

            /* Push self as first arg, then all others */
            PIRInst *pa_self = emit(PIR_PUSH_ARG);
            pa_self->operands[0] = self_val;
            pa_self->num_operands = 1;

            for (i = 0; i < total_args; i++) {
                PIRInst *pa = emit(PIR_PUSH_ARG);
                pa->operands[0] = arg_temps[i];
                pa->num_operands = 1;
            }

            PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *call = emit(PIR_CALL);
            call->result = result;
            call->str_val = pir_str_dup(mangled);
            call->int_val = total_args + 1; /* +1 for self */
            return result;
        }

        PIRValue obj = build_expr(func_node->data.attribute.object);

        /* Check for PIR-backed method: emit PIR_CALL with self prepended */
        if (stdlib_reg_ && sema) {
            TypeInfo *obj_type = sema->get_expr_type(func_node->data.attribute.object);
            if (obj_type && obj_type->kind != TY_ERROR && obj_type->kind != TY_ANY) {
                char pir_name[48];
                if (stdlib_reg_->is_pir_method(obj_type->kind, method, pir_name)) {
                    /* Push self as first arg, then the user args */
                    PIRInst *pa_self = emit(PIR_PUSH_ARG);
                    pa_self->operands[0] = obj;
                    pa_self->num_operands = 1;
                    for (i = 0; i < total_args; i++) {
                        PIRInst *pa = emit(PIR_PUSH_ARG);
                        pa->operands[0] = arg_temps[i];
                        pa->num_operands = 1;
                    }
                    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                    PIRInst *call = emit(PIR_CALL);
                    call->result = result;
                    call->str_val = pir_str_dup(pir_name);
                    call->int_val = total_args + 1; /* +1 for self */
                    return result;
                }
            }
        }

        /* Push args */
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
        call->str_val = pir_str_dup(method);
        call->int_val = total_args;
        /* Attach object type for typed method dispatch */
        if (sema) {
            TypeInfo *obj_type = sema->get_expr_type(func_node->data.attribute.object);
            if (obj_type && obj_type->kind != TY_ERROR)
                call->type_hint = obj_type;
        }
        return result;
    }

    /* Check for constructor call */
    if (func_node->kind == AST_NAME && sema) {
        const char *name = func_node->data.name.id;
        Symbol *sym = sema->lookup(name);
        if (sym && sym->kind == SYM_CLASS) {
            /* Constructor: alloc + optionally call __init__ + set_vtable */
            PIRValue obj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *alloc = emit(PIR_ALLOC_OBJ);
            alloc->result = obj;

            /* Mangle __init__ name */
            char init_name[256];
            sprintf(init_name, "%s____init__", name);

            /* Check if __init__ actually exists for this class.
             * If the class has no __init__, skip the call to avoid
             * calling through an uninitialized null global pointer. */
            int has_init = 0;
            if (sym->type && sym->type->class_info &&
                sym->type->class_info->members) {
                Symbol *m;
                for (m = sym->type->class_info->members; m; m = m->next) {
                    if (m->name && strcmp(m->name, "__init__") == 0) {
                        has_init = 1;
                        break;
                    }
                }
            }

            if (has_init) {
                /* Push self + args */
                PIRInst *pa_self = emit(PIR_PUSH_ARG);
                pa_self->operands[0] = obj;
                pa_self->num_operands = 1;

                for (i = 0; i < total_args; i++) {
                    PIRInst *pa = emit(PIR_PUSH_ARG);
                    pa->operands[0] = arg_temps[i];
                    pa->num_operands = 1;
                }

                /* Call __init__ */
                PIRValue init_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                PIRInst *call = emit(PIR_CALL);
                call->result = init_result;
                call->str_val = pir_str_dup(init_name);
                call->int_val = total_args + 1; /* +1 for self */
            }

            /* Set vtable — look up vtable index by class name */
            int vt_idx = -1;
            {
                int vi;
                for (vi = 0; vi < mod->num_vtables; vi++) {
                    if (pir_str_eq(mod->vtables[vi].class_name, name)) {
                        vt_idx = vi;
                        break;
                    }
                }
            }
            PIRInst *sv = emit(PIR_SET_VTABLE);
            sv->operands[0] = obj;
            sv->num_operands = 1;
            sv->str_val = pir_str_dup(name);
            sv->int_val = vt_idx;

            return obj;
        }
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
            if (tag < 0) tag = 10; /* PYDT_INSTANCE for user classes */
            arg_temps[1] = emit_const_int((long)tag);
        }
    }

    /* Special-case issubclass(TypeA, TypeB):
     * Resolve at compile time using class hierarchy from sema. */
    if (func_node->kind == AST_NAME && func_node->data.name.id &&
        pir_str_eq(func_node->data.name.id, "issubclass") && total_args >= 2) {
        ASTNode *cls_arg = node->data.call.args;
        ASTNode *base_arg = cls_arg ? cls_arg->next : 0;
        if (cls_arg && cls_arg->kind == AST_NAME && cls_arg->data.name.id &&
            base_arg && base_arg->kind == AST_NAME && base_arg->data.name.id) {
            const char *cls_name = cls_arg->data.name.id;
            const char *base_name = base_arg->data.name.id;
            int is_sub = 0;

            if (pir_str_eq(cls_name, base_name)) {
                is_sub = 1;
            } else if (pir_str_eq(cls_name, "bool") &&
                       pir_str_eq(base_name, "int")) {
                is_sub = 1;
            } else if (sema) {
                Symbol *cls_sym = sema->lookup(cls_name);
                if (cls_sym && cls_sym->type &&
                    cls_sym->type->kind == TY_CLASS &&
                    cls_sym->type->class_info) {
                    ClassInfo *ci = cls_sym->type->class_info->base;
                    while (ci) {
                        if (ci->name && pir_str_eq(ci->name, base_name)) {
                            is_sub = 1;
                            break;
                        }
                        ci = ci->base;
                    }
                }
            }

            /* Emit constant bool — skip the actual function call */
            PIRValue bval = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *cb = emit(PIR_CONST_BOOL);
            cb->result = bval;
            cb->int_val = is_sub;
            return bval;
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
        int fi;

        for (fi = 0; fi < num_func_defs; fi++) {
            if (pir_str_eq(func_defs[fi].name, callee_name)) {
                callee_def = func_defs[fi].node;
                break;
            }
        }

        if (callee_def) {
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

        if (has_star || has_dstar) {
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
        if (callee_def) {
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
    for (i = 0; i < total_args; i++) {
        PIRInst *pa = emit(PIR_PUSH_ARG);
        pa->operands[0] = arg_temps[i];
        pa->num_operands = 1;
    }

    {
        const char *fname = 0;
        if (func_node->kind == AST_NAME) {
            fname = func_node->data.name.id;
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

        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *call = emit(PIR_CALL);
        call->result = result;
        call->str_val = fname ? pir_str_dup(fname) : 0;
        call->int_val = total_args;
        /* Attach return type hint if available */
        if (sema) {
            TypeInfo *rt = sema->get_expr_type(node);
            if (rt) call->type_hint = rt;
        }

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
        if (obj_type && obj_type->kind != TY_ERROR)
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

        /* Generate start, stop, step with integer defaults
         * (matches legacy IR pattern: 0, 0x7FFFFFFF, 1) */
        PIRValue low = sl->data.slice.lower
                       ? build_expr(sl->data.slice.lower)
                       : emit_const_int(0);
        PIRValue high = sl->data.slice.upper
                        ? build_expr(sl->data.slice.upper)
                        : emit_const_int(0x7FFFFFFFL);
        PIRValue step = sl->data.slice.step
                        ? build_expr(sl->data.slice.step)
                        : emit_const_int(1);

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

        PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *inst = emit(PIR_SLICE);
        inst->result = result;
        inst->operands[0] = obj;
        inst->num_operands = 1;
        /* Attach object type for typed dispatch (str slice vs list slice) */
        if (sema) {
            TypeInfo *obj_type = sema->get_expr_type(node->data.subscript.object);
            if (obj_type && obj_type->kind != TY_ERROR)
                inst->type_hint = obj_type;
        }
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
        if (obj_type && obj_type->kind != TY_ERROR)
            inst->type_hint = obj_type;
    }
    return result;
}

PIRValue PIRBuilder::build_list_expr(ASTNode *node)
{
    ASTNode *elem;
    int count = 0;
    PIRValue elems[64];
    int i;

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

PIRValue PIRBuilder::build_listcomp(ASTNode *node)
{
    /* Create empty list */
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_LIST);
    inst->result = result;
    inst->int_val = 0;

    /* Process generators */
    build_listcomp_loop(node, node->data.listcomp.generators, result);

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
    PIRBlock *body_block = new_block("comp_body");
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
    var_map = 0;
    int outer_loop = loop_depth;
    loop_depth = 0;

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

    /* Body is a single expression */
    PIRValue body_val = build_expr(node->data.lambda.body);
    emit_return(body_val);

    end_func();

    /* Restore context */
    if (var_map) delete var_map;
    var_map = outer_var_map;
    current_func = outer_func;
    current_block = outer_block;
    loop_depth = outer_loop;

    /* Create function object */
    PIRValue fobj = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *mk = emit(PIR_MAKE_FUNCTION);
    mk->result = fobj;
    mk->str_val = pir_str_dup(name);
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
        }
    }

    /* Check for pending throw (raises exception via longjmp if set) */
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
/* yield from — Basic delegation (PEP 380 simplified)                */
/*                                                                   */
/* Desugars `yield from EXPR` into:                                  */
/*   _iter = get_iter(EXPR)                                          */
/*   loop:                                                           */
/*     _item = for_iter(_iter)  -> on StopIteration, jump to end     */
/*     yield _item                                                   */
/*     goto loop                                                     */
/*   end:                                                            */
/*     result = None                                                 */
/*                                                                   */
/* This is the basic version: send/throw are NOT forwarded to the    */
/* sub-iterator. Full PEP 380 forwarding is deferred.                */
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

    /* End block: sub-iterator exhausted; result is None */
    switch_to_block(end_block);
    return emit_const_none();
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
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_DICT);
    inst->result = result;
    inst->int_val = 0;

    build_dictcomp_loop(node, node->data.dictcomp.generators, result);
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
    PIRBlock *body_block = new_block("dcomp_body");
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
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_SET);
    inst->result = result;
    inst->int_val = 0;

    /* Uses listcomp struct (elt + generators) */
    build_setcomp_loop(node, node->data.listcomp.generators, result);
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
    /* Eagerly materialize as a list (same as list comprehension).
     * PyDOS has no lazy iteration infrastructure, so this is
     * semantically equivalent for bounded iterables. */
    PIRValue result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
    PIRInst *inst = emit(PIR_BUILD_LIST);
    inst->result = result;
    inst->int_val = 0;

    /* AST_GENEXPR reuses listcomp struct */
    build_listcomp_loop(node, node->data.listcomp.generators, result);
    return result;
}

/* --------------------------------------------------------------- */
/* With statement                                                    */
/* --------------------------------------------------------------- */
void PIRBuilder::build_with(ASTNode *node)
{
    ASTNode *item;
    int mgr_count = 0;
    int base_id = synth_counter_;
    char mgr_names[8][32];  /* max 8 context managers */

    /* Enter each context manager */
    for (item = node->data.with_stmt.items; item; item = item->next) {
        if (mgr_count >= 8) {
            report_error(node, "too many context managers (max 8)");
            break;
        }

        PIRValue mgr = build_expr(item->data.with_item.context_expr);

        /* Store manager in a synthetic variable (unique across nesting) */
        sprintf(mgr_names[mgr_count], "__with_mgr_%d__", base_id + mgr_count);
        synth_counter_++;
        var_alloca(mgr_names[mgr_count]);
        var_store(mgr_names[mgr_count], mgr);

        /* Call __enter__() — no args beyond self (passed via operands[0]) */
        PIRValue enter_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *enter_call = emit(PIR_CALL_METHOD);
        enter_call->result = enter_result;
        enter_call->operands[0] = mgr;
        enter_call->num_operands = 1;
        enter_call->str_val = pir_str_dup("__enter__");
        enter_call->int_val = 0;

        /* Bind to optional_vars if present */
        if (item->data.with_item.optional_vars) {
            build_store(item->data.with_item.optional_vars, enter_result);
        }

        mgr_count++;
    }

    /* Build body with try/except wrapper for __exit__ protocol */
    PIRBlock *try_block = new_block("with_try");
    PIRBlock *handler_block = new_block("with_exc");
    PIRBlock *exit_block = new_block("with_exit");
    PIRBlock *end_block = new_block("with_end");

    /* Setup try */
    {
        PIRInst *st = emit(PIR_SETUP_TRY);
        st->handler_block = handler_block;
        pir_block_add_edge(current_block, handler_block);
    }

    emit_branch(try_block);
    switch_to_block(try_block);

    /* Build the with body */
    build_stmts(node->data.with_stmt.body);

    if (!block_is_terminated()) {
        PIRInst *pt = emit(PIR_POP_TRY);
        (void)pt;
        emit_branch(exit_block);
    }

    /* Normal exit: call __exit__(None, None, None) for each manager */
    switch_to_block(exit_block);
    {
        int i;
        PIRValue none_val = emit_const_none();
        for (i = mgr_count - 1; i >= 0; i--) {
            PIRValue mgr_val = var_load(mgr_names[i]);
            PIRInst *pa1 = emit(PIR_PUSH_ARG);
            pa1->operands[0] = none_val;
            pa1->num_operands = 1;
            PIRInst *pa2 = emit(PIR_PUSH_ARG);
            pa2->operands[0] = none_val;
            pa2->num_operands = 1;
            PIRInst *pa3 = emit(PIR_PUSH_ARG);
            pa3->operands[0] = none_val;
            pa3->num_operands = 1;
            PIRValue exit_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *exit_call = emit(PIR_CALL_METHOD);
            exit_call->result = exit_result;
            exit_call->operands[0] = mgr_val;
            exit_call->num_operands = 1;
            exit_call->str_val = pir_str_dup("__exit__");
            exit_call->int_val = 3;
        }
    }
    emit_branch(end_block);

    /* Exception handler: call __exit__ and re-raise */
    switch_to_block(handler_block);
    {
        PIRInst *pt2 = emit(PIR_POP_TRY);
        (void)pt2;

        PIRValue exc = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        PIRInst *ge = emit(PIR_GET_EXCEPTION);
        ge->result = exc;

        int i;
        PIRBlock *reraise_block = new_block("with_reraise");
        PIRBlock *check_block = end_block;

        for (i = mgr_count - 1; i >= 0; i--) {
            PIRValue mgr_val = var_load(mgr_names[i]);
            PIRInst *pa1 = emit(PIR_PUSH_ARG);
            pa1->operands[0] = exc;
            pa1->num_operands = 1;
            PIRInst *pa2 = emit(PIR_PUSH_ARG);
            pa2->operands[0] = exc;
            pa2->num_operands = 1;
            PIRValue none_v = emit_const_none();
            PIRInst *pa3 = emit(PIR_PUSH_ARG);
            pa3->operands[0] = none_v;
            pa3->num_operands = 1;

            PIRValue exit_result = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
            PIRInst *exit_call = emit(PIR_CALL_METHOD);
            exit_call->result = exit_result;
            exit_call->operands[0] = mgr_val;
            exit_call->num_operands = 1;
            exit_call->str_val = pir_str_dup("__exit__");
            exit_call->int_val = 3;

            /* If __exit__ returns truthy, suppress exception */
            if (i == 0) {
                emit_cond_branch(exit_result, end_block, reraise_block);
            }
        }

        switch_to_block(reraise_block);
        PIRInst *rr = emit(PIR_RERAISE);
        (void)rr;
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

        /* Walk key-value pairs; for each: check key in subject, extract, match */
        PIRBlock *cur_block = 0;
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

        /* Look up vtable index for the class name */
        const char *cls_name = 0;
        if (func_node->kind == AST_NAME) {
            cls_name = func_node->data.name.id;
        }

        int vt_idx = -1;
        if (cls_name) {
            int vi;
            for (vi = 0; vi < mod->num_vtables; vi++) {
                if (pir_str_eq(mod->vtables[vi].class_name, cls_name)) {
                    vt_idx = vi;
                    break;
                }
            }
        }

        /* Emit PIR_CHECK_VTABLE: checks subject->vtable matches class vtable */
        PIRValue is_inst = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
        {
            PIRInst *chk = emit(PIR_CHECK_VTABLE);
            chk->result = is_inst;
            chk->operands[0] = subject;
            chk->num_operands = 1;
            chk->str_val = cls_name ? pir_str_dup(cls_name) : 0;
            chk->int_val = vt_idx;
        }

        PIRBlock *cls_body = new_block("cls_match");
        emit_cond_branch(is_inst, cls_body, fail_block);
        switch_to_block(cls_body);

        /* Match keyword arguments as attribute access */
        ASTNode *arg = pattern->data.call.args;
        while (arg) {
            if (arg->kind == AST_KEYWORD_ARG) {
                const char *attr_name = arg->data.keyword_arg.key;
                ASTNode *val_pattern = arg->data.keyword_arg.kw_value;

                /* Extract subject.attr_name */
                PIRValue attr_val = pir_func_alloc_value(current_func, PIR_TYPE_PYOBJ);
                {
                    PIRInst *ga = emit(PIR_GET_ATTR);
                    ga->result = attr_val;
                    ga->operands[0] = subject;
                    ga->num_operands = 1;
                    ga->str_val = pir_str_dup(attr_name);
                }

                int is_last_arg = (arg->next == 0);
                PIRBlock *next_arg = is_last_arg ? match_block : new_block("cls_next");

                build_pattern_match(val_pattern, attr_val, next_arg, fail_block);

                if (!is_last_arg) {
                    switch_to_block(next_arg);
                }
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
