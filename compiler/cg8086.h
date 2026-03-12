/*
 * codegen_8086.h - 8086 real-mode code generator for PyDOS compiler
 *
 * CodeGenerator8086 is a concrete implementation of CodeGeneratorBase
 * that generates Open Watcom Assembler (WASM) code for 16-bit 8086 processors
 * in real-mode DOS.
 *
 * Key features:
 * - Linear scan register allocation (AX, BX, CX, DX, SI, DI available)
 * - Far pointers (segment:offset) for all Python objects
 * - 32-bit arithmetic via Watcom runtime helpers (__I4M, __I4D, etc.)
 * - Stack-based argument passing (C calling convention)
 * - DGROUP (DS) segment for all static data
 *
 * C++98 compatible, no STL. Open Watcom targeting real-mode DOS.
 */

#ifndef CG8086_H
#define CG8086_H

#include "codegen.h"

/* ================================================================= */
/* CodeGenerator8086 — 8086 real-mode back-end                       */
/* ================================================================= */

class CodeGenerator8086 : public CodeGeneratorBase {
public:
    CodeGenerator8086();
    virtual ~CodeGenerator8086();

    /* =============================================================== */
    /* Pure virtual implementations                                     */
    /* =============================================================== */

    /* --------------------------------------------------------------- */
    /* File section generators                                          */
    /* --------------------------------------------------------------- */
    virtual void emit_header();
    virtual void emit_footer();
    virtual void emit_data_section();
    virtual void emit_code_section();
    virtual void emit_extern_declarations();

    /* --------------------------------------------------------------- */
    /* Function generation                                              */
    /* --------------------------------------------------------------- */
    virtual void emit_func(IRFunc *func);
    virtual void emit_prologue(IRFunc *func);
    virtual void emit_epilogue(IRFunc *func);

    /* --------------------------------------------------------------- */
    /* Per-instruction emitters                                         */
    /* --------------------------------------------------------------- */
    virtual void emit_const_int(IRInstr *instr);
    virtual void emit_const_str(IRInstr *instr);
    virtual void emit_const_float(IRInstr *instr);
    virtual void emit_const_none(IRInstr *instr);
    virtual void emit_const_bool(IRInstr *instr);

    virtual void emit_load_local(IRInstr *instr);
    virtual void emit_store_local(IRInstr *instr);
    virtual void emit_load_global(IRInstr *instr);
    virtual void emit_store_global(IRInstr *instr);

    virtual void emit_arithmetic(IRInstr *instr);
    virtual void emit_bitwise(IRInstr *instr);
    virtual void emit_unary(IRInstr *instr);
    virtual void emit_matmul(IRInstr *instr);
    virtual void emit_inplace(IRInstr *instr);
    virtual void emit_comparison(IRInstr *instr);

    virtual void emit_jump(IRInstr *instr);
    virtual void emit_jump_cond(IRInstr *instr);
    virtual void emit_ir_label(IRInstr *instr);

    virtual void emit_call(IRInstr *instr);
    virtual void emit_call_method(IRInstr *instr);
    virtual void emit_push_arg(IRInstr *instr);
    virtual void emit_return(IRInstr *instr);

    virtual void emit_incref(IRInstr *instr);
    virtual void emit_decref(IRInstr *instr);

    virtual void emit_build_list(IRInstr *instr);
    virtual void emit_build_dict(IRInstr *instr);
    virtual void emit_build_tuple(IRInstr *instr);
    virtual void emit_build_set(IRInstr *instr);

    virtual void emit_get_iter(IRInstr *instr);
    virtual void emit_for_iter(IRInstr *instr);

    virtual void emit_load_attr(IRInstr *instr);
    virtual void emit_store_attr(IRInstr *instr);
    virtual void emit_load_subscript(IRInstr *instr);
    virtual void emit_store_subscript(IRInstr *instr);
    virtual void emit_del_subscript(IRInstr *instr);
    virtual void emit_del_local(IRInstr *instr);
    virtual void emit_del_global(IRInstr *instr);
    virtual void emit_del_attr(IRInstr *instr);
    virtual void emit_load_slice(IRInstr *instr);
    virtual void emit_exc_match(IRInstr *instr);

    virtual void emit_setup_try(IRInstr *instr);
    virtual void emit_pop_try(IRInstr *instr);
    virtual void emit_raise(IRInstr *instr);
    virtual void emit_reraise(IRInstr *instr);
    virtual void emit_get_exception(IRInstr *instr);

    virtual void emit_alloc_obj(IRInstr *instr);
    virtual void emit_init_vtable(IRInstr *instr);
    virtual void emit_set_vtable(IRInstr *instr);
    virtual void emit_check_vtable(IRInstr *instr);

    virtual void emit_make_function(IRInstr *instr);
    virtual void emit_make_generator(IRInstr *instr);
    virtual void emit_make_coroutine(IRInstr *instr);
    virtual void emit_cor_set_result(IRInstr *instr);
    virtual void emit_make_cell(IRInstr *instr);
    virtual void emit_cell_get(IRInstr *instr);
    virtual void emit_cell_set(IRInstr *instr);
    virtual void emit_load_closure(IRInstr *instr);
    virtual void emit_set_closure(IRInstr *instr);
    virtual void emit_gen_load_pc(IRInstr *instr);
    virtual void emit_gen_set_pc(IRInstr *instr);
    virtual void emit_gen_load_local(IRInstr *instr);
    virtual void emit_gen_save_local(IRInstr *instr);
    virtual void emit_gen_check_throw(IRInstr *instr);
    virtual void emit_gen_get_sent(IRInstr *instr);

    virtual void emit_arith_i32(IRInstr *instr);
    virtual void emit_cmp_i32(IRInstr *instr);
    virtual void emit_box_int(IRInstr *instr);
    virtual void emit_unbox_int(IRInstr *instr);
    virtual void emit_box_bool(IRInstr *instr);
    virtual void emit_str_join(IRInstr *instr);
    virtual void emit_scope_enter(IRInstr *instr);
    virtual void emit_scope_track(IRInstr *instr);
    virtual void emit_scope_exit(IRInstr *instr);

private:
    /* =============================================================== */
    /* 8086-specific private helper methods                             */
    /* =============================================================== */

    /* Emit "mov ax, N / push ax" to push an immediate value.
     * 8086 has no "push imm16" instruction, so we use AX as intermediary. */
    void emit_push_imm(int val);

    /* Emit "push seg:offset" of a data label.
     * Pushes 4 bytes (offset then segment) to represent a far pointer. */
    void emit_push_far_const(const char *lbl);

    /* Emit "push ss / pop ds" to restore DS = DGROUP.
     * Used after calling functions that may have changed DS. */
    void emit_restore_ds();

    /* Compute the BP-relative offset for a temporary variable.
     * Temps are allocated on the stack in reverse order from BP. */
    int temp_bp_offset(int temp) const;

    /* Load a 4-byte far pointer temp (from stack) into DX:AX.
     * DX = segment, AX = offset. */
    void load_temp_to_dxax(int temp);

    /* Store a 4-byte far pointer from DX:AX into a temp slot on stack.
     * DX = segment, AX = offset. */
    void store_dxax_to_temp(int temp);

    /* Push a 4-byte far pointer temp onto the stack.
     * Loads from temp and pushes as seg:offset. */
    void push_temp(int temp);
};

#endif /* CG8086_H */
