/*
 * mono.h - Monomorphization pass for PyDOS Python-to-8086 compiler
 *
 * Finds all uses of generic classes/functions with concrete type
 * arguments, creates specialized (monomorphized) copies of the
 * generic definitions with type parameters substituted, and
 * inserts them into the module for code generation.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#ifndef MONO_H
#define MONO_H

#include "ast.h"
#include "types.h"
#include "sema.h"

struct MonoEntry {
    const char *generic_name;
    TypeInfo *type_args;
    int num_type_args;
    ASTNode *specialized_ast;
    char mangled_name[128];
    MonoEntry *next;
};

class Monomorphizer {
public:
    Monomorphizer();
    ~Monomorphizer();

    void init(SemanticAnalyzer *sema);

    /* Process module: find all generic instantiations and create specializations */
    int process(ASTNode *module);

    /* Get specialized name for a generic use */
    const char *get_specialized_name(const char *generic_name,
                                     TypeInfo *args, int num_args);

    /* Get list of all specializations (for codegen) */
    MonoEntry *get_entries() const;

    int get_error_count() const;

private:
    SemanticAnalyzer *sema;
    MonoEntry *entries;
    int error_count;
    const char *rewrite_self_base;     /* generic class base name when inside specialized class */
    const char *rewrite_self_mangled;  /* mangled class name when inside specialized class */

    /* Find generic class/function ASTs */
    ASTNode *find_generic_def(ASTNode *module, const char *name);

    /* Deep copy AST with type substitution */
    ASTNode *specialize(ASTNode *generic_ast, const char **param_names,
                        TypeInfo **concrete_types, int num_params);
    ASTNode *copy_node(ASTNode *node, const char **param_names,
                       TypeInfo **concrete_types, int num_params);

    /* Mangle name: Stack + [int] -> Stack_int */
    void mangle_name(char *buf, int bufsize, const char *base,
                     TypeInfo *args, int num_args);

    /* Walk AST looking for generic usage */
    void scan_for_instantiations(ASTNode *node, ASTNode *module);

    /* Check if already specialized */
    MonoEntry *find_entry(const char *name, TypeInfo *args, int num_args);

    /* After scan: inject specialized ASTs and rewrite references */
    void inject_and_rewrite(ASTNode *module);

    /* Rewrite Subscript(GenericClass, type) -> Name(mangled) in-place */
    void rewrite_references(ASTNode *node);

    /* Rewrite bare generic constructor calls using annotation type */
    void rewrite_bare_constructors(ASTNode *node);

    /* Check if a name is a generic class that was specialized */
    int is_generic_class(const char *name);

    /* Resolve subscript/type_generic type args and find matching entry */
    MonoEntry *resolve_entry_from_subscript(const char *class_name,
                                             ASTNode *index_node);
    MonoEntry *resolve_entry_from_type_generic(const char *gname,
                                                ASTNode *type_args);
};

#endif /* MONO_H */
