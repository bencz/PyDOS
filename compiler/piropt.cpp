/*
 * piropt.cpp - PIR optimization passes
 *
 * SSA-based optimization passes that run on PIR before lowering:
 *   1. Dead Block Elimination (DBE)
 *   2. Sparse Conditional Constant Propagation (SCCP)
 *   3. mem2reg (alloca promotion to SSA)
 *   4. Dead Instruction Elimination (DIE)
 *   5. Global Value Numbering (GVN)
 *   6. Loop-Invariant Code Motion (LICM)
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "piropt.h"
#include "pirutil.h"
#include "pirtyp.h"
#include "piresc.h"
#include "pirspc.h"
#include "pirhyg.h"
#include "types.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Selective pass-disabling flags (set from CLI) */
int piropt_skip_sccp = 0;
int piropt_skip_gvn = 0;
int piropt_skip_licm = 0;
int piropt_skip_specialize = 0;
int piropt_skip_scope = 0;
int piropt_skip_mem2reg = 0;
int piropt_skip_die = 0;
int piropt_skip_devirt = 0;
int piropt_skip_dbe = 0;
int piropt_skip_func_dedup = 0;
int piropt_skip_hygiene = 0;

/* ================================================================ */
/* PIROptimizer                                                      */
/* ================================================================ */

PIROptimizer::PIROptimizer()
{
    current_module = 0;
    stdlib_reg_ = 0;
    deduplicated_function_count_ = 0;
    hygiene_bundle_count_ = 0;
    hygiene_instruction_count_ = 0;
    hygiene_exception_fast_path_count_ = 0;
    hygiene_feature_mask_ = 0;
}

PIROptimizer::~PIROptimizer()
{
}

void PIROptimizer::set_stdlib(StdlibRegistry *reg)
{
    stdlib_reg_ = reg;
}

void PIROptimizer::optimize(PIRModule *mod)
{
    int i;
    PIRHygieneReport hygiene;
    current_module = mod;
    deduplicated_function_count_ = 0;
    hygiene_bundle_count_ = 0;
    hygiene_instruction_count_ = 0;
    hygiene_exception_fast_path_count_ = 0;
    hygiene_feature_mask_ = 0;
    for (i = 0; i < mod->functions.size(); i++) {
        if (mod->init_func && mod->functions[i] == mod->init_func) continue;
        optimize_function(mod->functions[i]);
    }
    if (mod->init_func) {
        optimize_function(mod->init_func);
    }

    /* Pass 13: remove compiler-generated scaffolding only after the entire
     * module has established which reflection surfaces are observable. */
    if (!piropt_skip_hygiene) {
        pir_hygiene_run(mod, &hygiene);
        hygiene_bundle_count_ = hygiene.metadata_bundles_removed;
        hygiene_instruction_count_ = hygiene.instructions_removed;
        hygiene_exception_fast_path_count_ = hygiene.exception_fast_paths;
        hygiene_feature_mask_ = hygiene.observed_features;
        if (!piropt_skip_die && hygiene.instructions_removed != 0) {
            for (i = 0; i < mod->functions.size(); i++) {
                dead_inst_eliminate(mod->functions[i]);
            }
        }
    }

    /* Pass 14: optimization and hygiene invalidate the earlier escape facts.
     * Recompute them from the exact PIR that scope insertion will consume. */
    for (i = 0; i < mod->functions.size(); i++) {
        escape_analyze(mod->functions[i]);
    }

    /* Pass 15: arena ownership is the last semantic transformation before
     * whole-module body sharing and lowering. */
    if (!piropt_skip_scope) {
        for (i = 0; i < mod->functions.size(); i++) {
            scope_insert(mod->functions[i]);
        }
    }
    if (!piropt_skip_func_dedup) {
        deduplicated_function_count_ = deduplicate_generic_functions(mod);
    }
}

int PIROptimizer::get_deduplicated_function_count() const
{
    return deduplicated_function_count_;
}

unsigned long PIROptimizer::get_hygiene_bundle_count() const
{
    return hygiene_bundle_count_;
}

unsigned long PIROptimizer::get_hygiene_instruction_count() const
{
    return hygiene_instruction_count_;
}

unsigned long PIROptimizer::get_hygiene_exception_fast_path_count() const
{
    return hygiene_exception_fast_path_count_;
}

unsigned PIROptimizer::get_hygiene_feature_mask() const
{
    return hygiene_feature_mask_;
}

/* ================================================================ */
/* Identical Generic Function Folding                               */
/* ================================================================ */

static int nullable_str_equal(const char *a, const char *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    return strcmp(a, b) == 0;
}

static int pir_value_equal(PIRValue a, PIRValue b)
{
    return a.id == b.id && a.type == b.type;
}

static int block_id_or_minus_one(PIRBlock *block)
{
    return block ? block->id : -1;
}

/* At this point devirtualization and specialization have already consumed
 * semantic type details.  The lowerers distinguish hints by TypeKind; generic
 * arguments and class names do not change the emitted ABI or primitive path. */
static int codegen_hint_equal(TypeInfo *a, TypeInfo *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    return a->kind == b->kind;
}

static int pir_inst_structurally_equal(PIRInst *a, PIRInst *b)
{
    int i;
    if (!a || !b) return a == b;
    if (a->op != b->op ||
        !pir_value_equal(a->result, b->result) ||
        a->num_operands != b->num_operands ||
        a->int_val != b->int_val ||
        !nullable_str_equal(a->str_val, b->str_val) ||
        block_id_or_minus_one(a->target_block) !=
            block_id_or_minus_one(b->target_block) ||
        block_id_or_minus_one(a->false_block) !=
            block_id_or_minus_one(b->false_block) ||
        block_id_or_minus_one(a->handler_block) !=
            block_id_or_minus_one(b->handler_block) ||
        !codegen_hint_equal(a->type_hint, b->type_hint)) {
        return 0;
    }

    for (i = 0; i < a->num_operands; i++) {
        if (!pir_value_equal(a->operands[i], b->operands[i])) return 0;
    }

    if (a->op == PIR_PHI) {
        if (a->extra.phi.count != b->extra.phi.count) return 0;
        for (i = 0; i < a->extra.phi.count; i++) {
            PIRPhiEntry *ae = &a->extra.phi.entries[i];
            PIRPhiEntry *be = &b->extra.phi.entries[i];
            if (!pir_value_equal(ae->value, be->value) ||
                block_id_or_minus_one(ae->block) !=
                    block_id_or_minus_one(be->block)) {
                return 0;
            }
        }
    }
    return 1;
}

static int pir_func_structurally_equal(PIRFunction *a, PIRFunction *b)
{
    int bi, pi;
    if (!a || !b) return 0;
    if (!a->generic_origin || !b->generic_origin ||
        !a->generic_method_name || !b->generic_method_name ||
        strcmp(a->generic_origin, b->generic_origin) != 0 ||
        strcmp(a->generic_method_name, b->generic_method_name) != 0) {
        return 0;
    }
    if (a->num_params != b->num_params ||
        a->num_locals != b->num_locals ||
        a->next_value_id != b->next_value_id ||
        a->is_generator != b->is_generator ||
        a->is_coroutine != b->is_coroutine ||
        a->params.size() != b->params.size() ||
        a->blocks.size() != b->blocks.size()) {
        return 0;
    }

    for (pi = 0; pi < a->params.size(); pi++) {
        if (!pir_value_equal(a->params[pi], b->params[pi])) return 0;
    }

    for (bi = 0; bi < a->blocks.size(); bi++) {
        PIRBlock *ab = a->blocks[bi];
        PIRBlock *bb = b->blocks[bi];
        PIRInst *ai;
        PIRInst *bii;
        if (ab->id != bb->id || ab->inst_count != bb->inst_count) return 0;
        ai = ab->first;
        bii = bb->first;
        while (ai && bii) {
            if (!pir_inst_structurally_equal(ai, bii)) return 0;
            ai = ai->next;
            bii = bii->next;
        }
        if (ai || bii) return 0;
    }
    return 1;
}

struct GenericFuncAlias {
    const char *old_name;
    const char *shared_name;
};

