/*
 * codegen_386.cpp - 386 protected-mode code generator for PyDOS compiler
 *
 * Implements CodeGenerator386: the concrete back-end that emits
 * Open Watcom Assembler (WASM) instructions for 32-bit 386 in
 * protected-mode DOS with FLAT memory model (DOS/4GW extender).
 *
 * Key architectural details:
 *   - All Python objects are near pointers (4 bytes: linear address)
 *   - Each local/temp slot = 4 bytes on the stack (one DWORD)
 *   - Parameters begin at [EBP+8] (near ret = 4, saved EBP = 4)
 *   - SAVE_AREA = 8 bytes (ESI + EDI pushed after EBP)
 *   - DS is always valid (flat model, no restore needed)
 *   - Return values in EAX (single register)
 *   - 386 supports push-immediate (push N directly)
 *   - String constants emitted in .DATA as _SC0, _SC1, ...
 *   - Global variables emitted as _G_name dd 0
 *   - VTable globals emitted as _VT_ClassName dd 0
 *   - Runtime function names end with trailing underscore (cdecl)
 *
 * C++98 compatible.  No STL -- arrays, manual memory only.
 */

#include "cg386.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *codegen_strdup(const char *s)
{
    int len = (int)strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* ================================================================= */
/* File-scope constants                                               */
/* ================================================================= */

/* ESI + EDI = 8 bytes pushed after EBP in the prologue */
#define SAVE_AREA_386       8

/* near return address (4) + saved EBP (4) = first param at [EBP+8] */
#define PARAM_BASE_386      8

/* Offset of the value union in PyDosObj (32-bit layout):
 * type(1) + flags(1) + pad(2) + refcount(4) = 8 bytes */
#define PYOBJ_V_OFFSET_32   8

/* ================================================================= */
/* Constructor / Destructor                                           */
/* ================================================================= */

CodeGenerator386::CodeGenerator386()
{
}

CodeGenerator386::~CodeGenerator386()
{
}

/* ================================================================= */
/* Private helper methods                                             */
/* ================================================================= */

int CodeGenerator386::slot_offset(int slot)
{
    return -(SAVE_AREA_386 + slot * 4 + 4);
}

void CodeGenerator386::load_temp_to_eax(int temp)
{
    int slot = current_func->num_locals + temp;
    emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(slot));
}

void CodeGenerator386::store_eax_to_temp(int temp)
{
    int slot = current_func->num_locals + temp;
    emit_line("mov  dword ptr [ebp%+d], eax", slot_offset(slot));
}

void CodeGenerator386::push_temp(int temp)
{
    int slot = current_func->num_locals + temp;
    emit_line("push dword ptr [ebp%+d]", slot_offset(slot));
}

/* ================================================================= */
/* emit_header                                                        */
/* ================================================================= */

void CodeGenerator386::emit_header()
{
    fprintf(out, "; PyDOS compiler output - 386 protected-mode WASM assembly\n");
    fprintf(out, ".386p\n");
    fprintf(out, ".MODEL FLAT\n");
    emit_blank();
}

/* ================================================================= */
/* emit_footer                                                        */
/* ================================================================= */

void CodeGenerator386::emit_footer()
{
    emit_blank();
    fprintf(out, "END\n");
}

/* ================================================================= */
/* emit_data_section                                                  */
/* ================================================================= */

void CodeGenerator386::emit_data_section()
{
    int i;
    fprintf(out, ".DATA\n");
    emit_blank();

    /* ---- String constants ---- */
    for (i = 0; i < mod->num_constants; i++) {
        if (mod->constants[i].kind != IRConst::CONST_STR) continue;
        if (!used_const[i]) continue;

        char lbl[32];
        const_label(lbl, sizeof(lbl), i);
        const char *sdata = mod->constants[i].str_val.data;
        int slen = mod->constants[i].str_val.len;

        fprintf(out, "%s", lbl);

        int has_special = 0;
        int j;
        for (j = 0; j < slen; j++) {
            unsigned char ch = (unsigned char)sdata[j];
            if (ch < 32 || ch > 126 || ch == '"' || ch == '\\') {
                has_special = 1;
                break;
            }
        }

        if (!has_special && slen > 0) {
            fprintf(out, " db \"%.*s\", 0\n", slen, sdata);
        } else if (slen == 0) {
            fprintf(out, " db 0\n");
        } else {
            fprintf(out, " db ");
            int in_str = 0;
            for (j = 0; j < slen; j++) {
                unsigned char ch = (unsigned char)sdata[j];
                if (ch >= 32 && ch <= 126 && ch != '"' && ch != '\\') {
                    if (!in_str) {
                        if (j > 0) fprintf(out, ", ");
                        fprintf(out, "\"");
                        in_str = 1;
                    }
                    fprintf(out, "%c", ch);
                } else {
                    if (in_str) {
                        fprintf(out, "\"");
                        in_str = 0;
                    }
                    if (j > 0 || in_str) fprintf(out, ", ");
                    fprintf(out, "%u", (unsigned int)ch);
                }
            }
            if (in_str) fprintf(out, "\"");
            fprintf(out, ", 0\n");
        }

        fprintf(out, "%s_LEN equ %d\n", lbl, slen);
    }

    emit_blank();

    /* ---- Global variables ---- */
    for (i = 0; i < num_globals; i++) {
        fprintf(out, "%s dd 0\n", globals[i].asm_name);
    }
    if (num_globals > 0) emit_blank();

    /* ---- VTable globals ---- */
    for (i = 0; i < num_vtable_globals; i++) {
        fprintf(out, "%s dd 0\n", vtable_globals[i].label);
    }
    if (num_vtable_globals > 0) emit_blank();

    /* ---- Method name strings ---- */
    for (i = 0; i < num_mn_strings; i++) {
        fprintf(out, "%s db \"%s\", 0\n",
                mn_strings[i].label, mn_strings[i].str_data);
    }
    if (num_mn_strings > 0) emit_blank();
}

/* ================================================================= */
/* emit_code_section                                                  */
/* ================================================================= */

void CodeGenerator386::emit_code_section()
{
    fprintf(out, ".CODE\n");
    emit_blank();

    emit_extern_declarations();
    emit_blank();

    IRFunc *f;
    for (f = mod->functions; f; f = f->next) {
        /* Skip init_func here; it is emitted separately below */
        if (mod->init_func && f == mod->init_func) continue;
        emit_func(f);
        emit_blank();
    }

    if (mod->init_func) {
        emit_func(mod->init_func);
        emit_blank();
    }

    /* ---- main entry point (386 protected mode, only for main module) ----
     * 32-bit clib3s startup calls "main" (no underscore),
     * unlike 16-bit clibl which calls "main_" (cdecl *_ naming). */
    if (is_main_module) {
        char init_label[128];
        func_label(init_label, sizeof(init_label), "__init__");

        fprintf(out, "PUBLIC main\n");
        fprintf(out, "main PROC NEAR\n");
        emit_line("push ebp");
        emit_line("mov  ebp, esp");
        emit_blank();

        emit_comment("init runtime");
        emit_line("call pydos_rt_init_");
        emit_blank();

        emit_comment("call __init__");
        emit_line("call %s", init_label);
        emit_blank();

        /* Call user-specified entry function (--entry) */
        if (has_main_func && entry_func) {
            char entry_label[128];
            func_label(entry_label, sizeof(entry_label), entry_func);
            emit_comment("call entry: %s()", entry_func);
            emit_line("call %s", entry_label);
            emit_blank();
        }

        emit_comment("shutdown runtime");
        emit_line("call pydos_rt_shutdown_");
        emit_blank();

        emit_comment("exit to DOS");
        emit_line("mov  eax, 4C00h");
        emit_line("int  21h");

        fprintf(out, "main ENDP\n");
    }
}

/* ================================================================= */
/* emit_extern_declarations                                           */
/* ================================================================= */

void CodeGenerator386::emit_extern_declarations()
{
    int i;
    for (i = 0; i < num_externs; i++) {
        fprintf(out, "EXTRN %s:NEAR\n", externs[i]);
    }
    /* Data externs: in flat model, NEAR works for both code and data */
    for (i = 0; i < num_data_externs; i++) {
        fprintf(out, "EXTRN %s:NEAR\n", data_externs[i]);
    }
}

/* ================================================================= */
/* emit_func                                                          */
/* ================================================================= */

void CodeGenerator386::emit_func(IRFunc *func)
{
    current_func = func;
    current_alloc = reg_allocator.allocate(func);

    char flbl[128];
    func_label(flbl, sizeof(flbl), func->name);

    fprintf(out, "PUBLIC %s\n", flbl);
    fprintf(out, "%s PROC NEAR\n", flbl);

    emit_prologue(func);

    IRInstr *ip;
    for (ip = func->first; ip; ip = ip->next) {
        emit_instr(ip);
    }

    fprintf(out, "_%s_epilogue:\n", func->name);
    emit_epilogue(func);

    fprintf(out, "%s ENDP\n", flbl);

    if (current_alloc) {
        if (current_alloc->ranges) {
            free(current_alloc->ranges);
        }
        free(current_alloc);
        current_alloc = 0;
    }
    current_func = 0;
}

/* ================================================================= */
/* emit_prologue                                                      */
/* ================================================================= */

