/*
 * codegen.cpp - Shared code generation base class for PyDOS compiler
 *
 * Implements CodeGeneratorBase: the architecture-independent infrastructure
 * used by all concrete back-ends (8086, 386).  This includes:
 *
 *   - External symbol tracking (require_extern / has_extern)
 *   - Global variable registration (_G_name)
 *   - VTable global and method-name string management
 *   - Dead string constant elimination (mark_const_used / used_const[])
 *   - Assembly emission helpers (emit_line, emit_comment, emit_label, etc.)
 *   - Label management (new_label, emit_local_label, const_label, func_label)
 *   - Runtime function name mapping (arith, cmp, bitwise, unary, builtins)
 *   - Pre-scan passes (prescan_module / prescan_func)
 *   - Instruction dispatch (emit_instr -- calls virtual emitters)
 *   - Main entry point (generate)
 *   - Factory function (create_codegen)
 *
 * Concrete back-ends live in codegen_8086.cpp and codegen_386.cpp.
 *
 * C++98 compatible.  No STL -- arrays, manual memory only.
 */

#include "codegen.h"
#include "cg8086.h"
#include "cg386.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

/* --------------------------------------------------------------- */
/* CodeGeneratorBase constructor / destructor                       */
/* --------------------------------------------------------------- */

CodeGeneratorBase::CodeGeneratorBase()
{
    out = 0;
    mod = 0;
    current_func = 0;
    current_alloc = 0;
    verbose = 0;
    label_counter = 0;
    target = 0;
    stdlib_reg_ = 0;
    module_name = 0;
    is_main_module = 1;
    has_main_func = 0;
    entry_func = 0;
    num_externs = 0;
    num_data_externs = 0;
    num_globals = 0;
    num_call_args = 0;
    num_vtable_globals = 0;
    num_mn_strings = 0;
    num_used_const = 0;

    externs_cap = 64;
    externs = (const char **)malloc(sizeof(const char *) * externs_cap);
    if (externs) memset(externs, 0, sizeof(const char *) * externs_cap);

    data_externs_cap = 8;
    data_externs = (const char **)malloc(sizeof(const char *) * data_externs_cap);
    if (data_externs) memset(data_externs, 0, sizeof(const char *) * data_externs_cap);

    globals_cap = 64;
    globals = (GlobalVar *)malloc(sizeof(GlobalVar) * globals_cap);
    if (globals) memset(globals, 0, sizeof(GlobalVar) * globals_cap);

    vtable_globals_cap = 32;
    vtable_globals = (VTableGlobal *)malloc(sizeof(VTableGlobal) * vtable_globals_cap);
    if (vtable_globals) memset(vtable_globals, 0, sizeof(VTableGlobal) * vtable_globals_cap);

    mn_strings_cap = 64;
    mn_strings = (MethodNameStr *)malloc(sizeof(MethodNameStr) * mn_strings_cap);
    if (mn_strings) memset(mn_strings, 0, sizeof(MethodNameStr) * mn_strings_cap);

    used_const_cap = 128;
    used_const = (int *)malloc(sizeof(int) * used_const_cap);
    if (used_const) memset(used_const, 0, sizeof(int) * used_const_cap);

    arg_temps_cap = 64;
    arg_temps = (int *)malloc(sizeof(int) * arg_temps_cap);
    if (arg_temps) memset(arg_temps, 0, sizeof(int) * arg_temps_cap);
}

CodeGeneratorBase::~CodeGeneratorBase()
{
    /* out is closed in generate() */
    if (externs) free(externs);
    if (globals) free(globals);
    if (vtable_globals) free(vtable_globals);
    if (mn_strings) free(mn_strings);
    if (used_const) free(used_const);
    if (arg_temps) free(arg_temps);
}

void CodeGeneratorBase::set_verbose(int v)
{
    verbose = v;
}

void CodeGeneratorBase::set_target(int t)
{
    target = t;
}

void CodeGeneratorBase::set_stdlib(StdlibRegistry *reg)
{
    stdlib_reg_ = reg;
}

/* --------------------------------------------------------------- */
/* String duplication helper (C89 compatible, no strdup dependency)  */
/* --------------------------------------------------------------- */

static char *codegen_strdup(const char *s)
{
    int len = (int)strlen(s) + 1;
    char *d = (char *)malloc(len);
    if (d) memcpy(d, s, len);
    return d;
}

/* --------------------------------------------------------------- */
/* Grow helpers for dynamic arrays                                  */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::grow_externs()
{
    int new_cap = externs_cap * 2;
    const char **np = (const char **)malloc(sizeof(const char *) * new_cap);
    if (!np) { fprintf(stderr, "codegen: out of memory (externs)\n"); exit(1); }
    memset(np, 0, sizeof(const char *) * new_cap);
    if (externs) { memcpy(np, externs, sizeof(const char *) * num_externs); free(externs); }
    externs = np;
    externs_cap = new_cap;
}

void CodeGeneratorBase::grow_globals()
{
    int new_cap = globals_cap * 2;
    GlobalVar *np = (GlobalVar *)malloc(sizeof(GlobalVar) * new_cap);
    if (!np) { fprintf(stderr, "codegen: out of memory (globals)\n"); exit(1); }
    memset(np, 0, sizeof(GlobalVar) * new_cap);
    if (globals) { memcpy(np, globals, sizeof(GlobalVar) * num_globals); free(globals); }
    globals = np;
    globals_cap = new_cap;
}

