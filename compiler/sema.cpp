/*
 * sema.cpp - Semantic analysis implementation for PyDOS Python-to-8086 compiler
 *
 * Walks the AST, builds scopes and symbol tables, resolves names,
 * infers/checks types, and reports errors. After analysis, every
 * expression node has an associated TypeInfo retrievable via
 * get_expr_type().
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 *
 * The AST uses a tagged-union structure (node->data.xxx.yyy).
 * See ast.h for the full ASTNode definition.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "sema.h"

/* ------------------------------------------------------------------ */
/* Helper: duplicate a C string                                        */
/* ------------------------------------------------------------------ */
static char *str_dup(const char *s)
{
    int len;
    char *d;
    if (!s) return 0;
    len = strlen(s);
    d = (char *)malloc(len + 1);
    if (d) strcpy(d, s);
    return d;
}

/* ------------------------------------------------------------------ */
/* Helper: count params in linked list                                 */
/* ------------------------------------------------------------------ */
static int count_params(Param *p)
{
    int n = 0;
    while (p) { n++; p = p->next; }
    return n;
}

/* ------------------------------------------------------------------ */
/* Helper: check if type is class-like (TY_CLASS or TY_GENERIC_INST)  */
/* TY_GENERIC_INST carries class_info from its base generic class,    */
/* so it should be treated identically in operator/call/attr checks.  */
/* ------------------------------------------------------------------ */
static int type_is_class_like(TypeInfo *t)
{
    return t->kind == TY_CLASS || t->kind == TY_GENERIC_INST;
}

/* ------------------------------------------------------------------ */
/* Constructor / Destructor                                            */
/* ------------------------------------------------------------------ */

SemanticAnalyzer::SemanticAnalyzer()
{
    current_scope = 0;
    error_count = 0;
    next_slot = 0;
    num_expr_types = 0;
    memset(expr_types, 0, sizeof(expr_types));
    search_paths = 0;
    num_search_paths = 0;
    scanned_modules = 0;
    num_scanned = 0;
    stdlib_reg_ = 0;
}

SemanticAnalyzer::~SemanticAnalyzer()
{
    while (current_scope) {
        pop_scope();
    }
    if (scanned_modules) {
        free(scanned_modules);
        scanned_modules = 0;
    }
}

void SemanticAnalyzer::set_stdlib(StdlibRegistry *reg)
{
    stdlib_reg_ = reg;
}

void SemanticAnalyzer::set_search_paths(const char **paths, int count)
{
    search_paths = paths;
    num_search_paths = count;
}

