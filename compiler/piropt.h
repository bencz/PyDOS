/*
 * piropt.h - PIR optimization pass manager
 *
 * Runs SSA-based optimization passes on PIR before lowering to flat IR:
 *   1. Dead Block Elimination
 *   2. SCCP (Sparse Conditional Constant Propagation)
 *   3. Dead Block Elimination (post-SCCP cleanup)
 *   4. mem2reg (alloca promotion to SSA with phi nodes)
 *   5. Type Inference (analysis)
 *   6. Escape Analysis (analysis)
 *   7. Devirtualization (CALL_METHOD -> direct CALL)
 *   8. Type-Guided Specialization (py_add -> add_i32)
 *   9. Dead Instruction Elimination
 *  10. GVN (Global Value Numbering)
 *  11. LICM (Loop-Invariant Code Motion)
 *  12. Dead Instruction Elimination (final cleanup)
 *  13. Whole-Module Hygiene
 *  14. Final Escape Analysis
 *  15. Arena Scope Insertion
 *
 * C++98 compatible, Open Watcom wpp.
 */

#ifndef PIROPT_H
#define PIROPT_H

#include "pir.h"
#include "pirdom.h"
#include "stdscan.h"

/* Flags to selectively disable individual optimization passes.
 * Set from CLI flags (--no-sccp, --no-gvn, etc.) for debugging. */
extern int piropt_skip_sccp;
extern int piropt_skip_gvn;
extern int piropt_skip_licm;
extern int piropt_skip_specialize;
extern int piropt_skip_scope;
extern int piropt_skip_mem2reg;
extern int piropt_skip_die;
extern int piropt_skip_devirt;
extern int piropt_skip_dbe;
extern int piropt_skip_func_dedup;
extern int piropt_skip_hygiene;

class PIROptimizer {
public:
    PIROptimizer();
    ~PIROptimizer();

    void set_stdlib(StdlibRegistry *reg);
    void optimize(PIRModule *mod);
    int get_deduplicated_function_count() const;
    unsigned long get_hygiene_bundle_count() const;
    unsigned long get_hygiene_instruction_count() const;
    unsigned long get_hygiene_exception_fast_path_count() const;
    unsigned get_hygiene_feature_mask() const;

private:
    DomInfo dom;
    DomFrontier df;
    LoopInfo loops;

    void optimize_function(PIRFunction *func);

    /* Individual passes */
    int dead_block_eliminate(PIRFunction *func);
    int dead_inst_eliminate(PIRFunction *func);
    int sccp(PIRFunction *func);
    int mem2reg(PIRFunction *func);
    int gvn(PIRFunction *func);
    int licm(PIRFunction *func);

    /* Analysis passes */
    void type_infer(PIRFunction *func);
    void escape_analyze(PIRFunction *func);

    /* Specialization pass */
    int specialize(PIRFunction *func);

    /* Arena scope insertion */
    void scope_insert(PIRFunction *func);

    /* Devirtualization */
    int devirtualize(PIRFunction *func);

    /* Whole-module sharing of identical monomorphized method bodies. */
    int deduplicate_generic_functions(PIRModule *mod);

    PIRModule *current_module;
    StdlibRegistry *stdlib_reg_;
    int deduplicated_function_count_;
    unsigned long hygiene_bundle_count_;
    unsigned long hygiene_instruction_count_;
    unsigned long hygiene_exception_fast_path_count_;
    unsigned hygiene_feature_mask_;
};

#endif /* PIROPT_H */
