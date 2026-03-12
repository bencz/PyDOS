/*
 * codegen.h - Abstract code generation base for PyDOS compiler
 *
 * Defines CodeGeneratorBase, an abstract base class that contains all
 * shared infrastructure (extern/global/vtable tracking, label helpers,
 * prescan passes, instruction dispatch) and declares pure virtual
 * methods for architecture-specific emission.
 *
 * Concrete back-ends (8086, 386) derive from this class and implement
 * the virtual methods. Use create_codegen() to obtain the right one.
 *
 * All Python objects are represented as far pointers (4 bytes:
 * segment:offset) to PyDosObj structures allocated by the runtime.
 *
 * C++98 compatible, Open Watcom targeting real-mode DOS.
 * No STL - arrays, manual memory only.
 */

#ifndef CODEGEN_H
#define CODEGEN_H

#include "ir.h"
#include "regalloc.h"
#include "stdscan.h"
#include <stdio.h>

/* ================================================================= */
/* Limits                                                             */
/* ================================================================= */

/* Maximum number of external runtime symbols we track */
#define CODEGEN_MAX_EXTERNS     256

/* Maximum number of global variables */
#define CODEGEN_MAX_GLOBALS     256

/* Maximum number of pushed args for a single call */
#define CODEGEN_MAX_CALL_ARGS   64

/* Maximum number of vtable globals */
#define CODEGEN_MAX_VTABLES     32

/* Maximum number of method-name string constants */
#define CODEGEN_MAX_MN_STRINGS  128

/* Maximum number of constants for dead-constant elimination */
#define CODEGEN_MAX_CONSTANTS   512

/* ================================================================= */
/* Target architecture enum                                           */
/* ================================================================= */

enum CodegenTarget {
    TARGET_8086 = 0,
    TARGET_386  = 1
};

/* ================================================================= */
/* CodeGeneratorBase — abstract base class                            */
/* ================================================================= */

class CodeGeneratorBase {
public:
    CodeGeneratorBase();
    virtual ~CodeGeneratorBase();

    /* Generate assembly file from IR module.
     * Returns 1 on success, 0 on failure.
     * This is the main entry point: prescan, header, data, code, footer. */
    int generate(IRModule *mod, const char *output_filename);

    /* Enable verbose output (source comments in assembly) */
    void set_verbose(int v);

    /* Set target architecture (0=8086, 1=386) */
    void set_target(int t);

    /* Set stdlib registry for builtin asm name lookup */
    void set_stdlib(StdlibRegistry *reg);

protected:
    /* --------------------------------------------------------------- */
    /* Core state                                                       */
    /* --------------------------------------------------------------- */
    FILE *out;
    IRModule *mod;
    IRFunc *current_func;
    RegAllocation *current_alloc;
    RegisterAllocator reg_allocator;
    int verbose;
    int label_counter;
    int target;  /* 0 = 8086, 1 = 386 */
    StdlibRegistry *stdlib_reg_;  /* NULL = no registry loaded */
    const char *module_name;    /* NULL = no qualification */
    int is_main_module;         /* 1 = emit main_ entry point */
    int has_main_func;          /* 1 = call entry func from main */
    const char *entry_func;     /* name of entry function */

    /* --------------------------------------------------------------- */
    /* External symbol tracking (dynamic)                               */
    /* --------------------------------------------------------------- */
    const char **externs;
    int externs_cap;
    int num_externs;
    void require_extern(const char *name);
    int has_extern(const char *name) const;

    /* Data externs: variables in DGROUP, not code labels.
     * On 8086, these must be EXTRN name:DWORD in the .DATA section
     * (not EXTRN name:FAR in .CODE, which creates wrong fixups). */
    const char **data_externs;
    int data_externs_cap;
    int num_data_externs;
    void require_data_extern(const char *name);

    /* --------------------------------------------------------------- */
    /* Global variable tracking (dynamic)                               */
    /* --------------------------------------------------------------- */
    struct GlobalVar {
        const char *name;       /* Python name */
        char asm_name[64];      /* assembly label: _G_name */
    };
    GlobalVar *globals;
    int globals_cap;
    int num_globals;
    void register_global(const char *name);
    const char *global_asm_name(const char *name);

    /* --------------------------------------------------------------- */
    /* VTable tracking for class vtable globals and method name strings */
    /* --------------------------------------------------------------- */
    struct VTableGlobal {
        char label[64];           /* _VT_ClassName */
    };
    VTableGlobal *vtable_globals;
    int vtable_globals_cap;
    int num_vtable_globals;