static const char *resolve_generic_alias(GenericFuncAlias *aliases,
                                         int alias_count,
                                         const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].old_name, name) == 0) {
            return aliases[i].shared_name;
        }
    }
    return name;
}

static const char *detach_generic_alias(GenericFuncAlias *aliases,
                                        int alias_count,
                                        const char *name)
{
    const char *resolved = resolve_generic_alias(aliases, alias_count, name);
    char *detached;
    size_t len;
    if (resolved == name) return name;

    /* Alias targets are PIRFunction::name strings.  Duplicate them before
     * the redundant function objects are destroyed so instructions and
     * vtable entries never retain pointers into a freed PIRFunction. */
    len = strlen(resolved);
    detached = (char *)malloc(len + 1);
    if (!detached) return name;
    memcpy(detached, resolved, len + 1);
    return detached;
}

static void rewrite_generic_aliases(PIRModule *mod,
                                    GenericFuncAlias *aliases,
                                    int alias_count)
{
    int fi, bi, vi, mi;
    for (fi = 0; fi < mod->functions.size(); fi++) {
        PIRFunction *func = mod->functions[fi];
        for (bi = 0; bi < func->blocks.size(); bi++) {
            PIRInst *inst;
            for (inst = func->blocks[bi]->first; inst; inst = inst->next) {
                if (inst->op == PIR_CALL ||
                    inst->op == PIR_MAKE_FUNCTION ||
                    inst->op == PIR_MAKE_GENERATOR ||
                    inst->op == PIR_MAKE_COROUTINE) {
                    inst->str_val = detach_generic_alias(
                        aliases, alias_count, inst->str_val);
                }
            }
        }
    }

    for (vi = 0; vi < mod->num_vtables; vi++) {
        for (mi = 0; mi < mod->vtables[vi].num_methods; mi++) {
            mod->vtables[vi].methods[mi].mangled_name =
                detach_generic_alias(
                    aliases, alias_count,
                    mod->vtables[vi].methods[mi].mangled_name);
        }
    }

    mod->entry_func = detach_generic_alias(
        aliases, alias_count, mod->entry_func);
}

int PIROptimizer::deduplicate_generic_functions(PIRModule *mod)
{
    int total_removed = 0;
    int changed;

    if (!mod) return 0;
    do {
        int count = mod->functions.size();
        int *removed;
        GenericFuncAlias *aliases;
        int alias_count = 0;
        int i, j;

        changed = 0;
        if (count < 2) break;
        removed = (int *)calloc(count, sizeof(int));
        aliases = (GenericFuncAlias *)malloc(
            sizeof(GenericFuncAlias) * count);
        if (!removed || !aliases) {
            if (removed) free(removed);
            if (aliases) free(aliases);
            break;
        }

        for (i = 0; i < count; i++) {
            PIRFunction *candidate = mod->functions[i];
            if (candidate == mod->init_func || !candidate->generic_origin)
                continue;
            for (j = 0; j < i; j++) {
                PIRFunction *shared = mod->functions[j];
                if (removed[j] || shared == mod->init_func) continue;
                if (pir_func_structurally_equal(candidate, shared)) {
                    aliases[alias_count].old_name = candidate->name;
                    aliases[alias_count].shared_name = shared->name;
                    alias_count++;
                    removed[i] = 1;
                    changed = 1;
                    break;
                }
            }
        }

        if (changed) {
            rewrite_generic_aliases(mod, aliases, alias_count);
            for (i = count - 1; i >= 0; i--) {
                if (removed[i]) {
                    PIRFunction *duplicate = mod->functions[i];
                    mod->functions.remove_at(i);
                    pir_func_free(duplicate);
                    total_removed++;
                }
            }
        }
        free(aliases);
        free(removed);
    } while (changed);

    return total_removed;
}

void PIROptimizer::optimize_function(PIRFunction *func)
{
    if (!func || func->blocks.size() == 0) return;

    /* Pass 1: Dead block elimination */
    if (!piropt_skip_dbe) {
        dead_block_eliminate(func);
    }

    /* Pass 2: SCCP */
    if (!piropt_skip_sccp) {
        dom.compute(func);
        sccp(func);
    }

    /* Pass 3: Dead block elimination (post-SCCP cleanup) */
    if (!piropt_skip_dbe) {
        dead_block_eliminate(func);
    }

    /* Pass 4: mem2reg */
    if (!piropt_skip_mem2reg) {
        dom.compute(func);
        df.compute(func, &dom);
        mem2reg(func);
    }

    /* Pass 5: Type inference (analysis only) */
    dom.compute(func);
    type_infer(func);

    /* Pass 6: Escape analysis (analysis only) */
    escape_analyze(func);

    /* Pass 7: guarded devirtualization. */
    if (!piropt_skip_devirt) {
        devirtualize(func);
    }

    /* Pass 8: Type-guided specialization */
    if (!piropt_skip_specialize) {
        specialize(func);
    }

    /* Pass 9: Dead instruction elimination */
    if (!piropt_skip_die) {
        dead_inst_eliminate(func);
    }

    /* Pass 10: GVN */
    if (!piropt_skip_gvn) {
        dom.compute(func);
        gvn(func);
    }

    /* Pass 11: LICM */
    if (!piropt_skip_licm) {
        dom.compute(func);
        loops.compute(func, &dom);
        licm(func);
    }

    /* Pass 12: Final dead instruction elimination */
    if (!piropt_skip_die) {
        dead_inst_eliminate(func);
    }

}

/* ================================================================ */
/* Dead Block Elimination                                            */
/* ================================================================ */

int PIROptimizer::dead_block_eliminate(PIRFunction *func)
{
    int i, j;
    int changed = 0;
    int reachable[PIRDOM_MAX_BLOCKS];

    memset(reachable, 0, sizeof(reachable));

    if (!func->entry_block) return 0;

    /* BFS from entry block */
    int worklist[PIRDOM_MAX_BLOCKS];
    int wl_size = 0;

    reachable[func->entry_block->id] = 1;
    worklist[wl_size++] = func->entry_block->id;

    while (wl_size > 0) {
        int b = worklist[--wl_size];
        PIRBlock *blk = pir_func_get_block(func, b);
        if (!blk) continue;

        for (j = 0; j < blk->succs.size(); j++) {
            int succ_id = blk->succs[j]->id;
            if (!reachable[succ_id]) {
                reachable[succ_id] = 1;
                worklist[wl_size++] = succ_id;
            }
        }
    }

    /* Remove unreachable blocks */
    for (i = func->blocks.size() - 1; i >= 0; i--) {
        PIRBlock *blk = func->blocks[i];
        if (!reachable[blk->id]) {
            /* Remove this block from predecessor lists of its successors */
            for (j = 0; j < blk->succs.size(); j++) {
                PIRBlock *succ = blk->succs[j];
                int k;
                for (k = 0; k < succ->preds.size(); k++) {
                    if (succ->preds[k] == blk) {
                        /* Remove by swapping with last */
                        succ->preds[k] = succ->preds[succ->preds.size() - 1];
                        succ->preds.pop_back();
                        k--;
                    }
                }
            }

            /* Free instructions */
            PIRInst *inst = blk->first;
            while (inst) {
                PIRInst *next = inst->next;
                if (inst->op == PIR_PHI && inst->extra.phi.entries) {
                    free(inst->extra.phi.entries);
                }
                free(inst);
                inst = next;
            }

            /* Remove block from function */
            func->blocks[i] = func->blocks[func->blocks.size() - 1];
            func->blocks.pop_back();
            changed = 1;
        }
    }

    return changed;
}

/* ================================================================ */
/* Dead Instruction Elimination                                      */
/* ================================================================ */

