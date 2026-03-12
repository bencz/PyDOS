/*
 * codegen_8086.cpp - 8086 real-mode code generator for PyDOS compiler
 *
 * Implements CodeGenerator8086: the concrete back-end that emits
 * Open Watcom Assembler (WASM) instructions for 16-bit 8086 in
 * real-mode DOS with LARGE memory model (far code + far data).
 *
 * Key architectural details:
 *   - All Python objects are far pointers (4 bytes: segment:offset)
 *   - Each local/temp slot = 4 bytes on the stack (hi word = seg, lo = off)
 *   - Parameters begin at [BP+6] (far ret = 4, saved BP = 2)
 *   - SAVE_AREA = 4 bytes (SI + DI pushed after BP)
 *   - DS must be restored after every far call (push ss / pop ds)
 *   - Return values in DX:AX (DX = segment, AX = offset)
 *   - 8086 has no push-immediate -- use "mov ax, N / push ax"
 *   - 32-bit arithmetic via Open Watcom helpers (__I4M, __I4D, etc.)
 *   - String constants emitted in .DATA as _SC0, _SC1, ...
 *   - Global variables emitted as _G_name dd 0
 *   - VTable globals emitted as _VT_ClassName dd 0
 *   - Runtime function names end with trailing underscore (cdecl)
 *
 * C++98 compatible.  No STL -- arrays, manual memory only.
 */

#include "cg8086.h"
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

/* SI + DI = 4 bytes pushed after BP in the prologue */
#define SAVE_AREA           4

/* far return address (4) + saved BP (2) = first param at [BP+6] */
#define PARAM_BASE_OFFSET   6

/* Offset of the value union in PyDosObj (16-bit layout):
 * type(1) + flags(1) + refcount(2) = 4 bytes */
#define PYOBJ_V_OFFSET_16   4

/* ================================================================= */
/* Static helper functions                                            */
/* ================================================================= */

/* Low word (offset) of a 4-byte slot at position 'slot'.
 * Slots grow downward from BP past the SAVE_AREA.
 * slot 0 => [BP - 8],  slot 1 => [BP - 12], etc.
 * Low word is at the more-negative address (little-endian). */
static int slot_offset_lo(int slot)
{
    return -(SAVE_AREA + slot * 4 + 4);
}

/* High word (segment) of a 4-byte slot at position 'slot'.
 * Two bytes above the low word. */
static int slot_offset_hi(int slot)
{
    return -(SAVE_AREA + slot * 4 + 2);
}

/* ================================================================= */
/* Constructor / Destructor                                           */
/* ================================================================= */

CodeGenerator8086::CodeGenerator8086()
{
    /* Base class constructor handles all init */
}

CodeGenerator8086::~CodeGenerator8086()
{
    /* Base class destructor handles cleanup */
}

/* ================================================================= */
/* Private helper methods                                             */
/* ================================================================= */

/* emit_push_imm -- push an immediate 16-bit value.
 * 8086 has no "push imm16" instruction, so we use AX as intermediary. */
void CodeGenerator8086::emit_push_imm(int val)
{
    emit_line("mov  ax, %d", val);
    emit_line("push ax");
}

/* emit_push_far_const -- push a far pointer to a data label.
 * Pushes segment first, then offset (4 bytes total, hi:lo order). */
void CodeGenerator8086::emit_push_far_const(const char *lbl)
{
    emit_line("mov  ax, seg %s", lbl);
    emit_line("push ax");
    emit_line("mov  ax, offset %s", lbl);
    emit_line("push ax");
}

/* emit_restore_ds -- restore DS = SS = DGROUP after a far call. */
void CodeGenerator8086::emit_restore_ds()
{
    emit_line("push ss");
    emit_line("pop  ds");
}

/* temp_bp_offset -- compute BP-relative offset for a temporary.
 * Temps are laid out after locals: slot = num_locals + temp. */
int CodeGenerator8086::temp_bp_offset(int temp) const
{
    int slot = current_func->num_locals + temp;
    return slot_offset_lo(slot);
}

/* load_temp_to_dxax -- load a 4-byte far pointer from stack temp
 * into DX:AX (DX = segment, AX = offset). */
void CodeGenerator8086::load_temp_to_dxax(int temp)
{
    int slot = current_func->num_locals + temp;
    int lo = slot_offset_lo(slot);
    int hi = slot_offset_hi(slot);
    emit_line("mov  ax, word ptr [bp%+d]", lo);
    emit_line("mov  dx, word ptr [bp%+d]", hi);
}

/* store_dxax_to_temp -- store DX:AX into a 4-byte temp slot. */
void CodeGenerator8086::store_dxax_to_temp(int temp)
{
    int slot = current_func->num_locals + temp;
    int lo = slot_offset_lo(slot);
    int hi = slot_offset_hi(slot);
    emit_line("mov  word ptr [bp%+d], ax", lo);
    emit_line("mov  word ptr [bp%+d], dx", hi);
}

/* push_temp -- push a 4-byte far pointer temp onto the stack.
 * Pushes hi word (segment) first, then lo word (offset). */
void CodeGenerator8086::push_temp(int temp)
{
    int slot = current_func->num_locals + temp;
    int lo = slot_offset_lo(slot);
    int hi = slot_offset_hi(slot);
    emit_line("push word ptr [bp%+d]", hi);
    emit_line("push word ptr [bp%+d]", lo);
}

/* ================================================================= */
/* emit_header                                                        */
/* ================================================================= */

void CodeGenerator8086::emit_header()
{
    fprintf(out, "; PyDOS compiler output - 8086 WASM assembly\n");
    fprintf(out, ".8086\n");
    fprintf(out, ".MODEL LARGE\n");
    emit_blank();
}

/* ================================================================= */
/* emit_footer                                                        */
/* ================================================================= */

void CodeGenerator8086::emit_footer()
{
    emit_blank();
    fprintf(out, "END\n");
}

/* ================================================================= */
/* emit_data_section                                                  */
/* ================================================================= */