void CodeGeneratorBase::grow_vtable_globals()
{
    int new_cap = vtable_globals_cap * 2;
    VTableGlobal *np = (VTableGlobal *)malloc(sizeof(VTableGlobal) * new_cap);
    if (!np) { fprintf(stderr, "codegen: out of memory (vtables)\n"); exit(1); }
    memset(np, 0, sizeof(VTableGlobal) * new_cap);
    if (vtable_globals) { memcpy(np, vtable_globals, sizeof(VTableGlobal) * num_vtable_globals); free(vtable_globals); }
    vtable_globals = np;
    vtable_globals_cap = new_cap;
}

void CodeGeneratorBase::grow_mn_strings()
{
    int new_cap = mn_strings_cap * 2;
    MethodNameStr *np = (MethodNameStr *)malloc(sizeof(MethodNameStr) * new_cap);
    if (!np) { fprintf(stderr, "codegen: out of memory (mn_strings)\n"); exit(1); }
    memset(np, 0, sizeof(MethodNameStr) * new_cap);
    if (mn_strings) { memcpy(np, mn_strings, sizeof(MethodNameStr) * num_mn_strings); free(mn_strings); }
    mn_strings = np;
    mn_strings_cap = new_cap;
}

void CodeGeneratorBase::grow_used_const(int needed)
{
    int new_cap = used_const_cap;
    while (new_cap < needed) new_cap *= 2;
    if (new_cap == used_const_cap) return;
    int *np = (int *)malloc(sizeof(int) * new_cap);
    if (!np) { fprintf(stderr, "codegen: out of memory (used_const)\n"); exit(1); }
    memset(np, 0, sizeof(int) * new_cap);
    if (used_const) { memcpy(np, used_const, sizeof(int) * used_const_cap); free(used_const); }
    used_const = np;
    used_const_cap = new_cap;
}

void CodeGeneratorBase::grow_arg_temps()
{
    int new_cap = arg_temps_cap * 2;
    int *np = (int *)malloc(sizeof(int) * new_cap);
    if (!np) { fprintf(stderr, "codegen: out of memory (arg_temps)\n"); exit(1); }
    memset(np, 0, sizeof(int) * new_cap);
    if (arg_temps) { memcpy(np, arg_temps, sizeof(int) * num_call_args); free(arg_temps); }
    arg_temps = np;
    arg_temps_cap = new_cap;
}

void CodeGeneratorBase::mark_const_used(int ci)
{
    if (ci >= 0) {
        if (ci >= used_const_cap) grow_used_const(ci + 1);
        used_const[ci] = 1;
    }
}

/* --------------------------------------------------------------- */
/* Assembly emission helpers                                        */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::emit_line(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(out, "    ");
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    va_end(ap);
}

void CodeGeneratorBase::emit_comment(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(out, "    ; ");
    vfprintf(out, fmt, ap);
    fprintf(out, "\n");
    va_end(ap);
}

void CodeGeneratorBase::emit_label(const char *name)
{
    fprintf(out, "%s:\n", name);
}

void CodeGeneratorBase::emit_blank()
{
    fprintf(out, "\n");
}

int CodeGeneratorBase::new_label()
{
    return label_counter++;
}

void CodeGeneratorBase::emit_local_label(int label_id)
{
    fprintf(out, "_L%d:\n", label_id);
}

/* --------------------------------------------------------------- */
/* External symbol management                                       */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::require_extern(const char *name)
{
    if (has_extern(name)) return;
    if (num_externs >= externs_cap) grow_externs();
    externs[num_externs++] = name;
}

int CodeGeneratorBase::has_extern(const char *name) const
{
    int i;
    for (i = 0; i < num_externs; i++) {
        if (strcmp(externs[i], name) == 0) return 1;
    }
    for (i = 0; i < num_data_externs; i++) {
        if (strcmp(data_externs[i], name) == 0) return 1;
    }
    return 0;
}

void CodeGeneratorBase::require_data_extern(const char *name)
{
    int i;
    /* Check both lists for duplicates */
    for (i = 0; i < num_data_externs; i++) {
        if (strcmp(data_externs[i], name) == 0) return;
    }
    if (has_extern(name)) return;
    if (num_data_externs >= data_externs_cap) {
        int new_cap = data_externs_cap * 2;
        const char **np = (const char **)malloc(sizeof(const char *) * new_cap);
        if (!np) { fprintf(stderr, "codegen: out of memory (data_externs)\n"); exit(1); }
        memset(np, 0, sizeof(const char *) * new_cap);
        if (data_externs) { memcpy(np, data_externs, sizeof(const char *) * num_data_externs); free(data_externs); }
        data_externs = np;
        data_externs_cap = new_cap;
    }
    data_externs[num_data_externs++] = name;
}

/* --------------------------------------------------------------- */
/* Global variable management                                       */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::register_global(const char *name)
{
    int i;
    for (i = 0; i < num_globals; i++) {
        if (strcmp(globals[i].name, name) == 0) return;
    }
    if (num_globals >= globals_cap) grow_globals();

    globals[num_globals].name = name;
    /* Build assembly name: _G_<name> or _G_modname__<name> */
    char *buf = globals[num_globals].asm_name;
    if (module_name) {
        sprintf(buf, "_G_%s__%.*s", module_name, 40, name);
    } else {
        strcpy(buf, "_G_");
        strncat(buf, name, 58);
    }
    buf[63] = '\0';
    num_globals++;
}