int PIROptimizer::dead_inst_eliminate(PIRFunction *func)
{
    int changed = 0;
    int i;

    /* Mark all instructions that are live.
     * An instruction is live if:
     *   - It has side effects (calls, stores, branches, etc.)
     *   - Its result is used by a live instruction
     * Walk backwards to propagate liveness through operand chains. */

    /* Count total instructions to size arrays */
    int total_insts = 0;
    for (i = 0; i < func->blocks.size(); i++) {
        PIRInst *inst;
        for (inst = func->blocks[i]->first; inst; inst = inst->next) {
            total_insts++;
        }
    }

    if (total_insts == 0) return 0;

    /* Build a mapping: value_id -> defining instruction.
     * Use a simple array indexed by value id. */
    int max_val = func->next_value_id;
    PIRInst **def_inst = (PIRInst **)malloc(sizeof(PIRInst *) * (max_val + 1));
    int *live = (int *)malloc(sizeof(int) * (max_val + 1));
    if (!def_inst || !live) {
        free(def_inst);
        free(live);
        return 0;
    }
    memset(def_inst, 0, sizeof(PIRInst *) * (max_val + 1));
    memset(live, 0, sizeof(int) * (max_val + 1));

    /* Pass 1: Map definitions and mark side-effecting as live */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRInst *inst;
        for (inst = func->blocks[i]->first; inst; inst = inst->next) {
            if (pir_value_valid(inst->result) &&
                inst->result.id >= 0 && inst->result.id < max_val) {
                def_inst[inst->result.id] = inst;
            }
            if (pir_has_side_effects(inst->op) || pir_is_terminator(inst->op)) {
                /* Mark this instruction and all its operands as live */
                if (pir_value_valid(inst->result) &&
                    inst->result.id >= 0 && inst->result.id < max_val) {
                    live[inst->result.id] = 1;
                }
                /* Mark operands */
                int j;
                for (j = 0; j < inst->num_operands; j++) {
                    if (pir_value_valid(inst->operands[j]) &&
                        inst->operands[j].id >= 0 &&
                        inst->operands[j].id < max_val) {
                        live[inst->operands[j].id] = 1;
                    }
                }
                if (inst->op == PIR_PHI) {
                    for (j = 0; j < inst->extra.phi.count; j++) {
                        PIRValue v = inst->extra.phi.entries[j].value;
                        if (pir_value_valid(v) && v.id >= 0 && v.id < max_val) {
                            live[v.id] = 1;
                        }
                    }
                }
            }
        }
    }

    /* Pass 2: Propagate liveness backwards.
     * If a value is live and has a defining instruction,
     * mark all operands of that instruction as live too. */
    int worklist_changed = 1;
    while (worklist_changed) {
        worklist_changed = 0;
        int v;
        for (v = 0; v < max_val; v++) {
            if (!live[v]) continue;
            PIRInst *dinst = def_inst[v];
            if (!dinst) continue;

            int j;
            for (j = 0; j < dinst->num_operands; j++) {
                if (pir_value_valid(dinst->operands[j]) &&
                    dinst->operands[j].id >= 0 &&
                    dinst->operands[j].id < max_val &&
                    !live[dinst->operands[j].id]) {
                    live[dinst->operands[j].id] = 1;
                    worklist_changed = 1;
                }
            }
            if (dinst->op == PIR_PHI) {
                for (j = 0; j < dinst->extra.phi.count; j++) {
                    PIRValue pv = dinst->extra.phi.entries[j].value;
                    if (pir_value_valid(pv) && pv.id >= 0 &&
                        pv.id < max_val && !live[pv.id]) {
                        live[pv.id] = 1;
                        worklist_changed = 1;
                    }
                }
            }
        }
    }

    /* Pass 3: Remove non-live, non-side-effecting instructions */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *block = func->blocks[i];
        PIRInst *inst = block->first;
        while (inst) {
            PIRInst *next = inst->next;
            if (!pir_has_side_effects(inst->op) &&
                !pir_is_terminator(inst->op) &&
                pir_value_valid(inst->result) &&
                inst->result.id >= 0 && inst->result.id < max_val &&
                !live[inst->result.id]) {
                pir_inst_remove(block, inst);
                changed = 1;
            }
            inst = next;
        }
    }

    free(def_inst);
    free(live);
    return changed;
}

/* ================================================================ */
/* SCCP — Sparse Conditional Constant Propagation                    */
/* ================================================================ */

/* Lattice values */
enum SCCPLattice {
    SCCP_UNDEF,      /* Not yet determined */
    SCCP_CONST,      /* Known constant */
    SCCP_VARYING     /* Multiple possible values */
};

struct SCCPValue {
    SCCPLattice state;
    long const_val;   /* Only valid when state == SCCP_CONST */
};

static SCCPValue sccp_undef() {
    SCCPValue v;
    v.state = SCCP_UNDEF;
    v.const_val = 0;
    return v;
}

static SCCPValue sccp_const(long val) {
    SCCPValue v;
    v.state = SCCP_CONST;
    v.const_val = val;
    return v;
}

static SCCPValue sccp_varying() {
    SCCPValue v;
    v.state = SCCP_VARYING;
    v.const_val = 0;
    return v;
}

static SCCPValue sccp_meet(SCCPValue a, SCCPValue b) {
    if (a.state == SCCP_UNDEF) return b;
    if (b.state == SCCP_UNDEF) return a;
    if (a.state == SCCP_VARYING || b.state == SCCP_VARYING) return sccp_varying();
    /* Both CONST */
    if (a.const_val == b.const_val) return a;
    return sccp_varying();
}