void CodeGenerator8086::emit_data_section()
{
    int i;
    fprintf(out, ".DATA\n");
    emit_blank();

    /* ---- Data externs (DGROUP variables, not code labels) ---- */
    for (i = 0; i < num_data_externs; i++) {
        fprintf(out, "EXTRN %s:DWORD\n", data_externs[i]);
    }
    if (num_data_externs > 0) emit_blank();

    /* ---- String constants ---- */
    for (i = 0; i < mod->num_constants; i++) {
        if (mod->constants[i].kind != IRConst::CONST_STR) continue;
        /* Only emit strings that are actually referenced */
        if (!used_const[i]) continue;

        char lbl[32];
        const_label(lbl, sizeof(lbl), i);
        const char *sdata = mod->constants[i].str_val.data;
        int slen = mod->constants[i].str_val.len;

        /* Emit the string data: db "...", 0
         * We need to handle special characters by emitting byte values. */
        fprintf(out, "%s", lbl);

        /* Check if string has special characters that need escaping */
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
            /* Simple case: printable string */
            fprintf(out, " db \"%.*s\", 0\n", slen, sdata);
        } else if (slen == 0) {
            fprintf(out, " db 0\n");
        } else {
            /* Complex case: emit as mix of quoted strings and byte values */
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

        /* Emit length equate */
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

void CodeGenerator8086::emit_code_section()
{
    fprintf(out, ".CODE\n");
    emit_blank();

    /* ---- External declarations ---- */
    emit_extern_declarations();
    emit_blank();

    /* ---- User-defined functions ---- */
    IRFunc *f;
    for (f = mod->functions; f; f = f->next) {
        /* Skip init_func here; it is emitted separately below */
        if (mod->init_func && f == mod->init_func) continue;
        emit_func(f);
        emit_blank();
    }

    /* ---- Module init function (__init__) ---- */
    if (mod->init_func) {
        emit_func(mod->init_func);
        emit_blank();
    }

    /* ---- main_ entry point (only for main module) ---- */
    if (is_main_module) {
        char init_label[128];
        func_label(init_label, sizeof(init_label), "__init__");

        fprintf(out, "PUBLIC main_\n");
        fprintf(out, "main_ PROC FAR\n");
        emit_line("push bp");
        emit_line("mov  bp, sp");
        emit_blank();

        /* Initialize the runtime */
        emit_comment("init runtime");
        emit_restore_ds();
        emit_line("call far ptr pydos_rt_init_");
        emit_blank();

        /* Call module __init__ (top-level code) */
        emit_comment("call __init__");
        emit_restore_ds();
        emit_line("call far ptr %s", init_label);
        emit_blank();

        /* Call user-specified entry function (--entry) */
        if (has_main_func && entry_func) {
            char entry_label[128];
            func_label(entry_label, sizeof(entry_label), entry_func);
            emit_comment("call entry: %s()", entry_func);
            emit_restore_ds();
            emit_line("call far ptr %s", entry_label);
            emit_blank();
        }

        /* Shutdown runtime */
        emit_comment("shutdown runtime");
        emit_restore_ds();
        emit_line("call far ptr pydos_rt_shutdown_");
        emit_blank();

        /* Exit to DOS via INT 21h function 4Ch */
        emit_comment("exit to DOS");
        emit_line("mov  ax, 4C00h");
        emit_line("int  21h");

        fprintf(out, "main_ ENDP\n");
    }
}

/* ================================================================= */
/* emit_extern_declarations                                           */
/* ================================================================= */

void CodeGenerator8086::emit_extern_declarations()
{
    int i;
    for (i = 0; i < num_externs; i++) {
        fprintf(out, "EXTRN %s:FAR\n", externs[i]);
    }
    /* Data externs are emitted in emit_data_section() as EXTRN name:DWORD,
     * not here as :FAR, because they reside in DGROUP (not a code segment). */
}

/* ================================================================= */
/* emit_func                                                          */
/* ================================================================= */

void CodeGenerator8086::emit_func(IRFunc *func)
{
    current_func = func;

    /* Run register allocator */
    current_alloc = reg_allocator.allocate(func);

    char flbl[128];
    func_label(flbl, sizeof(flbl), func->name);

    fprintf(out, "PUBLIC %s\n", flbl);
    fprintf(out, "%s PROC FAR\n", flbl);

    emit_prologue(func);

    /* Emit all instructions */
    IRInstr *ip;
    for (ip = func->first; ip; ip = ip->next) {
        emit_instr(ip);
    }

    /* Epilogue label (for return jumps) */
    fprintf(out, "_%s_epilogue:\n", func->name);
    emit_epilogue(func);

    fprintf(out, "%s ENDP\n", flbl);

    /* Clean up allocation */
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

void CodeGenerator8086::emit_prologue(IRFunc *func)
{
    /* Standard prologue: save BP, set frame, save callee-saved regs */
    emit_line("push bp");
    emit_line("mov  bp, sp");
    emit_line("push si");
    emit_line("push di");

    /* Calculate frame size: (num_locals + num_temps) * 4 bytes each */
    int total_slots = func->num_locals + func->num_temps;
    int frame_size = total_slots * 4;

    if (frame_size > 0) {
        emit_line("sub  sp, %d", frame_size);

        /* Zero-initialize all local/temp slots using rep stosw */
        emit_comment("zero-init locals and temps");
        emit_line("push es");
        emit_line("push ss");
        emit_line("pop  es");
        /* Point DI at the lowest stack slot */
        emit_line("lea  di, [bp%+d]", slot_offset_lo(total_slots - 1));
        emit_line("mov  cx, %d", total_slots * 2);  /* word count */
        emit_line("xor  ax, ax");
        emit_line("cld");
        emit_line("rep  stosw");
        emit_line("pop  es");
    }

    /* Copy parameters from [BP+6+p*4] into local slots [0..num_params-1] */
    if (func->num_params > 0) {
        emit_comment("copy parameters to local slots");
        int p;
        for (p = 0; p < func->num_params; p++) {
            int src_lo = PARAM_BASE_OFFSET + p * 4;
            int src_hi = PARAM_BASE_OFFSET + p * 4 + 2;
            int dst_lo = slot_offset_lo(p);
            int dst_hi = slot_offset_hi(p);
            emit_line("mov  ax, word ptr [bp+%d]", src_lo);
            emit_line("mov  word ptr [bp%+d], ax", dst_lo);
            emit_line("mov  ax, word ptr [bp+%d]", src_hi);
            emit_line("mov  word ptr [bp%+d], ax", dst_hi);
        }
    }

    emit_blank();
}

/* ================================================================= */
/* emit_epilogue                                                      */
/* ================================================================= */

void CodeGenerator8086::emit_epilogue(IRFunc *func)
{
    int total_slots = func->num_locals + func->num_temps;
    int frame_size = total_slots * 4;

    if (frame_size > 0) {
        emit_line("add  sp, %d", frame_size);
    }
    emit_line("pop  di");
    emit_line("pop  si");
    emit_line("mov  sp, bp");
    emit_line("pop  bp");
    emit_line("retf");
}

/* ================================================================= */
/* emit_const_int                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_const_int(IRInstr *instr)
{
    /* IR_CONST_INT: dest = new int object from constant pool
     * src1 = constant pool index */
    int ci = instr->src1;
    long val = 0;
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_INT) {
        val = mod->constants[ci].int_val;
    }

    emit_comment("CONST_INT %ld -> t%d", val, instr->dest);

    /* Push high word, then low word (C calling convention: right-to-left) */
    int hi = (int)((val >> 16) & 0xFFFF);
    int lo = (int)(val & 0xFFFF);
    emit_push_imm(hi);
    emit_push_imm(lo);

    /* Call pydos_obj_new_int_(long val) -- val is 32 bits = 2 words */
    require_extern("pydos_obj_new_int_");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_int_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* Result in DX:AX -- store to dest temp */
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_str                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_const_str(IRInstr *instr)
{
    /* IR_CONST_STR: dest = new string object
     * src1 = constant pool index */
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

    /* Push length */
    emit_push_imm(slen);

    /* Push far pointer to string data */
    emit_push_far_const(lbl);

    /* Call pydos_obj_new_str_(const char far *data, int len) */
    require_extern("pydos_obj_new_str_");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_str_");
    emit_line("add  sp, 6");
    emit_restore_ds();

    /* Result in DX:AX */
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_float                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_const_float(IRInstr *instr)
{
    /* IR_CONST_FLOAT: dest = new float object
     * src1 = constant pool index */
    int ci = instr->src1;
    double val = 0.0;
    if (ci >= 0 && ci < mod->num_constants &&
        mod->constants[ci].kind == IRConst::CONST_FLOAT) {
        val = mod->constants[ci].float_val;
    }

    emit_comment("CONST_FLOAT %g -> t%d", val, instr->dest);

    /* Push the 8 bytes of the double as 4 words (high to low) */
    unsigned char *bytes = (unsigned char *)&val;
    /* Push words in reverse order (right-to-left for C calling convention).
     * Double is 8 bytes = 4 words. We push word[3], word[2], word[1], word[0]. */
    int w;
    for (w = 3; w >= 0; w--) {
        int word_val = bytes[w * 2] | (bytes[w * 2 + 1] << 8);
        emit_push_imm(word_val);
    }

    /* Call pydos_obj_new_float_(double val) -- 8 bytes on stack */
    require_extern("pydos_obj_new_float_");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_float_");
    emit_line("add  sp, 8");
    emit_restore_ds();

    /* Result in DX:AX */
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_none                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_const_none(IRInstr *instr)
{
    emit_comment("CONST_NONE -> t%d", instr->dest);

    /* Call pydos_obj_new_none_(void) -- no args */
    require_extern("pydos_obj_new_none_");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_none_");
    emit_restore_ds();

    /* Result in DX:AX */
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_const_bool                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_const_bool(IRInstr *instr)
{
    /* src1 = 0 (False) or 1 (True) */
    emit_comment("CONST_BOOL %d -> t%d", instr->src1, instr->dest);

    emit_push_imm(instr->src1);

    /* Call pydos_obj_new_bool_(int val) */
    require_extern("pydos_obj_new_bool_");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_bool_");
    emit_line("add  sp, 2");
    emit_restore_ds();

    /* Result in DX:AX */
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_load_local                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_load_local(IRInstr *instr)
{
    /* IR_LOAD_LOCAL: dest = local[src1]
     * Copy 4 bytes from local slot src1 to temp dest */
    int local_slot = instr->src1;

    emit_comment("LOAD_LOCAL local[%d] -> t%d", local_slot, instr->dest);

    int src_lo = slot_offset_lo(local_slot);
    int src_hi = slot_offset_hi(local_slot);
    int dst_slot = current_func->num_locals + instr->dest;
    int dst_lo = slot_offset_lo(dst_slot);
    int dst_hi = slot_offset_hi(dst_slot);

    emit_line("mov  ax, word ptr [bp%+d]", src_lo);
    emit_line("mov  word ptr [bp%+d], ax", dst_lo);
    emit_line("mov  ax, word ptr [bp%+d]", src_hi);
    emit_line("mov  word ptr [bp%+d], ax", dst_hi);
}

/* ================================================================= */
/* emit_store_local                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_store_local(IRInstr *instr)
{
    /* IR_STORE_LOCAL: local[dest] = src1
     * Copy 4 bytes from temp src1 to local slot dest */
    int local_slot = instr->dest;

    emit_comment("STORE_LOCAL t%d -> local[%d]", instr->src1, local_slot);

    int src_slot = current_func->num_locals + instr->src1;
    int src_lo = slot_offset_lo(src_slot);
    int src_hi = slot_offset_hi(src_slot);
    int dst_lo = slot_offset_lo(local_slot);
    int dst_hi = slot_offset_hi(local_slot);

    emit_line("mov  ax, word ptr [bp%+d]", src_lo);
    emit_line("mov  word ptr [bp%+d], ax", dst_lo);
    emit_line("mov  ax, word ptr [bp%+d]", src_hi);
    emit_line("mov  word ptr [bp%+d], ax", dst_hi);
}

/* ================================================================= */
/* emit_load_global                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_load_global(IRInstr *instr)
{
    /* IR_LOAD_GLOBAL: dest = global[name]
     * src1 = constant pool index for the name string */
    int ci = instr->src1;
    if (ci < 0 || ci >= mod->num_constants ||
        mod->constants[ci].kind != IRConst::CONST_STR) {
        emit_comment("LOAD_GLOBAL: bad constant index %d", ci);
        return;
    }

    const char *gname = mod->constants[ci].str_val.data;

    /* Skip builtins -- they don't have global storage; emit_call
     * resolves them directly to far calls. */
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
    int dst_lo = slot_offset_lo(dst_slot);
    int dst_hi = slot_offset_hi(dst_slot);

    /* Load 4-byte far pointer from data segment global */
    emit_line("mov  ax, word ptr [%s]", aname);
    emit_line("mov  word ptr [bp%+d], ax", dst_lo);
    emit_line("mov  ax, word ptr [%s+2]", aname);
    emit_line("mov  word ptr [bp%+d], ax", dst_hi);
}

/* ================================================================= */
/* emit_store_global                                                  */
/* ================================================================= */

void CodeGenerator8086::emit_store_global(IRInstr *instr)
{
    /* IR_STORE_GLOBAL: global[name] = src1
     * dest = constant pool index for the name string */
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
    int src_lo = slot_offset_lo(src_slot);
    int src_hi = slot_offset_hi(src_slot);

    /* Store 4-byte far pointer to data segment global */
    emit_line("mov  ax, word ptr [bp%+d]", src_lo);
    emit_line("mov  word ptr [%s], ax", aname);
    emit_line("mov  ax, word ptr [bp%+d]", src_hi);
    emit_line("mov  word ptr [%s+2], ax", aname);
}

/* ================================================================= */
/* emit_arithmetic                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_arithmetic(IRInstr *instr)
{
    /* IR_ADD/SUB/MUL/DIV/FLOORDIV/MOD/POW: dest = src1 op src2
     * Both operands are PyDosObj far pointers. We call the appropriate
     * runtime function which takes two far pointers and returns a new one. */
    emit_comment("ARITH %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    /* Determine which runtime function to call based on type hints */
    const char *func_name = 0;

    /* Check for string concatenation: "+" on str type */
    if (instr->op == IR_ADD && instr->type_hint &&
        instr->type_hint->kind == TY_STR) {
        func_name = "pydos_str_concat_";
    }
    /* Check for polymorphic dispatch (class types, unknown, float, or complex) */
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
    /* Default: int arithmetic */
    else {
        func_name = runtime_arith_func(instr->op);
    }

    require_extern(func_name);

    /* Push src2 far pointer (right-to-left) */
    push_temp(instr->src2);
    /* Push src1 far pointer */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr %s", func_name);
    emit_line("add  sp, 8");
    emit_restore_ds();

    /* Result in DX:AX */
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_matmul                                                        */
/* ================================================================= */
void CodeGenerator8086::emit_matmul(IRInstr *instr)
{
    emit_comment("MATMUL t%d, t%d -> t%d", instr->src1, instr->src2, instr->dest);

    require_extern("pydos_obj_matmul_");

    push_temp(instr->src2);
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_matmul_");
    emit_line("add  sp, 8");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_inplace                                                       */
/* ================================================================= */
void CodeGenerator8086::emit_inplace(IRInstr *instr)
{
    emit_comment("INPLACE op=%d t%d, t%d -> t%d",
                 instr->extra, instr->src1, instr->src2, instr->dest);

    require_extern("pydos_obj_inplace_");

    /* Push op index (int, 2 bytes on 8086) — no push-imm on 8086 */
    emit_line("mov  ax, %d", instr->extra);
    emit_line("push ax");
    /* Push src2 far pointer */
    push_temp(instr->src2);
    /* Push src1 far pointer */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_inplace_");
    emit_line("add  sp, 10");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_bitwise                                                       */
/* ================================================================= */

void CodeGenerator8086::emit_bitwise(IRInstr *instr)
{
    /* IR_BITAND/BITOR/BITXOR/SHL/SHR: dest = src1 op src2 */
    const char *func_name = runtime_bitwise_func(instr->op);

    emit_comment("BITWISE %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    require_extern(func_name);

    /* Push src2 then src1 (right-to-left) */
    push_temp(instr->src2);
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr %s", func_name);
    emit_line("add  sp, 8");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_unary                                                         */
/* ================================================================= */

void CodeGenerator8086::emit_unary(IRInstr *instr)
{
    /* IR_NEG, IR_POS, IR_NOT, IR_BITNOT: dest = op src1 */
    emit_comment("UNARY %s t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->dest);

    switch (instr->op) {
    case IR_POS: {
        const char *func_name = runtime_unary_func(instr->op);
        require_extern(func_name);

        push_temp(instr->src1);
        emit_restore_ds();
        emit_line("call far ptr %s", func_name);
        emit_line("add  sp, 4");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
        break;
    }

    case IR_NOT: {
        /* Logical NOT: call is_truthy, XOR result with 1, create bool */
        require_extern("pydos_obj_is_truthy_");
        require_extern("pydos_obj_new_bool_");

        push_temp(instr->src1);
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_is_truthy_");
        emit_line("add  sp, 4");
        emit_restore_ds();

        /* AX = 0 or 1; XOR with 1 to negate */
        emit_line("xor  ax, 1");
        emit_line("push ax");

        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_bool_");
        emit_line("add  sp, 2");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
        break;
    }

    case IR_NEG:
    case IR_BITNOT: {
        const char *func_name = runtime_unary_func(instr->op);
        require_extern(func_name);

        push_temp(instr->src1);
        emit_restore_ds();
        emit_line("call far ptr %s", func_name);
        emit_line("add  sp, 4");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
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

void CodeGenerator8086::emit_comparison(IRInstr *instr)
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

        /* Compare low words (offset) */
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        emit_line("jne  _L%d", lbl_false);

        /* Compare high words (segment) */
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("jne  _L%d", lbl_false);

        /* Both equal: IS => true, IS_NOT => false */
        emit_push_imm((instr->op == IR_IS) ? 1 : 0);
        emit_line("jmp  _L%d", lbl_end);

        fprintf(out, "_L%d:\n", lbl_false);
        /* Not equal: IS => false, IS_NOT => true */
        emit_push_imm((instr->op == IR_IS) ? 0 : 1);

        fprintf(out, "_L%d:\n", lbl_end);
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_bool_");
        emit_line("add  sp, 2");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
        return;
    }

    case IR_IN:
    case IR_NOT_IN: {
        /* Check if src1 is in container src2 via pydos_obj_contains_ */
        require_extern("pydos_obj_contains_");
        require_extern("pydos_obj_new_bool_");

        /* cdecl pushes right-to-left: push item first, then container,
         * so container ends up as arg1 and item as arg2:
         * pydos_obj_contains_(container, item) */
        push_temp(instr->src1);
        push_temp(instr->src2);
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_contains_");
        emit_line("add  sp, 8");
        emit_restore_ds();

        /* AX = 0 or 1 (contains result) */
        if (instr->op == IR_NOT_IN) {
            emit_line("xor  ax, 1");
        }
        emit_line("push ax");
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_bool_");
        emit_line("add  sp, 2");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
        return;
    }

    case IR_CMP_EQ:
    case IR_CMP_NE: {
        /* Equality comparison via pydos_obj_equal_ */
        require_extern("pydos_obj_equal_");
        require_extern("pydos_obj_new_bool_");

        push_temp(instr->src2);
        push_temp(instr->src1);
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_equal_");
        emit_line("add  sp, 8");
        emit_restore_ds();

        /* AX = 1 if equal, 0 if not */
        if (instr->op == IR_CMP_NE) {
            emit_line("xor  ax, 1");
        }
        emit_line("push ax");
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_bool_");
        emit_line("add  sp, 2");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
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
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_compare_");
        emit_line("add  sp, 8");
        emit_restore_ds();

        /* AX = comparison result: negative, 0, or positive */
        int lbl_true = new_label();
        int lbl_end = new_label();

        emit_line("cmp  ax, 0");
        switch (instr->op) {
            case IR_CMP_LT: emit_line("jl   _L%d", lbl_true); break;
            case IR_CMP_LE: emit_line("jle  _L%d", lbl_true); break;
            case IR_CMP_GT: emit_line("jg   _L%d", lbl_true); break;
            case IR_CMP_GE: emit_line("jge  _L%d", lbl_true); break;
            default: break;
        }

        /* False path */
        emit_push_imm(0);
        emit_line("jmp  _L%d", lbl_end);

        /* True path */
        fprintf(out, "_L%d:\n", lbl_true);
        emit_push_imm(1);

        fprintf(out, "_L%d:\n", lbl_end);
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_bool_");
        emit_line("add  sp, 2");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
        return;
    }

}

/* ================================================================= */
/* emit_jump                                                          */
/* ================================================================= */

void CodeGenerator8086::emit_jump(IRInstr *instr)
{
    /* IR_JUMP: goto label (extra = label id) */
    emit_line("jmp  _L%d", instr->extra);
}

/* ================================================================= */
/* emit_jump_cond                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_jump_cond(IRInstr *instr)
{
    /* IR_JUMP_IF_TRUE / IR_JUMP_IF_FALSE: test src1, branch to extra */
    emit_comment("JUMP_%s t%d -> _L%d",
                 instr->op == IR_JUMP_IF_TRUE ? "TRUE" : "FALSE",
                 instr->src1, instr->extra);

    require_extern("pydos_obj_is_truthy_");

    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_is_truthy_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* AX = 0 (falsy) or 1 (truthy) */
    emit_line("test ax, ax");
    if (instr->op == IR_JUMP_IF_TRUE) {
        emit_line("jnz  _L%d", instr->extra);
    } else {
        emit_line("jz   _L%d", instr->extra);
    }
}

/* ================================================================= */
/* emit_ir_label                                                      */
/* ================================================================= */

void CodeGenerator8086::emit_ir_label(IRInstr *instr)
{
    /* IR_LABEL: emit label marker */
    fprintf(out, "_L%d:\n", instr->extra);
}

/* ================================================================= */
/* emit_push_arg                                                      */
/* ================================================================= */

void CodeGenerator8086::emit_push_arg(IRInstr *instr)
{
    /* IR_PUSH_ARG: accumulate argument temp for upcoming call.
     * We don't actually push to the hardware stack here -- we record
     * the temp index and push everything in emit_call. */
    if (num_call_args >= arg_temps_cap) grow_arg_temps();
    {
        arg_temps[num_call_args++] = instr->src1;
    }
}

/* ================================================================= */
/* emit_call                                                          */
/* ================================================================= */

void CodeGenerator8086::emit_call(IRInstr *instr)
{
    /* IR_CALL: dest = call src1(args...) extra=argc
     *
     * src1 is the temp holding the callable (function pointer or global).
     * For builtins, we look up the callee name from the preceding
     * LOAD_GLOBAL instruction and emit a direct far call.
     * For user functions, we look at the constant pool to determine
     * the function label and call it directly.
     * For indirect calls (e.g., method dispatch), we call dword ptr. */
    int argc = instr->extra;

    emit_comment("CALL t%d(%d args) -> t%d", instr->src1, argc, instr->dest);

    /* Try to resolve the callee name from IR.
     * Look backward for the LOAD_GLOBAL that loaded src1. */
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

    /* Check if this is a builtin function */
    const char *builtin_name = 0;
    if (callee_name) {
        builtin_name = builtin_asm_name(callee_name);
    }

    if (builtin_name) {
        /* ---- Builtin call: use argv array on stack ---- */
        emit_comment("builtin: %s (%d args)", callee_name, argc);
        require_extern(builtin_name);

        /* Allocate argv array on stack: argc * 4 bytes (far pointers).
         * Push argc, push SS:SI as argv pointer.
         * Stack layout: [arg0_lo, arg0_hi, arg1_lo, arg1_hi, ...] */
        if (argc > 0) {
            emit_line("sub  sp, %d", argc * 4);
            emit_line("mov  si, sp");

            /* Copy each argument to the argv area */
            int a;
            for (a = 0; a < argc && a < num_call_args; a++) {
                int arg_temp = arg_temps[a];
                int arg_slot = current_func->num_locals + arg_temp;
                /* Copy lo word */
                emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(arg_slot));
                emit_line("mov  word ptr ss:[si+%d], ax", a * 4);
                /* Copy hi word */
                emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(arg_slot));
                emit_line("mov  word ptr ss:[si+%d], ax", a * 4 + 2);
            }
        }

        /* cdecl: push args right-to-left.
         * Signature: func(int argc, PyDosObj far * far *argv)
         * So push argv first (rightmost), then argc (leftmost). */

        /* Push far pointer to argv (SS:SI) */
        if (argc > 0) {
            emit_line("push ss");
            emit_line("push si");
        } else {
            /* NULL argv for 0-arg calls */
            emit_push_imm(0);
            emit_push_imm(0);
        }

        /* Push argc */
        emit_push_imm(argc);

        emit_restore_ds();
        emit_line("call far ptr %s", builtin_name);
        /* Clean up: 4 bytes for argv ptr + 2 bytes for argc + argv array */
        emit_line("add  sp, %d", 6 + (argc > 0 ? argc * 4 : 0));
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);

    } else if (callee_name && strncmp(callee_name, "pydos_", 6) == 0) {
        /* ---- Runtime helper: direct cdecl far call ---- */
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
        emit_restore_ds();
        emit_line("call far ptr %s", ename);
        if (argc > 0) {
            emit_line("add  sp, %d", argc * 4);
        }
        emit_restore_ds();
        store_dxax_to_temp(instr->dest);

    } else {
        /* ---- User function or indirect call ---- */

        /* Check if we can resolve to a direct function name */
        int is_direct = 0;
        char flbl[128];

        if (callee_name) {
            /* Check if this is a known user function */
            IRFunc *f;
            for (f = mod->functions; f; f = f->next) {
                if (strcmp(f->name, callee_name) == 0) {
                    is_direct = 1;
                    func_label(flbl, sizeof(flbl), callee_name);
                    break;
                }
            }
            /* Also check __init__ */
            if (!is_direct && mod->init_func &&
                strcmp(mod->init_func->name, callee_name) == 0) {
                is_direct = 1;
                func_label(flbl, sizeof(flbl), callee_name);
            }
        }

        /* Push arguments right-to-left as 4-byte far pointers */
        int a;
        for (a = num_call_args - 1; a >= 0; a--) {
            push_temp(arg_temps[a]);
        }

        if (is_direct) {
            emit_restore_ds();
            emit_line("call far ptr %s", flbl);
        } else {
            /* Indirect call through function object or raw pointer.
             * Check if src1 came from IR_MAKE_FUNCTION -- if so, we
             * need to extract the code pointer from the func object. */
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
                int clo_off = PYOBJ_V_OFFSET_16 + 8;
                /* src1 holds a PyDosObj* (PYDT_FUNCTION). Extract code ptr:
                 * obj->v.func.code is at PYOBJ_V_OFFSET + 0 (first field). */
                emit_comment("indirect call through function object");
                load_temp_to_dxax(instr->src1);
                emit_line("mov  bx, ax");
                emit_line("mov  es, dx");
                /* Load closure into pydos_active_closure before call */
                require_extern("pydos_active_closure_");
                emit_line("mov  ax, word ptr es:[bx+%d]", clo_off);
                emit_line("mov  word ptr [pydos_active_closure_], ax");
                emit_line("mov  ax, word ptr es:[bx+%d]", clo_off + 2);
                emit_line("mov  word ptr [pydos_active_closure_+2], ax");
                /* Read code far ptr from obj->v.func.code */
                emit_line("mov  ax, word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);
                emit_line("mov  dx, word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
                /* Store extracted code pointer into the temp slot for indirect call */
                int call_slot = current_func->num_locals + instr->src1;
                emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(call_slot));
                emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(call_slot));
            }

            int call_slot = current_func->num_locals + instr->src1;
            int call_lo = slot_offset_lo(call_slot);
            emit_restore_ds();
            emit_line("call dword ptr [bp%+d]", call_lo);
        }

        /* Clean up arguments */
        if (argc > 0) {
            emit_line("add  sp, %d", argc * 4);
        }
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
    }

    /* Reset the arg accumulator for the next call */
    num_call_args = 0;
}

/* ================================================================= */
/* emit_call_method                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_call_method(IRInstr *instr)
{
    /* IR_CALL_METHOD: dest = src1.method(args...)
     * src1 = object temp, src2 = method name const index, extra = argc */
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
             * All fast-path methods use cdecl(self, PyDosObj far* arg0, ...) */
            int i;
            for (i = argc - 1; i >= 0; i--) {
                if (i < num_call_args)
                    push_temp(arg_temps[i]);
            }
            push_temp(instr->src1);   /* self */
            emit_restore_ds();
            emit_line("call far ptr %s", fast_asm);
            emit_line("add  sp, %d", (argc + 1) * 4);
            emit_restore_ds();
            store_dxax_to_temp(instr->dest);
            num_call_args = 0;
            return;
        }
    }

    /* ---- Generic method call via pydos_obj_call_method_ ---- */
    /* Build argv array on stack with self as first element.
     * argv = [self, arg0, arg1, ...], total_argc = argc + 1 */
    require_extern("pydos_obj_call_method_");

    int total_argc = argc + 1;  /* include self */
    emit_line("sub  sp, %d", total_argc * 4);
    emit_line("mov  si, sp");

    /* argv[0] = self (src1) */
    {
        int self_slot = current_func->num_locals + instr->src1;
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(self_slot));
        emit_line("mov  word ptr ss:[si], ax");
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(self_slot));
        emit_line("mov  word ptr ss:[si+2], ax");
    }

    /* argv[1..argc] = call args */
    {
        int a;
        for (a = 0; a < argc && a < num_call_args; a++) {
            int arg_slot = current_func->num_locals + arg_temps[a];
            emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(arg_slot));
            emit_line("mov  word ptr ss:[si+%d], ax", (a + 1) * 4);
            emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(arg_slot));
            emit_line("mov  word ptr ss:[si+%d], ax", (a + 1) * 4 + 2);
        }
    }

    /* Get the method name label */
    char mn_lbl[32];
    {
        int mn_idx = register_mn_string(method_name);
        sprintf(mn_lbl, "_MN_%d", mn_idx);
    }

    /* cdecl right-to-left for:
     * pydos_obj_call_method(const char far *name, unsigned int argc,
     *                       PyDosObj far * far *argv)
     * Self is in argv[0], NOT a separate parameter. */

    /* Push argv far ptr (SS:SI) — rightmost param */
    emit_line("push ss");
    emit_line("push si");

    /* Push argc */
    emit_push_imm(total_argc);

    /* Push method name far ptr — leftmost param */
    emit_push_far_const(mn_lbl);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_call_method_");
    /* Clean: name(4) + argc(2) + argv(4) + argv_array */
    emit_line("add  sp, %d", 10 + total_argc * 4);
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
    num_call_args = 0;
}