const char *CodeGeneratorBase::global_asm_name(const char *name)
{
    int i;
    for (i = 0; i < num_globals; i++) {
        if (strcmp(globals[i].name, name) == 0) {
            return globals[i].asm_name;
        }
    }
    /* Auto-register if not found */
    register_global(name);
    return globals[num_globals - 1].asm_name;
}

/* --------------------------------------------------------------- */
/* Method name string registration for vtable method names          */
/* --------------------------------------------------------------- */

int CodeGeneratorBase::register_mn_string(const char *name)
{
    /* Check if already registered */
    int i;
    for (i = 0; i < num_mn_strings; i++) {
        if (strcmp(mn_strings[i].str_data, name) == 0) {
            return i;
        }
    }
    if (num_mn_strings >= mn_strings_cap) grow_mn_strings();
    int idx = num_mn_strings;
    sprintf(mn_strings[idx].label, "_MN_%d", idx);
    mn_strings[idx].str_data = name;
    num_mn_strings++;
    return idx;
}

/* --------------------------------------------------------------- */
/* String constant label                                            */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::const_label(char *buf, int bufsize, int const_idx)
{
    /* _SC0, _SC1, _SC2, ... */
    sprintf(buf, "_SC%d", const_idx);
}

void CodeGeneratorBase::func_label(char *buf, int bufsize, const char *funcname)
{
    if (module_name) {
        /* Module-qualified: _modname__funcname */
        sprintf(buf, "_%s__%s", module_name, funcname);
    } else {
        /* Prefix with underscore for C linkage */
        sprintf(buf, "_%s", funcname);
    }
}

/* --------------------------------------------------------------- */
/* Runtime function name mapping                                    */
/* --------------------------------------------------------------- */

const char *CodeGeneratorBase::runtime_arith_func(IROp op)
{
    switch (op) {
        case IR_ADD:      return "pydos_int_add_";
        case IR_SUB:      return "pydos_int_sub_";
        case IR_MUL:      return "pydos_int_mul_";
        case IR_DIV:      return "pydos_int_truediv_";
        case IR_FLOORDIV: return "pydos_int_div_";
        case IR_MOD:      return "pydos_int_mod_";
        case IR_POW:      return "pydos_int_pow_";
        default:          return "pydos_int_add_";
    }
}

const char *CodeGeneratorBase::runtime_bitwise_func(IROp op)
{
    switch (op) {
        case IR_BITAND: return "pydos_int_bitand_";
        case IR_BITOR:  return "pydos_int_bitor_";
        case IR_BITXOR: return "pydos_int_bitxor_";
        case IR_SHL:    return "pydos_int_shl_";
        case IR_SHR:    return "pydos_int_shr_";
        default:        return "pydos_int_bitand_";
    }
}

const char *CodeGeneratorBase::runtime_unary_func(IROp op)
{
    switch (op) {
        case IR_NEG:    return "pydos_obj_neg_";
        case IR_BITNOT: return "pydos_obj_invert_";
        case IR_POS:    return "pydos_obj_pos_";
        default:        return "pydos_obj_neg_";
    }
}

const char *CodeGeneratorBase::runtime_cmp_func(IROp op)
{
    /* All comparisons go through pydos_obj_equal or pydos_int_compare.
     * We use a single compare function and then branch on the result. */
    (void)op;
    return "pydos_int_compare_";
}

const char *CodeGeneratorBase::builtin_asm_name(const char *py_name)
{
    /* All builtin/exception name→asm mappings come from stdlib.idx */
    if (stdlib_reg_ && stdlib_reg_->is_loaded()) {
        const char *asm_name = stdlib_reg_->find_builtin_asm_name(py_name);
        if (asm_name) return asm_name;
    }
    return 0;
}

const char *CodeGeneratorBase::resolve_fast_method(
    IRInstr *instr, const char *method_name, int argc)
{
    if (!instr->type_hint || !stdlib_reg_ || !stdlib_reg_->is_loaded())
        return 0;
    const BuiltinMethodEntry *me =
        stdlib_reg_->find_method(instr->type_hint->kind, method_name);
    if (!me || me->asm_name[0] == '\0')
        return 0;
    int expected = (me->fast_argc >= 0) ? me->fast_argc : me->num_params;
    if (argc != expected)
        return 0;
    return me->asm_name;
}

const char *CodeGeneratorBase::resolve_subscript_func(
    TypeInfo *type_hint, int is_store)
{
    if (!type_hint || !stdlib_reg_ || !stdlib_reg_->is_loaded())
        return 0;
    const BuiltinTypeEntry *te = stdlib_reg_->get_type_by_kind(type_hint->kind);
    if (!te) return 0;
    const char *fn = is_store ? te->op_setitem : te->op_getitem;
    if (!fn || fn[0] == '\0') return 0;
    return fn;
}

const char *CodeGeneratorBase::resolve_slice_func(TypeInfo *type_hint)
{
    if (!type_hint || !stdlib_reg_ || !stdlib_reg_->is_loaded())
        return 0;
    const BuiltinTypeEntry *te = stdlib_reg_->get_type_by_kind(type_hint->kind);
    if (!te) return 0;
    if (te->op_getslice[0] == '\0') return 0;
    return te->op_getslice;
}