void CodeGenerator386::emit_prologue(IRFunc *func)
{
    emit_line("push ebp");
    emit_line("mov  ebp, esp");
    emit_line("push esi");
    emit_line("push edi");

    int total_slots = func->num_locals + func->num_temps;
    int frame_size = total_slots * 4;

    if (frame_size > 0) {
        emit_line("sub  esp, %d", frame_size);

        /* Zero-initialize all local/temp slots using rep stosd */
        emit_comment("zero-init locals and temps");
        emit_line("push es");
        emit_line("push ds");
        emit_line("pop  es");
        emit_line("lea  edi, [ebp%+d]", slot_offset(total_slots - 1));
        emit_line("mov  ecx, %d", total_slots);
        emit_line("xor  eax, eax");
        emit_line("cld");
        emit_line("rep  stosd");
        emit_line("pop  es");
    }

    /* Copy parameters from [EBP+8+p*4] into local slots */
    if (func->num_params > 0) {
        emit_comment("copy parameters to local slots");
        int p;
        for (p = 0; p < func->num_params; p++) {
            int src = PARAM_BASE_386 + p * 4;
            int dst = slot_offset(p);
            emit_line("mov  eax, dword ptr [ebp+%d]", src);
            emit_line("mov  dword ptr [ebp%+d], eax", dst);
        }
    }

    emit_blank();
}

/* ================================================================= */
/* emit_epilogue                                                      */
/* ================================================================= */

void CodeGenerator386::emit_epilogue(IRFunc *func)
{
    int total_slots = func->num_locals + func->num_temps;
    int frame_size = total_slots * 4;

    if (frame_size > 0) {
        emit_line("add  esp, %d", frame_size);
    }
    emit_line("pop  edi");
    emit_line("pop  esi");
    emit_line("mov  esp, ebp");
    emit_line("pop  ebp");
    emit_line("ret");
}

/* ================================================================= */
/* emit_const_int                                                     */
/* ================================================================= */