/* ================================================================= */
/* emit_return                                                        */
/* ================================================================= */

void CodeGenerator8086::emit_return(IRInstr *instr)
{
    /* IR_RETURN: return src1 (or void if src1 == -1) */
    if (instr->src1 >= 0) {
        emit_comment("RETURN t%d", instr->src1);
        load_temp_to_dxax(instr->src1);
    } else {
        emit_comment("RETURN void");
        /* Return NULL pointer (0:0) */
        emit_line("xor  ax, ax");
        emit_line("xor  dx, dx");
    }

    /* Jump to the function epilogue */
    emit_line("jmp  _%s_epilogue", current_func->name);
}

/* ================================================================= */
/* emit_incref                                                        */
/* ================================================================= */

void CodeGenerator8086::emit_incref(IRInstr *instr)
{
    /* IR_INCREF: incref src1 */
    emit_comment("INCREF t%d", instr->src1);
    require_extern("pydos_incref_");

    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_incref_");
    emit_line("add  sp, 4");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_decref                                                        */
/* ================================================================= */

void CodeGenerator8086::emit_decref(IRInstr *instr)
{
    /* IR_DECREF: decref src1 */
    emit_comment("DECREF t%d", instr->src1);
    require_extern("pydos_decref_");

    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_decref_");
    emit_line("add  sp, 4");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_build_list                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_build_list(IRInstr *instr)
{
    /* IR_BUILD_LIST: dest = new list from top 'extra' pushed args */
    int count = instr->extra;

    emit_comment("BUILD_LIST %d -> t%d", count, instr->dest);

    require_extern("pydos_list_new_");
    require_extern("pydos_list_append_");

    /* Create new empty list */
    emit_line("mov  ax, %d", count);
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_list_new_");
    emit_line("add  sp, 2");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);

    /* Append each element */
    int a;
    for (a = 0; a < count && a < num_call_args; a++) {
        /* Push element */
        push_temp(arg_temps[a]);
        /* Push list */
        push_temp(instr->dest);

        emit_restore_ds();
        emit_line("call far ptr pydos_list_append_");
        emit_line("add  sp, 8");
        emit_restore_ds();
    }

    num_call_args = 0;
}

/* ================================================================= */
/* emit_build_dict                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_build_dict(IRInstr *instr)
{
    /* IR_BUILD_DICT: dest = new dict from top 'extra' key-value pairs.
     * The args are pushed as: key0, val0, key1, val1, ... */
    int count = instr->extra;  /* number of key-value pairs */

    emit_comment("BUILD_DICT %d -> t%d", count, instr->dest);

    require_extern("pydos_dict_new_");
    require_extern("pydos_dict_set_");

    /* Create new empty dict */
    emit_line("mov  ax, %d", count > 0 ? count : 8);
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_dict_new_");
    emit_line("add  sp, 2");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);

    /* Set each key-value pair */
    int a;
    for (a = 0; a < count; a++) {
        int key_idx = a * 2;
        int val_idx = a * 2 + 1;

        if (key_idx >= num_call_args || val_idx >= num_call_args) break;

        /* Push value, key, dict (right-to-left) */
        push_temp(arg_temps[val_idx]);
        push_temp(arg_temps[key_idx]);
        push_temp(instr->dest);

        emit_restore_ds();
        emit_line("call far ptr pydos_dict_set_");
        emit_line("add  sp, 12");
        emit_restore_ds();
    }

    num_call_args = 0;
}