ModuleInfo *SemanticAnalyzer::find_scanned_module(const char *module_name)
{
    int i;
    if (!module_name) return 0;
    for (i = 0; i < num_scanned; i++) {
        if (scanned_modules[i].valid &&
            scanned_modules[i].module_name &&
            strcmp(scanned_modules[i].module_name, module_name) == 0) {
            return &scanned_modules[i];
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                     */
/* ------------------------------------------------------------------ */

int SemanticAnalyzer::analyze(ASTNode *module)
{
    ASTNode *child;

    if (!module) return 0;

    error_count = 0;
    num_expr_types = 0;

    /* Create global scope */
    push_scope(0, 0);
    register_builtins();

    /* Analyze all top-level statements */
    /* Module body is in data.module.body (linked list via ->next) */
    if (module->kind == AST_MODULE) {
        for (child = module->data.module.body; child; child = child->next) {
            analyze_stmt(child);
        }
    } else {
        /* If given a non-module node, just analyze it as a statement */
        analyze_stmt(module);
    }

    /* Do not pop global scope - keep it for lookups */
    return error_count;
}

int SemanticAnalyzer::get_error_count() const
{
    return error_count;
}

Symbol *SemanticAnalyzer::lookup(const char *name)
{
    return resolve(name);
}

TypeInfo *SemanticAnalyzer::get_expr_type(ASTNode *expr)
{
    int i;
    if (!expr) return type_error;
    for (i = 0; i < num_expr_types; i++) {
        if (expr_types[i].node == expr) {
            return expr_types[i].type;
        }
    }
    return type_error;
}

void SemanticAnalyzer::set_expr_type(ASTNode *node, TypeInfo *type)
{
    int i;
    if (!node || !type) return;

    /* Update existing entry if present */
    for (i = 0; i < num_expr_types; i++) {
        if (expr_types[i].node == node) {
            expr_types[i].type = type;
            return;
        }
    }

    /* Add new entry */
    if (num_expr_types < 4096) {
        expr_types[num_expr_types].node = node;
        expr_types[num_expr_types].type = type;
        num_expr_types++;
    }
}

/* ------------------------------------------------------------------ */
/* Scope management                                                    */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::push_scope(int is_function, int is_class)
{
    Scope *s = (Scope *)malloc(sizeof(Scope));
    if (!s) {
        fprintf(stderr, "sema: out of memory allocating scope\n");
        return;
    }
    memset(s, 0, sizeof(Scope));
    s->parent = current_scope;
    s->is_function = is_function;
    s->is_class = is_class;
    s->scope_depth = current_scope ? current_scope->scope_depth + 1 : 0;

    if (is_function) {
        next_slot = 0;
    }

    /* Inherit context from parent */
    if (!is_function && current_scope) {
        s->func_name = current_scope->func_name;
        s->return_type = current_scope->return_type;
    }
    if (!is_class && current_scope) {
        s->class_name = current_scope->class_name;
    }

    current_scope = s;
}

void SemanticAnalyzer::pop_scope()
{
    Scope *old;
    Symbol *sym, *snext;

    if (!current_scope) return;

    old = current_scope;
    current_scope = old->parent;

    sym = old->symbols;
    while (sym) {
        snext = sym->next;
        free(sym);
        sym = snext;
    }
    free(old);
}

Symbol *SemanticAnalyzer::declare(const char *name, TypeInfo *type, SymbolKind kind)
{
    Symbol *existing;
    Symbol *sym;

    if (!current_scope || !name) return 0;

    /* Check for duplicate in current scope only */
    for (existing = current_scope->symbols; existing; existing = existing->next) {
        if (strcmp(existing->name, name) == 0) {
            /* Allow redeclaration (Python allows reassignment) */
            existing->type = type;
            existing->kind = kind;
            return existing;
        }
    }

    sym = (Symbol *)malloc(sizeof(Symbol));
    if (!sym) return 0;
    memset(sym, 0, sizeof(Symbol));
    sym->name = str_dup(name);
    sym->type = type;
    sym->kind = kind;
    sym->scope_depth = current_scope->scope_depth;
    sym->is_global = (current_scope->scope_depth == 0) ? 1 : 0;

    if (kind == SYM_VAR || kind == SYM_PARAM) {
        sym->slot = next_slot;
        next_slot++;
        current_scope->num_locals++;
    }

    sym->next = current_scope->symbols;
    current_scope->symbols = sym;
    return sym;
}

Symbol *SemanticAnalyzer::resolve(const char *name)
{
    Scope *s;
    Symbol *sym;

    if (!name) return 0;

    for (s = current_scope; s; s = s->parent) {
        for (sym = s->symbols; sym; sym = sym->next) {
            if (strcmp(sym->name, name) == 0) {
                return sym;
            }
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Builtin registration                                                */
/* ------------------------------------------------------------------ */

static TypeInfo *make_builtin_func(TypeInfo **param_types, int nparams,
                                   TypeInfo *ret)
{
    TypeInfo *params_list = 0;
    TypeInfo *prev = 0;
    int i;

    for (i = 0; i < nparams; i++) {
        TypeInfo *p = type_copy(param_types[i]);
        if (!p) continue;
        p->next = 0;
        if (prev) prev->next = p;
        else params_list = p;
        prev = p;
    }
    return type_new_func(params_list, nparams, ret);
}

/* Map TypeKind enum value from stdlib.idx to a TypeInfo pointer */
static TypeInfo *type_kind_to_info(int kind)
{
    switch (kind) {
    case TY_INT:    return type_int;
    case TY_FLOAT:  return type_float;
    case TY_BOOL:   return type_bool;
    case TY_STR:    return type_str;
    case TY_NONE:   return type_none;
    case TY_LIST:   return type_new_list(type_any);
    case TY_DICT:   return type_new_dict(type_any, type_any);
    case TY_RANGE:  return type_range;
    default:        return type_any;
    }
}

void SemanticAnalyzer::register_builtins()
{
    TypeInfo *pt[4];
    int i;

    /* Register all builtins from stdlib registry */
    if (stdlib_reg_ && stdlib_reg_->is_loaded()) {
        int nf = stdlib_reg_->get_num_funcs();
        for (i = 0; i < nf; i++) {
            const BuiltinFuncEntry *f = stdlib_reg_->get_func(i);
            if (!f || f->py_name[0] == '\0') continue;

            TypeInfo *ret = type_kind_to_info(f->ret_type_kind);
            int np = f->num_params;
            if (np > 4) np = 4;

            /* All params default to type_any for builtins */
            {
                int j;
                for (j = 0; j < np; j++) pt[j] = type_any;
            }

            declare(f->py_name, make_builtin_func(pt, np, ret), SYM_BUILTIN);
        }
    }

    /* Builtin constants */
    declare("True",  type_bool, SYM_BUILTIN);
    declare("False", type_bool, SYM_BUILTIN);
    declare("None",  type_none, SYM_BUILTIN);
}

/* ------------------------------------------------------------------ */
/* Error reporting                                                     */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::error(ASTNode *node, const char *msg)
{
    if (node) {
        fprintf(stderr, "%d:%d: error: %s\n", node->line, node->col, msg);
    } else {
        fprintf(stderr, "error: %s\n", msg);
    }
    error_count++;
}

void SemanticAnalyzer::error_fmt(ASTNode *node, const char *fmt, ...)
{
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsprintf(buf, fmt, ap);
    va_end(ap);
    error(node, buf);
}

void SemanticAnalyzer::warn(ASTNode *node, const char *msg)
{
    if (node) {
        fprintf(stderr, "%d:%d: warning: %s\n", node->line, node->col, msg);
    } else {
        fprintf(stderr, "warning: %s\n", msg);
    }
}

/* ------------------------------------------------------------------ */
/* Statement analysis dispatch                                         */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_stmt(ASTNode *node)
{
    if (!node) return;

    switch (node->kind) {
    case AST_FUNC_DEF:    analyze_funcdef(node);    break;
    case AST_CLASS_DEF:   analyze_classdef(node);   break;
    case AST_IF:          analyze_if(node);         break;
    case AST_WHILE:       analyze_while(node);      break;
    case AST_FOR:         analyze_for(node);        break;
    case AST_ASSIGN:      analyze_assign(node);     break;
    case AST_ANN_ASSIGN:  analyze_ann_assign(node); break;
    case AST_AUG_ASSIGN:  analyze_aug_assign(node); break;
    case AST_RETURN:      analyze_return(node);     break;
    case AST_TRY:         analyze_try(node);        break;
    case AST_RAISE:       analyze_raise(node);      break;
    case AST_IMPORT:
    case AST_IMPORT_FROM:
        analyze_import(node);
        break;
    case AST_EXPR_STMT:   analyze_expr_stmt(node);  break;
    case AST_PASS:
    case AST_BREAK:
    case AST_CONTINUE:
        break;
    case AST_GLOBAL: {
        int i;
        for (i = 0; i < node->data.global_stmt.num_names; i++) {
            Symbol *sym = declare(node->data.global_stmt.names[i], type_any, SYM_VAR);
            if (sym) sym->is_global = 1;
        }
        break;
    }
    case AST_NONLOCAL: {
        int i;
        for (i = 0; i < node->data.global_stmt.num_names; i++) {
            const char *vname = node->data.global_stmt.names[i];
            Symbol *sym = declare(vname, type_any, SYM_VAR);
            if (sym) sym->is_nonlocal = 1;

            /* Walk up to find the variable in an enclosing function scope */
            {
                Scope *s;
                int found = 0;
                for (s = current_scope->parent; s; s = s->parent) {
                    Symbol *outer_sym;
                    for (outer_sym = s->symbols; outer_sym; outer_sym = outer_sym->next) {
                        if (strcmp(outer_sym->name, vname) == 0) {
                            int already, j;
                            outer_sym->is_captured = 1;

                            /* Add to enclosing scope's cell_vars */
                            already = 0;
                            for (j = 0; j < s->num_cell_vars; j++) {
                                if (strcmp(s->cell_vars[j], vname) == 0) {
                                    already = 1;
                                    break;
                                }
                            }
                            if (!already && s->num_cell_vars < 32) {
                                s->cell_vars[s->num_cell_vars++] = str_dup(vname);
                            }

                            /* Add to current scope's free_vars */
                            already = 0;
                            for (j = 0; j < current_scope->num_free_vars; j++) {
                                if (strcmp(current_scope->free_vars[j], vname) == 0) {
                                    already = 1;
                                    break;
                                }
                            }
                            if (!already && current_scope->num_free_vars < 32) {
                                current_scope->free_vars[current_scope->num_free_vars++] =
                                    str_dup(vname);
                            }

                            found = 1;
                            break;
                        }
                    }
                    if (found) break;
                    /* Keep searching past non-function scopes (class, block) */
                }

                if (!found) {
                    error(node, "no binding for nonlocal variable found");
                }
            }
        }
        break;
    }
    case AST_DELETE:
        if (node->data.del_stmt.targets) {
            analyze_expr(node->data.del_stmt.targets);
        }
        break;
    case AST_WITH: {
        ASTNode *item;
        ASTNode *body;
        for (item = node->data.with_stmt.items; item; item = item->next) {
            if (item->kind == AST_WITH_ITEM) {
                if (item->data.with_item.context_expr) {
                    analyze_expr(item->data.with_item.context_expr);
                }
                if (item->data.with_item.optional_vars) {
                    /* Declare the "as" variable */
                    ASTNode *var_node = item->data.with_item.optional_vars;
                    if (var_node->kind == AST_NAME) {
                        declare(var_node->data.name.id, type_any, SYM_VAR);
                    }
                }
            }
        }
        for (body = node->data.with_stmt.body; body; body = body->next) {
            analyze_stmt(body);
        }
        break;
    }
    case AST_ASSERT:
        if (node->data.assert_stmt.test) {
            analyze_expr(node->data.assert_stmt.test);
        }
        if (node->data.assert_stmt.msg) {
            analyze_expr(node->data.assert_stmt.msg);
        }
        break;
    case AST_TYPE_ALIAS:
        analyze_type_alias(node);
        break;
    case AST_YIELD_STMT:
        break;
    default:
        /* Try as expression */
        analyze_expr(node);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Function definition                                                 */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_funcdef(ASTNode *node)
{
    TypeInfo *param_types_list = 0;
    TypeInfo *prev_pt = 0;
    TypeInfo *ret_type;
    TypeInfo *func_type;
    int num_params;
    Param *p;
    ASTNode *body;

    if (!node) return;

    /* Pre-declare PEP 695 function type parameters in enclosing scope
     * so they're visible during parameter/return type resolution */
    if (node->data.func_def.type_param_names &&
        node->data.func_def.num_type_params > 0) {
        int tp;
        for (tp = 0; tp < node->data.func_def.num_type_params; tp++) {
            declare(node->data.func_def.type_param_names[tp], type_any, SYM_BUILTIN);
        }
    }

    /* Resolve return type annotation */
    if (node->data.func_def.return_type) {
        ret_type = resolve_type(node->data.func_def.return_type);
    } else {
        ret_type = type_none;
    }

    /* Resolve parameter type annotations and build param type list */
    num_params = 0;
    for (p = node->data.func_def.params; p; p = p->next) {
        TypeInfo *pt;
        TypeInfo *ptcopy;

        /* Skip bare * separator (keyword-only marker) */
        if (p->is_star && p->name && strcmp(p->name, "*") == 0)
            continue;

        if (p->annotation) {
            pt = resolve_type(p->annotation);
        } else if (num_params == 0 && current_scope && current_scope->class_name) {
            /* First parameter of a class method (self) — infer class type */
            Symbol *cls_sym = resolve(current_scope->class_name);
            if (cls_sym && cls_sym->type) {
                pt = cls_sym->type;
            } else {
                pt = type_any;
            }
        } else {
            pt = type_any;
        }

        /* Wrap star-args type as list (tuple at runtime), kwargs as dict */
        if (p->is_star && p->name && strcmp(p->name, "*") != 0) {
            pt = type_new_list(pt);
        } else if (p->is_double_star) {
            pt = type_new_dict(type_str, pt);
        }

        ptcopy = type_copy(pt);
        if (ptcopy) {
            ptcopy->next = 0;
            if (prev_pt) prev_pt->next = ptcopy;
            else param_types_list = ptcopy;
            prev_pt = ptcopy;
        }
        num_params++;
    }

    /* Validate parameter ordering: positional-only (/) before * before ** */
    {
        int seen_star = 0, seen_dstar = 0, seen_posonly = 0;
        Param *vp;
        for (vp = node->data.func_def.params; vp; vp = vp->next) {
            if (vp->is_positional_only) {
                if (seen_star || seen_dstar)
                    error(node, "positional-only parameters must come before * and **");
                seen_posonly = 1;
            }
            if (vp->is_double_star) {
                if (seen_dstar) error(node, "only one **kwargs parameter allowed");
                if (vp->next) error(node, "**kwargs must be the last parameter");
                seen_dstar = 1;
            } else if (vp->is_star && vp->name && strcmp(vp->name, "*") != 0) {
                if (seen_star) error(node, "only one *args parameter allowed");
                if (seen_dstar) error(node, "*args must come before **kwargs");
                seen_star = 1;
            }
        }
    }

    /* Create function type */
    func_type = type_new_func(param_types_list, num_params, ret_type);

    /* Declare function in current (enclosing) scope */
    declare(node->data.func_def.name, func_type, SYM_FUNC);

    /* Push function scope */
    push_scope(1, 0);
    current_scope->return_type = ret_type;
    current_scope->func_name = node->data.func_def.name;

    /* Declare PEP 695 type parameters as type_any in scope */
    if (node->data.func_def.type_param_names &&
        node->data.func_def.num_type_params > 0) {
        int tp;
        for (tp = 0; tp < node->data.func_def.num_type_params; tp++) {
            declare(node->data.func_def.type_param_names[tp], type_any, SYM_BUILTIN);
        }
    }

    /* Declare parameters as locals (skip bare * separator) */
    {
        TypeInfo *pt_cur = param_types_list;
        for (p = node->data.func_def.params; p; p = p->next) {
            if (p->is_star && p->name && strcmp(p->name, "*") == 0)
                continue;
            TypeInfo *param_t = pt_cur ? pt_cur : type_any;
            declare(p->name, param_t, SYM_PARAM);
            if (pt_cur) pt_cur = pt_cur->next;
        }
    }

    /* Analyze body */
    for (body = node->data.func_def.body; body; body = body->next) {
        analyze_stmt(body);
    }

    /* Copy closure info from scope to AST node before popping */
    if (current_scope->num_cell_vars > 0) {
        int cv;
        node->data.func_def.cell_var_names =
            name_array_alloc(current_scope->num_cell_vars);
        node->data.func_def.num_cell_vars = current_scope->num_cell_vars;
        for (cv = 0; cv < current_scope->num_cell_vars; cv++) {
            node->data.func_def.cell_var_names[cv] = current_scope->cell_vars[cv];
        }
    }
    if (current_scope->num_free_vars > 0) {
        int fv;
        node->data.func_def.free_var_names =
            name_array_alloc(current_scope->num_free_vars);
        node->data.func_def.num_free_vars = current_scope->num_free_vars;
        for (fv = 0; fv < current_scope->num_free_vars; fv++) {
            node->data.func_def.free_var_names[fv] = current_scope->free_vars[fv];
        }
    }

    pop_scope();
}

/* ------------------------------------------------------------------ */
/* Class definition                                                    */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_classdef(ASTNode *node)
{
    ClassInfo *ci;
    TypeInfo *class_type;
    ASTNode *body;
    ASTNode *base_node;
    int base_count;

    if (!node) return;

    /* Create ClassInfo */
    ci = (ClassInfo *)malloc(sizeof(ClassInfo));
    if (!ci) return;
    memset(ci, 0, sizeof(ClassInfo));
    ci->name = str_dup(node->data.class_def.name);
    ci->vtable_size = 0;
    ci->is_generic = (node->data.class_def.num_type_params > 0) ? 1 : 0;

    /* Resolve base classes */
    base_count = 0;
    for (base_node = node->data.class_def.bases; base_node; base_node = base_node->next) {
        base_count++;
    }
    if (base_count > 0) {
        ci->bases = (ClassInfo **)malloc(sizeof(ClassInfo *) * base_count);
        if (ci->bases) {
            int idx = 0;
            for (base_node = node->data.class_def.bases; base_node; base_node = base_node->next) {
                Symbol *base_sym = 0;
                if (base_node->kind == AST_NAME && base_node->data.name.id) {
                    base_sym = resolve(base_node->data.name.id);
                }
                if (base_sym && base_sym->type && base_sym->type->class_info) {
                    ci->bases[idx] = base_sym->type->class_info;
                    if (idx == 0) {
                        ci->base = base_sym->type->class_info;
                    }
                    idx++;
                } else if (base_node->kind == AST_NAME && base_node->data.name.id) {
                    error_fmt(base_node, "undefined base class '%s'",
                              base_node->data.name.id);
                }
            }
            ci->num_bases = idx;
        }
    }

    /* Create class type and declare in current scope */
    class_type = type_new_class(ci);
    declare(node->data.class_def.name, class_type, SYM_CLASS);

    /* Push class scope */
    push_scope(0, 1);
    current_scope->class_name = node->data.class_def.name;

    /* Declare type parameters in class scope so T resolves in annotations */
    if (node->data.class_def.num_type_params > 0) {
        int tp;
        for (tp = 0; tp < node->data.class_def.num_type_params; tp++) {
            declare(node->data.class_def.type_param_names[tp], type_any, SYM_BUILTIN);
        }
    }

    /* Analyze class body */
    for (body = node->data.class_def.body; body; body = body->next) {
        analyze_stmt(body);

        /* If it's a function def, add to class members */
        if (body->kind == AST_FUNC_DEF && body->data.func_def.name) {
            Symbol *method = resolve(body->data.func_def.name);
            if (method) {
                Symbol *mem = (Symbol *)malloc(sizeof(Symbol));
                if (mem) {
                    memset(mem, 0, sizeof(Symbol));
                    mem->name = str_dup(body->data.func_def.name);
                    mem->type = method->type;
                    mem->kind = SYM_FUNC;
                    mem->next = ci->members;
                    ci->members = mem;
                    ci->vtable_size++;
                }
            }

        }
    }

    pop_scope();
}

/* ------------------------------------------------------------------ */
/* Control flow statements                                             */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_if(ASTNode *node)
{
    ASTNode *child;

    if (!node) return;

    if (node->data.if_stmt.condition) {
        TypeInfo *cond_type = analyze_expr(node->data.if_stmt.condition);
        (void)cond_type;
    }

    for (child = node->data.if_stmt.body; child; child = child->next) {
        analyze_stmt(child);
    }

    if (node->data.if_stmt.else_body) {
        ASTNode *eb;
        for (eb = node->data.if_stmt.else_body; eb; eb = eb->next) {
            analyze_stmt(eb);
        }
    }
}

void SemanticAnalyzer::analyze_while(ASTNode *node)
{
    ASTNode *child;

    if (!node) return;

    if (node->data.while_stmt.condition) {
        TypeInfo *cond_type = analyze_expr(node->data.while_stmt.condition);
        (void)cond_type;
    }

    for (child = node->data.while_stmt.body; child; child = child->next) {
        analyze_stmt(child);
    }

    if (node->data.while_stmt.else_body) {
        ASTNode *eb;
        for (eb = node->data.while_stmt.else_body; eb; eb = eb->next) {
            analyze_stmt(eb);
        }
    }
}

void SemanticAnalyzer::analyze_for(ASTNode *node)
{
    TypeInfo *iter_type;
    TypeInfo *elem_type;
    ASTNode *child;
    ASTNode *target;

    if (!node) return;

    /* Analyze the iterable */
    iter_type = type_any;
    if (node->data.for_stmt.iter) {
        iter_type = analyze_expr(node->data.for_stmt.iter);
    }

    /* Determine element type from iterable */
    elem_type = type_any;
    if (iter_type) {
        switch (iter_type->kind) {
        case TY_LIST:
        case TY_SET:
            if (iter_type->type_args) elem_type = iter_type->type_args;
            break;
        case TY_DICT:
            if (iter_type->type_args) elem_type = iter_type->type_args;
            break;
        case TY_STR:
            elem_type = type_str;
            break;
        case TY_RANGE:
            elem_type = type_int;
            break;
        case TY_BYTES:
            elem_type = type_int;
            break;
        default:
            break;
        }
    }

    /* Declare loop variable(s) */
    target = node->data.for_stmt.target;
    if (target) {
        if (target->kind == AST_NAME && target->data.name.id) {
            declare(target->data.name.id, elem_type, SYM_VAR);
        } else if (target->kind == AST_TUPLE_EXPR) {
            ASTNode *telem;
            for (telem = target->data.collection.elts; telem; telem = telem->next) {
                if (telem->kind == AST_NAME && telem->data.name.id) {
                    declare(telem->data.name.id, type_any, SYM_VAR);
                }
            }
        }
    }

    /* Analyze body */
    for (child = node->data.for_stmt.body; child; child = child->next) {
        analyze_stmt(child);
    }

    if (node->data.for_stmt.else_body) {
        ASTNode *eb;
        for (eb = node->data.for_stmt.else_body; eb; eb = eb->next) {
            analyze_stmt(eb);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Assignment statements                                               */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_assign(ASTNode *node)
{
    TypeInfo *val_type;
    ASTNode *tgt;
    Symbol *existing;

    if (!node) return;

    /* Analyze value */
    val_type = type_any;
    if (node->data.assign.value) {
        val_type = analyze_expr(node->data.assign.value);
    }

    /* Handle targets (linked list via ->next for multiple assignment) */
    for (tgt = node->data.assign.targets; tgt; tgt = tgt->next) {
        if (tgt->kind == AST_NAME && tgt->data.name.id) {
            existing = resolve(tgt->data.name.id);
            if (existing) {
                if (!type_compatible(existing->type, val_type) &&
                    existing->type->kind != TY_ANY &&
                    val_type->kind != TY_ANY) {
                    error_fmt(node, "cannot assign '%s' to variable '%s' of type '%s'",
                              type_to_string(val_type), tgt->data.name.id,
                              type_to_string(existing->type));
                }
                existing->type = val_type;
            } else {
                declare(tgt->data.name.id, val_type, SYM_VAR);
            }
        } else if (tgt->kind == AST_TUPLE_EXPR) {
            ASTNode *telem;
            for (telem = tgt->data.collection.elts; telem; telem = telem->next) {
                if (telem->kind == AST_NAME && telem->data.name.id) {
                    existing = resolve(telem->data.name.id);
                    if (!existing) {
                        declare(telem->data.name.id, type_any, SYM_VAR);
                    }
                }
            }
        } else {
            /* AST_ATTR or AST_SUBSCRIPT target */
            analyze_expr(tgt);

            /* Register instance attributes: self.xxx = expr */
            if (tgt->kind == AST_ATTR &&
                tgt->data.attribute.attr &&
                tgt->data.attribute.object &&
                tgt->data.attribute.object->kind == AST_NAME) {
                Symbol *obj_sym = resolve(tgt->data.attribute.object->data.name.id);
                if (obj_sym && obj_sym->kind == SYM_PARAM &&
                    obj_sym->type && type_is_class_like(obj_sym->type) &&
                    obj_sym->type->class_info) {
                    ClassInfo *ci = obj_sym->type->class_info;
                    Symbol *m;
                    int found = 0;
                    for (m = ci->members; m; m = m->next) {
                        if (strcmp(m->name, tgt->data.attribute.attr) == 0) {
                            found = 1;
                            break;
                        }
                    }
                    if (!found) {
                        Symbol *amem = (Symbol *)malloc(sizeof(Symbol));
                        if (amem) {
                            memset(amem, 0, sizeof(Symbol));
                            amem->name = str_dup(tgt->data.attribute.attr);
                            amem->type = val_type;
                            amem->kind = SYM_VAR;
                            amem->next = ci->members;
                            ci->members = amem;
                        }
                    }
                }
            }
        }
    }
}

void SemanticAnalyzer::analyze_ann_assign(ASTNode *node)
{
    TypeInfo *ann_type;
    TypeInfo *val_type;

    if (!node) return;

    /* Resolve annotation type */
    ann_type = type_any;
    if (node->data.ann_assign.annotation) {
        ann_type = resolve_type(node->data.ann_assign.annotation);
    }

    /* Analyze value if present */
    if (node->data.ann_assign.value) {
        val_type = analyze_expr(node->data.ann_assign.value);
        if (!type_compatible(ann_type, val_type) &&
            ann_type->kind != TY_ANY && val_type->kind != TY_ANY) {
            error_fmt(node, "type '%s' is not compatible with annotation '%s'",
                      type_to_string(val_type), type_to_string(ann_type));
        }
    }

    /* Declare variable with annotation type */
    {
        ASTNode *tgt = node->data.ann_assign.target;
        if (tgt && tgt->kind == AST_NAME && tgt->data.name.id) {
            declare(tgt->data.name.id, ann_type, SYM_VAR);
        } else if (tgt && (tgt->kind == AST_ATTR || tgt->kind == AST_SUBSCRIPT)) {
            /* Attribute/subscript targets — analyze the expression but no declaration needed */
            analyze_expr(tgt);
        }
    }
}

void SemanticAnalyzer::analyze_aug_assign(ASTNode *node)
{
    TypeInfo *target_type;
    TypeInfo *val_type;
    BinOp op;

    if (!node) return;

    op = node->data.aug_assign.op;

    target_type = type_any;
    if (node->data.aug_assign.target) {
        target_type = analyze_expr(node->data.aug_assign.target);
    }

    val_type = type_any;
    if (node->data.aug_assign.value) {
        val_type = analyze_expr(node->data.aug_assign.value);
    }

    if (type_is_numeric(target_type) && type_is_numeric(val_type)) {
        TypeInfo *result = type_common(target_type, val_type);
        ASTNode *tgt = node->data.aug_assign.target;
        if (tgt && tgt->kind == AST_NAME && tgt->data.name.id) {
            Symbol *sym = resolve(tgt->data.name.id);
            if (sym) sym->type = result;
        }
    } else if (target_type->kind == TY_STR && val_type->kind == TY_STR &&
               op == OP_ADD) {
        /* str += str */
    } else if (target_type->kind == TY_LIST && op == OP_ADD) {
        /* list += ... */
    } else if (target_type->kind == TY_STR && type_is_numeric(val_type) &&
               op == OP_MUL) {
        /* str *= int */
    } else if (type_is_class_like(target_type) || type_is_class_like(val_type)) {
        /* class instances may have __iadd__ etc. via vtable */
    } else if (target_type->kind == TY_ANY || val_type->kind == TY_ANY ||
               target_type->kind == TY_ERROR || val_type->kind == TY_ERROR) {
        /* suppress */
    } else {
        error_fmt(node, "unsupported augmented assignment between '%s' and '%s'",
                  type_to_string(target_type), type_to_string(val_type));
    }
}

/* ------------------------------------------------------------------ */
/* Return statement                                                    */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_return(ASTNode *node)
{
    TypeInfo *val_type;
    TypeInfo *expected;

    if (!node) return;

    val_type = type_none;
    if (node->data.ret.value) {
        val_type = analyze_expr(node->data.ret.value);
    }

    /* Find enclosing function's expected return type */
    expected = 0;
    {
        Scope *s;
        for (s = current_scope; s; s = s->parent) {
            if (s->is_function) {
                expected = s->return_type;
                break;
            }
        }
    }

    if (expected && !type_compatible(expected, val_type) &&
        expected->kind != TY_ANY && val_type->kind != TY_ANY) {
        error_fmt(node, "return type '%s' does not match expected '%s'",
                  type_to_string(val_type), type_to_string(expected));
    }
}

/* ------------------------------------------------------------------ */
/* Try / Raise / Import                                                */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_try(ASTNode *node)
{
    ASTNode *child;
    ASTNode *handler;

    if (!node) return;

    /* Analyze try body */
    for (child = node->data.try_stmt.body; child; child = child->next) {
        analyze_stmt(child);
    }

    /* Validate except vs except* mixing */
    {
        int has_regular = 0;
        int has_star = 0;
        for (handler = node->data.try_stmt.handlers; handler; handler = handler->next) {
            if (handler->kind == AST_EXCEPT_HANDLER) {
                if (handler->data.handler.is_star) {
                    has_star = 1;
                    /* Bare except* is not allowed (Python requires a type) */
                    if (!handler->data.handler.type) {
                        error(handler, "except* requires an exception type");
                    }
                } else {
                    has_regular = 1;
                }
            }
        }
        if (has_regular && has_star) {
            error(node, "cannot mix except and except* in the same try block");
        }
    }

    /* Analyze except handlers */
    for (handler = node->data.try_stmt.handlers; handler; handler = handler->next) {
        if (handler->kind == AST_EXCEPT_HANDLER) {
            if (handler->data.handler.name) {
                declare(handler->data.handler.name, type_any, SYM_VAR);
            }
            for (child = handler->data.handler.body; child; child = child->next) {
                analyze_stmt(child);
            }
        }
    }

    /* Analyze else body */
    if (node->data.try_stmt.else_body) {
        ASTNode *eb;
        for (eb = node->data.try_stmt.else_body; eb; eb = eb->next) {
            analyze_stmt(eb);
        }
    }

    /* Analyze finally body */
    if (node->data.try_stmt.finally_body) {
        ASTNode *fb;
        for (fb = node->data.try_stmt.finally_body; fb; fb = fb->next) {
            analyze_stmt(fb);
        }
    }
}

void SemanticAnalyzer::analyze_raise(ASTNode *node)
{
    if (!node) return;
    if (node->data.raise_stmt.exc) {
        analyze_expr(node->data.raise_stmt.exc);
    }
}

void SemanticAnalyzer::analyze_import(ASTNode *node)
{
    if (!node) return;

    if (node->kind == AST_IMPORT) {
        /* import module [as alias] */
        const char *name = node->data.import_stmt.alias
                           ? node->data.import_stmt.alias
                           : node->data.import_stmt.module;
        if (name) {
            declare(name, type_any, SYM_MODULE);
        }
    } else if (node->kind == AST_IMPORT_FROM) {
        /* from module import names */
        const char *mod_name = node->data.import_from.module;
        ModuleInfo *mi = 0;

        /* Try to scan the module if search_paths are configured */
        if (mod_name && num_search_paths > 0) {
            mi = find_scanned_module(mod_name);
            if (!mi && num_scanned < 32) {
                /* Allocate cache on first use */
                if (!scanned_modules) {
                    scanned_modules = (ModuleInfo *)malloc(
                        32 * sizeof(ModuleInfo));
                    if (scanned_modules)
                        memset(scanned_modules, 0,
                               32 * sizeof(ModuleInfo));
                }
                if (scanned_modules) {
                    module_scan(mod_name, search_paths, num_search_paths,
                                &scanned_modules[num_scanned]);
                    if (scanned_modules[num_scanned].valid) {
                        mi = &scanned_modules[num_scanned];
                        num_scanned++;
                    }
                }
            }
        }

        ASTNode *imp;
        for (imp = node->data.import_from.names; imp; imp = imp->next) {
            if (imp->kind == AST_IMPORT_NAME) {
                const char *name = imp->data.import_name.alias
                                   ? imp->data.import_name.alias
                                   : imp->data.import_name.imported_name;
                const char *orig = imp->data.import_name.imported_name;
                if (!name) continue;

                /* Try to classify via scanned module info */
                SymbolKind sk = SYM_VAR;
                if (mi && orig) {
                    int j;
                    for (j = 0; j < mi->num_functions; j++) {
                        if (strcmp(mi->functions[j].name, orig) == 0) {
                            sk = SYM_FUNC;
                            break;
                        }
                    }
                    if (sk == SYM_VAR) {
                        for (j = 0; j < mi->num_classes; j++) {
                            if (strcmp(mi->classes[j].name, orig) == 0) {
                                sk = SYM_CLASS;
                                break;
                            }
                        }
                    }
                }

                declare(name, type_any, sk);
            }
        }
    }
}

void SemanticAnalyzer::analyze_expr_stmt(ASTNode *node)
{
    if (!node) return;
    if (node->data.expr_stmt.expr) {
        analyze_expr(node->data.expr_stmt.expr);
    }
}

/* ------------------------------------------------------------------ */
/* Type alias analysis (compile-time only, no codegen)                 */
/* ------------------------------------------------------------------ */

void SemanticAnalyzer::analyze_type_alias(ASTNode *node)
{
    int i;

    if (!node) return;

    /* Pre-declare type params so RHS can reference them */
    for (i = 0; i < node->data.type_alias.num_type_params; i++) {
        declare(node->data.type_alias.type_param_names[i], type_any, SYM_BUILTIN);
    }

    /* Resolve the RHS type expression */
    TypeInfo *resolved = type_any;
    if (node->data.type_alias.value) {
        resolved = resolve_type(node->data.type_alias.value);
        if (!resolved) resolved = type_any;
    }

    /* Register alias name as a variable with the resolved type */
    declare(node->data.type_alias.name, resolved, SYM_VAR);
}

/* ------------------------------------------------------------------ */
/* Expression analysis dispatch                                        */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_expr(ASTNode *node)
{
    TypeInfo *result = type_error;

    if (!node) return type_error;

    switch (node->kind) {
    case AST_INT_LIT:
        result = type_int;
        break;
    case AST_FLOAT_LIT:
        result = type_float;
        break;
    case AST_COMPLEX_LIT:
        result = type_complex;
        break;
    case AST_STR_LIT:
        result = type_str;
        break;
    case AST_BOOL_LIT:
        result = type_bool;
        break;
    case AST_NONE_LIT:
        result = type_none;
        break;
    case AST_FSTRING: {
        ASTNode *part;
        for (part = node->data.fstring.parts; part; part = part->next) {
            analyze_expr(part);
        }
        result = type_str;
        break;
    }
    case AST_NAME:
        result = analyze_name(node);
        break;
    case AST_BINOP:
        result = analyze_binop(node);
        break;
    case AST_UNARYOP:
        result = analyze_unaryop(node);
        break;
    case AST_COMPARE:
        result = analyze_compare(node);
        break;
    case AST_BOOLOP:
        result = analyze_boolop(node);
        break;
    case AST_CALL:
        result = analyze_call(node);
        break;
    case AST_ATTR:
        result = analyze_attr(node);
        break;
    case AST_SUBSCRIPT:
        result = analyze_subscript(node);
        break;
    case AST_LIST_EXPR:
        result = analyze_listexpr(node);
        break;
    case AST_DICT_EXPR:
        result = analyze_dictexpr(node);
        break;
    case AST_TUPLE_EXPR:
        result = analyze_tupleexpr(node);
        break;
    case AST_SET_EXPR: {
        TypeInfo *elem_t = 0;
        ASTNode *elem;
        for (elem = node->data.collection.elts; elem; elem = elem->next) {
            TypeInfo *et = analyze_expr(elem);
            if (!elem_t) elem_t = et;
            else elem_t = type_common(elem_t, et);
        }
        result = type_new_set(elem_t ? elem_t : type_any);
        break;
    }
    case AST_IFEXPR:
        result = analyze_ifexpr(node);
        break;
    case AST_LAMBDA:
        result = analyze_lambda(node);
        break;
    case AST_STARRED:
        if (node->data.starred.value) {
            TypeInfo *inner = analyze_expr(node->data.starred.value);
            result = type_new_list(inner);
        } else {
            result = type_new_list(type_any);
        }
        break;
    case AST_SLICE: {
        if (node->data.slice.lower) analyze_expr(node->data.slice.lower);
        if (node->data.slice.upper) analyze_expr(node->data.slice.upper);
        if (node->data.slice.step) analyze_expr(node->data.slice.step);
        result = type_any;
        break;
    }
    case AST_YIELD_EXPR:
        if (node->data.yield_expr.value) {
            result = analyze_expr(node->data.yield_expr.value);
        } else {
            result = type_none;
        }
        break;
    case AST_YIELD_FROM_EXPR:
        if (node->data.yield_expr.value) {
            result = analyze_expr(node->data.yield_expr.value);
        } else {
            result = type_any;
        }
        break;
    case AST_AWAIT:
        if (node->data.starred.value) {
            result = analyze_expr(node->data.starred.value);
        } else {
            result = type_any;
        }
        break;
    case AST_LISTCOMP:
    case AST_SETCOMP:
    case AST_GENEXPR: {
        /* Analyze generators first to declare iteration variables */
        ASTNode *gen = node->data.listcomp.generators;
        while (gen) {
            if (gen->kind == AST_COMP_GENERATOR) {
                /* Analyze iterable */
                if (gen->data.comp_gen.iter) {
                    analyze_expr(gen->data.comp_gen.iter);
                }
                /* Declare target variable(s) */
                if (gen->data.comp_gen.target) {
                    if (gen->data.comp_gen.target->kind == AST_NAME &&
                        gen->data.comp_gen.target->data.name.id) {
                        declare(gen->data.comp_gen.target->data.name.id, type_any, SYM_VAR);
                    }
                }
                /* Analyze filter conditions */
                {
                    ASTNode *cond = gen->data.comp_gen.ifs;
                    while (cond) {
                        analyze_expr(cond);
                        cond = cond->next;
                    }
                }
            }
            gen = gen->next;
        }
        /* Then analyze element expression */
        if (node->data.listcomp.elt) {
            analyze_expr(node->data.listcomp.elt);
        }
        if (node->kind == AST_LISTCOMP) result = type_new_list(type_any);
        else if (node->kind == AST_SETCOMP) result = type_new_set(type_any);
        else result = type_any;
        break;
    }
    case AST_DICTCOMP: {
        /* Analyze generators first to declare iteration variables */
        ASTNode *dgen = node->data.dictcomp.generators;
        while (dgen) {
            if (dgen->kind == AST_COMP_GENERATOR) {
                if (dgen->data.comp_gen.iter)
                    analyze_expr(dgen->data.comp_gen.iter);
                if (dgen->data.comp_gen.target &&
                    dgen->data.comp_gen.target->kind == AST_NAME &&
                    dgen->data.comp_gen.target->data.name.id) {
                    declare(dgen->data.comp_gen.target->data.name.id, type_any, SYM_VAR);
                }
                ASTNode *dcond = dgen->data.comp_gen.ifs;
                while (dcond) { analyze_expr(dcond); dcond = dcond->next; }
            }
            dgen = dgen->next;
        }
        /* Then analyze key and value expressions */
        if (node->data.dictcomp.key) analyze_expr(node->data.dictcomp.key);
        if (node->data.dictcomp.value) analyze_expr(node->data.dictcomp.value);
        result = type_new_dict(type_any, type_any);
        break;
    }
    case AST_WALRUS: {
        TypeInfo *val = type_any;
        if (node->data.walrus.value) {
            val = analyze_expr(node->data.walrus.value);
        }
        if (node->data.walrus.target &&
            node->data.walrus.target->kind == AST_NAME &&
            node->data.walrus.target->data.name.id) {
            declare(node->data.walrus.target->data.name.id, val, SYM_VAR);
        }
        result = val;
        break;
    }
    case AST_KEYWORD_ARG:
        /* Analyze the keyword argument value */
        if (node->data.keyword_arg.kw_value) {
            result = analyze_expr(node->data.keyword_arg.kw_value);
        } else {
            result = type_any;
        }
        break;
    default:
        result = type_error;
        break;
    }

    set_expr_type(node, result);
    return result;
}

/* ------------------------------------------------------------------ */
/* Binary operation                                                    */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_binop(ASTNode *node)
{
    TypeInfo *left_type, *right_type;
    BinOp op;

    if (!node) return type_error;

    op = node->data.binop.op;
    left_type = analyze_expr(node->data.binop.left);
    right_type = analyze_expr(node->data.binop.right);

    if (left_type->kind == TY_ERROR) return type_error;
    if (right_type->kind == TY_ERROR) return type_error;
    if (left_type->kind == TY_ANY || right_type->kind == TY_ANY) return type_any;

    switch (op) {
    case OP_ADD:
        if (type_is_numeric(left_type) && type_is_numeric(right_type)) {
            return type_common(left_type, right_type);
        }
        if (left_type->kind == TY_STR && right_type->kind == TY_STR) return type_str;
        /* Implicit INT/BOOL + STR conversion: result is STR */
        if ((type_is_numeric(left_type) && right_type->kind == TY_STR) ||
            (left_type->kind == TY_STR && type_is_numeric(right_type)))
            return type_str;
        if (left_type->kind == TY_LIST && right_type->kind == TY_LIST) return left_type;
        if (left_type->kind == TY_TUPLE && right_type->kind == TY_TUPLE) return left_type;
        if (left_type->kind == TY_BYTES && right_type->kind == TY_BYTES) return type_bytes;
        /* Allow class + anything if the class might have __add__ */
        if (type_is_class_like(left_type)) return type_any;
        error_fmt(node, "unsupported operand types for +: '%s' and '%s'",
                  type_to_string(left_type), type_to_string(right_type));
        return type_error;

    case OP_SUB:
    case OP_FLOORDIV:
    case OP_MOD:
    case OP_POW:
        if (type_is_numeric(left_type) && type_is_numeric(right_type)) {
            return type_common(left_type, right_type);
        }
        if (left_type->kind == TY_SET && right_type->kind == TY_SET && op == OP_SUB) {
            return left_type;
        }
        /* Allow class operator overloading (__sub__, etc.) */
        if (type_is_class_like(left_type)) return type_any;
        error_fmt(node, "unsupported operand types for arithmetic: '%s' and '%s'",
                  type_to_string(left_type), type_to_string(right_type));
        return type_error;

    case OP_MUL:
        if (type_is_numeric(left_type) && type_is_numeric(right_type)) {
            return type_common(left_type, right_type);
        }
        if (left_type->kind == TY_STR && type_is_numeric(right_type)) return type_str;
        if (type_is_numeric(left_type) && right_type->kind == TY_STR) return type_str;
        if (left_type->kind == TY_LIST && type_is_numeric(right_type)) return left_type;
        if (type_is_numeric(left_type) && right_type->kind == TY_LIST) return right_type;
        /* Allow class operator overloading (__mul__) */
        if (type_is_class_like(left_type)) return type_any;
        error_fmt(node, "unsupported operand types for *: '%s' and '%s'",
                  type_to_string(left_type), type_to_string(right_type));
        return type_error;

    case OP_DIV:
        if (type_is_numeric(left_type) && type_is_numeric(right_type)) {
            return type_float;
        }
        /* Allow any/union types through — resolved at runtime */
        if (left_type->kind == TY_ANY || left_type->kind == TY_UNION ||
            right_type->kind == TY_ANY || right_type->kind == TY_UNION) {
            return type_float;
        }
        error_fmt(node, "unsupported operand types for /: '%s' and '%s'",
                  type_to_string(left_type), type_to_string(right_type));
        return type_error;

    case OP_BITAND:
    case OP_BITOR:
    case OP_BITXOR:
        if ((left_type->kind == TY_INT || left_type->kind == TY_BOOL) &&
            (right_type->kind == TY_INT || right_type->kind == TY_BOOL)) {
            return type_int;
        }
        if (left_type->kind == TY_SET && right_type->kind == TY_SET) {
            return left_type;
        }
        error_fmt(node, "unsupported operand types for bitwise op: '%s' and '%s'",
                  type_to_string(left_type), type_to_string(right_type));
        return type_error;

    case OP_LSHIFT:
    case OP_RSHIFT:
        if ((left_type->kind == TY_INT || left_type->kind == TY_BOOL) &&
            (right_type->kind == TY_INT || right_type->kind == TY_BOOL)) {
            return type_int;
        }
        error_fmt(node, "unsupported operand types for shift: '%s' and '%s'",
                  type_to_string(left_type), type_to_string(right_type));
        return type_error;

    case OP_MATMUL:
        return type_any;

    default:
        error(node, "unknown binary operator");
        return type_error;
    }
}

/* ------------------------------------------------------------------ */
/* Unary operation                                                     */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_unaryop(ASTNode *node)
{
    TypeInfo *operand_type;

    if (!node || !node->data.unaryop.operand) return type_error;

    operand_type = analyze_expr(node->data.unaryop.operand);

    if (operand_type->kind == TY_ERROR) return type_error;
    if (operand_type->kind == TY_ANY) return type_any;

    switch (node->data.unaryop.op) {
    case UNARY_NEG:
    case UNARY_POS:
        if (type_is_numeric(operand_type)) {
            if (operand_type->kind == TY_BOOL) return type_int;
            return operand_type;
        }
        if (type_is_class_like(operand_type)) return type_any;
        error_fmt(node, "unary +/- not supported for type '%s'",
                  type_to_string(operand_type));
        return type_error;

    case UNARY_NOT:
        return type_bool;

    case UNARY_BITNOT:
        if (operand_type->kind == TY_INT || operand_type->kind == TY_BOOL) {
            return type_int;
        }
        if (type_is_class_like(operand_type)) return type_any;
        error_fmt(node, "bitwise NOT not supported for type '%s'",
                  type_to_string(operand_type));
        return type_error;

    default:
        error(node, "unknown unary operator");
        return type_error;
    }
}

/* ------------------------------------------------------------------ */
/* Comparison                                                          */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_compare(ASTNode *node)
{
    TypeInfo *left_type;
    ASTNode *comp;
    int i;

    if (!node) return type_error;

    left_type = type_error;
    if (node->data.compare.left) {
        left_type = analyze_expr(node->data.compare.left);
    }

    /* Analyze all comparator expressions */
    comp = node->data.compare.comparators;
    for (i = 0; i < node->data.compare.num_ops; i++) {
        TypeInfo *right = type_any;
        if (comp) {
            right = analyze_expr(comp);
            comp = comp->next;
        }

        /* Ordering comparisons require compatible types */
        if (left_type->kind != TY_ANY && right->kind != TY_ANY &&
            left_type->kind != TY_ERROR && right->kind != TY_ERROR) {
            CmpOp cop = node->data.compare.ops[i];
            if (cop == CMP_LT || cop == CMP_GT ||
                cop == CMP_LE || cop == CMP_GE) {
                if (!type_is_numeric(left_type) || !type_is_numeric(right)) {
                    if (!type_equal(left_type, right) &&
                        left_type->kind != TY_STR) {
                        /* Other comparisons: may be valid at runtime */
                    }
                }
            }
        }
        left_type = right;
    }

    return type_bool;
}

/* ------------------------------------------------------------------ */
/* Boolean operation (and / or)                                        */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_boolop(ASTNode *node)
{
    TypeInfo *result_type = 0;
    ASTNode *child;

    if (!node) return type_error;

    for (child = node->data.boolop.values; child; child = child->next) {
        TypeInfo *child_type = analyze_expr(child);
        if (!result_type) {
            result_type = child_type;
        } else {
            result_type = type_common(result_type, child_type);
        }
    }

    return result_type ? result_type : type_any;
}

/* ------------------------------------------------------------------ */
/* Function / method call                                              */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_call(ASTNode *node)
{
    TypeInfo *func_type;
    ASTNode *arg;
    int arg_count;

    if (!node) return type_error;

    /* Analyze callee */
    func_type = type_any;
    if (node->data.call.func) {
        func_type = analyze_expr(node->data.call.func);
    }

    /* Count and analyze arguments */
    arg_count = 0;
    for (arg = node->data.call.args; arg; arg = arg->next) {
        analyze_expr(arg);
        arg_count++;
    }

    /* Determine return type */
    if (func_type->kind == TY_FUNC) {
        return func_type->ret_type ? func_type->ret_type : type_none;
    }

    if (type_is_class_like(func_type)) {
        /* Constructor call */
        return func_type;
    }

    if (func_type->kind == TY_ANY) {
        return type_any;
    }

    if (func_type->kind == TY_ERROR) {
        return type_error;
    }

    error_fmt(node, "type '%s' is not callable", type_to_string(func_type));
    return type_error;
}

/* ------------------------------------------------------------------ */
/* Attribute access                                                    */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_attr(ASTNode *node)
{
    TypeInfo *obj_type;
    const char *attr;

    if (!node) return type_error;

    obj_type = type_any;
    if (node->data.attribute.object) {
        obj_type = analyze_expr(node->data.attribute.object);
    }

    attr = node->data.attribute.attr;
    if (!attr) return type_any;

    if (obj_type->kind == TY_ANY || obj_type->kind == TY_ERROR) {
        return type_any;
    }

    /* Look up method in stdlib registry */
    if (stdlib_reg_ && stdlib_reg_->is_loaded()) {
        const BuiltinMethodEntry *m = stdlib_reg_->find_method(
            obj_type->kind, attr);
        if (m) {
            TypeInfo *ret = type_kind_to_info(m->ret_type_kind);
            int np = m->num_params;
            TypeInfo *pt_arr[4];
            int j;
            if (np > 4) np = 4;
            for (j = 0; j < np; j++) pt_arr[j] = type_any;

            /* Refine return type with parametric type info */
            if (obj_type->kind == TY_LIST) {
                TypeInfo *elem = obj_type->type_args
                                 ? obj_type->type_args : type_any;
                /* pop/any-returning → element type */
                if (m->ret_type_kind == TY_ANY) ret = elem;
                /* copy → same list type */
                if (m->ret_type_kind == TY_LIST) ret = obj_type;
            } else if (obj_type->kind == TY_DICT) {
                TypeInfo *key_t = obj_type->type_args
                                  ? obj_type->type_args : type_any;
                TypeInfo *val_t = (obj_type->type_args &&
                                   obj_type->type_args->next)
                                  ? obj_type->type_args->next : type_any;
                /* get/pop/setdefault → value type */
                if (m->ret_type_kind == TY_ANY) ret = val_t;
                if (strcmp(attr, "keys") == 0) ret = type_new_list(key_t);
                if (strcmp(attr, "values") == 0) ret = type_new_list(val_t);
            } else if (obj_type->kind == TY_SET) {
                /* union/copy/etc → same set type */
                if (m->ret_type_kind == TY_SET) ret = obj_type;
                /* pop → element type */
                if (m->ret_type_kind == TY_ANY) {
                    TypeInfo *elem = obj_type->type_args
                                     ? obj_type->type_args : type_any;
                    ret = elem;
                }
            }

            return make_builtin_func(pt_arr, np, ret);
        }
    }

    /* Class instance: look up in class members */
    if (type_is_class_like(obj_type) && obj_type->class_info) {
        Symbol *mem;
        ClassInfo *ci = obj_type->class_info;
        /* Search this class and base classes */
        while (ci) {
            for (mem = ci->members; mem; mem = mem->next) {
                if (strcmp(mem->name, attr) == 0) {
                    return mem->type;
                }
            }
            ci = ci->base;
        }
        return type_any;
    }

    return type_any;
}

/* ------------------------------------------------------------------ */
/* Subscript                                                           */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_subscript(ASTNode *node)
{
    TypeInfo *obj_type;
    TypeInfo *index_type;

    if (!node) return type_error;

    obj_type = type_any;
    index_type = type_any;

    if (node->data.subscript.object) {
        obj_type = analyze_expr(node->data.subscript.object);
    }
    if (node->data.subscript.index) {
        index_type = analyze_expr(node->data.subscript.index);
    }

    if (obj_type->kind == TY_ANY || obj_type->kind == TY_ERROR) {
        return type_any;
    }

    switch (obj_type->kind) {
    case TY_LIST:
        if (obj_type->type_args) return obj_type->type_args;
        return type_any;
    case TY_DICT:
        if (obj_type->type_args && obj_type->type_args->next) {
            return obj_type->type_args->next;
        }
        return type_any;
    case TY_TUPLE:
        return type_any;
    case TY_STR:
        return type_str;
    case TY_BYTES:
        if (index_type->kind == TY_INT) return type_int;
        return type_bytes;
    default:
        if (type_is_class_like(obj_type)) return type_any;
        error_fmt(node, "type '%s' is not subscriptable", type_to_string(obj_type));
        return type_error;
    }
}

/* ------------------------------------------------------------------ */
/* Name lookup                                                         */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_name(ASTNode *node)
{
    Symbol *sym;
    const char *id;

    if (!node) return type_error;
    id = node->data.name.id;
    if (!id) return type_error;

    sym = resolve(id);
    if (!sym) {
        error_fmt(node, "undefined name '%s'", id);
        return type_error;
    }
    return sym->type;
}

/* ------------------------------------------------------------------ */
/* Collection literals                                                 */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_listexpr(ASTNode *node)
{
    TypeInfo *elem_type = 0;
    ASTNode *elem;

    if (!node) return type_new_list(type_any);

    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        TypeInfo *et = analyze_expr(elem);
        if (!elem_type) elem_type = et;
        else elem_type = type_common(elem_type, et);
    }

    return type_new_list(elem_type ? elem_type : type_any);
}

TypeInfo *SemanticAnalyzer::analyze_dictexpr(ASTNode *node)
{
    TypeInfo *key_type = 0;
    TypeInfo *val_type = 0;
    ASTNode *k, *v;

    if (!node) return type_new_dict(type_any, type_any);

    /* dict.keys and dict.values are parallel linked lists */
    k = node->data.dict.keys;
    v = node->data.dict.values;
    while (k && v) {
        TypeInfo *kt = analyze_expr(k);
        TypeInfo *vt = analyze_expr(v);
        if (!key_type) key_type = kt; else key_type = type_common(key_type, kt);
        if (!val_type) val_type = vt; else val_type = type_common(val_type, vt);
        k = k->next;
        v = v->next;
    }

    return type_new_dict(key_type ? key_type : type_any,
                         val_type ? val_type : type_any);
}

TypeInfo *SemanticAnalyzer::analyze_tupleexpr(ASTNode *node)
{
    TypeInfo *elem_types = 0;
    TypeInfo *prev = 0;
    ASTNode *elem;
    int count = 0;

    if (!node) return type_new(TY_TUPLE, "tuple");

    for (elem = node->data.collection.elts; elem; elem = elem->next) {
        TypeInfo *et = analyze_expr(elem);
        TypeInfo *etcopy = type_copy(et);
        if (etcopy) {
            etcopy->next = 0;
            if (prev) prev->next = etcopy;
            else elem_types = etcopy;
            prev = etcopy;
        }
        count++;
    }

    return type_new_tuple(elem_types, count);
}

/* ------------------------------------------------------------------ */
/* Conditional expression (ternary)                                    */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_ifexpr(ASTNode *node)
{
    TypeInfo *then_type, *else_type;

    if (!node) return type_error;

    /* data.ifexpr: body if test else else_body */
    if (node->data.ifexpr.test) {
        analyze_expr(node->data.ifexpr.test);
    }

    then_type = type_any;
    if (node->data.ifexpr.body) {
        then_type = analyze_expr(node->data.ifexpr.body);
    }

    else_type = type_any;
    if (node->data.ifexpr.else_body) {
        else_type = analyze_expr(node->data.ifexpr.else_body);
    }

    return type_common(then_type, else_type);
}

/* ------------------------------------------------------------------ */
/* Lambda                                                              */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::analyze_lambda(ASTNode *node)
{
    TypeInfo *param_types_list = 0;
    TypeInfo *prev_pt = 0;
    TypeInfo *body_type;
    int num_params = 0;
    Param *p;

    if (!node) return type_error;

    push_scope(1, 0);
    current_scope->func_name = "<lambda>";

    for (p = node->data.lambda.params; p; p = p->next) {
        TypeInfo *pt;
        TypeInfo *ptcopy;

        if (p->annotation) {
            pt = resolve_type(p->annotation);
        } else {
            pt = type_any;
        }
        declare(p->name, pt, SYM_PARAM);

        ptcopy = type_copy(pt);
        if (ptcopy) {
            ptcopy->next = 0;
            if (prev_pt) prev_pt->next = ptcopy;
            else param_types_list = ptcopy;
            prev_pt = ptcopy;
        }
        num_params++;
    }

    body_type = type_any;
    if (node->data.lambda.body) {
        body_type = analyze_expr(node->data.lambda.body);
    }

    pop_scope();

    return type_new_func(param_types_list, num_params, body_type);
}

/* ------------------------------------------------------------------ */
/* Type annotation resolution                                          */
/* ------------------------------------------------------------------ */

TypeInfo *SemanticAnalyzer::resolve_type(ASTNode *type_node)
{
    if (!type_node) return type_any;

    switch (type_node->kind) {
    case AST_TYPE_NAME: {
        const char *name = type_node->data.type_name.tname;
        if (!name) return type_any;

        if (strcmp(name, "int") == 0)    return type_int;
        if (strcmp(name, "float") == 0)  return type_float;
        if (strcmp(name, "str") == 0)    return type_str;
        if (strcmp(name, "bool") == 0)   return type_bool;
        if (strcmp(name, "None") == 0)   return type_none;
        if (strcmp(name, "bytes") == 0)  return type_bytes;
        if (strcmp(name, "range") == 0)  return type_range;
        if (strcmp(name, "Any") == 0)    return type_any;
        if (strcmp(name, "object") == 0) return type_any;
        if (strcmp(name, "list") == 0)   return type_new_list(type_any);
        if (strcmp(name, "dict") == 0)   return type_new_dict(type_any, type_any);
        if (strcmp(name, "tuple") == 0)  return type_new(TY_TUPLE, "tuple");
        if (strcmp(name, "set") == 0)    return type_new_set(type_any);
        if (strcmp(name, "frozenset") == 0) return type_frozenset;
        if (strcmp(name, "complex") == 0) return type_complex;
        if (strcmp(name, "bytearray") == 0) return type_bytearray;

        /* Look up in scope */
        {
            Symbol *sym = resolve(name);
            if (sym) {
                if (sym->kind == SYM_CLASS) return sym->type;
                return sym->type;
            }
        }

        error_fmt(type_node, "unknown type '%s'", name);
        return type_error;
    }

    case AST_NAME: {
        /* Parser may use AST_NAME for type names in some contexts */
        const char *name = type_node->data.name.id;
        if (!name) return type_any;

        if (strcmp(name, "int") == 0)    return type_int;
        if (strcmp(name, "float") == 0)  return type_float;
        if (strcmp(name, "str") == 0)    return type_str;
        if (strcmp(name, "bool") == 0)   return type_bool;
        if (strcmp(name, "None") == 0)   return type_none;
        if (strcmp(name, "bytes") == 0)  return type_bytes;
        if (strcmp(name, "range") == 0)  return type_range;
        if (strcmp(name, "Any") == 0)    return type_any;
        if (strcmp(name, "object") == 0) return type_any;
        if (strcmp(name, "list") == 0)   return type_new_list(type_any);
        if (strcmp(name, "dict") == 0)   return type_new_dict(type_any, type_any);
        if (strcmp(name, "tuple") == 0)  return type_new(TY_TUPLE, "tuple");
        if (strcmp(name, "set") == 0)    return type_new_set(type_any);
        if (strcmp(name, "frozenset") == 0) return type_frozenset;
        if (strcmp(name, "complex") == 0) return type_complex;
        if (strcmp(name, "bytearray") == 0) return type_bytearray;

        {
            Symbol *sym = resolve(name);
            if (sym) return sym->type;
        }

        error_fmt(type_node, "unknown type '%s'", name);
        return type_error;
    }

    case AST_TYPE_GENERIC: {
        const char *base_name = type_node->data.type_generic.gname;
        ASTNode *arg;
        TypeInfo *resolved_args = 0;
        TypeInfo *prev_arg = 0;
        int arg_count = 0;

        for (arg = type_node->data.type_generic.type_args; arg; arg = arg->next) {
            TypeInfo *ra = resolve_type(arg);
            TypeInfo *racopy = type_copy(ra);
            if (racopy) {
                racopy->next = 0;
                if (prev_arg) prev_arg->next = racopy;
                else resolved_args = racopy;
                prev_arg = racopy;
            }
            arg_count++;
        }

        if (!base_name) return type_error;

        if (strcmp(base_name, "list") == 0 || strcmp(base_name, "List") == 0) {
            return type_new_list(resolved_args ? resolved_args : type_any);
        }
        if (strcmp(base_name, "dict") == 0 || strcmp(base_name, "Dict") == 0) {
            TypeInfo *key = resolved_args;
            TypeInfo *val = resolved_args ? resolved_args->next : 0;
            return type_new_dict(key ? key : type_any, val ? val : type_any);
        }
        if (strcmp(base_name, "tuple") == 0 || strcmp(base_name, "Tuple") == 0) {
            return type_new_tuple(resolved_args, arg_count);
        }
        if (strcmp(base_name, "set") == 0 || strcmp(base_name, "Set") == 0) {
            return type_new_set(resolved_args ? resolved_args : type_any);
        }
        if (strcmp(base_name, "Optional") == 0) {
            return type_new_optional(resolved_args ? resolved_args : type_any);
        }

        /* Generic class */
        {
            Symbol *sym = resolve(base_name);
            if (sym && sym->type) {
                return type_new_generic_inst(sym->type, resolved_args, arg_count);
            }
        }

        error_fmt(type_node, "unknown generic type '%s'", base_name);
        return type_error;
    }

    case AST_TYPE_UNION: {
        TypeInfo *member_types = 0;
        TypeInfo *prev_mt = 0;
        ASTNode *child;

        for (child = type_node->data.type_union.types; child; child = child->next) {
            TypeInfo *mt = resolve_type(child);
            TypeInfo *mtcopy = type_copy(mt);
            if (mtcopy) {
                mtcopy->next = 0;
                if (prev_mt) prev_mt->next = mtcopy;
                else member_types = mtcopy;
                prev_mt = mtcopy;
            }
        }
        return type_new_union(member_types);
    }

    case AST_TYPE_OPTIONAL: {
        /* type_optional reuses type_union.types for the inner type */
        TypeInfo *inner = type_any;
        if (type_node->data.type_union.types) {
            inner = resolve_type(type_node->data.type_union.types);
        }
        return type_new_optional(inner);
    }

    case AST_SUBSCRIPT: {
        /* list[int] parsed as subscript */
        ASTNode *base_node = type_node->data.subscript.object;
        ASTNode *idx_node = type_node->data.subscript.index;
        const char *bname = 0;

        if (base_node) {
            if (base_node->kind == AST_NAME) bname = base_node->data.name.id;
            else if (base_node->kind == AST_TYPE_NAME) bname = base_node->data.type_name.tname;
        }

        if (bname) {
            TypeInfo *arg_types = 0;
            TypeInfo *prev_a = 0;
            int nargs = 0;

            if (idx_node) {
                if (idx_node->kind == AST_TUPLE_EXPR) {
                    ASTNode *a;
                    for (a = idx_node->data.collection.elts; a; a = a->next) {
                        TypeInfo *at = resolve_type(a);
                        TypeInfo *atcopy = type_copy(at);
                        if (atcopy) {
                            atcopy->next = 0;
                            if (prev_a) prev_a->next = atcopy;
                            else arg_types = atcopy;
                            prev_a = atcopy;
                        }
                        nargs++;
                    }
                } else {
                    arg_types = resolve_type(idx_node);
                    if (arg_types) arg_types->next = 0;
                    nargs = 1;
                }
            }

            if (strcmp(bname, "list") == 0 || strcmp(bname, "List") == 0) {
                return type_new_list(arg_types ? arg_types : type_any);
            }
            if (strcmp(bname, "dict") == 0 || strcmp(bname, "Dict") == 0) {
                TypeInfo *k = arg_types;
                TypeInfo *v = arg_types ? arg_types->next : 0;
                return type_new_dict(k ? k : type_any, v ? v : type_any);
            }
            if (strcmp(bname, "tuple") == 0 || strcmp(bname, "Tuple") == 0) {
                return type_new_tuple(arg_types, nargs);
            }
            if (strcmp(bname, "set") == 0 || strcmp(bname, "Set") == 0) {
                return type_new_set(arg_types ? arg_types : type_any);
            }
            if (strcmp(bname, "Optional") == 0) {
                return type_new_optional(arg_types ? arg_types : type_any);
            }
            /* Generic class */
            {
                Symbol *sym = resolve(bname);
                if (sym && sym->type) {
                    return type_new_generic_inst(sym->type, arg_types, nargs);
                }
            }
        }
        return type_any;
    }

    case AST_ATTR: {
        const char *a = type_node->data.attribute.attr;
        if (a) {
            if (strcmp(a, "Optional") == 0) return type_new_optional(type_any);
            if (strcmp(a, "List") == 0)     return type_new_list(type_any);
            if (strcmp(a, "Dict") == 0)     return type_new_dict(type_any, type_any);
            if (strcmp(a, "Tuple") == 0)    return type_new(TY_TUPLE, "tuple");
            if (strcmp(a, "Set") == 0)      return type_new_set(type_any);
            if (strcmp(a, "Any") == 0)      return type_any;
        }
        return type_any;
    }

    case AST_BINOP: {
        /* int | str union syntax */
        if (type_node->data.binop.op == OP_BITOR) {
            TypeInfo *left = resolve_type(type_node->data.binop.left);
            TypeInfo *right = resolve_type(type_node->data.binop.right);
            TypeInfo *lcopy = type_copy(left);
            TypeInfo *rcopy = type_copy(right);
            if (lcopy && rcopy) {
                lcopy->next = rcopy;
                rcopy->next = 0;
                return type_new_union(lcopy);
            }
        }
        return type_any;
    }

    case AST_NONE_LIT:
        return type_none;

    default:
        return type_any;
    }
}