/* --------------------------------------------------------------- */
/* Pre-scan: discover all globals and required externs               */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::prescan_module()
{
    /* Always need runtime init/shutdown */
    require_extern("pydos_rt_init_");
    require_extern("pydos_rt_shutdown_");

    /* Scan init function */
    if (mod->init_func) {
        prescan_func(mod->init_func);
    }

    /* Scan all user functions */
    IRFunc *f;
    for (f = mod->functions; f; f = f->next) {
        prescan_func(f);
    }
}

void CodeGeneratorBase::prescan_func(IRFunc *func)
{
    IRInstr *ip;
    for (ip = func->first; ip; ip = ip->next) {
        switch (ip->op) {

        case IR_CONST_INT:
            require_extern("pydos_obj_new_int_");
            break;

        case IR_CONST_STR:
            require_extern("pydos_obj_new_str_");
            mark_const_used(ip->src1);
            break;

        case IR_CONST_FLOAT:
            require_extern("pydos_obj_new_float_");
            break;

        case IR_CONST_NONE:
            require_extern("pydos_obj_new_none_");
            break;

        case IR_CONST_BOOL:
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_ADD: case IR_SUB: case IR_MUL:
        case IR_DIV: case IR_FLOORDIV: case IR_MOD:
        case IR_POW:
            require_extern(runtime_arith_func(ip->op));
            /* For unknown/class/float/complex types, need polymorphic dispatch */
            if (!ip->type_hint || ip->type_hint->kind == TY_ANY ||
                ip->type_hint->kind == TY_CLASS ||
                ip->type_hint->kind == TY_GENERIC_INST ||
                ip->type_hint->kind == TY_ERROR ||
                ip->type_hint->kind == TY_FLOAT ||
                ip->type_hint->kind == TY_COMPLEX) {
                require_extern("pydos_obj_add_");
                require_extern("pydos_obj_sub_");
                require_extern("pydos_obj_mul_");
            }
            if (ip->op == IR_ADD && ip->type_hint &&
                ip->type_hint->kind == TY_STR) {
                require_extern("pydos_str_concat_");
            }
            break;

        case IR_MATMUL:
            require_extern("pydos_obj_matmul_");
            break;

        case IR_INPLACE:
            require_extern("pydos_obj_inplace_");
            break;

        case IR_BITAND: case IR_BITOR: case IR_BITXOR:
        case IR_SHL: case IR_SHR:
            require_extern(runtime_bitwise_func(ip->op));
            break;

        case IR_NEG:
            require_extern("pydos_obj_neg_");
            break;

        case IR_BITNOT:
            require_extern("pydos_obj_invert_");
            break;

        case IR_NOT:
            require_extern("pydos_obj_is_truthy_");
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_POS:
            require_extern("pydos_obj_pos_");
            break;

        case IR_CMP_EQ: case IR_CMP_NE:
            require_extern("pydos_obj_equal_");
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_CMP_LT: case IR_CMP_LE:
        case IR_CMP_GT: case IR_CMP_GE:
            require_extern("pydos_obj_compare_");
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_IS: case IR_IS_NOT:
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_IN: case IR_NOT_IN:
            require_extern("pydos_obj_contains_");
            require_extern("pydos_obj_new_bool_");
            break;

        /* Typed i32 arithmetic: may need Watcom __I4M, __I4D helpers on 8086 */
        case IR_ADD_I32: case IR_SUB_I32:
            /* No externs needed — native add/sub */
            break;
        case IR_MUL_I32:
            if (target == TARGET_8086) require_extern("__I4M");
            break;
        case IR_DIV_I32:
            if (target == TARGET_8086) require_extern("__I4D");
            break;
        case IR_MOD_I32:
            if (target == TARGET_8086) require_extern("__I4D");
            break;
        case IR_NEG_I32:
            break;
        case IR_CMP_I32_EQ: case IR_CMP_I32_NE:
        case IR_CMP_I32_LT: case IR_CMP_I32_LE:
        case IR_CMP_I32_GT: case IR_CMP_I32_GE:
            /* Unboxed result (0/1) — no runtime call needed.
               BOX_BOOL handles boxing via pydos_obj_new_bool_. */
            break;
        case IR_BOX_INT:
            require_extern("pydos_obj_new_int_");
            break;
        case IR_BOX_BOOL:
            require_extern("pydos_obj_new_bool_");
            break;
        case IR_UNBOX_INT:
            /* No externs — just a field access */
            break;
        case IR_STR_JOIN:
            require_extern("pydos_str_join_n_");
            break;
        case IR_SCOPE_ENTER:
            require_extern("pydos_arena_scope_enter_");
            break;
        case IR_SCOPE_TRACK:
            require_extern("pydos_arena_scope_track_");
            break;
        case IR_SCOPE_EXIT:
            require_extern("pydos_arena_scope_exit_");
            break;

        case IR_LOAD_SLICE: {
            const char *slice_fn = resolve_slice_func(ip->type_hint);
            if (slice_fn) {
                require_extern(slice_fn);
            } else {
                /* Unknown type — need both in case prescan can't determine */
                require_extern("pydos_str_slice_");
                require_extern("pydos_list_slice_");
            }
            break;
        }

        case IR_EXC_MATCH:
            require_extern("pydos_exc_matches_");
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_JUMP_IF_TRUE: case IR_JUMP_IF_FALSE:
            require_extern("pydos_obj_is_truthy_");
            break;

        case IR_CALL: {
            /* Detect indirect calls through function objects, which
             * access pydos_active_closure_ at emit time. The EXTRN
             * must be declared here because emit-time require_extern
             * runs after the EXTRN section is already written. */
            IRInstr *def;
            for (def = ip->prev; def; def = def->prev) {
                if (def->dest == ip->src1) break;
            }
            if (def) {
                if (def->op == IR_LOAD_LOCAL || def->op == IR_LOAD_ATTR ||
                    def->op == IR_MAKE_FUNCTION) {
                    require_data_extern("pydos_active_closure_");
                } else if (def->op == IR_LOAD_GLOBAL) {
                    const char *cname = 0;
                    if (def->src1 >= 0 && def->src1 < mod->num_constants &&
                        mod->constants[def->src1].kind == IRConst::CONST_STR) {
                        cname = mod->constants[def->src1].str_val.data;
                    }
                    if (cname) {
                        int is_bi = builtin_asm_name(cname) != 0;
                        int is_rt = strncmp(cname, "pydos_", 6) == 0;
                        int is_dir = 0;
                        if (!is_bi && !is_rt) {
                            IRFunc *f;
                            for (f = mod->functions; f; f = f->next) {
                                if (strcmp(f->name, cname) == 0) {
                                    is_dir = 1;
                                    break;
                                }
                            }
                            if (!is_dir && mod->init_func &&
                                strcmp(mod->init_func->name, cname) == 0) {
                                is_dir = 1;
                            }
                        }
                        if (!is_bi && !is_rt && !is_dir) {
                            require_data_extern("pydos_active_closure_");
                        }
                    }
                }
            }
            break;
        }

        case IR_CALL_METHOD: {
            require_extern("pydos_obj_call_method_");
            /* NOTE: do NOT mark_const_used(ip->src2) -- emit_call_method
               uses register_mn_string() to create _MN_N entries instead */

            const char *mn = "";
            if (ip->src2 >= 0 && ip->src2 < mod->num_constants &&
                mod->constants[ip->src2].kind == IRConst::CONST_STR) {
                mn = mod->constants[ip->src2].str_val.data;
                register_mn_string(mn);
            }

            /* Register fast-path extern from stdlib registry */
            if (ip->type_hint && stdlib_reg_ && stdlib_reg_->is_loaded()
                && mn[0] != '\0') {
                const BuiltinMethodEntry *me =
                    stdlib_reg_->find_method(ip->type_hint->kind, mn);
                if (me && me->asm_name[0] != '\0')
                    require_extern(me->asm_name);
            }
            break;
        }

        case IR_INCREF:
            require_extern("pydos_incref_");
            break;

        case IR_DECREF:
            require_extern("pydos_decref_");
            break;

        case IR_BUILD_LIST:
            require_extern("pydos_list_new_");
            require_extern("pydos_list_append_");
            break;

        case IR_BUILD_DICT:
            require_extern("pydos_dict_new_");
            require_extern("pydos_dict_set_");
            break;

        case IR_BUILD_TUPLE:
            require_extern("pydos_list_new_");  /* tuples use same alloc pattern */
            require_extern("pydos_list_append_");
            break;

        case IR_BUILD_SET:
            require_extern("pydos_dict_new_");
            require_extern("pydos_dict_set_");
            break;

        case IR_GET_ITER:
            require_extern("pydos_obj_get_iter_");
            break;

        case IR_FOR_ITER:
            require_extern("pydos_obj_iter_next_");
            break;

        case IR_LOAD_GLOBAL: {
            /* Check if this is a builtin function name */
            int ci = ip->src1;
            if (ci >= 0 && ci < mod->num_constants &&
                mod->constants[ci].kind == IRConst::CONST_STR) {
                const char *gname = mod->constants[ci].str_val.data;
                const char *bname = builtin_asm_name(gname);
                if (bname) {
                    require_extern(bname);
                    /* Don't register_global for builtins -- the
                       LOAD_GLOBAL result is never used; emit_call()
                       resolves builtins to direct far calls. */
                } else if (strncmp(gname, "pydos_", 6) == 0) {
                    /* Runtime helper function — register as extern,
                       not global variable */
                    char ename[128];
                    int len = strlen(gname);
                    if (len > 0 && gname[len-1] == '_') {
                        strcpy(ename, gname);
                    } else {
                        sprintf(ename, "%s_", gname);
                    }
                    require_extern(codegen_strdup(ename));
                } else {
                    register_global(gname);
                }
            }
            break;
        }

        case IR_STORE_GLOBAL: {
            int ci = ip->dest;
            if (ci >= 0 && ci < mod->num_constants &&
                mod->constants[ci].kind == IRConst::CONST_STR) {
                const char *gname = mod->constants[ci].str_val.data;
                register_global(gname);
            }
            break;
        }

        case IR_LOAD_ATTR:
            require_extern("pydos_obj_get_attr_");
            require_extern("pydos_obj_set_attr_");
            mark_const_used(ip->src2);
            break;

        case IR_STORE_ATTR:
            require_extern("pydos_obj_get_attr_");
            require_extern("pydos_obj_set_attr_");
            mark_const_used(ip->dest);
            break;

        case IR_LOAD_SUBSCRIPT: {
            const char *sub_fn = resolve_subscript_func(ip->type_hint, 0);
            if (sub_fn)
                require_extern(sub_fn);
            else
                require_extern("pydos_obj_getitem_");
            break;
        }

        case IR_STORE_SUBSCRIPT: {
            const char *sub_fn = resolve_subscript_func(ip->type_hint, 1);
            if (sub_fn)
                require_extern(sub_fn);
            else
                require_extern("pydos_obj_setitem_");
            break;
        }

        case IR_DEL_SUBSCRIPT:
            require_extern("pydos_obj_delitem_");
            break;

        case IR_DEL_LOCAL:
            require_extern("pydos_decref_");
            break;

        case IR_DEL_GLOBAL: {
            require_extern("pydos_decref_");
            int dg_ci = ip->dest;
            if (dg_ci >= 0 && dg_ci < mod->num_constants &&
                mod->constants[dg_ci].kind == IRConst::CONST_STR) {
                register_global(mod->constants[dg_ci].str_val.data);
                mark_const_used(dg_ci);
            }
            break;
        }

        case IR_DEL_ATTR:
            require_extern("pydos_obj_del_attr_");
            mark_const_used(ip->dest);
            break;

        case IR_ALLOC_OBJ:
            require_extern("pydos_obj_alloc_");
            break;

        case IR_INIT_VTABLE: {
            /* Register vtable global and required externs */
            int vt_idx = ip->extra;
            if (vt_idx >= 0 && vt_idx < mod->num_class_vtables) {
                ClassVTableInfo *vti = &mod->class_vtables[vt_idx];
                /* Register vtable global label: _VT_ClassName (dedup) */
                char tmp_vt_label[64];
                sprintf(tmp_vt_label, "_VT_%s", vti->class_name);
                int vt_already = 0;
                int vk;
                for (vk = 0; vk < num_vtable_globals; vk++) {
                    if (strcmp(vtable_globals[vk].label, tmp_vt_label) == 0) {
                        vt_already = 1;
                        break;
                    }
                }
                if (!vt_already) {
                    if (num_vtable_globals >= vtable_globals_cap) grow_vtable_globals();
                    strcpy(vtable_globals[num_vtable_globals].label, tmp_vt_label);
                    num_vtable_globals++;
                }
                /* Register method name strings */
                int mi;
                for (mi = 0; mi < vti->num_methods; mi++) {
                    register_mn_string(vti->methods[mi].python_name);
                }
                require_extern("pydos_vtable_create_");
                require_extern("pydos_vtable_add_method_");
                require_extern("pydos_vtable_set_name_");
                /* Register class name string for vtable repr */
                register_mn_string(vti->class_name);
                if (vti->base_class_name && vti->base_class_name[0] != '\0') {
                    require_extern("pydos_vtable_inherit_");
                    /* Also register parent vtable global */
                    char pvt[64];
                    sprintf(pvt, "_VT_%s", vti->base_class_name);
                    int pvt_exists = 0;
                    int pk;
                    for (pk = 0; pk < num_vtable_globals; pk++) {
                        if (strcmp(vtable_globals[pk].label, pvt) == 0) {
                            pvt_exists = 1;
                            break;
                        }
                    }
                    if (!pvt_exists) {
                        if (num_vtable_globals >= vtable_globals_cap) grow_vtable_globals();
                        strcpy(vtable_globals[num_vtable_globals].label, pvt);
                        num_vtable_globals++;
                    }
                }
            }
            break;
        }

        case IR_SET_VTABLE:
            require_extern("pydos_obj_set_vtable_");
            break;

        case IR_CHECK_VTABLE:
            require_extern("pydos_obj_isinstance_vtable_");
            require_extern("pydos_obj_new_bool_");
            break;

        case IR_MAKE_FUNCTION:
            require_extern("pydos_func_new_");
            mark_const_used(ip->src1);
            /* Closure attachment: emitter calls pydos_incref_ on the list */
            if (ip->src2 >= 0) {
                require_extern("pydos_incref_");
            }
            /* Any function object may be called indirectly via the
             * from_make_func path, which accesses pydos_active_closure_ */
            require_data_extern("pydos_active_closure_");
            break;

        case IR_MAKE_CELL:
            require_extern("pydos_cell_new_");
            break;

        case IR_CELL_GET:
            require_extern("pydos_cell_get_");
            break;

        case IR_CELL_SET:
            require_extern("pydos_cell_set_");
            break;

        case IR_LOAD_CLOSURE:
            require_data_extern("pydos_active_closure_");
            break;

        case IR_SET_CLOSURE:
            require_data_extern("pydos_active_closure_");
            break;

        case IR_MAKE_GENERATOR:
            require_extern("pydos_gen_new_");
            mark_const_used(ip->src1);
            break;

        case IR_MAKE_COROUTINE:
            require_extern("pydos_cor_new_");
            mark_const_used(ip->src1);
            break;

        case IR_COR_SET_RESULT:
            /* Direct struct write — no extern needed,
             * but we need INCREF/DECREF for refcounting */
            require_extern("pydos_incref_");
            require_extern("pydos_decref_");
            break;

        case IR_GEN_LOAD_LOCAL:
            require_extern("pydos_list_get_");
            break;

        case IR_GEN_SAVE_LOCAL:
            require_extern("pydos_list_set_");
            break;

        case IR_GEN_CHECK_THROW:
            require_extern("pydos_gen_check_throw_");
            break;

        case IR_GEN_GET_SENT:
            require_data_extern("pydos_gen_sent_");
            break;

        case IR_SETUP_TRY:
            require_extern("pydos_exc_alloc_frame_");
            /* 16-bit clibl uses "_setjmp_" (cdecl *_ naming);
             * 32-bit clib3s uses "_setjmp" (no trailing underscore) */
            require_extern(target == 1 ? "_setjmp" : "_setjmp_");
            break;

        case IR_POP_TRY:
            require_extern("pydos_exc_pop_");
            break;

        case IR_RAISE:
            require_extern("pydos_exc_raise_obj_");
            break;

        case IR_RERAISE:
            require_extern("pydos_exc_raise_obj_");
            require_extern("pydos_exc_current_");
            break;

        case IR_GET_EXCEPTION:
            require_extern("pydos_exc_current_");
            break;

        default:
            break;
        }
    }
}