/* ================================================================= */
/* emit_build_tuple                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_build_tuple(IRInstr *instr)
{
    /* IR_BUILD_TUPLE: same as list for now (tuples use list storage) */
    int count = instr->extra;

    emit_comment("BUILD_TUPLE %d -> t%d", count, instr->dest);

    require_extern("pydos_list_new_");
    require_extern("pydos_list_append_");

    /* Create new empty list (used as tuple) */
    emit_line("mov  ax, %d", count);
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_list_new_");
    emit_line("add  sp, 2");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);

    /* Append each element */
    int a;
    for (a = 0; a < count && a < num_call_args; a++) {
        push_temp(arg_temps[a]);
        push_temp(instr->dest);

        emit_restore_ds();
        emit_line("call far ptr pydos_list_append_");
        emit_line("add  sp, 8");
        emit_restore_ds();
    }

    /* Change type tag from PYDT_LIST to PYDT_TUPLE */
    load_temp_to_dxax(instr->dest);
    emit_line("push ds");
    emit_line("mov  ds, dx");
    emit_line("mov  bx, ax");
    emit_line("mov  byte ptr [bx], 7");  /* PYDT_TUPLE */
    emit_line("pop  ds");

    num_call_args = 0;
}

/* ================================================================= */
/* emit_build_set                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_build_set(IRInstr *instr)
{
    /* IR_BUILD_SET: create dict (set uses dict with None values) */
    int count = instr->extra;

    emit_comment("BUILD_SET %d -> t%d", count, instr->dest);

    require_extern("pydos_dict_new_");
    require_extern("pydos_dict_set_");
    require_extern("pydos_obj_new_none_");

    /* Create new empty dict (used as set) */
    emit_line("mov  ax, %d", count > 0 ? count : 8);
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_dict_new_");
    emit_line("add  sp, 2");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);

    /* For each element, add to dict with None value */
    int a;
    for (a = 0; a < count && a < num_call_args; a++) {
        /* Get None as value */
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_none_");
        emit_restore_ds();
        emit_line("push dx");
        emit_line("push ax");

        /* Push key */
        push_temp(arg_temps[a]);
        /* Push dict */
        push_temp(instr->dest);

        emit_restore_ds();
        emit_line("call far ptr pydos_dict_set_");
        emit_line("add  sp, 12");
        emit_restore_ds();
    }

    /* Mark as set type: change type tag */
    /* Note: in runtime, PYDT_SET = 8 */
    load_temp_to_dxax(instr->dest);
    emit_line("push ds");
    emit_line("mov  ds, dx");
    emit_line("mov  bx, ax");
    emit_line("mov  byte ptr [bx], 8");  /* PYDT_SET */
    emit_line("pop  ds");

    num_call_args = 0;
}