int PIROptimizer::sccp(PIRFunction *func)
{
    int changed = 0;
    int max_val = func->next_value_id;
    int i;

    if (max_val <= 0) return 0;

    SCCPValue *lattice = (SCCPValue *)malloc(sizeof(SCCPValue) * max_val);
    int *block_executable = (int *)malloc(sizeof(int) * PIRDOM_MAX_BLOCKS);
    if (!lattice || !block_executable) {
        free(lattice);
        free(block_executable);
        return 0;
    }

    /* Initialize all values to UNDEF */
    for (i = 0; i < max_val; i++) {
        lattice[i] = sccp_undef();
    }
    memset(block_executable, 0, sizeof(int) * PIRDOM_MAX_BLOCKS);

    /* Parameters are VARYING (unknown input) */
    for (i = 0; i < func->params.size(); i++) {
        int pid = func->params[i].id;
        if (pid >= 0 && pid < max_val) {
            lattice[pid] = sccp_varying();
        }
    }

    /* Entry block is executable */
    if (func->entry_block) {
        block_executable[func->entry_block->id] = 1;
    }

    /* Worklists: CFG edges and SSA values */
    int cfg_worklist[PIRDOM_MAX_BLOCKS];
    int cfg_wl_size = 0;
    int ssa_worklist[4096];
    int ssa_wl_size = 0;

    /* Seed with entry block */
    if (func->entry_block) {
        cfg_worklist[cfg_wl_size++] = func->entry_block->id;
    }

    /* Iterate until both worklists are empty */
    while (cfg_wl_size > 0 || ssa_wl_size > 0) {
        /* Process CFG worklist */
        while (cfg_wl_size > 0) {
            int blk_id = cfg_worklist[--cfg_wl_size];
            PIRBlock *blk = pir_func_get_block(func, blk_id);
            if (!blk) continue;

            PIRInst *inst;
            for (inst = blk->first; inst; inst = inst->next) {
                if (!pir_value_valid(inst->result)) continue;
                int rid = inst->result.id;
                if (rid < 0 || rid >= max_val) continue;

                SCCPValue old_val = lattice[rid];
                SCCPValue new_val = sccp_varying(); /* default */

                switch (inst->op) {
                case PIR_CONST_INT:
                    new_val = sccp_const(inst->int_val);
                    break;

                case PIR_CONST_BOOL:
                    new_val = sccp_const(inst->int_val);
                    break;

                case PIR_CONST_NONE:
                case PIR_CONST_STR:
                case PIR_CONST_FLOAT:
                    new_val = sccp_varying(); /* non-integer constants */
                    break;

                case PIR_PY_ADD:
                case PIR_PY_SUB:
                case PIR_PY_MUL:
                case PIR_PY_FLOORDIV:
                case PIR_PY_MOD: {
                    /* Try to fold if both operands are int constants */
                    if (inst->num_operands >= 2) {
                        int op0 = inst->operands[0].id;
                        int op1 = inst->operands[1].id;
                        if (op0 >= 0 && op0 < max_val &&
                            op1 >= 0 && op1 < max_val &&
                            lattice[op0].state == SCCP_CONST &&
                            lattice[op1].state == SCCP_CONST) {
                            long a = lattice[op0].const_val;
                            long b = lattice[op1].const_val;
                            switch (inst->op) {
                            case PIR_PY_ADD: new_val = sccp_const(a + b); break;
                            case PIR_PY_SUB: new_val = sccp_const(a - b); break;
                            case PIR_PY_MUL: new_val = sccp_const(a * b); break;
                            case PIR_PY_FLOORDIV:
                                if (b != 0) new_val = sccp_const(a / b);
                                break;
                            case PIR_PY_MOD:
                                if (b != 0) new_val = sccp_const(a % b);
                                break;
                            default: break;
                            }
                        } else if (op0 >= 0 && op0 < max_val &&
                                   op1 >= 0 && op1 < max_val) {
                            /* If any operand is UNDEF, result is UNDEF;
                             * if any is VARYING, result is VARYING */
                            if (lattice[op0].state == SCCP_UNDEF ||
                                lattice[op1].state == SCCP_UNDEF) {
                                new_val = sccp_undef();
                            }
                        }
                    }
                    break;
                }

                case PIR_PY_NEG:
                    if (inst->num_operands >= 1) {
                        int op0 = inst->operands[0].id;
                        if (op0 >= 0 && op0 < max_val &&
                            lattice[op0].state == SCCP_CONST) {
                            new_val = sccp_const(-lattice[op0].const_val);
                        } else if (op0 >= 0 && op0 < max_val &&
                                   lattice[op0].state == SCCP_UNDEF) {
                            new_val = sccp_undef();
                        }
                    }
                    break;

                case PIR_PY_CMP_EQ:
                case PIR_PY_CMP_NE:
                case PIR_PY_CMP_LT:
                case PIR_PY_CMP_LE:
                case PIR_PY_CMP_GT:
                case PIR_PY_CMP_GE: {
                    if (inst->num_operands >= 2) {
                        int op0 = inst->operands[0].id;
                        int op1 = inst->operands[1].id;
                        if (op0 >= 0 && op0 < max_val &&
                            op1 >= 0 && op1 < max_val &&
                            lattice[op0].state == SCCP_CONST &&
                            lattice[op1].state == SCCP_CONST) {
                            long a = lattice[op0].const_val;
                            long b = lattice[op1].const_val;
                            int res = 0;
                            switch (inst->op) {
                            case PIR_PY_CMP_EQ: res = (a == b); break;
                            case PIR_PY_CMP_NE: res = (a != b); break;
                            case PIR_PY_CMP_LT: res = (a < b); break;
                            case PIR_PY_CMP_LE: res = (a <= b); break;
                            case PIR_PY_CMP_GT: res = (a > b); break;
                            case PIR_PY_CMP_GE: res = (a >= b); break;
                            default: break;
                            }
                            new_val = sccp_const(res);
                        } else if (op0 >= 0 && op0 < max_val &&
                                   op1 >= 0 && op1 < max_val &&
                                   (lattice[op0].state == SCCP_UNDEF ||
                                    lattice[op1].state == SCCP_UNDEF)) {
                            new_val = sccp_undef();
                        }
                    }
                    break;
                }

                case PIR_PHI: {
                    /* Meet of all incoming values from executable blocks */
                    new_val = sccp_undef();
                    int j;
                    for (j = 0; j < inst->extra.phi.count; j++) {
                        PIRBlock *pred = inst->extra.phi.entries[j].block;
                        if (!pred || !block_executable[pred->id]) continue;
                        int vid = inst->extra.phi.entries[j].value.id;
                        if (vid >= 0 && vid < max_val) {
                            new_val = sccp_meet(new_val, lattice[vid]);
                        } else {
                            new_val = sccp_varying();
                        }
                    }
                    break;
                }

                default:
                    /* Conservative: anything else is VARYING */
                    new_val = sccp_varying();
                    break;
                }

                /* Update lattice */
                SCCPValue merged = sccp_meet(old_val, new_val);
                if (merged.state != old_val.state ||
                    (merged.state == SCCP_CONST && merged.const_val != old_val.const_val)) {
                    lattice[rid] = merged;
                    /* Add all users to SSA worklist */
                    if (ssa_wl_size < 4096) {
                        ssa_worklist[ssa_wl_size++] = rid;
                    }
                }
            }

            /* Process terminator for CFG edges */
            PIRInst *term = blk->last;
            if (term && term->op == PIR_COND_BRANCH) {
                int cond_id = term->operands[0].id;
                if (cond_id >= 0 && cond_id < max_val &&
                    lattice[cond_id].state == SCCP_CONST) {
                    /* Known direction */
                    PIRBlock *target = lattice[cond_id].const_val ?
                        term->target_block : term->false_block;
                    if (target && !block_executable[target->id]) {
                        block_executable[target->id] = 1;
                        cfg_worklist[cfg_wl_size++] = target->id;
                    }
                } else {
                    /* Both edges executable */
                    if (term->target_block && !block_executable[term->target_block->id]) {
                        block_executable[term->target_block->id] = 1;
                        cfg_worklist[cfg_wl_size++] = term->target_block->id;
                    }
                    if (term->false_block && !block_executable[term->false_block->id]) {
                        block_executable[term->false_block->id] = 1;
                        cfg_worklist[cfg_wl_size++] = term->false_block->id;
                    }
                }
            } else if (term && term->op == PIR_BRANCH) {
                if (term->target_block && !block_executable[term->target_block->id]) {
                    block_executable[term->target_block->id] = 1;
                    cfg_worklist[cfg_wl_size++] = term->target_block->id;
                }
            }
        }

        /* Process SSA worklist: re-evaluate users of changed values */
        while (ssa_wl_size > 0) {
            int val_id = ssa_worklist[--ssa_wl_size];
            /* Find all users and re-add their blocks to cfg worklist */
            int bi;
            for (bi = 0; bi < func->blocks.size(); bi++) {
                PIRBlock *blk = func->blocks[bi];
                if (!block_executable[blk->id]) continue;
                PIRInst *inst;
                for (inst = blk->first; inst; inst = inst->next) {
                    int uses_val = 0;
                    int j;
                    for (j = 0; j < inst->num_operands; j++) {
                        if (inst->operands[j].id == val_id) {
                            uses_val = 1;
                            break;
                        }
                    }
                    if (inst->op == PIR_PHI && !uses_val) {
                        for (j = 0; j < inst->extra.phi.count; j++) {
                            if (inst->extra.phi.entries[j].value.id == val_id) {
                                uses_val = 1;
                                break;
                            }
                        }
                    }
                    if (uses_val && pir_value_valid(inst->result)) {
                        int rid = inst->result.id;
                        if (rid >= 0 && rid < max_val) {
                            /* Re-evaluate this instruction */
                            if (cfg_wl_size < PIRDOM_MAX_BLOCKS) {
                                /* Re-add the block (will re-evaluate all insts) */
                                cfg_worklist[cfg_wl_size++] = blk->id;
                            }
                        }
                    }
                }
            }
        }
    }

    /* Apply transformations */
    /* 1. Replace CONST values with PIR_CONST_INT */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (!pir_value_valid(inst->result)) continue;
            int rid = inst->result.id;
            if (rid < 0 || rid >= max_val) continue;
            if (lattice[rid].state != SCCP_CONST) continue;

            /* Don't replace if already a constant instruction */
            if (inst->op == PIR_CONST_INT || inst->op == PIR_CONST_BOOL ||
                inst->op == PIR_CONST_STR) continue;

            /* Determine correct constant type */
            {
                int is_bool_result = 0;
                switch (inst->op) {
                case PIR_PY_CMP_EQ: case PIR_PY_CMP_NE:
                case PIR_PY_CMP_LT: case PIR_PY_CMP_LE:
                case PIR_PY_CMP_GT: case PIR_PY_CMP_GE:
                case PIR_PY_NOT:
                case PIR_PY_IN: case PIR_PY_NOT_IN:
                    is_bool_result = 1;
                    break;
                default:
                    break;
                }
                inst->op = is_bool_result ? PIR_CONST_BOOL : PIR_CONST_INT;
            }
            inst->int_val = lattice[rid].const_val;
            inst->num_operands = 0;
            inst->str_val = 0;
            inst->target_block = 0;
            inst->false_block = 0;
            inst->handler_block = 0;
            changed = 1;
        }
    }

    /* 2. Fold known branches */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        PIRInst *term = blk->last;
        if (!term || term->op != PIR_COND_BRANCH) continue;

        int cond_id = term->operands[0].id;
        if (cond_id < 0 || cond_id >= max_val) continue;
        if (lattice[cond_id].state != SCCP_CONST) continue;

        /* Convert to unconditional branch */
        PIRBlock *taken = lattice[cond_id].const_val ?
            term->target_block : term->false_block;
        PIRBlock *not_taken = lattice[cond_id].const_val ?
            term->false_block : term->target_block;

        term->op = PIR_BRANCH;
        term->target_block = taken;
        term->false_block = 0;
        term->num_operands = 0;

        /* Remove edge to not-taken block */
        if (not_taken) {
            int j;
            for (j = 0; j < blk->succs.size(); j++) {
                if (blk->succs[j] == not_taken) {
                    blk->succs[j] = blk->succs[blk->succs.size() - 1];
                    blk->succs.pop_back();
                    break;
                }
            }
            for (j = 0; j < not_taken->preds.size(); j++) {
                if (not_taken->preds[j] == blk) {
                    not_taken->preds[j] = not_taken->preds[not_taken->preds.size() - 1];
                    not_taken->preds.pop_back();
                    break;
                }
            }
        }

        changed = 1;
    }

    free(lattice);
    free(block_executable);
    return changed;
}

