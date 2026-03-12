/*
 * pirbld.h - PIR Builder (AST -> PIR conversion)
 *
 * Converts AST (post-Sema, post-Mono) into SSA-based PIR.
 * Mirrors IRGenerator logic but produces basic blocks with
 * typed values instead of flat instruction lists with temps.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIRBLD_H
#define PIRBLD_H

#include "pir.h"
#include "ast.h"
#include "sema.h"
#include "types.h"
#include "stdscan.h"

class PIRBuilder {
public:
    PIRBuilder();
    ~PIRBuilder();

    void       init(SemanticAnalyzer *sema);
    PIRModule *build(ASTNode *module_node);

    /* Set stdlib registry for exception code lookup */
    void set_stdlib(StdlibRegistry *reg);

    int get_error_count() const;

private:
    SemanticAnalyzer *sema;
    PIRModule        *mod;
    PIRFunction      *current_func;
    PIRBlock         *current_block;
    int               error_count;
    StdlibRegistry   *stdlib_reg_;

    /* Variable tracking: name -> alloca PIRValue
       Each function has its own var_map, saved/restored on nesting */
    PdHashMap<const char *, PIRValue> *var_map;

    /* Cell tracking: name -> cell PIRValue (for captured/nonlocal vars)
       If a name is in cell_map, var_load/store use cell_get/set instead */
    PdHashMap<const char *, PIRValue> *cell_map;

    /* Closure tracking: func name -> closure list PIRValue
       When calling a function in this map, emit SET_CLOSURE first */
    PdHashMap<const char *, PIRValue> *closure_map;

    /* Loop control: break/continue target blocks */
    PIRBlock *break_targets[32];
    PIRBlock *continue_targets[32];
    int       loop_depth;

    /* Class context */
    const char *current_class_name;
    const char *current_base_class_name;

    /* Generator / coroutine context */
    int      is_building_coroutine; /* 1 if building async def (vs generator) */
    PIRValue gen_val;           /* PIRValue holding generator object in resume func */
    int      gen_num_locals;
    int      gen_state_count;
    PIRBlock *gen_state_blocks[32];  /* Blocks for resume points */
    const char *gen_local_names[64]; /* Stable name→index mapping for gen save/restore */
    int      gen_local_count;        /* Number of tracked gen locals */
    int      gen_for_iter_count;     /* Unique ID for generator for-loop iterators */

    /* Synthetic variable counter (unique across nested with/match) */
    int      synth_counter_;

    /* Arg accumulation for calls */
    PIRValue arg_vals[64];
    int      arg_top;

    /* Function definitions for default param lookup */
    struct FuncDefEntry {
        const char *name;
        ASTNode    *node;
    };
    FuncDefEntry func_defs[256];
    int          num_func_defs;

    /* Constant pool management */
    int add_const_str(const char *data, int len);

    /* Block management */
    PIRBlock *new_block(const char *label);
    void      switch_to_block(PIRBlock *block);
    int       block_is_terminated() const;

    /* Emit helpers */
    PIRInst  *emit(PIROp op);
    PIRValue  emit_const_int(long val);
    PIRValue  emit_const_float(double val);
    PIRValue  emit_const_bool(int val);
    PIRValue  emit_const_str(const char *s, int len);
    PIRValue  emit_const_none();
    void      emit_branch(PIRBlock *target);
    void      emit_cond_branch(PIRValue cond, PIRBlock *true_blk, PIRBlock *false_blk);
    void      emit_return(PIRValue val);
    void      emit_return_none();

    /* Variable access (alloca/load/store) */
    PIRValue  var_alloca(const char *name);
    PIRValue  var_load(const char *name);
    void      var_store(const char *name, PIRValue val);
    int       var_exists(const char *name);

    /* Function management */
    PIRFunction *begin_func(const char *name);
    void         end_func();

    /* Statement generation */
    void build_stmt(ASTNode *node);
    void build_stmts(ASTNode *first);
    void build_funcdef(ASTNode *node);
    void build_classdef(ASTNode *node);
    void build_if(ASTNode *node);
    void build_while(ASTNode *node);
    void build_for(ASTNode *node);
    void build_assign(ASTNode *node);
    void build_ann_assign(ASTNode *node);
    void build_aug_assign(ASTNode *node);
    void build_return(ASTNode *node);
    void build_expr_stmt(ASTNode *node);
    void build_try(ASTNode *node);
    void build_raise(ASTNode *node);
    void build_break(ASTNode *node);
    void build_continue(ASTNode *node);
    void build_pass(ASTNode *node);
    void build_assert(ASTNode *node);
    void build_delete(ASTNode *node);
    void build_with(ASTNode *node);
    void build_match(ASTNode *node);
    void build_pattern_match(ASTNode *pattern, PIRValue subject,
                             PIRBlock *match_block, PIRBlock *fail_block);

    /* Expression generation (returns SSA value) */
    PIRValue build_expr(ASTNode *node);
    PIRValue build_binop(ASTNode *node);
    PIRValue build_unaryop(ASTNode *node);
    PIRValue build_compare(ASTNode *node);
    PIRValue build_boolop(ASTNode *node);
    PIRValue build_call(ASTNode *node);
    PIRValue build_attr(ASTNode *node);
    PIRValue build_subscript(ASTNode *node);
    PIRValue build_name(ASTNode *node);
    PIRValue build_int_lit(ASTNode *node);
    PIRValue build_float_lit(ASTNode *node);
    PIRValue build_complex_lit(ASTNode *node);
    PIRValue build_str_lit(ASTNode *node);
    PIRValue build_bool_lit(ASTNode *node);
    PIRValue build_none_lit(ASTNode *node);
    PIRValue build_list_expr(ASTNode *node);
    PIRValue build_dict_expr(ASTNode *node);
    PIRValue build_tuple_expr(ASTNode *node);
    PIRValue build_set_expr(ASTNode *node);
    PIRValue build_fstring(ASTNode *node);
    PIRValue build_listcomp(ASTNode *node);
    void     build_listcomp_loop(ASTNode *comp_node, ASTNode *gen, PIRValue result_val);
    PIRValue build_dictcomp(ASTNode *node);
    void     build_dictcomp_loop(ASTNode *comp_node, ASTNode *gen, PIRValue result_val);
    PIRValue build_setcomp(ASTNode *node);
    void     build_setcomp_loop(ASTNode *comp_node, ASTNode *gen, PIRValue result_val);
    PIRValue build_genexpr(ASTNode *node);
    PIRValue build_walrus(ASTNode *node);
    PIRValue build_lambda(ASTNode *node);
    PIRValue build_ifexpr(ASTNode *node);
    PIRValue build_yield(ASTNode *node);
    PIRValue build_yield_from(ASTNode *node);

    /* Emit a yield point: save state, return val, resume block,
     * restore state, check_throw, get_sent. Returns sent value. */
    PIRValue emit_yield_point(PIRValue val);

    /* Store to target (name, attr, subscript, tuple unpack) */
    void build_store(ASTNode *target, PIRValue val);

    /* Error reporting */
    void report_error(ASTNode *node, const char *msg);
};

#endif /* PIRBLD_H */