/* ================================================================= */
/* emit_get_iter                                                      */
/* ================================================================= */

void CodeGenerator8086::emit_get_iter(IRInstr *instr)
{
    /* IR_GET_ITER: dest = iter(src1) */
    emit_comment("GET_ITER t%d -> t%d", instr->src1, instr->dest);
    require_extern("pydos_obj_get_iter_");

    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_get_iter_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_for_iter                                                      */
/* ================================================================= */

void CodeGenerator8086::emit_for_iter(IRInstr *instr)
{
    /* IR_FOR_ITER: dest = next(src1), jump to extra if StopIteration
     * Returns NULL (0:0) when iterator is exhausted. */
    emit_comment("FOR_ITER t%d -> t%d, end=_L%d", instr->src1, instr->dest, instr->extra);
    require_extern("pydos_obj_iter_next_");

    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_iter_next_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* Check if result is NULL (DX:AX == 0:0) */
    store_dxax_to_temp(instr->dest);
    emit_line("mov  cx, ax");
    emit_line("or   cx, dx");
    emit_line("jz   _L%d", instr->extra);
}

/* ================================================================= */
/* emit_load_attr                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_load_attr(IRInstr *instr)
{
    /* IR_LOAD_ATTR: dest = src1.attr  (src2 = attr name const index) */
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

    /* Push far pointer to attr name string */
    emit_push_far_const(lbl);
    /* Push object */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_get_attr_");
    emit_line("add  sp, 8");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_store_attr                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_store_attr(IRInstr *instr)
{
    /* IR_STORE_ATTR: src1.attr = src2  (dest = attr name const index) */
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
    /* Push far pointer to attr name string */
    emit_push_far_const(lbl);
    /* Push object */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_set_attr_");
    emit_line("add  sp, 12");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_load_subscript                                                */
/* ================================================================= */

void CodeGenerator8086::emit_load_subscript(IRInstr *instr)
{
    /* IR_LOAD_SUBSCRIPT: dest = src1[src2] */
    emit_comment("LOAD_SUBSCRIPT t%d[t%d] -> t%d", instr->src1, instr->src2, instr->dest);

    const char *sub_fn = resolve_subscript_func(instr->type_hint, 0);
    if (sub_fn && instr->type_hint &&
        (instr->type_hint->kind == TY_LIST || instr->type_hint->kind == TY_STR ||
         instr->type_hint->kind == TY_TUPLE)) {
        /* Integer-indexed types: unbox index, call fn(obj, long idx) */
        require_extern(sub_fn);

        int idx_slot = current_func->num_locals + instr->src2;
        emit_line("les  bx, dword ptr [bp%+d]", slot_offset_lo(idx_slot));
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);

        push_temp(instr->src1);

        emit_restore_ds();
        emit_line("call far ptr %s", sub_fn);
        emit_line("add  sp, 8");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);

    } else if (sub_fn && instr->type_hint &&
               instr->type_hint->kind == TY_DICT) {
        /* Dict: fn(dict, key_obj, default_obj) */
        require_extern(sub_fn);

        emit_push_imm(0);
        emit_push_imm(0);
        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_restore_ds();
        emit_line("call far ptr %s", sub_fn);
        emit_line("add  sp, 12");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);

    } else {
        /* Generic subscript */
        require_extern("pydos_obj_getitem_");

        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_restore_ds();
        emit_line("call far ptr pydos_obj_getitem_");
        emit_line("add  sp, 8");
        emit_restore_ds();

        store_dxax_to_temp(instr->dest);
    }
}

/* ================================================================= */
/* emit_load_slice                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_load_slice(IRInstr *instr)
{
    /* IR_LOAD_SLICE: dest = src1[start:stop:step]
     * start/stop/step were pushed as PUSH_ARG 0/1/2 and are in arg_temps.
     * We need to unbox the int values from the PyDosObj arg temps and
     * call pydos_str_slice_ or pydos_list_slice_(obj, start, stop, step).
     * The args are in the arg_temps from PUSH_ARG instructions. */

    emit_comment("LOAD_SLICE t%d = t%d[start:stop:step]", instr->dest, instr->src1);

    /* Determine which slice function to call via registry */
    const char *fn = resolve_slice_func(instr->type_hint);
    if (!fn) fn = "pydos_list_slice_";  /* fallback */
    require_extern(fn);

    /* Push step (arg 2) — unbox int from PyDosObj */
    {
        int step_slot = current_func->num_locals + arg_temps[2];
        emit_line("les  bx, dword ptr [bp%+d]", slot_offset_lo(step_slot));
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);
    }

    /* Push stop (arg 1) — unbox int */
    {
        int stop_slot = current_func->num_locals + arg_temps[1];
        emit_line("les  bx, dword ptr [bp%+d]", slot_offset_lo(stop_slot));
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);
    }

    /* Push start (arg 0) — unbox int */
    {
        int start_slot = current_func->num_locals + arg_temps[0];
        emit_line("les  bx, dword ptr [bp%+d]", slot_offset_lo(start_slot));
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);
    }

    /* Push object */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr %s", fn);
    emit_line("add  sp, 16");    /* 4 args: obj(4) + start(4) + stop(4) + step(4) = 16 */
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
    num_call_args = 0;
}

/* ================================================================= */
/* emit_exc_match                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_exc_match(IRInstr *instr)
{
    /* IR_EXC_MATCH: dest = pydos_exc_matches_(src1, src2)
     * src1 = exception object temp, src2 = type code (as constant) */
    emit_comment("EXC_MATCH t%d = t%d matches type %d", instr->dest, instr->src1, instr->src2);
    require_extern("pydos_exc_matches_");
    require_extern("pydos_obj_new_bool_");

    /* Push type_code (plain int) */
    emit_push_imm(instr->src2);

    /* Push exception object */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_exc_matches_");
    emit_line("add  sp, 6");     /* obj (4 bytes) + int (2 bytes) */
    emit_restore_ds();

    /* AX = 0 or 1 */
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_bool_");
    emit_line("add  sp, 2");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_store_subscript                                               */
/* ================================================================= */

void CodeGenerator8086::emit_store_subscript(IRInstr *instr)
{
    /* IR_STORE_SUBSCRIPT: src1[src2] = dest (dest = value temp) */
    emit_comment("STORE_SUBSCRIPT t%d[t%d] = t%d", instr->src1, instr->src2, instr->dest);

    const char *set_fn = resolve_subscript_func(instr->type_hint, 1);
    if (set_fn && instr->type_hint &&
        (instr->type_hint->kind == TY_LIST || instr->type_hint->kind == TY_TUPLE)) {
        /* Integer-indexed set: fn(obj, long idx, value) */
        require_extern(set_fn);

        push_temp(instr->dest);

        int idx_slot = current_func->num_locals + instr->src2;
        emit_line("les  bx, dword ptr [bp%+d]", slot_offset_lo(idx_slot));
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
        emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);

        push_temp(instr->src1);

        emit_restore_ds();
        emit_line("call far ptr %s", set_fn);
        emit_line("add  sp, 12");
        emit_restore_ds();

    } else if (set_fn && instr->type_hint &&
               instr->type_hint->kind == TY_DICT) {
        /* Dict set: fn(dict, key_obj, value_obj) */
        require_extern(set_fn);

        push_temp(instr->dest);
        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_restore_ds();
        emit_line("call far ptr %s", set_fn);
        emit_line("add  sp, 12");
        emit_restore_ds();

    } else {
        /* Generic set item */
        require_extern("pydos_obj_setitem_");

        push_temp(instr->dest);
        push_temp(instr->src2);
        push_temp(instr->src1);

        emit_restore_ds();
        emit_line("call far ptr pydos_obj_setitem_");
        emit_line("add  sp, 12");
        emit_restore_ds();
    }
}