void CodeGenerator386::emit_const_int(IRInstr *instr)
{
    int ci = instr->src1;
    long val = 0;
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_INT) {
        val = mod->constants[ci].int_val;
    }

    emit_comment("CONST_INT %ld -> t%d", val, instr->dest);

    /* Push 32-bit int value directly */
    emit_line("push %ld", val);

    require_extern("pydos_obj_new_int_");
    emit_line("call pydos_obj_new_int_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_str                                                     */
/* ================================================================= */

void CodeGenerator386::emit_const_str(IRInstr *instr)
{
    int ci = instr->src1;
    int slen = 0;
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_STR) {
        slen = mod->constants[ci].str_val.len;
    }

    char lbl[32];
    const_label(lbl, sizeof(lbl), ci);

    emit_comment("CONST_STR \"%s\" -> t%d",
                 (ci >= 0 && ci < mod->num_constants) ?
                 mod->constants[ci].str_val.data : "?", instr->dest);

    /* Push length, then near pointer to string data */
    emit_line("push %d", slen);
    emit_line("push offset %s", lbl);

    require_extern("pydos_obj_new_str_");
    emit_line("call pydos_obj_new_str_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_float                                                   */
/* ================================================================= */

void CodeGenerator386::emit_const_float(IRInstr *instr)
{
    int ci = instr->src1;
    double val = 0.0;
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_FLOAT) {
        val = mod->constants[ci].float_val;
    }

    emit_comment("CONST_FLOAT %g -> t%d", val, instr->dest);

    /* Push the 8 bytes of the double as 2 dwords (high then low) */
    unsigned char *bytes = (unsigned char *)&val;
    unsigned long hi_dw = ((unsigned long)bytes[7] << 24) |
                          ((unsigned long)bytes[6] << 16) |
                          ((unsigned long)bytes[5] << 8) |
                          (unsigned long)bytes[4];
    unsigned long lo_dw = ((unsigned long)bytes[3] << 24) |
                          ((unsigned long)bytes[2] << 16) |
                          ((unsigned long)bytes[1] << 8) |
                          (unsigned long)bytes[0];

    emit_line("push 0%lXh", hi_dw & 0xFFFFFFFFUL);
    emit_line("push 0%lXh", lo_dw & 0xFFFFFFFFUL);

    require_extern("pydos_obj_new_float_");
    emit_line("call pydos_obj_new_float_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_none                                                    */
/* ================================================================= */

void CodeGenerator386::emit_const_none(IRInstr *instr)
{
    emit_comment("CONST_NONE -> t%d", instr->dest);

    require_extern("pydos_obj_new_none_");
    emit_line("call pydos_obj_new_none_");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_bool                                                    */
/* ================================================================= */

void CodeGenerator386::emit_const_bool(IRInstr *instr)
{
    emit_comment("CONST_BOOL %d -> t%d", instr->src1, instr->dest);

    emit_line("push %d", instr->src1);

    require_extern("pydos_obj_new_bool_");
    emit_line("call pydos_obj_new_bool_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_load_local                                                    */
/* ================================================================= */

void CodeGenerator386::emit_load_local(IRInstr *instr)
{
    int local_slot = instr->src1;

    emit_comment("LOAD_LOCAL local[%d] -> t%d", local_slot, instr->dest);

    int src = slot_offset(local_slot);
    int dst_slot = current_func->num_locals + instr->dest;
    int dst = slot_offset(dst_slot);

    emit_line("mov  eax, dword ptr [ebp%+d]", src);
    emit_line("mov  dword ptr [ebp%+d], eax", dst);
}

/* ================================================================= */
/* emit_store_local                                                   */
/* ================================================================= */

void CodeGenerator386::emit_store_local(IRInstr *instr)
{
    int local_slot = instr->dest;

    emit_comment("STORE_LOCAL t%d -> local[%d]", instr->src1, local_slot);

    int src_slot = current_func->num_locals + instr->src1;
    int src = slot_offset(src_slot);
    int dst = slot_offset(local_slot);

    emit_line("mov  eax, dword ptr [ebp%+d]", src);
    emit_line("mov  dword ptr [ebp%+d], eax", dst);
}

/* ================================================================= */
/* emit_load_global                                                   */
/* ================================================================= */

void CodeGenerator386::emit_load_global(IRInstr *instr)
{
    int ci = instr->src1;
    if (ci < 0 || ci >= mod->num_constants ||
        mod->constants[ci].kind != IRConst::CONST_STR) {
        emit_comment("LOAD_GLOBAL: bad constant index %d", ci);
        return;
    }

    const char *gname = mod->constants[ci].str_val.data;

    if (builtin_asm_name(gname)) {
        emit_comment("LOAD_GLOBAL (builtin) '%s' -> t%d (skip)", gname, instr->dest);
        return;
    }

    if (strncmp(gname, "pydos_", 6) == 0) {
        emit_comment("LOAD_GLOBAL (runtime) '%s' -> t%d (skip)", gname, instr->dest);
        return;
    }

    const char *aname = global_asm_name(gname);
    emit_comment("LOAD_GLOBAL '%s' -> t%d", gname, instr->dest);

    int dst_slot = current_func->num_locals + instr->dest;
    int dst = slot_offset(dst_slot);

    /* Load 4-byte near pointer from data segment global */
    emit_line("mov  eax, dword ptr [%s]", aname);
    emit_line("mov  dword ptr [ebp%+d], eax", dst);
}

/* ================================================================= */
/* emit_store_global                                                  */
/* ================================================================= */

void CodeGenerator386::emit_store_global(IRInstr *instr)
{
    int ci = instr->dest;
    if (ci < 0 || ci >= mod->num_constants ||
        mod->constants[ci].kind != IRConst::CONST_STR) {
        emit_comment("STORE_GLOBAL: bad constant index %d", ci);
        return;
    }

    const char *gname = mod->constants[ci].str_val.data;
    const char *aname = global_asm_name(gname);

    emit_comment("STORE_GLOBAL t%d -> '%s'", instr->src1, gname);

    int src_slot = current_func->num_locals + instr->src1;
    int src = slot_offset(src_slot);

    /* Store 4-byte near pointer to data segment global */
    emit_line("mov  eax, dword ptr [ebp%+d]", src);
    emit_line("mov  dword ptr [%s], eax", aname);
}

/* ================================================================= */
/* emit_arithmetic                                                    */
/* ================================================================= */

void CodeGenerator386::emit_arithmetic(IRInstr *instr)
{
    emit_comment("ARITH %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    const char *func_name = 0;

    if (instr->op == IR_ADD && instr->type_hint &&
        instr->type_hint->kind == TY_STR) {
        func_name = "pydos_str_concat_";
    }
    else if (!instr->type_hint || instr->type_hint->kind == TY_ANY ||
             instr->type_hint->kind == TY_CLASS ||
             instr->type_hint->kind == TY_GENERIC_INST ||
             instr->type_hint->kind == TY_ERROR ||
             instr->type_hint->kind == TY_FLOAT ||
             instr->type_hint->kind == TY_COMPLEX) {
        switch (instr->op) {
            case IR_ADD: func_name = "pydos_obj_add_"; break;
            case IR_SUB: func_name = "pydos_obj_sub_"; break;
            case IR_MUL: func_name = "pydos_obj_mul_"; break;
            default: func_name = runtime_arith_func(instr->op); break;
        }
    }
    else {
        func_name = runtime_arith_func(instr->op);
    }

    require_extern(func_name);

    /* Push src2 then src1 (right-to-left) */
    push_temp(instr->src2);
    push_temp(instr->src1);

    emit_line("call %s", func_name);
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_matmul                                                        */
/* ================================================================= */
void CodeGenerator386::emit_matmul(IRInstr *instr)
{
    emit_comment("MATMUL t%d, t%d -> t%d", instr->src1, instr->src2, instr->dest);

    require_extern("pydos_obj_matmul_");

    push_temp(instr->src2);
    push_temp(instr->src1);

    emit_line("call pydos_obj_matmul_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_inplace                                                       */
/* ================================================================= */
void CodeGenerator386::emit_inplace(IRInstr *instr)
{
    emit_comment("INPLACE op=%d t%d, t%d -> t%d",
                 instr->extra, instr->src1, instr->src2, instr->dest);

    require_extern("pydos_obj_inplace_");

    emit_line("push %d", instr->extra);
    push_temp(instr->src2);
    push_temp(instr->src1);

    emit_line("call pydos_obj_inplace_");
    emit_line("add  esp, 12");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_bitwise                                                       */
/* ================================================================= */

void CodeGenerator386::emit_bitwise(IRInstr *instr)
{
    const char *func_name = runtime_bitwise_func(instr->op);

    emit_comment("BITWISE %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    require_extern(func_name);

    push_temp(instr->src2);
    push_temp(instr->src1);

    emit_line("call %s", func_name);
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_unary                                                         */
/* ================================================================= */

void CodeGenerator386::emit_unary(IRInstr *instr)
{
    emit_comment("UNARY %s t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->dest);

    switch (instr->op) {
    case IR_POS: {
        const char *func_name = runtime_unary_func(instr->op);
        require_extern(func_name);

        push_temp(instr->src1);
        emit_line("call %s", func_name);
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        break;
    }

    case IR_NOT: {
        require_extern("pydos_obj_is_truthy_");
        require_extern("pydos_obj_new_bool_");

        push_temp(instr->src1);
        emit_line("call pydos_obj_is_truthy_");
        emit_line("add  esp, 4");

        /* EAX = 0 or 1; XOR with 1 to negate */
        emit_line("xor  eax, 1");
        emit_line("push eax");

        emit_line("call pydos_obj_new_bool_");
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        break;
    }

    case IR_NEG:
    case IR_BITNOT: {
        const char *func_name = runtime_unary_func(instr->op);
        require_extern(func_name);

        push_temp(instr->src1);
        emit_line("call %s", func_name);
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        break;
    }

    default:
        emit_comment("UNIMPLEMENTED unary op: %s", irop_name(instr->op));
        break;
    }
}

/* ================================================================= */
/* emit_comparison                                                    */
/* ================================================================= */

void CodeGenerator386::emit_comparison(IRInstr *instr)
{
    emit_comment("CMP %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    switch (instr->op) {

    case IR_IS:
    case IR_IS_NOT: {
        require_extern("pydos_obj_new_bool_");

        int s1_slot = current_func->num_locals + instr->src1;
        int s2_slot = current_func->num_locals + instr->src2;
        int lbl_false = new_label();
        int lbl_end = new_label();

        /* Single dword comparison (near pointer) */
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(s1_slot));
        emit_line("cmp  eax, dword ptr [ebp%+d]", slot_offset(s2_slot));
        emit_line("jne  _L%d", lbl_false);

        /* Equal: IS => true(1), IS_NOT => false(0) */
        emit_line("push %d", (instr->op == IR_IS) ? 1 : 0);
        emit_line("jmp  _L%d", lbl_end);

        fprintf(out, "_L%d:\n", lbl_false);
        /* Not equal: IS => false(0), IS_NOT => true(1) */
        emit_line("push %d", (instr->op == IR_IS) ? 0 : 1);

        fprintf(out, "_L%d:\n", lbl_end);
        emit_line("call pydos_obj_new_bool_");
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        return;
    }

    case IR_IN:
    case IR_NOT_IN: {
        require_extern("pydos_obj_contains_");
        require_extern("pydos_obj_new_bool_");

        /* cdecl pushes right-to-left: push item first, then container,
         * so container ends up as arg1 and item as arg2:
         * pydos_obj_contains_(container, item) */
        push_temp(instr->src1);
        push_temp(instr->src2);
        emit_line("call pydos_obj_contains_");
        emit_line("add  esp, 8");

        if (instr->op == IR_NOT_IN) {
            emit_line("xor  eax, 1");
        }
        emit_line("push eax");
        emit_line("call pydos_obj_new_bool_");
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        return;
    }

    case IR_CMP_EQ:
    case IR_CMP_NE: {
        require_extern("pydos_obj_equal_");
        require_extern("pydos_obj_new_bool_");

        push_temp(instr->src2);
        push_temp(instr->src1);
        emit_line("call pydos_obj_equal_");
        emit_line("add  esp, 8");

        if (instr->op == IR_CMP_NE) {
            emit_line("xor  eax, 1");
        }
        emit_line("push eax");
        emit_line("call pydos_obj_new_bool_");
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        return;
    }

    default:
        break;
    }

    /* Ordered comparisons: LT, LE, GT, GE */
    if (instr->op == IR_CMP_LT || instr->op == IR_CMP_LE ||
        instr->op == IR_CMP_GT || instr->op == IR_CMP_GE) {
        require_extern("pydos_obj_compare_");
        require_extern("pydos_obj_new_bool_");

        push_temp(instr->src2);
        push_temp(instr->src1);
        emit_line("call pydos_obj_compare_");
        emit_line("add  esp, 8");

        int lbl_true = new_label();
        int lbl_end = new_label();

        emit_line("cmp  eax, 0");
        switch (instr->op) {
            case IR_CMP_LT: emit_line("jl   _L%d", lbl_true); break;
            case IR_CMP_LE: emit_line("jle  _L%d", lbl_true); break;
            case IR_CMP_GT: emit_line("jg   _L%d", lbl_true); break;
            case IR_CMP_GE: emit_line("jge  _L%d", lbl_true); break;
            default: break;
        }

        /* False path */
        emit_line("push 0");
        emit_line("jmp  _L%d", lbl_end);

        /* True path */
        fprintf(out, "_L%d:\n", lbl_true);
        emit_line("push 1");

        fprintf(out, "_L%d:\n", lbl_end);
        emit_line("call pydos_obj_new_bool_");
        emit_line("add  esp, 4");

        store_eax_to_temp(instr->dest);
        return;
    }
}

/* ================================================================= */
/* emit_jump                                                          */
/* ================================================================= */

void CodeGenerator386::emit_jump(IRInstr *instr)
{
    emit_line("jmp  _L%d", instr->extra);
}

/* ================================================================= */
/* emit_jump_cond                                                     */
/* ================================================================= */

void CodeGenerator386::emit_jump_cond(IRInstr *instr)
{
    emit_comment("JUMP_%s t%d -> _L%d",
                 instr->op == IR_JUMP_IF_TRUE ? "TRUE" : "FALSE",
                 instr->src1, instr->extra);

    require_extern("pydos_obj_is_truthy_");

    push_temp(instr->src1);
    emit_line("call pydos_obj_is_truthy_");
    emit_line("add  esp, 4");

    emit_line("test eax, eax");
    if (instr->op == IR_JUMP_IF_TRUE) {
        emit_line("jnz  _L%d", instr->extra);
    } else {
        emit_line("jz   _L%d", instr->extra);
    }
}

/* ================================================================= */
/* emit_ir_label                                                      */
/* ================================================================= */

void CodeGenerator386::emit_ir_label(IRInstr *instr)
{
    fprintf(out, "_L%d:\n", instr->extra);
}

/* ================================================================= */
/* emit_push_arg                                                      */
/* ================================================================= */

void CodeGenerator386::emit_push_arg(IRInstr *instr)
{
    if (num_call_args >= arg_temps_cap) grow_arg_temps();
    arg_temps[num_call_args++] = instr->src1;
}

/* ================================================================= */
/* emit_call                                                          */
/* ================================================================= */

void CodeGenerator386::emit_call(IRInstr *instr)
{
    int argc = instr->extra;

    emit_comment("CALL t%d(%d args) -> t%d", instr->src1, argc, instr->dest);

    /* Resolve callee name from preceding LOAD_GLOBAL */
    const char *callee_name = 0;
    int callee_ci = -1;
    IRInstr *scan;
    for (scan = instr->prev; scan; scan = scan->prev) {
        if (scan->op == IR_LOAD_GLOBAL && scan->dest == instr->src1) {
            callee_ci = scan->src1;
            if (callee_ci >= 0 && callee_ci < mod->num_constants &&
                mod->constants[callee_ci].kind == IRConst::CONST_STR) {
                callee_name = mod->constants[callee_ci].str_val.data;
            }
            break;
        }
    }

    const char *builtin_name = 0;
    if (callee_name) {
        builtin_name = builtin_asm_name(callee_name);
    }

    if (builtin_name) {
        /* ---- Builtin call: use argv array on stack ---- */
        emit_comment("builtin: %s (%d args)", callee_name, argc);
        require_extern(builtin_name);

        if (argc > 0) {
            emit_line("sub  esp, %d", argc * 4);
            emit_line("mov  esi, esp");

            int a;
            for (a = 0; a < argc && a < num_call_args; a++) {
                int arg_slot = current_func->num_locals + arg_temps[a];
                emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(arg_slot));
                emit_line("mov  dword ptr [esi+%d], eax", a * 4);
            }
        }

        /* cdecl: push args right-to-left.
         * Signature: func(int argc, PyDosObj *argv)
         * So push argv first (rightmost), then argc (leftmost). */

        /* Push near pointer to argv */
        if (argc > 0) {
            emit_line("push esi");
        } else {
            emit_line("push 0");
        }

        /* Push argc (4 bytes in 32-bit mode) */
        emit_line("push %d", argc);

        emit_line("call %s", builtin_name);
        /* Clean up: argv_ptr(4) + argc(4) + argv_array */
        emit_line("add  esp, %d", 8 + (argc > 0 ? argc * 4 : 0));

        store_eax_to_temp(instr->dest);

    } else if (callee_name && strncmp(callee_name, "pydos_", 6) == 0) {
        /* ---- Runtime helper: direct cdecl call ---- */
        char ename[128];
        int len = strlen(callee_name);
        if (len > 0 && callee_name[len-1] == '_') {
            strcpy(ename, callee_name);
        } else {
            sprintf(ename, "%s_", callee_name);
        }
        require_extern(codegen_strdup(ename));

        int a;
        for (a = num_call_args - 1; a >= 0; a--) {
            push_temp(arg_temps[a]);
        }
        emit_line("call %s", ename);
        if (argc > 0) {
            emit_line("add  esp, %d", argc * 4);
        }
        store_eax_to_temp(instr->dest);

    } else {
        /* ---- User function or indirect call ---- */
        int is_direct = 0;
        char flbl[128];

        if (callee_name) {
            IRFunc *f;
            for (f = mod->functions; f; f = f->next) {
                if (strcmp(f->name, callee_name) == 0) {
                    is_direct = 1;
                    func_label(flbl, sizeof(flbl), callee_name);
                    break;
                }
            }
            if (!is_direct && mod->init_func &&
                strcmp(mod->init_func->name, callee_name) == 0) {
                is_direct = 1;
                func_label(flbl, sizeof(flbl), callee_name);
            }
        }

        /* Push arguments right-to-left (4 bytes each, near pointers) */
        int a;
        for (a = num_call_args - 1; a >= 0; a--) {
            push_temp(arg_temps[a]);
        }

        if (is_direct) {
            emit_line("call %s", flbl);
        } else {
            /* Indirect call through function object or raw pointer.
             * Check if src1 came from IR_MAKE_FUNCTION -- extract code ptr. */
            int from_make_func = 0;
            IRInstr *sc2;
            for (sc2 = instr->prev; sc2; sc2 = sc2->prev) {
                if (sc2->dest == instr->src1) {
                    if (sc2->op == IR_MAKE_FUNCTION ||
                        sc2->op == IR_LOAD_LOCAL ||
                        sc2->op == IR_LOAD_ATTR ||
                        sc2->op == IR_LOAD_GLOBAL) {
                        from_make_func = 1;
                    }
                    break;
                }
            }

            if (from_make_func) {
                int clo_off = PYOBJ_V_OFFSET_32 + 8;
                int call_slot;
                /* Extract code pointer from func object:
                 * obj->v.func.code at PYOBJ_V_OFFSET + 0 */
                emit_comment("indirect call through function object");
                load_temp_to_eax(instr->src1);
                /* Load closure into pydos_active_closure before call */
                require_extern("pydos_active_closure_");
                emit_line("mov  ecx, dword ptr [eax+%d]", clo_off);
                emit_line("mov  dword ptr [pydos_active_closure_], ecx");
                /* Extract code pointer */
                emit_line("mov  eax, dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);
                /* Store extracted code ptr back to temp slot */
                call_slot = current_func->num_locals + instr->src1;
                emit_line("mov  dword ptr [ebp%+d], eax", slot_offset(call_slot));
            }

            int call_slot = current_func->num_locals + instr->src1;
            emit_line("call dword ptr [ebp%+d]", slot_offset(call_slot));
        }

        if (argc > 0) {
            emit_line("add  esp, %d", argc * 4);
        }

        store_eax_to_temp(instr->dest);
    }

    num_call_args = 0;
}

/* ================================================================= */
/* emit_call_method                                                   */
/* ================================================================= */

void CodeGenerator386::emit_call_method(IRInstr *instr)
{
    int argc = instr->extra;
    int name_ci = instr->src2;

    const char *method_name = "";
    if (name_ci >= 0 && name_ci < mod->num_constants &&
        mod->constants[name_ci].kind == IRConst::CONST_STR) {
        method_name = mod->constants[name_ci].str_val.data;
    }

    emit_comment("CALL_METHOD t%d.%s(%d args) -> t%d",
                 instr->src1, method_name, argc, instr->dest);

    /* ---- Registry-driven fast-path ---- */
    {
        const char *fast_asm = resolve_fast_method(instr, method_name, argc);
        if (fast_asm) {
            /* Direct call: push args right-to-left, push self, call asm_name.
             * All fast-path methods use cdecl(self, PyDosObj* arg0, arg1, ...) */
            int i;
            for (i = argc - 1; i >= 0; i--) {
                if (i < num_call_args)
                    push_temp(arg_temps[i]);
            }
            push_temp(instr->src1);   /* self */
            emit_line("call %s", fast_asm);
            emit_line("add  esp, %d", (argc + 1) * 4);
            store_eax_to_temp(instr->dest);
            num_call_args = 0;
            return;
        }
    }

    /* ---- Generic method call via pydos_obj_call_method_ ---- */
    require_extern("pydos_obj_call_method_");

    int total_argc = argc + 1;  /* include self */
    emit_line("sub  esp, %d", total_argc * 4);
    emit_line("mov  esi, esp");

    /* argv[0] = self */
    {
        int self_slot = current_func->num_locals + instr->src1;
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(self_slot));
        emit_line("mov  dword ptr [esi], eax");
    }

    /* argv[1..argc] = call args */
    {
        int a;
        for (a = 0; a < argc && a < num_call_args; a++) {
            int arg_slot = current_func->num_locals + arg_temps[a];
            emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(arg_slot));
            emit_line("mov  dword ptr [esi+%d], eax", (a + 1) * 4);
        }
    }

    /* Get method name label */
    char mn_lbl[32];
    {
        int mn_idx = register_mn_string(method_name);
        sprintf(mn_lbl, "_MN_%d", mn_idx);
    }

    /* cdecl right-to-left for:
     * pydos_obj_call_method(const char *name, unsigned int argc,
     *                       PyDosObj **argv)
     * Self is in argv[0], NOT a separate parameter. */

    /* Push argv near ptr — rightmost param */
    emit_line("push esi");

    /* Push argc */
    emit_line("push %d", total_argc);

    /* Push method name near ptr — leftmost param */
    emit_line("push offset %s", mn_lbl);

    emit_line("call pydos_obj_call_method_");
    /* Clean: name(4) + argc(4) + argv(4) + argv_array */
    emit_line("add  esp, %d", 12 + total_argc * 4);

    store_eax_to_temp(instr->dest);
    num_call_args = 0;
}

/* ================================================================= */
/* emit_return                                                        */
/* ================================================================= */

void CodeGenerator386::emit_return(IRInstr *instr)
{
    if (instr->src1 >= 0) {
        emit_comment("RETURN t%d", instr->src1);
        load_temp_to_eax(instr->src1);
    } else {
        emit_comment("RETURN void");
        emit_line("xor  eax, eax");
    }

    emit_line("jmp  _%s_epilogue", current_func->name);
}

/* ================================================================= */
/* emit_incref                                                        */
/* ================================================================= */

void CodeGenerator386::emit_incref(IRInstr *instr)
{
    emit_comment("INCREF t%d", instr->src1);
    require_extern("pydos_incref_");

    push_temp(instr->src1);
    emit_line("call pydos_incref_");
    emit_line("add  esp, 4");
}

/* ================================================================= */
/* emit_decref                                                        */
/* ================================================================= */

void CodeGenerator386::emit_decref(IRInstr *instr)
{
    emit_comment("DECREF t%d", instr->src1);
    require_extern("pydos_decref_");

    push_temp(instr->src1);
    emit_line("call pydos_decref_");
    emit_line("add  esp, 4");
}

/* ================================================================= */
/* emit_build_list                                                    */
/* ================================================================= */

void CodeGenerator386::emit_build_list(IRInstr *instr)
{
    int count = instr->extra;

    emit_comment("BUILD_LIST %d -> t%d", count, instr->dest);

    require_extern("pydos_list_new_");
    require_extern("pydos_list_append_");

    emit_line("push %d", count);
    emit_line("call pydos_list_new_");
    emit_line("add  esp, 4");
    store_eax_to_temp(instr->dest);

    int a;
    for (a = 0; a < count && a < num_call_args; a++) {
        push_temp(arg_temps[a]);
        push_temp(instr->dest);

        emit_line("call pydos_list_append_");
        emit_line("add  esp, 8");
    }

    num_call_args = 0;
}

/* ================================================================= */
/* emit_build_dict                                                    */
/* ================================================================= */

void CodeGenerator386::emit_build_dict(IRInstr *instr)
{
    int count = instr->extra;

    emit_comment("BUILD_DICT %d -> t%d", count, instr->dest);

    require_extern("pydos_dict_new_");
    require_extern("pydos_dict_set_");

    emit_line("push %d", count > 0 ? count : 8);
    emit_line("call pydos_dict_new_");
    emit_line("add  esp, 4");
    store_eax_to_temp(instr->dest);

    int a;
    for (a = 0; a < count; a++) {
        int key_idx = a * 2;
        int val_idx = a * 2 + 1;

        if (key_idx >= num_call_args || val_idx >= num_call_args) break;

        push_temp(arg_temps[val_idx]);
        push_temp(arg_temps[key_idx]);
        push_temp(instr->dest);

        emit_line("call pydos_dict_set_");
        emit_line("add  esp, 12");
    }

    num_call_args = 0;
}

/* ================================================================= */
/* emit_build_tuple                                                   */
/* ================================================================= */

void CodeGenerator386::emit_build_tuple(IRInstr *instr)
{
    int count = instr->extra;

    emit_comment("BUILD_TUPLE %d -> t%d", count, instr->dest);

    require_extern("pydos_list_new_");
    require_extern("pydos_list_append_");

    emit_line("push %d", count);
    emit_line("call pydos_list_new_");
    emit_line("add  esp, 4");
    store_eax_to_temp(instr->dest);

    int a;
    for (a = 0; a < count && a < num_call_args; a++) {
        push_temp(arg_temps[a]);
        push_temp(instr->dest);

        emit_line("call pydos_list_append_");
        emit_line("add  esp, 8");
    }

    /* Change type tag from PYDT_LIST to PYDT_TUPLE */
    load_temp_to_eax(instr->dest);
    emit_line("mov  byte ptr [eax], 7");  /* PYDT_TUPLE */

    num_call_args = 0;
}

/* ================================================================= */
/* emit_build_set                                                     */
/* ================================================================= */

void CodeGenerator386::emit_build_set(IRInstr *instr)
{
    int count = instr->extra;

    emit_comment("BUILD_SET %d -> t%d", count, instr->dest);

    require_extern("pydos_dict_new_");
    require_extern("pydos_dict_set_");
    require_extern("pydos_obj_new_none_");

    /* Create new empty dict (used as set) */
    emit_line("push %d", count > 0 ? count : 8);
    emit_line("call pydos_dict_new_");
    emit_line("add  esp, 4");
    store_eax_to_temp(instr->dest);

    /* For each element, add to dict with None value */
    int a;
    for (a = 0; a < count && a < num_call_args; a++) {
        /* Get None as value */
        emit_line("call pydos_obj_new_none_");
        emit_line("push eax");

        /* Push key */
        push_temp(arg_temps[a]);
        /* Push dict */
        push_temp(instr->dest);

        emit_line("call pydos_dict_set_");
        emit_line("add  esp, 12");
    }

    /* Mark as set type: change type tag to PYDT_SET (8) */
    load_temp_to_eax(instr->dest);
    emit_line("mov  byte ptr [eax], 8");

    num_call_args = 0;
}

/* ================================================================= */
/* emit_get_iter                                                      */
/* ================================================================= */

void CodeGenerator386::emit_get_iter(IRInstr *instr)
{
    emit_comment("GET_ITER t%d -> t%d", instr->src1, instr->dest);
    require_extern("pydos_obj_get_iter_");

    push_temp(instr->src1);
    emit_line("call pydos_obj_get_iter_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_for_iter                                                      */
/* ================================================================= */

void CodeGenerator386::emit_for_iter(IRInstr *instr)
{
    emit_comment("FOR_ITER t%d -> t%d, end=_L%d", instr->src1, instr->dest, instr->extra);
    require_extern("pydos_obj_iter_next_");

    push_temp(instr->src1);
    emit_line("call pydos_obj_iter_next_");
    emit_line("add  esp, 4");

    /* Check if result is NULL (EAX == 0) */
    store_eax_to_temp(instr->dest);
    emit_line("test eax, eax");
    emit_line("jz   _L%d", instr->extra);
}

/* ================================================================= */
/* emit_load_attr                                                     */
/* ================================================================= */

void CodeGenerator386::emit_load_attr(IRInstr *instr)
{
    int ci = instr->src2;
    const char *attr_name = "";
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_STR) {
        attr_name = mod->constants[ci].str_val.data;
    }

    emit_comment("LOAD_ATTR t%d.%s -> t%d", instr->src1, attr_name, instr->dest);
    require_extern("pydos_obj_get_attr_");

    char lbl[32];
    const_label(lbl, sizeof(lbl), ci);

    /* Push near pointer to attr name string */
    emit_line("push offset %s", lbl);
    /* Push object */
    push_temp(instr->src1);

    emit_line("call pydos_obj_get_attr_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_store_attr                                                    */
/* ================================================================= */

void CodeGenerator386::emit_store_attr(IRInstr *instr)
{
    int ci = instr->dest;
    const char *attr_name = "";
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_STR) {
        attr_name = mod->constants[ci].str_val.data;
    }

    emit_comment("STORE_ATTR t%d.%s = t%d", instr->src1, attr_name, instr->src2);
    require_extern("pydos_obj_set_attr_");

    char lbl[32];
    const_label(lbl, sizeof(lbl), ci);

    /* Push value */
    push_temp(instr->src2);
    /* Push near pointer to attr name string */
    emit_line("push offset %s", lbl);
    /* Push object */
    push_temp(instr->src1);

    emit_line("call pydos_obj_set_attr_");
    emit_line("add  esp, 12");
}

/* ================================================================= */
/* emit_load_subscript                                                */
/* ================================================================= */

void CodeGenerator386::emit_load_subscript(IRInstr *instr)
{
    emit_comment("LOAD_SUBSCRIPT t%d[t%d] -> t%d", instr->src1, instr->src2, instr->dest);

    const char *sub_fn = resolve_subscript_func(instr->type_hint, 0);
    if (sub_fn && instr->type_hint &&
        (instr->type_hint->kind == TY_LIST || instr->type_hint->kind == TY_STR ||
         instr->type_hint->kind == TY_TUPLE)) {
        /* Integer-indexed types: unbox index, call fn(obj, long idx) */
        require_extern(sub_fn);

        int idx_slot = current_func->num_locals + instr->src2;
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(idx_slot));
        emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);

        push_temp(instr->src1);

        emit_line("call %s", sub_fn);
        emit_line("add  esp, 8");

        store_eax_to_temp(instr->dest);

    } else if (sub_fn && instr->type_hint &&
               instr->type_hint->kind == TY_DICT) {
        /* Dict: fn(dict, key_obj, default_obj) */
        require_extern(sub_fn);

        emit_line("push 0");
        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_line("call %s", sub_fn);
        emit_line("add  esp, 12");

        store_eax_to_temp(instr->dest);

    } else {
        require_extern("pydos_obj_getitem_");

        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_line("call pydos_obj_getitem_");
        emit_line("add  esp, 8");

        store_eax_to_temp(instr->dest);
    }
}

/* ================================================================= */
/* emit_load_slice                                                    */
/* ================================================================= */

void CodeGenerator386::emit_load_slice(IRInstr *instr)
{
    emit_comment("LOAD_SLICE t%d = t%d[start:stop:step]", instr->dest, instr->src1);

    const char *fn = resolve_slice_func(instr->type_hint);
    if (!fn) fn = "pydos_list_slice_";  /* fallback */
    require_extern(fn);

    /* Push step (arg 2) — unbox int from PyDosObj */
    {
        int step_slot = current_func->num_locals + arg_temps[2];
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(step_slot));
        emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);
    }

    /* Push stop (arg 1) — unbox int */
    {
        int stop_slot = current_func->num_locals + arg_temps[1];
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(stop_slot));
        emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);
    }

    /* Push start (arg 0) — unbox int */
    {
        int start_slot = current_func->num_locals + arg_temps[0];
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(start_slot));
        emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);
    }

    /* Push object */
    push_temp(instr->src1);

    emit_line("call %s", fn);
    emit_line("add  esp, 16");    /* 4 args: obj(4) + start(4) + stop(4) + step(4) = 16 */

    store_eax_to_temp(instr->dest);
    num_call_args = 0;
}

/* ================================================================= */
/* emit_exc_match                                                     */
/* ================================================================= */

void CodeGenerator386::emit_exc_match(IRInstr *instr)
{
    emit_comment("EXC_MATCH t%d = t%d matches type %d", instr->dest, instr->src1, instr->src2);
    require_extern("pydos_exc_matches_");
    require_extern("pydos_obj_new_bool_");

    /* Push type_code (plain int, 4 bytes on 386) */
    emit_line("push %d", instr->src2);

    /* Push exception object */
    push_temp(instr->src1);

    emit_line("call pydos_exc_matches_");
    emit_line("add  esp, 8");     /* obj (4) + int (4) */

    /* EAX = 0 or 1 */
    emit_line("push eax");
    emit_line("call pydos_obj_new_bool_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_store_subscript                                               */
/* ================================================================= */

void CodeGenerator386::emit_store_subscript(IRInstr *instr)
{
    emit_comment("STORE_SUBSCRIPT t%d[t%d] = t%d", instr->src1, instr->src2, instr->dest);

    const char *set_fn = resolve_subscript_func(instr->type_hint, 1);
    if (set_fn && instr->type_hint &&
        (instr->type_hint->kind == TY_LIST || instr->type_hint->kind == TY_TUPLE)) {
        /* Integer-indexed set: fn(obj, long idx, value) */
        require_extern(set_fn);

        push_temp(instr->dest);

        int idx_slot = current_func->num_locals + instr->src2;
        emit_line("mov  eax, dword ptr [ebp%+d]", slot_offset(idx_slot));
        emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);

        push_temp(instr->src1);

        emit_line("call %s", set_fn);
        emit_line("add  esp, 12");

    } else if (set_fn && instr->type_hint &&
               instr->type_hint->kind == TY_DICT) {
        /* Dict set: fn(dict, key_obj, value_obj) */
        require_extern(set_fn);

        push_temp(instr->dest);
        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_line("call %s", set_fn);
        emit_line("add  esp, 12");

    } else {
        require_extern("pydos_obj_setitem_");

        push_temp(instr->dest);
        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_line("call pydos_obj_setitem_");
        emit_line("add  esp, 12");
    }
}

/* ================================================================= */
/* emit_del_subscript                                                 */
/* ================================================================= */

void CodeGenerator386::emit_del_subscript(IRInstr *instr)
{
    emit_comment("DEL_SUBSCRIPT del t%d[t%d]", instr->src1, instr->src2);
    require_extern("pydos_obj_delitem_");
    push_temp(instr->src2);   /* key */
    push_temp(instr->src1);   /* obj */
    emit_line("call pydos_obj_delitem_");
    emit_line("add  esp, 8");
}

/* ================================================================= */
/* emit_del_local                                                     */
/* ================================================================= */

void CodeGenerator386::emit_del_local(IRInstr *instr)
{
    /* IR_DEL_LOCAL: DECREF local[src1], then zero the slot.
     * src1 = local slot index. */
    int slot = instr->src1;
    int off = slot_offset(slot);

    emit_comment("DEL_LOCAL slot=%d", slot);
    require_extern("pydos_decref_");

    /* Push old pointer value for DECREF */
    emit_line("push dword ptr [ebp%+d]", off);
    emit_line("call pydos_decref_");
    emit_line("add  esp, 4");

    /* Zero the slot */
    emit_line("mov  dword ptr [ebp%+d], 0", off);
}

/* ================================================================= */
/* emit_del_global                                                    */
/* ================================================================= */

void CodeGenerator386::emit_del_global(IRInstr *instr)
{
    /* IR_DEL_GLOBAL: DECREF global[name], then zero it.
     * dest = constant pool index for the name string. */
    int ci = instr->dest;
    if (ci < 0 || ci >= mod->num_constants ||
        mod->constants[ci].kind != IRConst::CONST_STR) {
        emit_comment("DEL_GLOBAL: bad constant index %d", ci);
        return;
    }

    const char *gname = mod->constants[ci].str_val.data;
    const char *aname = global_asm_name(gname);

    emit_comment("DEL_GLOBAL '%s'", gname);
    require_extern("pydos_decref_");

    /* Push old pointer value for DECREF */
    emit_line("push dword ptr [%s]", aname);
    emit_line("call pydos_decref_");
    emit_line("add  esp, 4");

    /* Zero the global */
    emit_line("mov  dword ptr [%s], 0", aname);
}

/* ================================================================= */
/* emit_del_attr                                                      */
/* ================================================================= */

void CodeGenerator386::emit_del_attr(IRInstr *instr)
{
    /* IR_DEL_ATTR: del src1.attr  (dest = attr name const index) */
    int ci = instr->dest;
    const char *attr_name = "";
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_STR) {
        attr_name = mod->constants[ci].str_val.data;
    }

    emit_comment("DEL_ATTR t%d.%s", instr->src1, attr_name);
    require_extern("pydos_obj_del_attr_");

    char lbl[32];
    const_label(lbl, sizeof(lbl), ci);

    /* Push near pointer to attr name string */
    emit_line("push offset %s", lbl);
    /* Push object */
    push_temp(instr->src1);

    emit_line("call pydos_obj_del_attr_");
    emit_line("add  esp, 8");
}

/* ================================================================= */
/* emit_setup_try                                                     */
/* ================================================================= */

void CodeGenerator386::emit_setup_try(IRInstr *instr)
{
    emit_comment("SETUP_TRY handler=_L%d", instr->extra);

    require_extern("pydos_exc_alloc_frame_");
    require_extern("_setjmp");

    /* Allocate exception frame -- returns near pointer in EAX */
    emit_line("call pydos_exc_alloc_frame_");

    /* Call _setjmp(jmp_buf *) — 32-bit clib3s uses "_setjmp"
     * (no trailing underscore, unlike 16-bit cdecl "*_" naming) */
    emit_line("push eax");
    emit_line("call _setjmp");
    emit_line("add  esp, 4");

    /* EAX = 0 on initial call, non-zero on longjmp (exception) */
    emit_line("test eax, eax");
    emit_line("jnz  _L%d", instr->extra);
}

/* ================================================================= */
/* emit_pop_try                                                       */
/* ================================================================= */

void CodeGenerator386::emit_pop_try(IRInstr *instr)
{
    emit_comment("POP_TRY");
    require_extern("pydos_exc_pop_");

    emit_line("call pydos_exc_pop_");
}

/* ================================================================= */
/* emit_raise                                                         */
/* ================================================================= */

void CodeGenerator386::emit_raise(IRInstr *instr)
{
    emit_comment("RAISE t%d", instr->src1);
    require_extern("pydos_exc_raise_obj_");

    push_temp(instr->src1);
    emit_line("call pydos_exc_raise_obj_");
    emit_line("add  esp, 4");
}

/* ================================================================= */
/* emit_reraise                                                       */
/* ================================================================= */

void CodeGenerator386::emit_reraise(IRInstr *instr)
{
    emit_comment("RERAISE");
    require_extern("pydos_exc_current_");
    require_extern("pydos_exc_raise_obj_");

    /* Get current exception object into EAX */
    emit_line("call pydos_exc_current_");

    /* Push EAX and raise it */
    emit_line("push eax");
    emit_line("call pydos_exc_raise_obj_");
    emit_line("add  esp, 4");
}

/* ================================================================= */
/* emit_get_exception                                                 */
/* ================================================================= */

void CodeGenerator386::emit_get_exception(IRInstr *instr)
{
    emit_comment("GET_EXCEPTION -> t%d", instr->dest);
    require_extern("pydos_exc_current_");

    emit_line("call pydos_exc_current_");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_alloc_obj                                                     */
/* ================================================================= */

void CodeGenerator386::emit_alloc_obj(IRInstr *instr)
{
    emit_comment("ALLOC_OBJ -> t%d", instr->dest);
    require_extern("pydos_obj_alloc_");

    emit_line("call pydos_obj_alloc_");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_init_vtable                                                   */
/* ================================================================= */

void CodeGenerator386::emit_init_vtable(IRInstr *instr)
{
    int vt_idx = instr->extra;
    if (vt_idx < 0 || vt_idx >= mod->num_class_vtables) {
        emit_comment("INIT_VTABLE: bad vtable index %d", vt_idx);
        return;
    }

    ClassVTableInfo *vti = &mod->class_vtables[vt_idx];
    emit_comment("INIT_VTABLE for %s (%d methods)", vti->class_name, vti->num_methods);

    char vt_label[64];
    sprintf(vt_label, "_VT_%s", vti->class_name);

    /* Create the vtable: returns near pointer in EAX */
    require_extern("pydos_vtable_create_");
    emit_line("call pydos_vtable_create_");

    /* Store to vtable global (single dword) */
    emit_line("mov  dword ptr [%s], eax", vt_label);

    /* Set class name for repr fallback */
    {
        int cn_idx = register_mn_string(vti->class_name);
        char cn_lbl[32];
        sprintf(cn_lbl, "_MN_%d", cn_idx);

        require_extern("pydos_vtable_set_name_");
        emit_comment("  set class name '%s'", vti->class_name);

        /* Push near pointer to class name string */
        emit_line("push offset %s", cn_lbl);

        /* Push vtable near pointer */
        emit_line("push dword ptr [%s]", vt_label);

        emit_line("call pydos_vtable_set_name_");
        emit_line("add  esp, 8");
    }

    /* Add each method */
    require_extern("pydos_vtable_add_method_");

    int mi;
    for (mi = 0; mi < vti->num_methods; mi++) {
        const char *py_name = vti->methods[mi].python_name;
        const char *mangled = vti->methods[mi].mangled_name;
        int mn_idx = register_mn_string(py_name);
        char mn_lbl[32];
        sprintf(mn_lbl, "_MN_%d", mn_idx);

        char func_lbl[128];
        func_label(func_lbl, sizeof(func_lbl), mangled);

        if (!has_extern(func_lbl)) {
            require_extern(func_lbl);
        }

        emit_comment("  add method '%s' -> %s", py_name, func_lbl);

        /* Push near pointer to function implementation */
        emit_line("push offset %s", func_lbl);

        /* Push near pointer to method name string */
        emit_line("push offset %s", mn_lbl);

        /* Push vtable near pointer */
        emit_line("push dword ptr [%s]", vt_label);

        emit_line("call pydos_vtable_add_method_");
        emit_line("add  esp, 12");
    }

    /* Inherit from base class vtable(s) if applicable */
    if (vti->base_class_name && vti->base_class_name[0] != '\0') {
        require_extern("pydos_vtable_inherit_");

        char pvt_label[64];
        sprintf(pvt_label, "_VT_%s", vti->base_class_name);

        emit_comment("  inherit from %s", vti->base_class_name);

        /* Push parent vtable near pointer */
        emit_line("push dword ptr [%s]", pvt_label);

        /* Push child vtable near pointer */
        emit_line("push dword ptr [%s]", vt_label);

        emit_line("call pydos_vtable_inherit_");
        emit_line("add  esp, 8");

        /* Inherit from additional bases (multiple inheritance) */
        {
            int ebi;
            for (ebi = 0; ebi < vti->num_extra_bases; ebi++) {
                char eb_label[64];
                sprintf(eb_label, "_VT_%s", vti->extra_bases[ebi]);

                emit_comment("  inherit from %s", vti->extra_bases[ebi]);

                emit_line("push dword ptr [%s]", eb_label);
                emit_line("push dword ptr [%s]", vt_label);

                emit_line("call pydos_vtable_inherit_");
                emit_line("add  esp, 8");
            }
        }
    }
}

/* ================================================================= */
/* emit_set_vtable                                                    */
/* ================================================================= */

void CodeGenerator386::emit_set_vtable(IRInstr *instr)
{
    int vt_idx = instr->extra;
    if (vt_idx < 0 || vt_idx >= mod->num_class_vtables) {
        emit_comment("SET_VTABLE: bad vtable index %d", vt_idx);
        return;
    }

    ClassVTableInfo *vti = &mod->class_vtables[vt_idx];
    char vt_label[64];
    sprintf(vt_label, "_VT_%s", vti->class_name);

    emit_comment("SET_VTABLE t%d -> %s", instr->src1, vti->class_name);
    require_extern("pydos_obj_set_vtable_");

    /* Push vtable near pointer */
    emit_line("push dword ptr [%s]", vt_label);

    /* Push object near pointer */
    push_temp(instr->src1);

    emit_line("call pydos_obj_set_vtable_");
    emit_line("add  esp, 8");
}

/* ================================================================= */
/* emit_check_vtable                                                   */
/* CHECK_VTABLE: dest = isinstance_vtable(src1, _VT_ClassName)         */
/* ================================================================= */

void CodeGenerator386::emit_check_vtable(IRInstr *instr)
{
    int vt_idx = instr->extra;
    if (vt_idx < 0 || vt_idx >= mod->num_class_vtables) {
        emit_comment("CHECK_VTABLE: bad vtable index %d", vt_idx);
        /* Return False */
        require_extern("pydos_obj_new_bool_");
        emit_line("push 0");
        emit_line("call pydos_obj_new_bool_");
        emit_line("add  esp, 4");
        store_eax_to_temp(instr->dest);
        return;
    }

    ClassVTableInfo *vti = &mod->class_vtables[vt_idx];
    char vt_label[64];
    sprintf(vt_label, "_VT_%s", vti->class_name);

    emit_comment("CHECK_VTABLE t%d isinstance %s", instr->src1, vti->class_name);
    require_extern("pydos_obj_isinstance_vtable_");
    require_extern("pydos_obj_new_bool_");

    /* Push vtable near pointer */
    emit_line("push dword ptr [%s]", vt_label);

    /* Push object near pointer */
    push_temp(instr->src1);

    emit_line("call pydos_obj_isinstance_vtable_");
    emit_line("add  esp, 8");

    /* Result is int (0/1) in EAX; box it to a bool PyDosObj */
    emit_line("push eax");
    emit_line("call pydos_obj_new_bool_");
    emit_line("add  esp, 4");
    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_make_function                                                  */
/* ================================================================= */

void CodeGenerator386::emit_make_function(IRInstr *instr)
{
    int name_ci = instr->src1;
    const char *fname = "";
    if (name_ci >= 0 && name_ci < mod->num_constants &&
        mod->constants[name_ci].kind == IRConst::CONST_STR) {
        fname = mod->constants[name_ci].str_val.data;
    }

    char flbl[128];
    func_label(flbl, sizeof(flbl), fname);

    emit_comment("MAKE_FUNCTION %s -> t%d", fname, instr->dest);
    require_extern("pydos_func_new_");

    /* Push name string near pointer */
    {
        char slbl[64];
        const_label(slbl, sizeof(slbl), name_ci);
        emit_line("push offset %s", slbl);
    }

    /* Push code near pointer (address of the function label) */
    emit_line("push offset %s", flbl);

    emit_line("call pydos_func_new_");
    emit_line("add  esp, 8");

    /* If src2 != -1, store closure into func->closure */
    if (instr->src2 >= 0) {
        int clo_off = PYOBJ_V_OFFSET_32 + 8;
        emit_comment("store closure into func->closure");
        /* EAX = newly created func object */
        store_eax_to_temp(instr->dest);
        load_temp_to_eax(instr->dest);
        emit_line("mov  ecx, eax");  /* ECX = func obj */
        load_temp_to_eax(instr->src2);
        emit_line("mov  dword ptr [ecx+%d], eax", clo_off);
        /* INCREF the closure */
        require_extern("pydos_incref_");
        emit_line("push eax");
        emit_line("call pydos_incref_");
        emit_line("add  esp, 4");
        /* Result is already in dest temp */
        return;
    }

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_make_cell / emit_cell_get / emit_cell_set / emit_load_closure */
/* ================================================================= */

void CodeGenerator386::emit_make_cell(IRInstr *instr)
{
    emit_comment("MAKE_CELL -> t%d", instr->dest);
    emit_line("call pydos_cell_new_");
    store_eax_to_temp(instr->dest);
}

void CodeGenerator386::emit_cell_get(IRInstr *instr)
{
    emit_comment("CELL_GET t%d -> t%d", instr->src1, instr->dest);
    push_temp(instr->src1);
    emit_line("call pydos_cell_get_");
    emit_line("add  esp, 4");
    store_eax_to_temp(instr->dest);
}

void CodeGenerator386::emit_cell_set(IRInstr *instr)
{
    emit_comment("CELL_SET t%d, t%d", instr->src1, instr->src2);
    /* Push value (src2) then cell (src1) — right-to-left for stack convention */
    push_temp(instr->src2);
    push_temp(instr->src1);
    emit_line("call pydos_cell_set_");
    emit_line("add  esp, 8");
}

void CodeGenerator386::emit_load_closure(IRInstr *instr)
{
    emit_comment("LOAD_CLOSURE -> t%d", instr->dest);
    emit_line("mov  eax, dword ptr [pydos_active_closure_]");
    store_eax_to_temp(instr->dest);
}

void CodeGenerator386::emit_set_closure(IRInstr *instr)
{
    emit_comment("SET_CLOSURE t%d", instr->src1);
    load_temp_to_eax(instr->src1);
    emit_line("mov  dword ptr [pydos_active_closure_], eax");
}

/* ================================================================= */
/* emit_make_generator                                                 */
/* ================================================================= */

void CodeGenerator386::emit_make_generator(IRInstr *instr)
{
    int name_ci = instr->src1;
    const char *fname = "";
    if (name_ci >= 0 && name_ci < mod->num_constants &&
        mod->constants[name_ci].kind == IRConst::CONST_STR) {
        fname = mod->constants[name_ci].str_val.data;
    }

    char flbl[128];
    func_label(flbl, sizeof(flbl), fname);

    emit_comment("MAKE_GENERATOR %s (locals=%d) -> t%d", fname, instr->extra, instr->dest);
    require_extern("pydos_gen_new_");

    /* Push num_locals */
    emit_line("push %d", instr->extra);

    /* Push resume function near pointer */
    emit_line("push offset %s", flbl);

    emit_line("call pydos_gen_new_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_make_coroutine                                                 */
/* ================================================================= */

void CodeGenerator386::emit_make_coroutine(IRInstr *instr)
{
    /* IR_MAKE_COROUTINE: same as MAKE_GENERATOR but calls pydos_cor_new_ */
    int name_ci = instr->src1;
    const char *fname = "";
    if (name_ci >= 0 && name_ci < mod->num_constants &&
        mod->constants[name_ci].kind == IRConst::CONST_STR) {
        fname = mod->constants[name_ci].str_val.data;
    }

    char flbl[128];
    func_label(flbl, sizeof(flbl), fname);

    emit_comment("MAKE_COROUTINE %s (locals=%d) -> t%d", fname, instr->extra, instr->dest);
    require_extern("pydos_cor_new_");

    /* Push num_locals */
    emit_line("push %d", instr->extra);

    /* Push resume function near pointer */
    emit_line("push offset %s", flbl);

    emit_line("call pydos_cor_new_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_cor_set_result                                                 */
/* ================================================================= */

void CodeGenerator386::emit_cor_set_result(IRInstr *instr)
{
    /* IR_COR_SET_RESULT: cor->v.gen.state = src2
     * src1 = cor temp, src2 = value temp
     *
     * PyDosGen layout after PYOBJ_V_OFFSET_32 (8 for 32-bit):
     *   resume: 4 bytes (near ptr)  at +8
     *   state:  4 bytes (near ptr)  at +12  (PYOBJ_V_OFFSET_32 + 4)
     *
     * INCREF new value, DECREF old state. */
    int gen_temp = instr->src1;
    int vt = instr->src2;
    int state_off = PYOBJ_V_OFFSET_32 + 4;

    emit_comment("COR_SET_RESULT: t%d->state = t%d", gen_temp, vt);

    /* Load gen pointer into EAX, save old state for DECREF */
    load_temp_to_eax(gen_temp);
    emit_line("push dword ptr [eax+%d]", state_off);  /* old state */

    /* Load new value into ECX */
    int val_slot = current_func->num_locals + vt;
    emit_line("mov  ecx, dword ptr [ebp%+d]", slot_offset(val_slot));

    /* Reload gen (load above may have reused EAX) */
    load_temp_to_eax(gen_temp);

    /* Store new value */
    emit_line("mov  dword ptr [eax+%d], ecx", state_off);

    /* INCREF new value */
    emit_line("push ecx");
    emit_line("call pydos_incref_");
    emit_line("add  esp, 4");

    /* DECREF old state (still on stack from push above) */
    emit_line("call pydos_decref_");
    emit_line("add  esp, 4");
}

/* ================================================================= */
/* emit_gen_load_pc                                                    */
/* ================================================================= */

void CodeGenerator386::emit_gen_load_pc(IRInstr *instr)
{
    /* dest = gen->pc; src1 = gen temp
     * 32-bit: PyDosGen.pc at PYOBJ_V_OFFSET(8) + resume(4) + state(4) = 16
     * Box the result via pydos_obj_new_int_ for IR_CMP dispatch. */
    emit_comment("GEN_LOAD_PC t%d -> t%d", instr->src1, instr->dest);

    load_temp_to_eax(instr->src1);
    emit_line("mov  eax, dword ptr [eax+%d]", PYOBJ_V_OFFSET_32 + 8);

    /* Box the raw int */
    emit_line("push eax");
    emit_line("call pydos_obj_new_int_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_gen_set_pc                                                     */
/* ================================================================= */

void CodeGenerator386::emit_gen_set_pc(IRInstr *instr)
{
    /* gen->pc = extra; src1 = gen temp */
    emit_comment("GEN_SET_PC t%d = %d", instr->src1, instr->extra);

    load_temp_to_eax(instr->src1);
    emit_line("mov  dword ptr [eax+%d], %d", PYOBJ_V_OFFSET_32 + 8, instr->extra);
}

/* ================================================================= */
/* emit_gen_load_local                                                 */
/* ================================================================= */

void CodeGenerator386::emit_gen_load_local(IRInstr *instr)
{
    /* dest = gen->locals[extra]; src1 = gen temp, extra = index */
    emit_comment("GEN_LOAD_LOCAL t%d = gen_t%d.locals[%d]", instr->dest, instr->src1, instr->extra);
    require_extern("pydos_list_get_");

    /* Push index (long, 4 bytes) */
    emit_line("push %d", instr->extra);

    /* Push gen->locals (near pointer at PYOBJ_V_OFFSET + 12) */
    load_temp_to_eax(instr->src1);
    emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32 + 12);

    emit_line("call pydos_list_get_");
    emit_line("add  esp, 8");

    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_gen_save_local                                                 */
/* ================================================================= */

void CodeGenerator386::emit_gen_save_local(IRInstr *instr)
{
    /* gen->locals[extra] = src2; src1 = gen temp, extra = index */
    emit_comment("GEN_SAVE_LOCAL gen_t%d.locals[%d] = t%d", instr->src1, instr->extra, instr->src2);
    require_extern("pydos_list_set_");

    /* Push value (near ptr) */
    push_temp(instr->src2);

    /* Push index (long) */
    emit_line("push %d", instr->extra);

    /* Push gen->locals (near pointer) */
    load_temp_to_eax(instr->src1);
    emit_line("push dword ptr [eax+%d]", PYOBJ_V_OFFSET_32 + 12);

    emit_line("call pydos_list_set_");
    emit_line("add  esp, 12");
}

/* ================================================================= */
/* emit_gen_check_throw / emit_gen_get_sent                          */
/* ================================================================= */

void CodeGenerator386::emit_gen_check_throw(IRInstr *instr)
{
    /* call pydos_gen_check_throw_(gen) → returns int (0=ok, 1=throw)
     * If throw detected: return NULL from resume function via mini-epilogue.
     * Exception is raised by pydos_gen_throw() on the current C stack. */
    int lbl_ok = new_label();

    emit_comment("GEN_CHECK_THROW t%d", instr->src1);
    push_temp(instr->src1);
    emit_line("call pydos_gen_check_throw_");
    emit_line("add  esp, 4");
    emit_line("test eax, eax");
    emit_line("jz   _L%d", lbl_ok);

    /* Throw detected — return NULL from resume function.
     * Mini-epilogue: restore callee-saved regs via known EBP offsets,
     * then mov esp,ebp to reset stack.  Avoids 'lea esp,[ebp-N]'
     * which may have WASM encoding issues on some DOS environments. */
    emit_line("xor  eax, eax");
    emit_line("mov  edi, dword ptr [ebp-%d]", SAVE_AREA_386);
    emit_line("mov  esi, dword ptr [ebp-%d]", SAVE_AREA_386 - 4);
    emit_line("mov  esp, ebp");
    emit_line("pop  ebp");
    emit_line("ret");

    emit_local_label(lbl_ok);
}

void CodeGenerator386::emit_gen_get_sent(IRInstr *instr)
{
    /* dest = pydos_gen_sent_ global; transfer ownership (no clear needed,
     * the next send/next will overwrite it) */
    emit_comment("GEN_GET_SENT -> t%d", instr->dest);
    emit_line("mov  eax, dword ptr [pydos_gen_sent_]");
    store_eax_to_temp(instr->dest);
}

/* ================================================================= */
/* Typed i32 arithmetic (386: native 32-bit registers)               */
/* ================================================================= */

void CodeGenerator386::emit_arith_i32(IRInstr *instr)
{
    int s1_off = slot_offset(current_func->num_locals + instr->src1);
    int s2_off = instr->src2 >= 0 ? slot_offset(current_func->num_locals + instr->src2) : 0;
    int d_off  = slot_offset(current_func->num_locals + instr->dest);

    emit_comment("ARITH_I32 %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1,
                 instr->src2 >= 0 ? instr->src2 : 0, instr->dest);

    switch (instr->op) {
    case IR_ADD_I32:
        emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
        emit_line("add  eax, dword ptr [ebp%+d]", s2_off);
        emit_line("mov  dword ptr [ebp%+d], eax", d_off);
        break;

    case IR_SUB_I32:
        emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
        emit_line("sub  eax, dword ptr [ebp%+d]", s2_off);
        emit_line("mov  dword ptr [ebp%+d], eax", d_off);
        break;

    case IR_MUL_I32:
        emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
        emit_line("imul eax, dword ptr [ebp%+d]", s2_off);
        emit_line("mov  dword ptr [ebp%+d], eax", d_off);
        break;

    case IR_DIV_I32:
        emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
        emit_line("cdq");
        emit_line("idiv dword ptr [ebp%+d]", s2_off);
        emit_line("mov  dword ptr [ebp%+d], eax", d_off);
        break;

    case IR_MOD_I32:
        emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
        emit_line("cdq");
        emit_line("idiv dword ptr [ebp%+d]", s2_off);
        emit_line("mov  dword ptr [ebp%+d], edx", d_off);
        break;

    case IR_NEG_I32:
        emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
        emit_line("neg  eax");
        emit_line("mov  dword ptr [ebp%+d], eax", d_off);
        break;

    default:
        emit_comment("UNIMPLEMENTED i32 arith: %s", irop_name(instr->op));
        break;
    }
}

/* ================================================================= */
/* Typed i32 comparison (386)                                         */
/* ================================================================= */

void CodeGenerator386::emit_cmp_i32(IRInstr *instr)
{
    int s1_off = slot_offset(current_func->num_locals + instr->src1);
    int s2_off = slot_offset(current_func->num_locals + instr->src2);
    int d_off  = slot_offset(current_func->num_locals + instr->dest);
    int true_label = new_label();
    int end_label = new_label();
    const char *jmp_instr = "je";

    emit_comment("CMP_I32 %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    switch (instr->op) {
    case IR_CMP_I32_EQ: jmp_instr = "je";  break;
    case IR_CMP_I32_NE: jmp_instr = "jne"; break;
    case IR_CMP_I32_LT: jmp_instr = "jl";  break;
    case IR_CMP_I32_LE: jmp_instr = "jle"; break;
    case IR_CMP_I32_GT: jmp_instr = "jg";  break;
    case IR_CMP_I32_GE: jmp_instr = "jge"; break;
    default: break;
    }

    emit_line("mov  eax, dword ptr [ebp%+d]", s1_off);
    emit_line("cmp  eax, dword ptr [ebp%+d]", s2_off);
    emit_line("%s   _L%d", jmp_instr, true_label);

    /* False: store unboxed 0 */
    emit_line("mov  dword ptr [ebp%+d], 0", d_off);
    emit_line("jmp  _L%d", end_label);

    /* True: store unboxed 1 */
    emit_local_label(true_label);
    emit_line("mov  dword ptr [ebp%+d], 1", d_off);

    emit_local_label(end_label);
}

/* ================================================================= */
/* Box/Unbox (386)                                                    */
/* ================================================================= */

void CodeGenerator386::emit_box_int(IRInstr *instr)
{
    int s1_off = slot_offset(current_func->num_locals + instr->src1);

    emit_comment("BOX_INT t%d -> t%d", instr->src1, instr->dest);

    emit_line("push dword ptr [ebp%+d]", s1_off);
    emit_line("call pydos_obj_new_int_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

void CodeGenerator386::emit_unbox_int(IRInstr *instr)
{
    int d_off = slot_offset(current_func->num_locals + instr->dest);

    emit_comment("UNBOX_INT t%d -> t%d", instr->src1, instr->dest);

    load_temp_to_eax(instr->src1);
    emit_line("mov  eax, dword ptr [eax+%d]", PYOBJ_V_OFFSET_32);
    emit_line("mov  dword ptr [ebp%+d], eax", d_off);
}

void CodeGenerator386::emit_box_bool(IRInstr *instr)
{
    int s1_off = slot_offset(current_func->num_locals + instr->src1);

    emit_comment("BOX_BOOL t%d -> t%d", instr->src1, instr->dest);

    emit_line("push dword ptr [ebp%+d]", s1_off);
    emit_line("call pydos_obj_new_bool_");
    emit_line("add  esp, 4");

    store_eax_to_temp(instr->dest);
}

void CodeGenerator386::emit_str_join(IRInstr *instr)
{
    /* dest:pyobj = pydos_str_join_n_(count, part0, part1, ...) */
    int count = instr->extra;
    int a;
    int stack_bytes;

    emit_comment("STR_JOIN %d parts -> t%d", count, instr->dest);

    /* Push parts right-to-left (cdecl convention).
     * Use count (from instr->extra) instead of num_call_args,
     * which may have been reset by intervening CALL instructions. */
    for (a = count - 1; a >= 0; a--) {
        push_temp(arg_temps[a]);
    }

    /* Push count (unsigned int, 4 bytes on 32-bit) */
    emit_line("push %d", count);

    emit_line("call pydos_str_join_n_");

    /* Clean up: 4 (count) + 4*count (near pointers) */
    stack_bytes = 4 + 4 * count;
    emit_line("add  esp, %d", stack_bytes);

    /* Result in EAX */
    store_eax_to_temp(instr->dest);

    num_call_args = 0;
}

/* ================================================================ */
/* Arena scope operations                                            */
/* ================================================================ */

void CodeGenerator386::emit_scope_enter(IRInstr *instr)
{
    (void)instr;
    emit_line("; SCOPE_ENTER");
    emit_line("call pydos_arena_scope_enter_");
}

void CodeGenerator386::emit_scope_track(IRInstr *instr)
{
    emit_line("; SCOPE_TRACK t%d", instr->src1);
    load_temp_to_eax(instr->src1);
    emit_line("push eax");
    emit_line("call pydos_arena_scope_track_");
    emit_line("add  esp, 4");
}

void CodeGenerator386::emit_scope_exit(IRInstr *instr)
{
    (void)instr;
    emit_line("; SCOPE_EXIT");
    emit_line("call pydos_arena_scope_exit_");
}