    struct MethodNameStr {
        char label[32];           /* _MN_0, _MN_1, ... */
        const char *str_data;     /* "__add__", "__str__", etc. */
    };
    MethodNameStr *mn_strings;
    int mn_strings_cap;
    int num_mn_strings;

    /* Register a method name string and return its label index */
    int register_mn_string(const char *name);

    /* --------------------------------------------------------------- */
    /* Dead string constant elimination (dynamic)                       */
    /* --------------------------------------------------------------- */
    int *used_const;
    int used_const_cap;
    int num_used_const;
    void mark_const_used(int ci);

    /* --------------------------------------------------------------- */
    /* Call argument stack for IR_PUSH_ARG / IR_CALL sequence (dynamic)  */
    /* --------------------------------------------------------------- */
    int *arg_temps;
    int arg_temps_cap;
    int num_call_args;

    /* Grow helpers */
    void grow_externs();
    void grow_globals();
    void grow_vtable_globals();
    void grow_mn_strings();
    void grow_used_const(int needed);
    void grow_arg_temps();

    /* --------------------------------------------------------------- */
    /* Text output helpers (non-virtual)                                */
    /* --------------------------------------------------------------- */
    void emit_line(const char *fmt, ...);
    void emit_comment(const char *fmt, ...);
    void emit_label(const char *name);
    void emit_blank();

    /* --------------------------------------------------------------- */
    /* Label helpers (non-virtual)                                      */
    /* --------------------------------------------------------------- */
    int new_label();
    void emit_local_label(int label_id);
    void const_label(char *buf, int bufsize, int const_idx);
    void func_label(char *buf, int bufsize, const char *funcname);

    /* --------------------------------------------------------------- */
    /* Runtime function name mapping (non-virtual)                      */
    /* --------------------------------------------------------------- */
    const char *runtime_arith_func(IROp op);
    const char *runtime_cmp_func(IROp op);
    const char *runtime_bitwise_func(IROp op);
    const char *runtime_unary_func(IROp op);
    const char *builtin_asm_name(const char *py_name);

    /* Registry-driven fast-path method resolution.
     * Returns asm_name for a direct fast-path call, or NULL for generic dispatch.
     * Matches when: type_hint set, registry loaded, asm_name non-empty, and
     * argc matches fast_argc (or num_params if fast_argc == -1). */
    const char *resolve_fast_method(IRInstr *instr, const char *method_name, int argc);

    /* Registry-driven subscript/slice function resolution.
     * Returns operator function name or NULL for generic dispatch. */
    const char *resolve_subscript_func(TypeInfo *type_hint, int is_store);
    const char *resolve_slice_func(TypeInfo *type_hint);

    /* --------------------------------------------------------------- */
    /* Pre-scan passes (non-virtual)                                    */
    /* --------------------------------------------------------------- */

    /* Scan entire IR module to discover globals and required externs */
    void prescan_module();
    void prescan_func(IRFunc *func);

    /* --------------------------------------------------------------- */
    /* Instruction dispatch (non-virtual)                               */
    /* --------------------------------------------------------------- */

    /* Switch on opcode and call the appropriate virtual emitter */
    void emit_instr(IRInstr *instr);

    /* =============================================================== */
    /* Pure virtual methods — implemented by architecture back-ends     */
    /* =============================================================== */

    /* --------------------------------------------------------------- */
    /* File section generators                                          */
    /* --------------------------------------------------------------- */
    virtual void emit_header() = 0;
    virtual void emit_footer() = 0;
    virtual void emit_data_section() = 0;
    virtual void emit_code_section() = 0;
    virtual void emit_extern_declarations() = 0;

    /* --------------------------------------------------------------- */
    /* Function generation                                              */
    /* --------------------------------------------------------------- */
    virtual void emit_func(IRFunc *func) = 0;
    virtual void emit_prologue(IRFunc *func) = 0;
    virtual void emit_epilogue(IRFunc *func) = 0;

    /* --------------------------------------------------------------- */
    /* Per-instruction emitters                                         */
    /* --------------------------------------------------------------- */
    virtual void emit_const_int(IRInstr *instr) = 0;
    virtual void emit_const_str(IRInstr *instr) = 0;
    virtual void emit_const_float(IRInstr *instr) = 0;
    virtual void emit_const_none(IRInstr *instr) = 0;
    virtual void emit_const_bool(IRInstr *instr) = 0;

    virtual void emit_load_local(IRInstr *instr) = 0;
    virtual void emit_store_local(IRInstr *instr) = 0;
    virtual void emit_load_global(IRInstr *instr) = 0;
    virtual void emit_store_global(IRInstr *instr) = 0;