/* ================================================================ */
/* mem2reg — Alloca Promotion to SSA                                 */
/* ================================================================ */

struct AllocaInfo {
    int value_id;           /* PIR value id of the alloca */
    const char *name;       /* Variable name */
    int promotable;         /* 1 if all uses are load/store */
    int def_blocks[PIRDOM_MAX_BLOCKS]; /* blocks that store to this alloca */
    int num_def_blocks;
};

#define MAX_ALLOCAS 256

int PIROptimizer::mem2reg(PIRFunction *func)
{
    int changed = 0;
    int i, j;
    AllocaInfo allocas[MAX_ALLOCAS];
    int num_allocas = 0;

    /* Step 1: Identify promotable allocas */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (inst->op != PIR_ALLOCA) continue;
            if (num_allocas >= MAX_ALLOCAS) break;

            AllocaInfo *ai = &allocas[num_allocas];
            ai->value_id = inst->result.id;
            ai->name = inst->str_val;
            ai->promotable = 1;
            ai->num_def_blocks = 0;
            num_allocas++;
        }
    }

    if (num_allocas == 0) return 0;

    /* Mark parameter allocas as non-promotable.
     * The flat IR codegen copies parameters to local slots 0..num_params-1.
     * If we promote parameter allocas, those slots disappear, and the
     * codegen's param copy overwrites phi slots. So keep param allocas. */
    if (func->blocks.size() > 0) {
        PIRBlock *entry = func->blocks[0];
        PIRInst *inst;
        for (inst = entry->first; inst; inst = inst->next) {
            if (inst->op == PIR_STORE && inst->num_operands >= 2) {
                int alloca_id = inst->operands[0].id;
                int val_id = inst->operands[1].id;
                /* Check if the stored value is a function parameter */
                int pi;
                for (pi = 0; pi < func->params.size(); pi++) {
                    if (func->params[pi].id == val_id) {
                        /* This store puts a param into an alloca.
                         * Mark the alloca as non-promotable. */
                        for (j = 0; j < num_allocas; j++) {
                            if (allocas[j].value_id == alloca_id) {
                                allocas[j].promotable = 0;
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }
    }

    /* Check if each alloca is promotable: only used by PIR_LOAD and PIR_STORE */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (inst->op == PIR_LOAD && inst->num_operands >= 1) {
                /* Load from alloca — this is fine */
            } else if (inst->op == PIR_STORE && inst->num_operands >= 2) {
                /* Store to alloca — record the defining block */
                int alloca_id = inst->operands[0].id;
                for (j = 0; j < num_allocas; j++) {
                    if (allocas[j].value_id == alloca_id) {
                        /* Add block to def_blocks if not already there */
                        int k, found = 0;
                        for (k = 0; k < allocas[j].num_def_blocks; k++) {
                            if (allocas[j].def_blocks[k] == blk->id) {
                                found = 1;
                                break;
                            }
                        }
                        if (!found && allocas[j].num_def_blocks < PIRDOM_MAX_BLOCKS) {
                            allocas[j].def_blocks[allocas[j].num_def_blocks++] = blk->id;
                        }
                        break;
                    }
                }
            } else {
                /* Check if any alloca is used as an operand in a non-load/store instruction */
                int k;
                for (k = 0; k < inst->num_operands; k++) {
                    int op_id = inst->operands[k].id;
                    for (j = 0; j < num_allocas; j++) {
                        if (allocas[j].value_id == op_id &&
                            inst->op != PIR_ALLOCA) {
                            allocas[j].promotable = 0;
                        }
                    }
                }
            }
        }
    }

    /* Also check PIR_CALL str_val references: if a call references a name
     * that matches an alloca, the alloca must not be promoted — the lowerer
     * uses the alloca's local slot to resolve the callee by name. */
    for (i = 0; i < func->blocks.size(); i++) {
        PIRBlock *blk = func->blocks[i];
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (inst->op == PIR_CALL && inst->str_val) {
                for (j = 0; j < num_allocas; j++) {
                    if (allocas[j].promotable && allocas[j].name &&
                        strcmp(allocas[j].name, inst->str_val) == 0) {
                        allocas[j].promotable = 0;
                    }
                }
            }
        }
    }

    /* Step 2: For each promotable alloca, insert phi nodes at IDF */
    for (i = 0; i < num_allocas; i++) {
        if (!allocas[i].promotable) continue;
        if (allocas[i].num_def_blocks == 0) continue;

        /* Compute iterated dominance frontier of def blocks */
        int phi_blocks[PIRDOM_MAX_BLOCKS];
        int num_phi_blocks = 0;
        int wl[PIRDOM_MAX_BLOCKS];
        int wl_size = 0;

        /* Seed worklist with def blocks */
        for (j = 0; j < allocas[i].num_def_blocks; j++) {
            wl[wl_size++] = allocas[i].def_blocks[j];
        }

        /* Iterate: add DF(def_block) to phi_blocks, and iterate */
        while (wl_size > 0) {
            int b = wl[--wl_size];
            int k;
            for (k = 0; k < df.df[b].size(); k++) {
                int df_block = df.df[b][k];
                /* Check if we already have a phi here */
                int already = 0;
                int m;
                for (m = 0; m < num_phi_blocks; m++) {
                    if (phi_blocks[m] == df_block) {
                        already = 1;
                        break;
                    }
                }
                if (!already && num_phi_blocks < PIRDOM_MAX_BLOCKS) {
                    phi_blocks[num_phi_blocks++] = df_block;
                    wl[wl_size++] = df_block;
                }
            }
        }

        /* Insert phi nodes */
        for (j = 0; j < num_phi_blocks; j++) {
            PIRBlock *blk = pir_func_get_block(func, phi_blocks[j]);
            if (!blk) continue;

            PIRInst *phi = pir_inst_new(PIR_PHI);
            phi->result = pir_func_alloc_value(func, PIR_TYPE_PYOBJ);
            phi->int_val = allocas[i].value_id; /* tag with alloca id for renaming */

            /* Allocate phi entries for each predecessor */
            int num_preds = blk->preds.size();
            phi->extra.phi.count = num_preds;
            phi->extra.phi.entries = (PIRPhiEntry *)malloc(
                sizeof(PIRPhiEntry) * (num_preds > 0 ? num_preds : 1));

            int k;
            for (k = 0; k < num_preds; k++) {
                phi->extra.phi.entries[k].value = pir_value_none();
                phi->extra.phi.entries[k].block = blk->preds[k];
            }

            /* Insert at beginning of block */
            phi->next = blk->first;
            phi->prev = 0;
            if (blk->first) {
                blk->first->prev = phi;
            } else {
                blk->last = phi;
            }
            blk->first = phi;
            blk->inst_count++;
        }
    }

    /* Step 3: Rename — walk dominator tree with value stack per alloca */
    /* Simple approach: dominator tree preorder walk */
    int *current_def = (int *)malloc(sizeof(int) * num_allocas);
    /* current_def[i] = current SSA value id for alloca i, -1 = undefined */
    for (i = 0; i < num_allocas; i++) {
        current_def[i] = -1;
    }

    /* Recursive rename using explicit stack */
    struct RenameFrame {
        int block_id;
        int saved_defs[MAX_ALLOCAS]; /* saved current_def for restore on backtrack */
    };

    int max_blocks = func->blocks.size();
    RenameFrame *rename_stack = (RenameFrame *)malloc(sizeof(RenameFrame) * (max_blocks + 1));
    int *rename_visited = (int *)malloc(sizeof(int) * PIRDOM_MAX_BLOCKS);
    memset(rename_visited, 0, sizeof(int) * PIRDOM_MAX_BLOCKS);

    int rs_top = 0;

    /* Push entry block */
    if (func->entry_block) {
        /* Initialize current_def from parameters (alloca stores in entry block) */
        /* The PIR builder stores params into allocas in the entry block.
         * We handle these during the rename pass below. */

        rename_stack[rs_top].block_id = func->entry_block->id;
        memcpy(rename_stack[rs_top].saved_defs, current_def, sizeof(int) * num_allocas);
        rs_top++;
    }

    while (rs_top > 0) {
        rs_top--;
        int blk_id = rename_stack[rs_top].block_id;

        if (rename_visited[blk_id]) {
            /* Restore saved defs */
            memcpy(current_def, rename_stack[rs_top].saved_defs, sizeof(int) * num_allocas);
            continue;
        }
        rename_visited[blk_id] = 1;

        /* Save current state for backtrack */
        int saved[MAX_ALLOCAS];
        memcpy(saved, current_def, sizeof(int) * num_allocas);

        PIRBlock *blk = pir_func_get_block(func, blk_id);
        if (!blk) continue;

        /* Process phi nodes first — fill in current values for incoming from us */
        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (inst->op != PIR_PHI) break; /* phis are at the start */

            /* This phi was inserted for an alloca.
             * inst->int_val holds the alloca_id (tagged in step 2) */
            int alloca_id = (int)inst->int_val;
            for (j = 0; j < num_allocas; j++) {
                if (allocas[j].value_id == alloca_id && allocas[j].promotable) {
                    current_def[j] = inst->result.id;
                    break;
                }
            }
        }

        /* Rename loads and stores in this block */
        inst = blk->first;
        while (inst) {
            PIRInst *next = inst->next;

            if (inst->op == PIR_STORE && inst->num_operands >= 2) {
                /* Store to alloca: update current_def */
                int alloca_id = inst->operands[0].id;
                for (j = 0; j < num_allocas; j++) {
                    if (allocas[j].value_id == alloca_id && allocas[j].promotable) {
                        current_def[j] = inst->operands[1].id;
                        /* Remove the store */
                        pir_inst_remove(blk, inst);
                        changed = 1;
                        break;
                    }
                }
            } else if (inst->op == PIR_LOAD && inst->num_operands >= 1) {
                /* Load from alloca: replace with current_def */
                int alloca_id = inst->operands[0].id;
                for (j = 0; j < num_allocas; j++) {
                    if (allocas[j].value_id == alloca_id && allocas[j].promotable) {
                        if (current_def[j] >= 0) {
                            /* Replace all uses of this load's result with current_def */
                            PIRValue old_v = inst->result;
                            PIRValue new_v = pir_value_make(current_def[j], PIR_TYPE_PYOBJ);
                            pir_replace_all_uses(func, old_v, new_v);
                            pir_inst_remove(blk, inst);
                            changed = 1;
                        }
                        break;
                    }
                }
            } else if (inst->op == PIR_ALLOCA) {
                /* Remove alloca if promotable */
                for (j = 0; j < num_allocas; j++) {
                    if (allocas[j].value_id == inst->result.id && allocas[j].promotable) {
                        pir_inst_remove(blk, inst);
                        changed = 1;
                        break;
                    }
                }
            }

            inst = next;
        }

        /* Fill in phi operands in successor blocks */
        for (j = 0; j < blk->succs.size(); j++) {
            PIRBlock *succ = blk->succs[j];
            PIRInst *phi;
            for (phi = succ->first; phi; phi = phi->next) {
                if (phi->op != PIR_PHI) break;
                int alloca_id = (int)phi->int_val;
                int ai;
                for (ai = 0; ai < num_allocas; ai++) {
                    if (allocas[ai].value_id == alloca_id && allocas[ai].promotable) {
                        /* Fill in the phi entry for predecessor 'blk' */
                        int k;
                        for (k = 0; k < phi->extra.phi.count; k++) {
                            if (phi->extra.phi.entries[k].block == blk) {
                                if (current_def[ai] >= 0) {
                                    phi->extra.phi.entries[k].value =
                                        pir_value_make(current_def[ai], PIR_TYPE_PYOBJ);
                                }
                                break;
                            }
                        }
                        break;
                    }
                }
            }
        }

        /* Push children in dominator tree (in reverse order for correct traversal) */
        /* First, push a "restore" frame */
        rename_stack[rs_top].block_id = blk_id;
        memcpy(rename_stack[rs_top].saved_defs, saved, sizeof(int) * num_allocas);
        rs_top++;

        /* Find dominated blocks and push them */
        int bi;
        for (bi = func->blocks.size() - 1; bi >= 0; bi--) {
            int child_id = func->blocks[bi]->id;
            if (child_id != blk_id && dom.idom[child_id] == blk_id) {
                if (rs_top < max_blocks + 1) {
                    rename_stack[rs_top].block_id = child_id;
                    memcpy(rename_stack[rs_top].saved_defs, current_def, sizeof(int) * num_allocas);
                    rs_top++;
                }
            }
        }
    }

    free(current_def);
    free(rename_stack);
    free(rename_visited);

    return changed;
}