/* ================================================================= */
/* emit_del_subscript                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_del_subscript(IRInstr *instr)
{
    emit_comment("DEL_SUBSCRIPT del t%d[t%d]", instr->src1, instr->src2);
    require_extern("pydos_obj_delitem_");
    push_temp(instr->src2);   /* key */
    push_temp(instr->src1);   /* obj */
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_delitem_");
    emit_line("add  sp, 8");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_del_local                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_del_local(IRInstr *instr)
{
    /* IR_DEL_LOCAL: DECREF local[src1], then zero the slot.
     * src1 = local slot index. */
    int slot = instr->src1;
    int lo = slot_offset_lo(slot);
    int hi = slot_offset_hi(slot);

    emit_comment("DEL_LOCAL slot=%d", slot);
    require_extern("pydos_decref_");

    /* Push old far pointer value for DECREF */
    emit_line("push word ptr [bp%+d]", hi);
    emit_line("push word ptr [bp%+d]", lo);
    emit_restore_ds();
    emit_line("call far ptr pydos_decref_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* Zero the slot (4 bytes: offset + segment) */
    emit_line("mov  word ptr [bp%+d], 0", lo);
    emit_line("mov  word ptr [bp%+d], 0", hi);
}

/* ================================================================= */
/* emit_del_global                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_del_global(IRInstr *instr)
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

    /* Push old far pointer value for DECREF */
    emit_line("push word ptr [%s+2]", aname);
    emit_line("push word ptr [%s]", aname);
    emit_restore_ds();
    emit_line("call far ptr pydos_decref_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* Zero the global (4 bytes: offset + segment) */
    emit_line("mov  word ptr [%s], 0", aname);
    emit_line("mov  word ptr [%s+2], 0", aname);
}

/* ================================================================= */
/* emit_del_attr                                                      */
/* ================================================================= */

void CodeGenerator8086::emit_del_attr(IRInstr *instr)
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

    /* Push far pointer to attr name string */
    emit_push_far_const(lbl);
    /* Push object */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_del_attr_");
    emit_line("add  sp, 8");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_setup_try                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_setup_try(IRInstr *instr)
{
    /* IR_SETUP_TRY: push exception frame, jump to 'extra' on exception.
     * Uses setjmp: allocate a frame, call _setjmp_, if non-zero
     * jump to the handler label. */
    emit_comment("SETUP_TRY handler=_L%d", instr->extra);

    require_extern("pydos_exc_alloc_frame_");
    require_extern("_setjmp_");

    /* Allocate exception frame -- returns far pointer to jmp_buf in DX:AX */
    emit_restore_ds();
    emit_line("call far ptr pydos_exc_alloc_frame_");
    emit_restore_ds();

    /* Call _setjmp_(jmp_buf far *) -- push DX:AX as arg */
    emit_line("push dx");
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr _setjmp_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* AX = 0 on initial call (normal path), non-zero on longjmp (exception) */
    emit_line("test ax, ax");
    emit_line("jnz  _L%d", instr->extra);
}

/* ================================================================= */
/* emit_pop_try                                                       */
/* ================================================================= */

void CodeGenerator8086::emit_pop_try(IRInstr *instr)
{
    /* IR_POP_TRY: pop the current exception frame */
    emit_comment("POP_TRY");
    require_extern("pydos_exc_pop_");

    emit_restore_ds();
    emit_line("call far ptr pydos_exc_pop_");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_raise                                                         */
/* ================================================================= */

void CodeGenerator8086::emit_raise(IRInstr *instr)
{
    /* IR_RAISE: raise src1 */
    emit_comment("RAISE t%d", instr->src1);
    require_extern("pydos_exc_raise_obj_");

    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_exc_raise_obj_");
    emit_line("add  sp, 4");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_reraise                                                       */
/* ================================================================= */

void CodeGenerator8086::emit_reraise(IRInstr *instr)
{
    /* IR_RERAISE: re-raise the current exception */
    emit_comment("RERAISE");
    require_extern("pydos_exc_current_");
    require_extern("pydos_exc_raise_obj_");

    /* Get the current exception object */
    emit_restore_ds();
    emit_line("call far ptr pydos_exc_current_");
    emit_restore_ds();

    /* Push DX:AX (the exception object) */
    emit_line("push dx");
    emit_line("push ax");

    /* Raise it */
    emit_restore_ds();
    emit_line("call far ptr pydos_exc_raise_obj_");
    emit_line("add  sp, 4");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_get_exception                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_get_exception(IRInstr *instr)
{
    /* IR_GET_EXCEPTION: dest = current exception object */
    emit_comment("GET_EXCEPTION -> t%d", instr->dest);
    require_extern("pydos_exc_current_");

    emit_restore_ds();
    emit_line("call far ptr pydos_exc_current_");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_alloc_obj                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_alloc_obj(IRInstr *instr)
{
    /* IR_ALLOC_OBJ: dest = allocate new empty object */
    emit_comment("ALLOC_OBJ -> t%d", instr->dest);
    require_extern("pydos_obj_alloc_");

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_alloc_");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_init_vtable                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_init_vtable(IRInstr *instr)
{
    /* IR_INIT_VTABLE: initialize vtable for class (extra = class_vtable_idx) */
    int vt_idx = instr->extra;
    if (vt_idx < 0 || vt_idx >= mod->num_class_vtables) {
        emit_comment("INIT_VTABLE: bad vtable index %d", vt_idx);
        return;
    }

    ClassVTableInfo *vti = &mod->class_vtables[vt_idx];
    emit_comment("INIT_VTABLE for %s (%d methods)", vti->class_name, vti->num_methods);

    char vt_label[64];
    sprintf(vt_label, "_VT_%s", vti->class_name);

    /* Create the vtable: pydos_vtable_create_() => DX:AX */
    require_extern("pydos_vtable_create_");
    emit_restore_ds();
    emit_line("call far ptr pydos_vtable_create_");
    emit_restore_ds();

    /* Store to vtable global */
    emit_line("mov  word ptr [%s], ax", vt_label);
    emit_line("mov  word ptr [%s+2], dx", vt_label);

    /* Set class name for repr fallback */
    {
        int cn_idx = register_mn_string(vti->class_name);
        char cn_lbl[32];
        sprintf(cn_lbl, "_MN_%d", cn_idx);

        require_extern("pydos_vtable_set_name_");
        emit_comment("  set class name '%s'", vti->class_name);

        /* Push far pointer to class name string */
        emit_push_far_const(cn_lbl);

        /* Push vtable far pointer */
        emit_line("push word ptr [%s+2]", vt_label);
        emit_line("push word ptr [%s]", vt_label);

        emit_restore_ds();
        emit_line("call far ptr pydos_vtable_set_name_");
        emit_line("add  sp, 8");
        emit_restore_ds();
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

        /* Ensure the mangled function is declared extern */
        if (!has_extern(func_lbl)) {
            require_extern(func_lbl);
        }

        emit_comment("  add method '%s' -> %s", py_name, func_lbl);

        /* Push far pointer to function implementation */
        emit_line("mov  ax, seg %s", func_lbl);
        emit_line("push ax");
        emit_line("mov  ax, offset %s", func_lbl);
        emit_line("push ax");

        /* Push far pointer to method name string */
        emit_push_far_const(mn_lbl);

        /* Push vtable far pointer */
        emit_line("push word ptr [%s+2]", vt_label);
        emit_line("push word ptr [%s]", vt_label);

        emit_restore_ds();
        emit_line("call far ptr pydos_vtable_add_method_");
        emit_line("add  sp, 12");
        emit_restore_ds();
    }

    /* Inherit from base class vtable(s) if applicable */
    if (vti->base_class_name && vti->base_class_name[0] != '\0') {
        require_extern("pydos_vtable_inherit_");

        char pvt_label[64];
        sprintf(pvt_label, "_VT_%s", vti->base_class_name);

        emit_comment("  inherit from %s", vti->base_class_name);

        /* Push parent vtable far pointer */
        emit_line("push word ptr [%s+2]", pvt_label);
        emit_line("push word ptr [%s]", pvt_label);

        /* Push child vtable far pointer */
        emit_line("push word ptr [%s+2]", vt_label);
        emit_line("push word ptr [%s]", vt_label);

        emit_restore_ds();
        emit_line("call far ptr pydos_vtable_inherit_");
        emit_line("add  sp, 8");
        emit_restore_ds();

        /* Inherit from additional bases (multiple inheritance) */
        {
            int ebi;
            for (ebi = 0; ebi < vti->num_extra_bases; ebi++) {
                char eb_label[64];
                sprintf(eb_label, "_VT_%s", vti->extra_bases[ebi]);

                emit_comment("  inherit from %s", vti->extra_bases[ebi]);

                emit_line("push word ptr [%s+2]", eb_label);
                emit_line("push word ptr [%s]", eb_label);
                emit_line("push word ptr [%s+2]", vt_label);
                emit_line("push word ptr [%s]", vt_label);

                emit_restore_ds();
                emit_line("call far ptr pydos_vtable_inherit_");
                emit_line("add  sp, 8");
                emit_restore_ds();
            }
        }
    }
}

/* ================================================================= */
/* emit_set_vtable                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_set_vtable(IRInstr *instr)
{
    /* IR_SET_VTABLE: set vtable on object
     * src1 = object temp, extra = class_vtable_idx */
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

    /* Push vtable far pointer */
    emit_line("push word ptr [%s+2]", vt_label);
    emit_line("push word ptr [%s]", vt_label);

    /* Push object far pointer */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_set_vtable_");
    emit_line("add  sp, 8");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_check_vtable                                                   */
/* CHECK_VTABLE: dest = isinstance_vtable(src1, _VT_ClassName)         */
/* ================================================================= */

void CodeGenerator8086::emit_check_vtable(IRInstr *instr)
{
    int vt_idx = instr->extra;
    if (vt_idx < 0 || vt_idx >= mod->num_class_vtables) {
        emit_comment("CHECK_VTABLE: bad vtable index %d", vt_idx);
        /* Return False */
        require_extern("pydos_obj_new_bool_");
        emit_line("push 0");
        emit_restore_ds();
        emit_line("call far ptr pydos_obj_new_bool_");
        emit_line("add  sp, 2");
        emit_restore_ds();
        store_dxax_to_temp(instr->dest);
        return;
    }

    ClassVTableInfo *vti = &mod->class_vtables[vt_idx];
    char vt_label[64];
    sprintf(vt_label, "_VT_%s", vti->class_name);

    emit_comment("CHECK_VTABLE t%d isinstance %s", instr->src1, vti->class_name);
    require_extern("pydos_obj_isinstance_vtable_");
    require_extern("pydos_obj_new_bool_");

    /* Push vtable far pointer (segment:offset) */
    emit_line("push word ptr [%s+2]", vt_label);
    emit_line("push word ptr [%s]", vt_label);

    /* Push object far pointer */
    push_temp(instr->src1);

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_isinstance_vtable_");
    emit_line("add  sp, 8");
    emit_restore_ds();

    /* Result is int (0/1) in AX; box it to a bool PyDosObj */
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_bool_");
    emit_line("add  sp, 2");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_make_function                                                  */
/* ================================================================= */

void CodeGenerator8086::emit_make_function(IRInstr *instr)
{
    /* IR_MAKE_FUNCTION: dest = create func obj
     * src1 = name const index (string label of the function)
     * The function's code pointer is the assembly label for that name. */
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

    /* Push name string far pointer (right-to-left: name, then code) */
    {
        char slbl[64];
        const_label(slbl, sizeof(slbl), name_ci);
        emit_push_far_const(slbl);
    }

    /* Push code far pointer (seg:offset of the function label) */
    emit_line("mov  ax, seg %s", flbl);
    emit_line("push ax");
    emit_line("mov  ax, offset %s", flbl);
    emit_line("push ax");

    emit_restore_ds();
    emit_line("call far ptr pydos_func_new_");
    emit_line("add  sp, 8");
    emit_restore_ds();

    /* If src2 != -1, store closure into func->closure (offset PYOBJ_V_OFFSET+8) */
    if (instr->src2 >= 0) {
        int clo_off = PYOBJ_V_OFFSET_16 + 8;
        emit_comment("store closure into func->closure");
        /* Save newly created func object (DX:AX) to its dest temp first */
        store_dxax_to_temp(instr->dest);
        /* Load func object into ES:BX */
        load_temp_to_dxax(instr->dest);
        emit_line("mov  bx, ax");
        emit_line("mov  es, dx");
        /* Load closure far ptr from src2 temp into DX:AX */
        load_temp_to_dxax(instr->src2);
        /* Store closure into func obj */
        emit_line("mov  word ptr es:[bx+%d], ax", clo_off);
        emit_line("mov  word ptr es:[bx+%d], dx", clo_off + 2);
        /* INCREF the closure */
        require_extern("pydos_incref_");
        push_temp(instr->src2);
        emit_restore_ds();
        emit_line("call far ptr pydos_incref_");
        emit_line("add  sp, 4");
        emit_restore_ds();
        /* Result is already in dest temp from store above */
        return;
    }

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_make_cell / emit_cell_get / emit_cell_set / emit_load_closure */
/* ================================================================= */

void CodeGenerator8086::emit_make_cell(IRInstr *instr)
{
    emit_comment("MAKE_CELL -> t%d", instr->dest);
    emit_restore_ds();
    emit_line("call far ptr pydos_cell_new_");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);
}

void CodeGenerator8086::emit_cell_get(IRInstr *instr)
{
    emit_comment("CELL_GET t%d -> t%d", instr->src1, instr->dest);
    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_cell_get_");
    emit_line("add  sp, 4");
    emit_restore_ds();
    store_dxax_to_temp(instr->dest);
}

void CodeGenerator8086::emit_cell_set(IRInstr *instr)
{
    emit_comment("CELL_SET t%d, t%d", instr->src1, instr->src2);
    /* Push value (src2) then cell (src1) — right-to-left */
    push_temp(instr->src2);
    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_cell_set_");
    emit_line("add  sp, 8");
    emit_restore_ds();
}

void CodeGenerator8086::emit_load_closure(IRInstr *instr)
{
    emit_comment("LOAD_CLOSURE -> t%d", instr->dest);
    emit_line("mov  ax, word ptr [pydos_active_closure_]");
    emit_line("mov  dx, word ptr [pydos_active_closure_+2]");
    store_dxax_to_temp(instr->dest);
}

void CodeGenerator8086::emit_set_closure(IRInstr *instr)
{
    emit_comment("SET_CLOSURE t%d", instr->src1);
    load_temp_to_dxax(instr->src1);
    emit_line("mov  word ptr [pydos_active_closure_], ax");
    emit_line("mov  word ptr [pydos_active_closure_+2], dx");
}

/* ================================================================= */
/* emit_make_generator                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_make_generator(IRInstr *instr)
{
    /* IR_MAKE_GENERATOR: dest = create gen obj
     * src1 = name const index (resume function label)
     * extra = num_locals */
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

    /* Push num_locals (int, 2 bytes) */
    emit_push_imm(instr->extra);

    /* Push resume function far pointer */
    emit_line("mov  ax, seg %s", flbl);
    emit_line("push ax");
    emit_line("mov  ax, offset %s", flbl);
    emit_line("push ax");

    emit_restore_ds();
    emit_line("call far ptr pydos_gen_new_");
    emit_line("add  sp, 6");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_make_coroutine                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_make_coroutine(IRInstr *instr)
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

    /* Push num_locals (int, 2 bytes) */
    emit_push_imm(instr->extra);

    /* Push resume function far pointer */
    emit_line("mov  ax, seg %s", flbl);
    emit_line("push ax");
    emit_line("mov  ax, offset %s", flbl);
    emit_line("push ax");

    emit_restore_ds();
    emit_line("call far ptr pydos_cor_new_");
    emit_line("add  sp, 6");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_cor_set_result                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_cor_set_result(IRInstr *instr)
{
    /* IR_COR_SET_RESULT: cor->v.gen.state = src2
     * src1 = cor temp, src2 = value temp
     *
     * PyDosGen layout after PYOBJ_V_OFFSET_16 (4 for 16-bit):
     *   resume: 4 bytes (far ptr)   at +4
     *   state:  4 bytes (far ptr)   at +8  (PYOBJ_V_OFFSET_16 + 4)
     *
     * We must INCREF the new value and DECREF the old state. */
    int gen_temp = instr->src1;
    int vt = instr->src2;
    int state_off = PYOBJ_V_OFFSET_16 + 4;  /* offset of gen.state in PyDosObj */

    emit_comment("COR_SET_RESULT: t%d->state = t%d", gen_temp, vt);

    /* Load gen far pointer into ES:BX */
    int gen_slot = current_func->num_locals + gen_temp;
    emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(gen_slot));
    emit_line("mov  es, word ptr [bp%+d]", slot_offset_hi(gen_slot));

    /* Push old state far pointer for later DECREF (seg first, off second) */
    emit_line("push word ptr es:[bx+%d]", state_off + 2); /* seg */
    emit_line("push word ptr es:[bx+%d]", state_off);     /* off */

    /* Load new value far ptr into DX:AX (doesn't trash ES:BX) */
    load_temp_to_dxax(vt);

    /* Store new value into gen->state */
    emit_line("mov  word ptr es:[bx+%d], ax", state_off);
    emit_line("mov  word ptr es:[bx+%d], dx", state_off + 2);

    /* INCREF new value */
    emit_line("push dx");
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_incref_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* DECREF old state (already on stack from above) */
    emit_restore_ds();
    emit_line("call far ptr pydos_decref_");
    emit_line("add  sp, 4");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_gen_load_pc                                                    */
/* ================================================================= */

void CodeGenerator8086::emit_gen_load_pc(IRInstr *instr)
{
    /* dest = gen->pc; src1 = gen temp
     * PyDosGen.pc is at PYOBJ_V_OFFSET + 8 on 16-bit:
     *   resume(4) + state(4) = 8, then pc(int, 2 bytes)
     * We box the result via pydos_obj_new_int_ so dispatch
     * can use standard IR_CMP_EQ / IR_CMP_LT on PyDosObj. */
    emit_comment("GEN_LOAD_PC t%d -> t%d", instr->src1, instr->dest);

    /* Load gen far pointer into ES:BX */
    int gen_slot = current_func->num_locals + instr->src1;
    emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(gen_slot));
    emit_line("mov  es, word ptr [bp%+d]", slot_offset_hi(gen_slot));

    /* Read gen->v.gen.pc (at offset V+8) — raw 16-bit int */
    emit_line("mov  ax, word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 8);
    emit_line("cwd");  /* sign-extend AX to DX:AX (long) */

    /* Box into PyDosObj: push long value, call pydos_obj_new_int_ */
    emit_line("push dx");
    emit_line("push ax");
    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_int_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    /* Store boxed result (DX:AX) */
    int dest_slot = current_func->num_locals + instr->dest;
    emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(dest_slot));
    emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(dest_slot));
}

/* ================================================================= */
/* emit_gen_set_pc                                                     */
/* ================================================================= */

void CodeGenerator8086::emit_gen_set_pc(IRInstr *instr)
{
    /* gen->pc = extra; src1 = gen temp */
    emit_comment("GEN_SET_PC t%d = %d", instr->src1, instr->extra);

    int gen_slot = current_func->num_locals + instr->src1;
    emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(gen_slot));
    emit_line("mov  es, word ptr [bp%+d]", slot_offset_hi(gen_slot));

    emit_line("mov  word ptr es:[bx+%d], %d", PYOBJ_V_OFFSET_16 + 8, instr->extra);
}

/* ================================================================= */
/* emit_gen_load_local                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_gen_load_local(IRInstr *instr)
{
    /* dest = gen->locals[extra]; src1 = gen temp, extra = index */
    emit_comment("GEN_LOAD_LOCAL t%d = gen_t%d.locals[%d]", instr->dest, instr->src1, instr->extra);
    require_extern("pydos_list_get_");

    /* Push index (long) */
    emit_push_imm(0);
    emit_push_imm(instr->extra);

    /* Push gen->locals (far pointer) */
    int gen_slot = current_func->num_locals + instr->src1;
    emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(gen_slot));
    emit_line("mov  es, word ptr [bp%+d]", slot_offset_hi(gen_slot));
    /* gen->v.gen.locals is at PYOBJ_V_OFFSET + 10 (16-bit):
     * resume(4) + state(4) + pc(2) = 10, then locals is a far ptr (4 bytes) */
    emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 10 + 2);  /* seg */
    emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 10);      /* off */

    emit_restore_ds();
    emit_line("call far ptr pydos_list_get_");
    emit_line("add  sp, 8");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* emit_gen_save_local                                                 */
/* ================================================================= */

void CodeGenerator8086::emit_gen_save_local(IRInstr *instr)
{
    /* gen->locals[extra] = src2; src1 = gen temp, extra = index */
    emit_comment("GEN_SAVE_LOCAL gen_t%d.locals[%d] = t%d", instr->src1, instr->extra, instr->src2);
    require_extern("pydos_list_set_");

    /* Push value (far ptr, src2) */
    push_temp(instr->src2);

    /* Push index (long) */
    emit_push_imm(0);
    emit_push_imm(instr->extra);

    /* Push gen->locals (far pointer) */
    int gen_slot = current_func->num_locals + instr->src1;
    emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(gen_slot));
    emit_line("mov  es, word ptr [bp%+d]", slot_offset_hi(gen_slot));
    emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 10 + 2);
    emit_line("push word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 10);

    emit_restore_ds();
    emit_line("call far ptr pydos_list_set_");
    emit_line("add  sp, 12");
    emit_restore_ds();
}