/* --------------------------------------------------------------- */
/* generate() - main entry point                                    */
/* --------------------------------------------------------------- */

int CodeGeneratorBase::generate(IRModule *module, const char *output_filename)
{
    mod = module;
    module_name = module->module_name;
    is_main_module = module->is_main_module;
    has_main_func = module->has_main_func;
    entry_func = module->entry_func;
    label_counter = 0;
    num_externs = 0;
    num_data_externs = 0;
    num_globals = 0;
    num_call_args = 0;
    num_vtable_globals = 0;
    num_mn_strings = 0;

    /* Pre-scan to discover all needed externs and globals */
    prescan_module();

    /* Scan all IR functions to find the maximum label ID used by the
     * IR generator, then set label_counter above it so that codegen's
     * own labels (from new_label()) never collide with IR labels. */
    {
        int max_label = -1;
        IRFunc *sf;
        for (sf = mod->functions; sf; sf = sf->next) {
            IRInstr *ip;
            for (ip = sf->first; ip; ip = ip->next) {
                if (ip->op == IR_LABEL || ip->op == IR_JUMP ||
                    ip->op == IR_JUMP_IF_TRUE || ip->op == IR_JUMP_IF_FALSE ||
                    ip->op == IR_FOR_ITER || ip->op == IR_SETUP_TRY) {
                    if (ip->extra > max_label) max_label = ip->extra;
                }
            }
        }
        if (mod->init_func) {
            IRInstr *ip;
            for (ip = mod->init_func->first; ip; ip = ip->next) {
                if (ip->op == IR_LABEL || ip->op == IR_JUMP ||
                    ip->op == IR_JUMP_IF_TRUE || ip->op == IR_JUMP_IF_FALSE ||
                    ip->op == IR_FOR_ITER || ip->op == IR_SETUP_TRY) {
                    if (ip->extra > max_label) max_label = ip->extra;
                }
            }
        }
        label_counter = max_label + 1;
    }

    out = fopen(output_filename, "w");
    if (!out) {
        error_fatal("Cannot open output file: %s", output_filename);
        return 0;
    }

    emit_header();
    emit_data_section();
    emit_code_section();
    emit_footer();

    fclose(out);
    out = 0;
    return 1;
}