/* ================================================================ */
/* GVN — Global Value Numbering                                      */
/* ================================================================ */

struct GVNEntry {
    PIROp op;
    int vn_op0;
    int vn_op1;
    long int_val;
    const char *str_val;
    int result_id;     /* SSA value id of the canonical computation */
    int block_id;      /* block containing canonical instruction */
};

#define GVN_TABLE_SIZE 512

static unsigned gvn_hash(PIROp op, int vn0, int vn1, long iv, const char *sv)
{
    unsigned h = (unsigned)op * 31;
    h = h * 37 + (unsigned)vn0;
    h = h * 37 + (unsigned)vn1;
    h = h * 37 + (unsigned)(iv & 0xFFFF);
    if (sv) {
        const char *p = sv;
        while (*p) { h = h * 31 + (unsigned)*p; p++; }
    }
    return h % GVN_TABLE_SIZE;
}

int PIROptimizer::gvn(PIRFunction *func)
{
    int changed = 0;
    int max_val = func->next_value_id;

    /* Value number table: vn[value_id] = canonical value number */
    int *vn = (int *)malloc(sizeof(int) * (max_val + 1));
    if (!vn) return 0;

    int i;
    for (i = 0; i <= max_val; i++) {
        vn[i] = i; /* Initially, each value is its own VN */
    }

    /* Hash table for GVN entries */
    GVNEntry *table = (GVNEntry *)calloc(GVN_TABLE_SIZE, sizeof(GVNEntry));
    int *table_used = (int *)calloc(GVN_TABLE_SIZE, sizeof(int));
    if (!table || !table_used) {
        free(vn);
        free(table);
        free(table_used);
        return 0;
    }

    /* Walk blocks in dominator tree preorder (RPO approximation) */
    for (i = 0; i < dom.num_blocks; i++) {
        int blk_id = dom.rpo_order[i];
        PIRBlock *blk = pir_func_get_block(func, blk_id);
        if (!blk) continue;

        PIRInst *inst;
        for (inst = blk->first; inst; inst = inst->next) {
            if (!pir_value_valid(inst->result)) continue;
            if (!pir_is_pure(inst->op)) continue;

            /* Skip alloca, load, store — not hashable */
            if (inst->op == PIR_ALLOCA || inst->op == PIR_LOAD ||
                inst->op == PIR_STORE || inst->op == PIR_PHI) continue;

            int vn0 = -1, vn1 = -1;
            if (inst->num_operands >= 1 && pir_value_valid(inst->operands[0])) {
                int id = inst->operands[0].id;
                vn0 = (id >= 0 && id < max_val) ? vn[id] : id;
            }
            if (inst->num_operands >= 2 && pir_value_valid(inst->operands[1])) {
                int id = inst->operands[1].id;
                vn1 = (id >= 0 && id < max_val) ? vn[id] : id;
            }

            unsigned h = gvn_hash(inst->op, vn0, vn1, inst->int_val, inst->str_val);

            /* Linear probe lookup */
            int found = 0;
            int probe;
            for (probe = 0; probe < GVN_TABLE_SIZE; probe++) {
                int idx = (h + probe) % GVN_TABLE_SIZE;
                if (!table_used[idx]) {
                    /* Insert new entry */
                    table_used[idx] = 1;
                    table[idx].op = inst->op;
                    table[idx].vn_op0 = vn0;
                    table[idx].vn_op1 = vn1;
                    table[idx].int_val = inst->int_val;
                    table[idx].str_val = inst->str_val;
                    table[idx].result_id = inst->result.id;
                    table[idx].block_id = blk_id;
                    break;
                }
                /* Check if existing entry matches */
                if (table[idx].op == inst->op &&
                    table[idx].vn_op0 == vn0 &&
                    table[idx].vn_op1 == vn1 &&
                    table[idx].int_val == inst->int_val) {
                    /* Check str_val match */
                    int str_match = 0;
                    if (!table[idx].str_val && !inst->str_val) str_match = 1;
                    else if (table[idx].str_val && inst->str_val &&
                             strcmp(table[idx].str_val, inst->str_val) == 0) str_match = 1;

                    if (str_match) {
                        /* Verify canonical instruction dominates current block */
                        if (!dom.dominates(table[idx].block_id, blk_id)) {
                            /* Cannot replace — canonical doesn't dominate us.
                             * Update table to point to us if we dominate it,
                             * so future matches can still benefit from GVN. */
                            if (dom.dominates(blk_id, table[idx].block_id)) {
                                table[idx].result_id = inst->result.id;
                                table[idx].block_id = blk_id;
                            }
                            found = 1;
                            break;
                        }
                        int rid = inst->result.id;
                        if (rid >= 0 && rid < max_val) {
                            vn[rid] = table[idx].result_id;
                            /* Replace all uses */
                            pir_replace_all_uses(func, inst->result,
                                pir_value_make(table[idx].result_id, inst->result.type));
                            /* PIRBuilder places the exceptional edge directly
                             * after the operation it guards.  Once GVN removes
                             * this evaluation, retaining that edge creates an
                             * orphan check and can route an unrelated pending
                             * exception.  Delete the pair atomically; final
                             * DIE removes the now-unused computation. */
                            if (inst->next != 0 &&
                                inst->next->op == PIR_CHECK_EXCEPTION)
                                pir_inst_remove(blk, inst->next);
                            changed = 1;
                        }
                        found = 1;
                        break;
                    }
                }
            }

            /* Set value number for this result */
            if (!found && inst->result.id >= 0 && inst->result.id < max_val) {
                vn[inst->result.id] = inst->result.id;
            }
        }
    }

    free(vn);
    free(table);
    free(table_used);
    return changed;
}