/* ================================================================= */
/* emit_gen_check_throw / emit_gen_get_sent                          */
/* ================================================================= */

void CodeGenerator8086::emit_gen_check_throw(IRInstr *instr)
{
    /* call pydos_gen_check_throw_(gen) → returns int in AX (0=ok, 1=throw)
     * If throw detected: return NULL far ptr (0:0) from resume function
     * via mini-epilogue. Exception is raised by pydos_gen_throw() on
     * the current C stack. */
    int lbl_ok = new_label();

    emit_comment("GEN_CHECK_THROW t%d", instr->src1);
    push_temp(instr->src1);
    emit_restore_ds();
    emit_line("call far ptr pydos_gen_check_throw_");
    emit_line("add  sp, 4");
    emit_restore_ds();
    emit_line("test ax, ax");
    emit_line("jz   _L%d", lbl_ok);

    /* Throw detected — return NULL far ptr (DX:AX = 0:0).
     * Mini-epilogue: restore callee-saved regs via known BP offsets,
     * then mov sp,bp to reset stack.  Avoids 'lea sp,[bp-N]'
     * which may have WASM encoding issues on some DOS environments. */
    emit_line("xor  ax, ax");
    emit_line("xor  dx, dx");
    emit_line("mov  di, word ptr [bp-%d]", SAVE_AREA);
    emit_line("mov  si, word ptr [bp-%d]", SAVE_AREA - 2);
    emit_line("mov  sp, bp");
    emit_line("pop  bp");
    emit_line("retf");

    emit_local_label(lbl_ok);
}

