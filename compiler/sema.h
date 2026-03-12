/*
 * sema.h - Semantic analysis for PyDOS Python-to-8086 compiler
 *
 * Performs scope analysis, name resolution, type checking, and
 * type inference on the AST produced by the parser. Populates
 * a symbol table and annotates expression nodes with their types.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#ifndef SEMA_H
#define SEMA_H

#include "ast.h"
#include "types.h"
#include "modscan.h"
#include "stdscan.h"

enum SymbolKind {
    SYM_VAR, SYM_FUNC, SYM_CLASS, SYM_PARAM, SYM_BUILTIN, SYM_MODULE
};

struct Symbol {
    const char *name;
    TypeInfo *type;
    SymbolKind kind;
    int scope_depth;
    int slot;           /* local variable index for codegen */
    int is_global;
    int is_nonlocal;
    int is_captured;    /* var is referenced by inner nonlocal */
    Symbol *next;       /* linked list in scope */
};

struct Scope {
    Scope *parent;
    Symbol *symbols;    /* linked list */
    int num_locals;
    int scope_depth;
    int is_function;    /* true if function scope (for return type checking) */
    int is_class;       /* true if class scope */
    TypeInfo *return_type; /* expected return type if function scope */
    const char *func_name; /* name of enclosing function */
    const char *class_name; /* name of enclosing class */

    /* Closure analysis (populated when processing nonlocal statements) */
    const char *cell_vars[32];   /* vars captured by inner functions */
    int num_cell_vars;
    const char *free_vars[32];   /* nonlocal vars from outer scope */
    int num_free_vars;
};

class SemanticAnalyzer {
public:
    SemanticAnalyzer();
    ~SemanticAnalyzer();

    int analyze(ASTNode *module);
    int get_error_count() const;

    /* Set stdlib registry for builtin/method lookup */
    void set_stdlib(StdlibRegistry *reg);

    /* Set module search paths for import scanning */
    void set_search_paths(const char **paths, int count);

    /* Lookup after analysis */
    Symbol *lookup(const char *name);
    TypeInfo *get_expr_type(ASTNode *expr);

private:
    Scope *current_scope;
    int error_count;
    int next_slot;

    /* Scope management */
    void push_scope(int is_function, int is_class);
    void pop_scope();
    Symbol *declare(const char *name, TypeInfo *type, SymbolKind kind);
    Symbol *resolve(const char *name);

    /* Register builtins */
    void register_builtins();

    /* Analysis methods */
    void analyze_stmt(ASTNode *node);
    void analyze_funcdef(ASTNode *node);
    void analyze_classdef(ASTNode *node);
    void analyze_if(ASTNode *node);
    void analyze_while(ASTNode *node);
    void analyze_for(ASTNode *node);
    void analyze_assign(ASTNode *node);
    void analyze_ann_assign(ASTNode *node);
    void analyze_aug_assign(ASTNode *node);
    void analyze_return(ASTNode *node);
    void analyze_try(ASTNode *node);
    void analyze_raise(ASTNode *node);
    void analyze_import(ASTNode *node);
    void analyze_expr_stmt(ASTNode *node);
    void analyze_type_alias(ASTNode *node);

    TypeInfo *analyze_expr(ASTNode *node);
    TypeInfo *analyze_binop(ASTNode *node);
    TypeInfo *analyze_unaryop(ASTNode *node);
    TypeInfo *analyze_compare(ASTNode *node);
    TypeInfo *analyze_boolop(ASTNode *node);
    TypeInfo *analyze_call(ASTNode *node);
    TypeInfo *analyze_attr(ASTNode *node);
    TypeInfo *analyze_subscript(ASTNode *node);
    TypeInfo *analyze_name(ASTNode *node);
    TypeInfo *analyze_listexpr(ASTNode *node);
    TypeInfo *analyze_dictexpr(ASTNode *node);
    TypeInfo *analyze_tupleexpr(ASTNode *node);
    TypeInfo *analyze_ifexpr(ASTNode *node);
    TypeInfo *analyze_lambda(ASTNode *node);

    /* Type annotation resolution */
    TypeInfo *resolve_type(ASTNode *type_node);

    /* Error reporting */
    void error(ASTNode *node, const char *msg);
    void error_fmt(ASTNode *node, const char *fmt, ...);
    void warn(ASTNode *node, const char *msg);

    /* Store expression types (simple array mapping node pointer to type) */
    struct ExprTypeEntry {
        ASTNode *node;
        TypeInfo *type;
    };
    ExprTypeEntry expr_types[4096];
    int num_expr_types;
    void set_expr_type(ASTNode *node, TypeInfo *type);

    /* Stdlib registry */
    StdlibRegistry *stdlib_reg_;

    /* Module search paths for import scanning */
    const char **search_paths;
    int num_search_paths;
    ModuleInfo *scanned_modules;   /* heap-allocated array (max 32) */
    int num_scanned;

    /* Lookup a scanned module (returns NULL if not found/scanned) */
    ModuleInfo *find_scanned_module(const char *module_name);
};

#endif /* SEMA_H */