/* --------------------------------------------------------------- */
/* emit_instr - dispatch to per-opcode virtual handler              */
/* --------------------------------------------------------------- */

void CodeGeneratorBase::emit_instr(IRInstr *instr)
{
    if (verbose && instr->op != IR_LABEL && instr->op != IR_NOP) {
        emit_comment("IR: %s dest=%d src1=%d src2=%d extra=%d",
                      irop_name(instr->op), instr->dest,
                      instr->src1, instr->src2, instr->extra);
    }

    switch (instr->op) {

    case IR_CONST_INT:      emit_const_int(instr);      break;
    case IR_CONST_STR:      emit_const_str(instr);      break;
    case IR_CONST_FLOAT:    emit_const_float(instr);    break;
    case IR_CONST_NONE:     emit_const_none(instr);     break;
    case IR_CONST_BOOL:     emit_const_bool(instr);     break;

    case IR_LOAD_LOCAL:     emit_load_local(instr);     break;
    case IR_STORE_LOCAL:    emit_store_local(instr);    break;
    case IR_LOAD_GLOBAL:    emit_load_global(instr);    break;
    case IR_STORE_GLOBAL:   emit_store_global(instr);   break;

    case IR_ADD: case IR_SUB: case IR_MUL:
    case IR_DIV: case IR_FLOORDIV: case IR_MOD:
    case IR_POW:
        emit_arithmetic(instr);
        break;

    case IR_MATMUL:
        emit_matmul(instr);
        break;

    case IR_INPLACE:
        emit_inplace(instr);
        break;

    case IR_BITAND: case IR_BITOR: case IR_BITXOR:
    case IR_SHL: case IR_SHR:
        emit_bitwise(instr);
        break;

    case IR_NEG: case IR_POS: case IR_NOT: case IR_BITNOT:
        emit_unary(instr);
        break;

    case IR_CMP_EQ: case IR_CMP_NE:
    case IR_CMP_LT: case IR_CMP_LE:
    case IR_CMP_GT: case IR_CMP_GE:
    case IR_IS: case IR_IS_NOT:
    case IR_IN: case IR_NOT_IN:
        emit_comparison(instr);
        break;

    case IR_JUMP:           emit_jump(instr);           break;
    case IR_JUMP_IF_TRUE:
    case IR_JUMP_IF_FALSE:  emit_jump_cond(instr);      break;
    case IR_LABEL:          emit_ir_label(instr);       break;

    case IR_PUSH_ARG:       emit_push_arg(instr);       break;
    case IR_CALL:           emit_call(instr);           break;
    case IR_CALL_METHOD:    emit_call_method(instr);    break;
    case IR_RETURN:         emit_return(instr);         break;

    case IR_INCREF:         emit_incref(instr);         break;
    case IR_DECREF:         emit_decref(instr);         break;

    case IR_BUILD_LIST:     emit_build_list(instr);     break;
    case IR_BUILD_DICT:     emit_build_dict(instr);     break;
    case IR_BUILD_TUPLE:    emit_build_tuple(instr);    break;
    case IR_BUILD_SET:      emit_build_set(instr);      break;

    case IR_GET_ITER:       emit_get_iter(instr);       break;
    case IR_FOR_ITER:       emit_for_iter(instr);       break;

    case IR_LOAD_ATTR:      emit_load_attr(instr);      break;
    case IR_STORE_ATTR:     emit_store_attr(instr);     break;

    case IR_LOAD_SUBSCRIPT:  emit_load_subscript(instr);  break;
    case IR_STORE_SUBSCRIPT: emit_store_subscript(instr); break;
    case IR_DEL_SUBSCRIPT:   emit_del_subscript(instr);   break;
    case IR_DEL_LOCAL:       emit_del_local(instr);       break;
    case IR_DEL_GLOBAL:      emit_del_global(instr);      break;
    case IR_DEL_ATTR:        emit_del_attr(instr);        break;
    case IR_LOAD_SLICE:      emit_load_slice(instr);      break;
    case IR_EXC_MATCH:       emit_exc_match(instr);       break;

    case IR_SETUP_TRY:     emit_setup_try(instr);      break;
    case IR_POP_TRY:       emit_pop_try(instr);        break;
    case IR_RAISE:          emit_raise(instr);          break;
    case IR_RERAISE:        emit_reraise(instr);        break;
    case IR_GET_EXCEPTION:  emit_get_exception(instr);  break;

    case IR_ALLOC_OBJ:     emit_alloc_obj(instr);      break;
    case IR_INIT_VTABLE:   emit_init_vtable(instr);    break;
    case IR_SET_VTABLE:    emit_set_vtable(instr);     break;
    case IR_CHECK_VTABLE:  emit_check_vtable(instr);   break;

    case IR_MAKE_FUNCTION:  emit_make_function(instr);  break;
    case IR_MAKE_GENERATOR: emit_make_generator(instr); break;
    case IR_MAKE_COROUTINE: emit_make_coroutine(instr); break;
    case IR_COR_SET_RESULT: emit_cor_set_result(instr); break;
    case IR_MAKE_CELL:      emit_make_cell(instr);      break;
    case IR_CELL_GET:       emit_cell_get(instr);       break;
    case IR_CELL_SET:       emit_cell_set(instr);       break;
    case IR_LOAD_CLOSURE:   emit_load_closure(instr);   break;
    case IR_SET_CLOSURE:    emit_set_closure(instr);    break;
    case IR_GEN_LOAD_PC:    emit_gen_load_pc(instr);    break;
    case IR_GEN_SET_PC:     emit_gen_set_pc(instr);     break;
    case IR_GEN_LOAD_LOCAL: emit_gen_load_local(instr); break;
    case IR_GEN_SAVE_LOCAL: emit_gen_save_local(instr); break;
    case IR_GEN_CHECK_THROW: emit_gen_check_throw(instr); break;
    case IR_GEN_GET_SENT:    emit_gen_get_sent(instr);    break;

    /* Typed i32 arithmetic (specialization) */
    case IR_ADD_I32: case IR_SUB_I32: case IR_MUL_I32:
    case IR_DIV_I32: case IR_MOD_I32: case IR_NEG_I32:
        emit_arith_i32(instr);
        break;

    case IR_CMP_I32_EQ: case IR_CMP_I32_NE:
    case IR_CMP_I32_LT: case IR_CMP_I32_LE:
    case IR_CMP_I32_GT: case IR_CMP_I32_GE:
        emit_cmp_i32(instr);
        break;

    case IR_BOX_INT:    emit_box_int(instr);    break;
    case IR_UNBOX_INT:  emit_unbox_int(instr);  break;
    case IR_BOX_BOOL:   emit_box_bool(instr);   break;

    case IR_STR_JOIN:   emit_str_join(instr);   break;

    case IR_SCOPE_ENTER: emit_scope_enter(instr); break;
    case IR_SCOPE_TRACK: emit_scope_track(instr); break;
    case IR_SCOPE_EXIT:  emit_scope_exit(instr);  break;

    case IR_NOP:
        break;

    case IR_COMMENT:
        if (verbose) {
            int ci = instr->src1;
            if (ci >= 0 && ci < mod->num_constants &&
                mod->constants[ci].kind == IRConst::CONST_STR) {
                emit_comment("Python: %s", mod->constants[ci].str_val.data);
            }
        }
        break;

    default:
        emit_comment("UNIMPLEMENTED: %s", irop_name(instr->op));
        break;
    }
}

/* --------------------------------------------------------------- */
/* Factory function                                                 */
/* --------------------------------------------------------------- */

CodeGeneratorBase *create_codegen(int tgt)
{
    CodeGeneratorBase *cg = 0;
    switch (tgt) {
    case TARGET_8086: cg = new CodeGenerator8086(); break;
    case TARGET_386:  cg = new CodeGenerator386();  break;
    default:          return 0;
    }
    cg->set_target(tgt);
    return cg;
}