void CodeGenerator8086::emit_gen_get_sent(IRInstr *instr)
{
    /* dest = pydos_gen_sent_ global; transfer ownership (no clear needed,
     * the next send/next will overwrite it) */
    emit_comment("GEN_GET_SENT -> t%d", instr->dest);
    emit_line("mov  ax, word ptr [pydos_gen_sent_]");
    emit_line("mov  dx, word ptr [pydos_gen_sent_+2]");
    store_dxax_to_temp(instr->dest);
}

/* ================================================================= */
/* Typed i32 arithmetic (8086: DX:AX 32-bit pairs on stack)          */
/* ================================================================= */

void CodeGenerator8086::emit_arith_i32(IRInstr *instr)
{
    int s1_slot = current_func->num_locals + instr->src1;
    int s2_slot = current_func->num_locals + instr->src2;
    int d_slot  = current_func->num_locals + instr->dest;

    emit_comment("ARITH_I32 %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1,
                 instr->src2 >= 0 ? instr->src2 : 0, instr->dest);

    switch (instr->op) {
    case IR_ADD_I32:
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("mov  dx, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("add  ax, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        emit_line("adc  dx, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(d_slot));
        break;

    case IR_SUB_I32:
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("mov  dx, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("sub  ax, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        emit_line("sbb  dx, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(d_slot));
        break;

    case IR_MUL_I32:
        /* Watcom helper: __I4M: DX:AX = DX:AX * CX:BX */
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("mov  dx, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        emit_line("mov  cx, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("call far ptr __I4M");
        emit_restore_ds();
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(d_slot));
        break;

    case IR_DIV_I32:
        /* Watcom helper: __I4D: DX:AX = DX:AX / CX:BX (rem in CX:BX) */
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("mov  dx, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        emit_line("mov  cx, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("call far ptr __I4D");
        emit_restore_ds();
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(d_slot));
        break;

    case IR_MOD_I32:
        /* Watcom helper: __I4D: remainder in CX:BX after DX:AX / CX:BX */
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("mov  dx, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("mov  bx, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        emit_line("mov  cx, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("call far ptr __I4D");
        emit_restore_ds();
        /* Remainder is in CX:BX */
        emit_line("mov  word ptr [bp%+d], bx", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], cx", slot_offset_hi(d_slot));
        break;

    case IR_NEG_I32:
        s1_slot = current_func->num_locals + instr->src1;
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("mov  dx, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("neg  dx");
        emit_line("neg  ax");
        emit_line("sbb  dx, 0");
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(d_slot));
        break;

    default:
        emit_comment("UNIMPLEMENTED i32 arith: %s", irop_name(instr->op));
        break;
    }
}

/* ================================================================= */
/* Typed i32 comparison (8086: 32-bit compare on stack)               */
/* ================================================================= */

void CodeGenerator8086::emit_cmp_i32(IRInstr *instr)
{
    int s1_slot = current_func->num_locals + instr->src1;
    int s2_slot = current_func->num_locals + instr->src2;
    int true_label = new_label();
    int end_label = new_label();
    const char *jmp_instr = "je";

    emit_comment("CMP_I32 %s t%d, t%d -> t%d",
                 irop_name(instr->op), instr->src1, instr->src2, instr->dest);

    /* Determine the jump instruction for the comparison.
     * We compare high words first, then low words for multi-word compare. */
    switch (instr->op) {
    case IR_CMP_I32_EQ: jmp_instr = "je";  break;
    case IR_CMP_I32_NE: jmp_instr = "jne"; break;
    case IR_CMP_I32_LT: jmp_instr = "jl";  break;
    case IR_CMP_I32_LE: jmp_instr = "jle"; break;
    case IR_CMP_I32_GT: jmp_instr = "jg";  break;
    case IR_CMP_I32_GE: jmp_instr = "jge"; break;
    default: break;
    }

    /* For equality/inequality, compare both words */
    if (instr->op == IR_CMP_I32_EQ || instr->op == IR_CMP_I32_NE) {
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        if (instr->op == IR_CMP_I32_EQ) {
            int ne_label = new_label();
            emit_line("jne  _L%d", ne_label);
            emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(s1_slot));
            emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_hi(s2_slot));
            emit_line("je   _L%d", true_label);
            emit_local_label(ne_label);
        } else {
            emit_line("jne  _L%d", true_label);
            emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(s1_slot));
            emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_hi(s2_slot));
            emit_line("jne  _L%d", true_label);
        }
    } else {
        /* Signed ordering: compare high word first, then low word unsigned */
        int hi_done = new_label();
        /* For high-word: use strict version (LE->jl, GE->jg) since
           equal high words need low-word comparison */
        const char *hi_jmp = jmp_instr;
        if (instr->op == IR_CMP_I32_LE) hi_jmp = "jl";
        else if (instr->op == IR_CMP_I32_GE) hi_jmp = "jg";
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_hi(s1_slot));
        emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_hi(s2_slot));
        emit_line("%s   _L%d", hi_jmp, true_label);
        emit_line("jne  _L%d", hi_done);
        /* High words equal — compare low words (unsigned) */
        emit_line("mov  ax, word ptr [bp%+d]", slot_offset_lo(s1_slot));
        emit_line("cmp  ax, word ptr [bp%+d]", slot_offset_lo(s2_slot));
        /* For unsigned low-word compare, map signed jump to unsigned */
        {
            const char *ujmp = "je";
            switch (instr->op) {
            case IR_CMP_I32_LT: ujmp = "jb";  break;
            case IR_CMP_I32_LE: ujmp = "jbe"; break;
            case IR_CMP_I32_GT: ujmp = "ja";  break;
            case IR_CMP_I32_GE: ujmp = "jae"; break;
            default: break;
            }
            emit_line("%s   _L%d", ujmp, true_label);
        }
        emit_local_label(hi_done);
    }

    /* False path: store unboxed 0 */
    {
        int d_slot = current_func->num_locals + instr->dest;
        emit_line("xor  ax, ax");
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], ax", slot_offset_hi(d_slot));
    }
    emit_line("jmp  _L%d", end_label);

    /* True path: store unboxed 1 */
    emit_local_label(true_label);
    {
        int d_slot = current_func->num_locals + instr->dest;
        emit_line("mov  word ptr [bp%+d], 1", slot_offset_lo(d_slot));
        emit_line("mov  word ptr [bp%+d], 0", slot_offset_hi(d_slot));
    }

    emit_local_label(end_label);
}

/* ================================================================= */
/* Box/Unbox (8086)                                                   */
/* ================================================================= */

void CodeGenerator8086::emit_box_int(IRInstr *instr)
{
    /* dest:pyobj = pydos_obj_new_int_(src1:i32) */
    int s1_slot = current_func->num_locals + instr->src1;

    emit_comment("BOX_INT t%d -> t%d", instr->src1, instr->dest);

    /* Push 32-bit int value (high word, then low word) */
    emit_line("push word ptr [bp%+d]", slot_offset_hi(s1_slot));
    emit_line("push word ptr [bp%+d]", slot_offset_lo(s1_slot));

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_int_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

void CodeGenerator8086::emit_unbox_int(IRInstr *instr)
{
    /* dest:i32 = src1->v.int_val */
    int s1_slot = current_func->num_locals + instr->src1;
    int d_slot  = current_func->num_locals + instr->dest;

    emit_comment("UNBOX_INT t%d -> t%d", instr->src1, instr->dest);

    /* Load far pointer, dereference v.int_val */
    emit_line("les  bx, dword ptr [bp%+d]", slot_offset_lo(s1_slot));
    emit_line("mov  ax, word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16);
    emit_line("mov  dx, word ptr es:[bx+%d]", PYOBJ_V_OFFSET_16 + 2);
    emit_line("mov  word ptr [bp%+d], ax", slot_offset_lo(d_slot));
    emit_line("mov  word ptr [bp%+d], dx", slot_offset_hi(d_slot));
}

void CodeGenerator8086::emit_box_bool(IRInstr *instr)
{
    /* dest:pyobj = pydos_obj_new_bool_(src1:bool) */
    int s1_slot = current_func->num_locals + instr->src1;

    emit_comment("BOX_BOOL t%d -> t%d", instr->src1, instr->dest);

    /* Push 32-bit value (the bool is stored in a 4-byte slot) */
    emit_line("push word ptr [bp%+d]", slot_offset_hi(s1_slot));
    emit_line("push word ptr [bp%+d]", slot_offset_lo(s1_slot));

    emit_restore_ds();
    emit_line("call far ptr pydos_obj_new_bool_");
    emit_line("add  sp, 4");
    emit_restore_ds();

    store_dxax_to_temp(instr->dest);
}

void CodeGenerator8086::emit_str_join(IRInstr *instr)
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

    /* Push count (unsigned int, 2 bytes on 16-bit) */
    emit_line("mov  ax, %d", count);
    emit_line("push ax");

    emit_restore_ds();
    emit_line("call far ptr pydos_str_join_n_");

    /* Clean up: 2 (count) + 4*count (far pointers) */
    stack_bytes = 2 + 4 * count;
    emit_line("add  sp, %d", stack_bytes);
    emit_restore_ds();

    /* Result in DX:AX */
    store_dxax_to_temp(instr->dest);

    num_call_args = 0;
}

/* ================================================================ */
/* Arena scope operations                                            */
/* ================================================================ */

void CodeGenerator8086::emit_scope_enter(IRInstr *instr)
{
    (void)instr;
    emit_line("; SCOPE_ENTER");
    emit_line("call far ptr pydos_arena_scope_enter_");
    emit_restore_ds();
}

void CodeGenerator8086::emit_scope_track(IRInstr *instr)
{
    int slot = current_func->num_locals + instr->src1;
    int lo = slot_offset_lo(slot);
    int hi = slot_offset_hi(slot);
    emit_line("; SCOPE_TRACK t%d", instr->src1);
    emit_line("push word ptr [bp%+d]", hi);
    emit_line("push word ptr [bp%+d]", lo);
    emit_line("call far ptr pydos_arena_scope_track_");
    emit_line("add  sp, 4");
    emit_restore_ds();
}

void CodeGenerator8086::emit_scope_exit(IRInstr *instr)
{
    (void)instr;
    emit_line("; SCOPE_EXIT");
    emit_line("call far ptr pydos_arena_scope_exit_");
    emit_restore_ds();
}
