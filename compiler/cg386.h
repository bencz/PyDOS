/*
 * codegen_386.h - 386 protected-mode code generator for PyDOS
 *
 * CodeGenerator386 is a concrete back-end that targets 32-bit protected mode
 * (FLAT memory model) on Intel 386+ processors. This is suitable for modern
 * DOS extenders (e.g., DOS/4GW, DOS/32A, CWSDPMI).
 *
 * Architectural notes:
 * - Directive: .386P / .MODEL FLAT (not .8086 / .MODEL LARGE)
 * - 32-bit registers: EAX, EBX, ECX, EDX, ESI, EDI, ESP, EBP
 * - Near pointers: 4-byte linear addresses (not far seg:off)
 * - Return value in EAX (4 bytes, not DX:AX)
 * - Call convention: call func / ret (not call far ptr / retf)
 * - No DS restore needed (flat model, DS always valid)
 * - Parameter base: [EBP+8] (near ret=4 + saved EBP=4)
 * - Each local slot: 4 bytes (one DWORD, not two words)
 * - SAVE_AREA: 8 bytes (ESI + EDI, 4 bytes each)
 * - External symbols: EXTRN func:NEAR (not :FAR)
 * - System: dos4g (not dos)
 * - PYOBJ_V_OFFSET: 8 (struct alignment with 32-bit int)
 * - Immediate values can be pushed directly (push N)
 *
 * C++98 compatible. No STL containers.
 */

#ifndef CG386_H
#define CG386_H

#include "codegen.h"

/* ================================================================= */
/* CodeGenerator386 — 386 protected-mode code generator              */
/* ================================================================= */

class CodeGenerator386 : public CodeGeneratorBase {
public:
    CodeGenerator386();
    virtual ~CodeGenerator386();

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
    /* --------------------------------------------------------------- */
    /* 386-specific private helper methods                              */
    /* --------------------------------------------------------------- */

    /* Compute EBP offset for a local variable slot.
     * In 386 FLAT model, each slot is one DWORD (4 bytes).
     * Negative offsets from EBP (locals are at [EBP - N]). */
    int slot_offset(int slot);

    /* Load a 4-byte near pointer from local temp slot into EAX.
     * Uses EBP-relative addressing. */
    void load_temp_to_eax(int temp);

    /* Store EAX (4-byte value) into a local temp slot.
     * Uses EBP-relative addressing. */
    void store_eax_to_temp(int temp);

    /* Push a DWORD temp value onto the stack.
     * Used for argument passing in function calls. */
    void push_temp(int temp);
};

#endif /* CG386_H */