    virtual void emit_arithmetic(IRInstr *instr) = 0;
    virtual void emit_bitwise(IRInstr *instr) = 0;
    virtual void emit_unary(IRInstr *instr) = 0;
    virtual void emit_matmul(IRInstr *instr) = 0;
    virtual void emit_inplace(IRInstr *instr) = 0;
    virtual void emit_comparison(IRInstr *instr) = 0;

    virtual void emit_jump(IRInstr *instr) = 0;
    virtual void emit_jump_cond(IRInstr *instr) = 0;
    virtual void emit_ir_label(IRInstr *instr) = 0;

    virtual void emit_call(IRInstr *instr) = 0;
    virtual void emit_call_method(IRInstr *instr) = 0;
    virtual void emit_push_arg(IRInstr *instr) = 0;
    virtual void emit_return(IRInstr *instr) = 0;

    virtual void emit_incref(IRInstr *instr) = 0;
    virtual void emit_decref(IRInstr *instr) = 0;

    virtual void emit_build_list(IRInstr *instr) = 0;
    virtual void emit_build_dict(IRInstr *instr) = 0;
    virtual void emit_build_tuple(IRInstr *instr) = 0;
    virtual void emit_build_set(IRInstr *instr) = 0;

    virtual void emit_get_iter(IRInstr *instr) = 0;
    virtual void emit_for_iter(IRInstr *instr) = 0;

    virtual void emit_load_attr(IRInstr *instr) = 0;
    virtual void emit_store_attr(IRInstr *instr) = 0;
    virtual void emit_load_subscript(IRInstr *instr) = 0;
    virtual void emit_store_subscript(IRInstr *instr) = 0;
    virtual void emit_del_subscript(IRInstr *instr) = 0;
    virtual void emit_del_local(IRInstr *instr) = 0;
    virtual void emit_del_global(IRInstr *instr) = 0;
    virtual void emit_del_attr(IRInstr *instr) = 0;
    virtual void emit_load_slice(IRInstr *instr) = 0;
    virtual void emit_exc_match(IRInstr *instr) = 0;

    virtual void emit_setup_try(IRInstr *instr) = 0;
    virtual void emit_pop_try(IRInstr *instr) = 0;
    virtual void emit_raise(IRInstr *instr) = 0;
    virtual void emit_reraise(IRInstr *instr) = 0;
    virtual void emit_get_exception(IRInstr *instr) = 0;

    virtual void emit_alloc_obj(IRInstr *instr) = 0;
    virtual void emit_init_vtable(IRInstr *instr) = 0;
    virtual void emit_set_vtable(IRInstr *instr) = 0;
    virtual void emit_check_vtable(IRInstr *instr) = 0;

    virtual void emit_make_function(IRInstr *instr) = 0;
    virtual void emit_make_generator(IRInstr *instr) = 0;
    virtual void emit_make_coroutine(IRInstr *instr) = 0;
    virtual void emit_cor_set_result(IRInstr *instr) = 0;
    virtual void emit_make_cell(IRInstr *instr) = 0;
    virtual void emit_cell_get(IRInstr *instr) = 0;
    virtual void emit_cell_set(IRInstr *instr) = 0;
    virtual void emit_load_closure(IRInstr *instr) = 0;
    virtual void emit_set_closure(IRInstr *instr) = 0;
    virtual void emit_gen_load_pc(IRInstr *instr) = 0;
    virtual void emit_gen_set_pc(IRInstr *instr) = 0;
    virtual void emit_gen_load_local(IRInstr *instr) = 0;
    virtual void emit_gen_save_local(IRInstr *instr) = 0;
    virtual void emit_gen_check_throw(IRInstr *instr) = 0;
    virtual void emit_gen_get_sent(IRInstr *instr) = 0;

    /* Typed i32 arithmetic and box/unbox (specialization) */
    virtual void emit_arith_i32(IRInstr *instr) = 0;
    virtual void emit_cmp_i32(IRInstr *instr) = 0;
    virtual void emit_box_int(IRInstr *instr) = 0;
    virtual void emit_unbox_int(IRInstr *instr) = 0;
    virtual void emit_box_bool(IRInstr *instr) = 0;
    virtual void emit_str_join(IRInstr *instr) = 0;

    /* Arena scope operations */
    virtual void emit_scope_enter(IRInstr *instr) = 0;
    virtual void emit_scope_track(IRInstr *instr) = 0;
    virtual void emit_scope_exit(IRInstr *instr) = 0;
};

/* ================================================================= */
/* Factory function                                                   */
/* ================================================================= */

/* Create a concrete code generator for the given target architecture.
 * Returns NULL if the target is not supported.
 * Caller owns the returned object and must delete it. */
CodeGeneratorBase *create_codegen(int target);

#endif /* CODEGEN_H */