/* ================================================================ */
/* LICM — Loop-Invariant Code Motion                                 */
/* ================================================================ */

int PIROptimizer::licm(PIRFunction *func)
{
    int changed = 0;
    int li;

    if (loops.num_loops == 0) return 0;

    for (li = 0; li < loops.num_loops; li++) {
        NaturalLoop *loop = &loops.loops[li];
        if (loop->body.size() == 0) continue;

        /* Find or create preheader.
         * For simplicity, we look for a single predecessor of the header
         * that is NOT in the loop body. */
        PIRBlock *header = pir_func_get_block(func, loop->header_id);
        if (!header) continue;

        PIRBlock *preheader = 0;
        int pi;
        for (pi = 0; pi < header->preds.size(); pi++) {
            if (!loops.is_in_loop(li, header->preds[pi]->id)) {
                if (!preheader) {
                    preheader = header->preds[pi];
                } else {
                    /* Multiple predecessors outside loop —
                     * would need to create a new preheader block.
                     * Skip this loop for now. */
                    preheader = 0;
                    break;
                }
            }
        }

        if (!preheader) continue;
        loop->preheader_id = preheader->id;

        /* Identify loop-invariant instructions and hoist to preheader */
        int hoist_changed = 1;
        while (hoist_changed) {
            hoist_changed = 0;
            int bi;
            for (bi = 0; bi < loop->body.size(); bi++) {
                PIRBlock *blk = pir_func_get_block(func, loop->body[bi]);
                if (!blk) continue;

                PIRInst *inst = blk->first;
                while (inst) {
                    PIRInst *next = inst->next;

                    /* Skip terminators, side-effecting, phis, allocas */
                    if (pir_is_terminator(inst->op) ||
                        pir_has_side_effects(inst->op) ||
                        inst->op == PIR_PHI ||
                        inst->op == PIR_ALLOCA) {
                        inst = next;
                        continue;
                    }

                    /* Check if all operands are defined outside the loop
                     * (loop-invariant) */
                    int invariant = 1;
                    int oi;
                    for (oi = 0; oi < inst->num_operands; oi++) {
                        if (!pir_value_valid(inst->operands[oi])) continue;

                        /* Check if this operand is defined in the loop */
                        int op_id = inst->operands[oi].id;
                        int def_in_loop = 0;
                        int lbi;
                        for (lbi = 0; lbi < loop->body.size(); lbi++) {
                            PIRBlock *lb = pir_func_get_block(func, loop->body[lbi]);
                            if (!lb) continue;
                            PIRInst *di;
                            for (di = lb->first; di; di = di->next) {
                                if (pir_value_valid(di->result) &&
                                    di->result.id == op_id) {
                                    def_in_loop = 1;
                                    break;
                                }
                            }
                            if (def_in_loop) break;
                        }
                        if (def_in_loop) {
                            invariant = 0;
                            break;
                        }
                    }

                    if (!invariant) {
                        inst = next;
                        continue;
                    }

                    /* Check that instruction dominates all loop exits */
                    int dom_all_exits = 1;
                    int ei;
                    for (ei = 0; ei < loop->exits.size(); ei++) {
                        if (!dom.dominates(blk->id, loop->exits[ei])) {
                            dom_all_exits = 0;
                            break;
                        }
                    }

                    if (!dom_all_exits) {
                        inst = next;
                        continue;
                    }

                    /* Hoist: remove from current block, insert before
                     * preheader's terminator */
                    /* Unlink from current block */
                    if (inst->prev) {
                        inst->prev->next = inst->next;
                    } else {
                        blk->first = inst->next;
                    }
                    if (inst->next) {
                        inst->next->prev = inst->prev;
                    } else {
                        blk->last = inst->prev;
                    }
                    blk->inst_count--;

                    /* Insert before preheader's terminator */
                    PIRInst *term = preheader->last;
                    if (term && pir_is_terminator(term->op)) {
                        inst->next = term;
                        inst->prev = term->prev;
                        if (term->prev) {
                            term->prev->next = inst;
                        } else {
                            preheader->first = inst;
                        }
                        term->prev = inst;
                    } else {
                        /* No terminator? Append */
                        inst->next = 0;
                        inst->prev = preheader->last;
                        if (preheader->last) {
                            preheader->last->next = inst;
                        } else {
                            preheader->first = inst;
                        }
                        preheader->last = inst;
                    }
                    preheader->inst_count++;

                    hoist_changed = 1;
                    changed = 1;

                    inst = next;
                }
            }
        }
    }

    return changed;
}

/* ================================================================ */
/* Type Inference (wrapper — delegates to pirtyp.cpp)                */
/* ================================================================ */

void PIROptimizer::type_infer(PIRFunction *func)
{
    pir_type_infer(func, &dom, stdlib_reg_);
}

/* ================================================================ */
/* Escape Analysis (wrapper — delegates to piresc.cpp)               */
/* ================================================================ */

void PIROptimizer::escape_analyze(PIRFunction *func)
{
    pir_escape_analyze(func);
}

/* ================================================================ */
/* Type-Guided Specialization (wrapper — delegates to pirspc.cpp)    */
/* ================================================================ */

int PIROptimizer::specialize(PIRFunction *func)
{
    return pir_specialize(func);
}

/* ================================================================ */
/* Devirtualization (wrapper — delegates to pirspc.cpp)              */
/* ================================================================ */

int PIROptimizer::devirtualize(PIRFunction *func)
{
    return pir_devirtualize(func, current_module);
}

/* ================================================================ */
/* Arena Scope Insertion                                              */
/* ================================================================ */

/* Persistent stores and yields transfer the producer's owned reference.
 * Returns are intentionally not included: scope_insert promotes the selected
 * result with INCREF before releasing local tickets. */
static int value_transfers_persistently(PIRFunction *func, int value_id,
                                        unsigned char *visiting, int count)
{
    int bi;
    if (value_id < 0 || value_id >= count) return 0;
    if (visiting[value_id]) return 0;
    visiting[value_id] = 1;

    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst;
        for (inst = block->first; inst; inst = inst->next) {
            if ((inst->op == PIR_YIELD || inst->op == PIR_STORE_GLOBAL ||
                 inst->op == PIR_SET_CLOSURE) &&
                inst->num_operands > 0 &&
                inst->operands[0].id == value_id) {
                visiting[value_id] = 0;
                return 1;
            }
            if (inst->op == PIR_PHI && pir_value_valid(inst->result)) {
                int pi;
                for (pi = 0; pi < inst->extra.phi.count; pi++) {
                    if (inst->extra.phi.entries[pi].value.id == value_id &&
                        value_transfers_persistently(func, inst->result.id,
                                                     visiting, count)) {
                        visiting[value_id] = 0;
                        return 1;
                    }
                }
            }
        }
    }
    visiting[value_id] = 0;
    return 0;
}

/* The callable gateway still contains legacy native setup helpers and method
 * fallbacks whose borrowed/owned return convention is not uniform.  Python
 * functions, constructors and builtins already return owned references. */
static int arena_trackable_owned_inst(const PIRInst *inst)
{
    PIROp op;
    if (!inst) return 0;
    op = inst->op;
    if (!pir_returns_owned_pyobj(op)) return 0;
    if (op == PIR_CALL) {
        /* Legacy pydos_* setup helpers predate the uniform callable return
         * convention.  Python functions, classes and builtins already cross
         * this boundary with an owned result. */
        return !inst->str_val || strncmp(inst->str_val, "pydos_", 6) != 0;
    }
    return op != PIR_CALL_METHOD && op != PIR_GUARDED_CALL_METHOD;
}

void PIROptimizer::scope_insert(PIRFunction *func)
{
    FuncEscapeResult *esc;
    int bi;
    int inserted_enter;
    int needs_scope;
    unsigned char *visiting;

    if (!func || func->blocks.size() == 0) return;
    esc = func->escape_info;
    if (!esc) return;

    visiting = (unsigned char *)calloc((size_t)esc->count,
                                       sizeof(unsigned char));
    if (!visiting) return;

    inserted_enter = 0;
    needs_scope = 0;

    /* Allocation may live in a non-entry CFG block.  Generator resume
     * functions are the canonical case: the entry block only dispatches on
     * the saved program counter and the owned values are created in state
     * blocks.  Decide globally, then place one scope at the real function
     * entry so every path has balanced ownership accounting. */
    for (bi = 0; bi < func->blocks.size() && !needs_scope; bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *candidate;
        for (candidate = block->first; candidate;
             candidate = candidate->next) {
            if (arena_trackable_owned_inst(candidate)) {
                int vid = candidate->result.id;
                if (vid >= 0 && vid < esc->count &&
                    !value_transfers_persistently(func, vid, visiting,
                                                  esc->count)) {
                    needs_scope = 1;
                    break;
                }
            }
        }
    }

    if (needs_scope && func->entry_block) {
        PIRBlock *entry = func->entry_block;
        PIRInst *enter = pir_inst_new(PIR_SCOPE_ENTER);
        enter->num_operands = 0;
        enter->line = entry->first ? entry->first->line : 0;
        enter->prev = 0;
        enter->next = entry->first;
        if (entry->first) entry->first->prev = enter;
        else entry->last = enter;
        entry->first = enter;
        entry->inst_count++;
        inserted_enter = 1;
    }

    for (bi = 0; bi < func->blocks.size(); bi++) {
        PIRBlock *block = func->blocks[bi];
        PIRInst *inst, *next_inst;

        for (inst = block->first; inst; inst = next_inst) {
            next_inst = inst->next;

            /* Argument references are safe to release at scope exit: any
             * callee that retains them must INCREF according to the runtime
             * ownership convention. */
            if (inserted_enter && arena_trackable_owned_inst(inst)) {
                int vid = inst->result.id;
                if (vid >= 0 && vid < esc->count &&
                    !value_transfers_persistently(func, vid, visiting,
                                                  esc->count)) {
                    PIRInst *track = pir_inst_new(PIR_SCOPE_TRACK);
                    track->operands[0] = inst->result;
                    track->num_operands = 1;
                    /* Lookups return a new reference to an object which may
                     * remain owned by a container.  Do not arena-mark that
                     * shared object; record only the temporary reference. */
                    track->int_val =
                        (inst->op == PIR_GET_ATTR ||
                         inst->op == PIR_SUBSCR_GET) ? 1 : 0;
                    track->line = inst->line;
                    /* Insert after inst */
                    track->prev = inst;
                    track->next = inst->next;
                    if (inst->next) {
                        inst->next->prev = track;
                    } else {
                        block->last = track;
                    }
                    inst->next = track;
                    block->inst_count++;
                    next_inst = track->next;
                }
            }

            /* Insert SCOPE_EXIT before each RETURN/RETURN_NONE */
            if (inserted_enter &&
                (inst->op == PIR_RETURN || inst->op == PIR_RETURN_NONE)) {
                PIRInst *exit_inst = pir_inst_new(PIR_SCOPE_EXIT);
                PIRInst *retain_inst = 0;
                exit_inst->num_operands = 0;
                exit_inst->line = inst->line;
                if (inst->op == PIR_RETURN && inst->num_operands > 0 &&
                    pir_value_valid(inst->operands[0])) {
                    retain_inst = pir_inst_new(PIR_INCREF);
                    retain_inst->operands[0] = inst->operands[0];
                    retain_inst->num_operands = 1;
                    retain_inst->line = inst->line;
                    inst->int_val = 1; /* return already owns its result */
                }
                /* Insert before the return */
                exit_inst->next = inst;
                exit_inst->prev = retain_inst ? retain_inst : inst->prev;
                if (retain_inst) {
                    retain_inst->prev = inst->prev;
                    retain_inst->next = exit_inst;
                }
                if (inst->prev) {
                    inst->prev->next = retain_inst ? retain_inst : exit_inst;
                } else {
                    block->first = retain_inst ? retain_inst : exit_inst;
                }
                inst->prev = exit_inst;
                block->inst_count += retain_inst ? 2 : 1;
            }
        }
    }
    free(visiting);
}
