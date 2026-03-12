/*
 * mono.cpp - Monomorphization implementation for PyDOS Python-to-8086 compiler
 *
 * Scans the AST for generic type instantiations (e.g. Stack[int]),
 * locates the original generic definition, deep-copies the AST with
 * type parameters replaced by concrete types, renames the copy to a
 * mangled name, and re-runs semantic analysis on the specialized copy.
 *
 * Uses the tagged-union AST structure from ast.h.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mono.h"

/* ------------------------------------------------------------------ */
/* Helper: duplicate a C string                                        */
/* ------------------------------------------------------------------ */
static char *mono_str_dup(const char *s)
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
/* Helper: safe string concatenation into buffer                       */
/* ------------------------------------------------------------------ */
static int buf_append(char *buf, int pos, int max, const char *s)
{
    while (*s && pos < max - 1) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    return pos;
}

/* ------------------------------------------------------------------ */
/* Helper: substitute a string if it matches a type parameter name.    */
/* Returns a dup of the substitution or a dup of the original.         */
/* ------------------------------------------------------------------ */
static const char *substitute_name(const char *name,
                                   const char **param_names,
                                   TypeInfo **concrete_types,
                                   int num_params)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < num_params; i++) {
        if (param_names[i] && strcmp(name, param_names[i]) == 0) {
            if (concrete_types[i] && concrete_types[i]->name) {
                return mono_str_dup(concrete_types[i]->name);
            }
        }
    }
    return mono_str_dup(name);
}

/* ------------------------------------------------------------------ */
/* Helper: deep-copy a linked list of ASTNode siblings                 */
/* ------------------------------------------------------------------ */
static ASTNode *copy_node_list(ASTNode *head,
                               const char **pn, TypeInfo **ct, int np,
                               ASTNode *(*copier)(ASTNode *, const char **,
                                                  TypeInfo **, int));

/* Forward declaration - copy_node is a member, but we need a static
 * trampoline for the list helper */
/* We'll implement the list copy inline instead. */

/* ------------------------------------------------------------------ */
/* Constructor / Destructor                                            */
/* ------------------------------------------------------------------ */

Monomorphizer::Monomorphizer()
{
    sema = 0;
    entries = 0;
    error_count = 0;
    rewrite_self_base = 0;
    rewrite_self_mangled = 0;
}

Monomorphizer::~Monomorphizer()
{
    MonoEntry *e, *next;
    e = entries;
    while (e) {
        next = e->next;
        free(e);
        e = next;
    }
    entries = 0;
}

/* ------------------------------------------------------------------ */
/* Public interface                                                     */
/* ------------------------------------------------------------------ */

void Monomorphizer::init(SemanticAnalyzer *s)
{
    sema = s;
}

/* Forward declaration - defined below scan_for_instantiations */
static TypeInfo *resolve_simple_type(ASTNode *node, SemanticAnalyzer *sa);

int Monomorphizer::process(ASTNode *module)
{
    if (!module) return 0;
    error_count = 0;
    scan_for_instantiations(module, module);

    /* Create default all-any specializations for generic classes that have
     * type params but were never explicitly instantiated with type args.
     * This handles bare calls like TypedDict() inside monomorphized code. */
    if (module->kind == AST_MODULE) {
        ASTNode *child;
        for (child = module->data.module.body; child; child = child->next) {
            if (child->kind == AST_CLASS_DEF &&
                child->data.class_def.name &&
                child->data.class_def.num_type_params > 0 &&
                !is_generic_class(child->data.class_def.name)) {
                /* This generic class has no specializations — create default */
                const char *gname = child->data.class_def.name;
                int ntp = child->data.class_def.num_type_params;
                const char *pnames[16];
                TypeInfo *ptypes[16];
                TypeInfo *args = 0;
                TypeInfo *prev_arg = 0;
                int i;

                if (ntp > 16) ntp = 16;
                for (i = 0; i < ntp; i++) {
                    if (child->data.class_def.type_param_names &&
                        i < child->data.class_def.num_type_params) {
                        pnames[i] = mono_str_dup(child->data.class_def.type_param_names[i]);
                    } else {
                        char tname[8];
                        sprintf(tname, "T%d", i);
                        pnames[i] = mono_str_dup(tname);
                    }
                    ptypes[i] = type_any;
                    /* Build args list for mangle_name */
                    {
                        TypeInfo *acopy = type_copy(type_any);
                        if (acopy) {
                            acopy->next = 0;
                            if (prev_arg) prev_arg->next = acopy;
                            else args = acopy;
                            prev_arg = acopy;
                        }
                    }
                }

                {
                    ASTNode *specialized = specialize(child, pnames, ptypes, ntp);
                    if (specialized) {
                        MonoEntry *entry = (MonoEntry *)malloc(sizeof(MonoEntry));
                        if (entry) {
                            memset(entry, 0, sizeof(MonoEntry));
                            entry->generic_name = mono_str_dup(gname);
                            entry->type_args = args;
                            entry->num_type_args = ntp;
                            entry->specialized_ast = specialized;
                            mangle_name(entry->mangled_name,
                                       (int)sizeof(entry->mangled_name),
                                       gname, args, ntp);
                            if (specialized->kind == AST_CLASS_DEF) {
                                specialized->data.class_def.name =
                                    mono_str_dup(entry->mangled_name);
                            }
                            entry->next = entries;
                            entries = entry;
                            if (sema) {
                                sema->analyze(specialized);
                            }
                        }
                    }
                }
            }
        }
    }

    /* After scanning, inject specialized definitions and rewrite references */
    if (entries && module->kind == AST_MODULE) {
        inject_and_rewrite(module);
    }

    return error_count;
}

/* ------------------------------------------------------------------ */
/* Check if a name is a generic class that was specialized             */
/* ------------------------------------------------------------------ */

int Monomorphizer::is_generic_class(const char *name)
{
    MonoEntry *e;
    if (!name) return 0;
    for (e = entries; e; e = e->next) {
        if (strcmp(e->generic_name, name) == 0) return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Resolve subscript type args and find matching MonoEntry             */
/* ------------------------------------------------------------------ */

MonoEntry *Monomorphizer::resolve_entry_from_subscript(const char *class_name,
                                                        ASTNode *index_node)
{
    TypeInfo *args = 0;
    TypeInfo *prev = 0;
    int nargs = 0;

    if (!index_node) return 0;

    if (index_node->kind == AST_TUPLE_EXPR) {
        ASTNode *elem;
        for (elem = index_node->data.collection.elts; elem; elem = elem->next) {
            TypeInfo *at = resolve_simple_type(elem, sema);
            TypeInfo *acopy = type_copy(at);
            if (acopy) {
                acopy->next = 0;
                if (prev) prev->next = acopy;
                else args = acopy;
                prev = acopy;
            }
            nargs++;
        }
    } else {
        args = type_copy(resolve_simple_type(index_node, sema));
        if (args) args->next = 0;
        nargs = 1;
    }

    return find_entry(class_name, args, nargs);
}

MonoEntry *Monomorphizer::resolve_entry_from_type_generic(const char *gname,
                                                           ASTNode *type_args)
{
    TypeInfo *args = 0;
    TypeInfo *prev = 0;
    int nargs = 0;
    ASTNode *arg;

    for (arg = type_args; arg; arg = arg->next) {
        TypeInfo *at = resolve_simple_type(arg, sema);
        TypeInfo *acopy = type_copy(at);
        if (acopy) {
            acopy->next = 0;
            if (prev) prev->next = acopy;
            else args = acopy;
            prev = acopy;
        }
        nargs++;
    }

    return find_entry(gname, args, nargs);
}

/* ------------------------------------------------------------------ */
/* Inject specialized ASTs into module and rewrite all references      */
/* ------------------------------------------------------------------ */

void Monomorphizer::inject_and_rewrite(ASTNode *module)
{
    MonoEntry *e;
    ASTNode *child;

    /* 1. Remove generic class definitions from module body.
     *    This includes both classes that have specializations (MonoEntries)
     *    AND classes declared with type parameters but never instantiated. */
    ASTNode **prev_ptr = &module->data.module.body;
    child = module->data.module.body;
    while (child) {
        if (child->kind == AST_CLASS_DEF && child->data.class_def.name &&
            (is_generic_class(child->data.class_def.name) ||
             child->data.class_def.num_type_params > 0)) {
            *prev_ptr = child->next;
            child = child->next;
        } else {
            prev_ptr = &child->next;
            child = child->next;
        }
    }

    /* 2. Prepend specialized class definitions to the module body */
    for (e = entries; e; e = e->next) {
        if (e->specialized_ast) {
            e->specialized_ast->next = module->data.module.body;
            module->data.module.body = e->specialized_ast;
        }
    }

    /* 3. Rewrite all references in the AST */
    rewrite_references(module);

    /* 4. Rewrite bare generic constructor calls using annotation type.
     *    e.g. `x: TypedDict_str_int = TypedDict()` → `TypedDict_str_int()` */
    rewrite_bare_constructors(module);
}

/* ------------------------------------------------------------------ */
/* Rewrite Subscript/TypeGeneric references to mangled names           */
/* ------------------------------------------------------------------ */

void Monomorphizer::rewrite_references(ASTNode *node)
{
    ASTNode *child;

    if (!node) return;

    /* Rewrite bare AST_NAME references to generic class names.
     * Handles self-referencing constructors inside monomorphized classes,
     * e.g. Queryable(data) inside Queryable_any -> Queryable_any(data) */
    if (node->kind == AST_NAME && node->data.name.id &&
        is_generic_class(node->data.name.id)) {

        const char *gname = node->data.name.id;

        /* Case 1: Self-reference inside current monomorphized class */
        if (rewrite_self_base && strcmp(gname, rewrite_self_base) == 0 &&
            rewrite_self_mangled) {
            node->data.name.id = mono_str_dup(rewrite_self_mangled);
            return;
        }

        /* Case 2: Reference to another generic class - find best match */
        {
            MonoEntry *match = 0;
            int count = 0;
            MonoEntry *any_match = 0;
            MonoEntry *e;
            for (e = entries; e; e = e->next) {
                if (e->generic_name && strcmp(e->generic_name, gname) == 0) {
                    match = e;
                    count++;
                    /* Check for _any suffix (default/fallback specialization) */
                    {
                        int glen = (int)strlen(gname);
                        if (strcmp(e->mangled_name + glen, "_any") == 0) {
                            any_match = e;
                        }
                    }
                }
            }
            if (count == 1 && match) {
                node->data.name.id = mono_str_dup(match->mangled_name);
                return;
            }
            if (any_match) {
                node->data.name.id = mono_str_dup(any_match->mangled_name);
                return;
            }
        }
    }

    /* Rewrite AST_SUBSCRIPT on a generic class → AST_NAME */
    if (node->kind == AST_SUBSCRIPT) {
        ASTNode *obj = node->data.subscript.object;
        if (obj && obj->kind == AST_NAME && obj->data.name.id &&
            is_generic_class(obj->data.name.id)) {
            MonoEntry *match = resolve_entry_from_subscript(
                obj->data.name.id, node->data.subscript.index);
            if (match) {
                node->kind = AST_NAME;
                node->data.name.id = mono_str_dup(match->mangled_name);
                return; /* Node rewritten, no children to recurse */
            }
        }
    }

    /* Rewrite AST_TYPE_GENERIC on a generic class → AST_TYPE_NAME */
    if (node->kind == AST_TYPE_GENERIC && node->data.type_generic.gname &&
        is_generic_class(node->data.type_generic.gname)) {
        MonoEntry *match = resolve_entry_from_type_generic(
            node->data.type_generic.gname, node->data.type_generic.type_args);
        if (match) {
            node->kind = AST_TYPE_NAME;
            node->data.type_name.tname = mono_str_dup(match->mangled_name);
            return; /* Node rewritten, no children to recurse */
        }
    }

    /* Recurse through all children (same structure as scan_for_instantiations) */
    switch (node->kind) {
    case AST_MODULE:
        for (child = node->data.module.body; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_FUNC_DEF:
        for (child = node->data.func_def.body; child; child = child->next)
            rewrite_references(child);
        {
            Param *p;
            for (p = node->data.func_def.params; p; p = p->next) {
                if (p->annotation) rewrite_references(p->annotation);
            }
        }
        if (node->data.func_def.return_type)
            rewrite_references(node->data.func_def.return_type);
        break;
    case AST_CLASS_DEF:
    {
        /* Track context for self-referencing rewrites */
        const char *prev_base = rewrite_self_base;
        const char *prev_mangled = rewrite_self_mangled;

        /* Check if this class is a monomorphized specialization */
        const char *cname = node->data.class_def.name;
        MonoEntry *ce;
        for (ce = entries; ce; ce = ce->next) {
            if (ce->specialized_ast && strcmp(ce->mangled_name, cname) == 0) {
                rewrite_self_base = ce->generic_name;
                rewrite_self_mangled = ce->mangled_name;
                break;
            }
        }

        for (child = node->data.class_def.body; child; child = child->next)
            rewrite_references(child);
        for (child = node->data.class_def.bases; child; child = child->next)
            rewrite_references(child);

        /* Restore context */
        rewrite_self_base = prev_base;
        rewrite_self_mangled = prev_mangled;
        break;
    }
    case AST_IF:
        if (node->data.if_stmt.condition)
            rewrite_references(node->data.if_stmt.condition);
        for (child = node->data.if_stmt.body; child; child = child->next)
            rewrite_references(child);
        for (child = node->data.if_stmt.else_body; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_WHILE:
        if (node->data.while_stmt.condition)
            rewrite_references(node->data.while_stmt.condition);
        for (child = node->data.while_stmt.body; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_FOR:
        if (node->data.for_stmt.iter)
            rewrite_references(node->data.for_stmt.iter);
        for (child = node->data.for_stmt.body; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_ASSIGN:
        if (node->data.assign.value)
            rewrite_references(node->data.assign.value);
        for (child = node->data.assign.targets; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_ANN_ASSIGN:
        if (node->data.ann_assign.annotation)
            rewrite_references(node->data.ann_assign.annotation);
        if (node->data.ann_assign.value)
            rewrite_references(node->data.ann_assign.value);
        if (node->data.ann_assign.target)
            rewrite_references(node->data.ann_assign.target);
        break;
    case AST_AUG_ASSIGN:
        if (node->data.aug_assign.value)
            rewrite_references(node->data.aug_assign.value);
        break;
    case AST_RETURN:
        if (node->data.ret.value)
            rewrite_references(node->data.ret.value);
        break;
    case AST_EXPR_STMT:
        if (node->data.expr_stmt.expr)
            rewrite_references(node->data.expr_stmt.expr);
        break;
    case AST_BINOP:
        rewrite_references(node->data.binop.left);
        rewrite_references(node->data.binop.right);
        break;
    case AST_UNARYOP:
        if (node->data.unaryop.operand)
            rewrite_references(node->data.unaryop.operand);
        break;
    case AST_COMPARE:
        if (node->data.compare.left)
            rewrite_references(node->data.compare.left);
        for (child = node->data.compare.comparators; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_BOOLOP:
        for (child = node->data.boolop.values; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_CALL:
        if (node->data.call.func)
            rewrite_references(node->data.call.func);
        for (child = node->data.call.args; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_ATTR:
        if (node->data.attribute.object)
            rewrite_references(node->data.attribute.object);
        break;
    case AST_SUBSCRIPT:
        if (node->data.subscript.object)
            rewrite_references(node->data.subscript.object);
        if (node->data.subscript.index)
            rewrite_references(node->data.subscript.index);
        break;
    case AST_LIST_EXPR:
    case AST_TUPLE_EXPR:
    case AST_SET_EXPR:
        for (child = node->data.collection.elts; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_DICT_EXPR:
        for (child = node->data.dict.keys; child; child = child->next)
            rewrite_references(child);
        for (child = node->data.dict.values; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_TYPE_GENERIC:
        for (child = node->data.type_generic.type_args; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_TYPE_UNION:
        for (child = node->data.type_union.types; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_TRY:
        for (child = node->data.try_stmt.body; child; child = child->next)
            rewrite_references(child);
        for (child = node->data.try_stmt.handlers; child; child = child->next)
            rewrite_references(child);
        for (child = node->data.try_stmt.else_body; child; child = child->next)
            rewrite_references(child);
        for (child = node->data.try_stmt.finally_body; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_EXCEPT_HANDLER:
        for (child = node->data.handler.body; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_RAISE:
        if (node->data.raise_stmt.exc)
            rewrite_references(node->data.raise_stmt.exc);
        break;
    case AST_FSTRING:
        for (child = node->data.fstring.parts; child; child = child->next)
            rewrite_references(child);
        break;
    case AST_IFEXPR:
        if (node->data.ifexpr.body)
            rewrite_references(node->data.ifexpr.body);
        if (node->data.ifexpr.test)
            rewrite_references(node->data.ifexpr.test);
        if (node->data.ifexpr.else_body)
            rewrite_references(node->data.ifexpr.else_body);
        break;
    case AST_WALRUS:
        if (node->data.walrus.value)
            rewrite_references(node->data.walrus.value);
        break;
    case AST_STARRED:
        if (node->data.starred.value)
            rewrite_references(node->data.starred.value);
        break;
    case AST_TYPE_ALIAS:
        if (node->data.type_alias.value)
            rewrite_references(node->data.type_alias.value);
        break;
    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* Rewrite bare generic constructor calls using annotation type.       */
/* e.g. `x: TypedDict_str_int = TypedDict()` → `TypedDict_str_int()` */
/* ------------------------------------------------------------------ */

void Monomorphizer::rewrite_bare_constructors(ASTNode *node)
{
    ASTNode *child;
    if (!node) return;

    if (node->kind == AST_ANN_ASSIGN) {
        ASTNode *annot = node->data.ann_assign.annotation;
        ASTNode *value = node->data.ann_assign.value;
        /* Get the mangled name from the annotation — could be AST_NAME
         * (from subscript rewrite) or AST_TYPE_NAME (from type_generic rewrite) */
        const char *annot_name = 0;
        if (annot && annot->kind == AST_NAME)
            annot_name = annot->data.name.id;
        else if (annot && annot->kind == AST_TYPE_NAME)
            annot_name = annot->data.type_name.tname;

        if (annot_name && value &&
            value->kind == AST_CALL && value->data.call.func &&
            value->data.call.func->kind == AST_NAME) {
            const char *call_name = value->data.call.func->data.name.id;
            if (call_name) {
                int call_len = (int)strlen(call_name);
                /* Check: annotation starts with call_name followed by '_' */
                if (strncmp(annot_name, call_name, call_len) == 0 &&
                    annot_name[call_len] == '_') {
                    value->data.call.func->data.name.id =
                        mono_str_dup(annot_name);
                }
            }
        }
    }

    /* Recurse into statement bodies */
    switch (node->kind) {
    case AST_MODULE:
        for (child = node->data.module.body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_FUNC_DEF:
        for (child = node->data.func_def.body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_CLASS_DEF:
        for (child = node->data.class_def.body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_IF:
        for (child = node->data.if_stmt.body; child; child = child->next)
            rewrite_bare_constructors(child);
        for (child = node->data.if_stmt.else_body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_WHILE:
        for (child = node->data.while_stmt.body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_FOR:
        for (child = node->data.for_stmt.body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_TRY:
        for (child = node->data.try_stmt.body; child; child = child->next)
            rewrite_bare_constructors(child);
        for (child = node->data.try_stmt.handlers; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    case AST_EXCEPT_HANDLER:
        for (child = node->data.handler.body; child; child = child->next)
            rewrite_bare_constructors(child);
        break;
    default:
        break;
    }
}

const char *Monomorphizer::get_specialized_name(const char *generic_name,
                                                 TypeInfo *args, int num_args)
{
    MonoEntry *e = find_entry(generic_name, args, num_args);
    if (e) return e->mangled_name;
    return 0;
}

MonoEntry *Monomorphizer::get_entries() const
{
    return entries;
}

int Monomorphizer::get_error_count() const
{
    return error_count;
}

/* ------------------------------------------------------------------ */
/* Name mangling                                                       */
/* ------------------------------------------------------------------ */

static void type_mangle_append(TypeInfo *t, char *buf, int *pos, int max)
{
    if (!t) {
        *pos = buf_append(buf, *pos, max, "any");
        return;
    }

    switch (t->kind) {
    case TY_INT:   *pos = buf_append(buf, *pos, max, "int");   break;
    case TY_FLOAT: *pos = buf_append(buf, *pos, max, "float"); break;
    case TY_BOOL:  *pos = buf_append(buf, *pos, max, "bool");  break;
    case TY_STR:   *pos = buf_append(buf, *pos, max, "str");   break;
    case TY_NONE:  *pos = buf_append(buf, *pos, max, "None");  break;
    case TY_BYTES: *pos = buf_append(buf, *pos, max, "bytes"); break;
    case TY_RANGE: *pos = buf_append(buf, *pos, max, "range"); break;
    case TY_ANY:   *pos = buf_append(buf, *pos, max, "any");   break;
    case TY_ERROR: *pos = buf_append(buf, *pos, max, "err");   break;

    case TY_LIST:
        *pos = buf_append(buf, *pos, max, "list_");
        if (t->type_args) type_mangle_append(t->type_args, buf, pos, max);
        else *pos = buf_append(buf, *pos, max, "any");
        break;

    case TY_DICT:
        *pos = buf_append(buf, *pos, max, "dict_");
        if (t->type_args) {
            type_mangle_append(t->type_args, buf, pos, max);
            *pos = buf_append(buf, *pos, max, "_");
            if (t->type_args->next)
                type_mangle_append(t->type_args->next, buf, pos, max);
            else
                *pos = buf_append(buf, *pos, max, "any");
        } else {
            *pos = buf_append(buf, *pos, max, "any_any");
        }
        break;

    case TY_TUPLE: {
        TypeInfo *elem;
        *pos = buf_append(buf, *pos, max, "tuple");
        for (elem = t->type_args; elem; elem = elem->next) {
            *pos = buf_append(buf, *pos, max, "_");
            type_mangle_append(elem, buf, pos, max);
        }
        break;
    }

    case TY_SET:
        *pos = buf_append(buf, *pos, max, "set_");
        if (t->type_args) type_mangle_append(t->type_args, buf, pos, max);
        else *pos = buf_append(buf, *pos, max, "any");
        break;

    case TY_CLASS:
        if (t->name) *pos = buf_append(buf, *pos, max, t->name);
        else *pos = buf_append(buf, *pos, max, "cls");
        break;

    case TY_GENERIC_PARAM:
        if (t->name) *pos = buf_append(buf, *pos, max, t->name);
        else *pos = buf_append(buf, *pos, max, "T");
        break;

    case TY_GENERIC_INST: {
        TypeInfo *a;
        if (t->name) *pos = buf_append(buf, *pos, max, t->name);
        for (a = t->type_args; a; a = a->next) {
            *pos = buf_append(buf, *pos, max, "_");
            type_mangle_append(a, buf, pos, max);
        }
        break;
    }

    case TY_OPTIONAL:
        *pos = buf_append(buf, *pos, max, "opt_");
        if (t->type_args) type_mangle_append(t->type_args, buf, pos, max);
        else *pos = buf_append(buf, *pos, max, "any");
        break;

    case TY_UNION: {
        TypeInfo *m;
        *pos = buf_append(buf, *pos, max, "union");
        for (m = t->type_args; m; m = m->next) {
            *pos = buf_append(buf, *pos, max, "_");
            type_mangle_append(m, buf, pos, max);
        }
        break;
    }

    case TY_FUNC:
        *pos = buf_append(buf, *pos, max, "func");
        break;
    }
}

void Monomorphizer::mangle_name(char *buf, int bufsize, const char *base,
                                 TypeInfo *args, int num_args)
{
    int pos = 0;
    int i;
    TypeInfo *a;

    pos = buf_append(buf, pos, bufsize, base);

    a = args;
    for (i = 0; i < num_args && a; i++) {
        pos = buf_append(buf, pos, bufsize, "_");
        type_mangle_append(a, buf, &pos, bufsize);
        a = a->next;
    }
}

/* ------------------------------------------------------------------ */
/* Entry lookup                                                        */
/* ------------------------------------------------------------------ */

MonoEntry *Monomorphizer::find_entry(const char *name, TypeInfo *args,
                                      int num_args)
{
    MonoEntry *e;
    for (e = entries; e; e = e->next) {
        if (!e->generic_name || !name) continue;
        if (strcmp(e->generic_name, name) != 0) continue;
        if (e->num_type_args != num_args) continue;
        {
            TypeInfo *ea = e->type_args;
            TypeInfo *ga = args;
            int match = 1;
            int i;
            for (i = 0; i < num_args; i++) {
                if (!ea || !ga) { match = 0; break; }
                if (!type_equal(ea, ga)) { match = 0; break; }
                ea = ea->next;
                ga = ga->next;
            }
            if (match) return e;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Find generic definition in module                                   */
/* ------------------------------------------------------------------ */

ASTNode *Monomorphizer::find_generic_def(ASTNode *module, const char *name)
{
    ASTNode *child;

    if (!module || !name) return 0;

    /* Module body */
    child = 0;
    if (module->kind == AST_MODULE) {
        child = module->data.module.body;
    }

    for (; child; child = child->next) {
        if (child->kind == AST_CLASS_DEF && child->data.class_def.name &&
            strcmp(child->data.class_def.name, name) == 0) {
            return child;
        }
        if (child->kind == AST_FUNC_DEF && child->data.func_def.name &&
            strcmp(child->data.func_def.name, name) == 0) {
            return child;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Deep copy helper: copy a linked list of ASTNode siblings            */
/* ------------------------------------------------------------------ */

ASTNode *Monomorphizer::copy_node(ASTNode *node, const char **param_names,
                                   TypeInfo **concrete_types, int num_params)
{
    ASTNode *c;
    ASTNode *child, *cpy, *prev;

    if (!node) return 0;

    /* Use ast_alloc for arena-managed nodes */
    c = ast_alloc(node->kind, node->line, node->col);
    if (!c) return 0;

    /* Copy the entire data union verbatim first, then deep-copy pointers */
    memcpy(&c->data, &node->data, sizeof(node->data));

    /*
     * Now deep-copy all pointer fields within the union based on kind.
     * We also substitute type parameter names where appropriate.
     */
    switch (node->kind) {
    case AST_MODULE:
        c->data.module.body = 0;
        prev = 0;
        for (child = node->data.module.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.module.body = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_FUNC_DEF:
        c->data.func_def.name = substitute_name(node->data.func_def.name,
                                                  param_names, concrete_types,
                                                  num_params);
        /* Copy params */
        {
            Param *sp, *dp, *prev_p = 0;
            c->data.func_def.params = 0;
            for (sp = node->data.func_def.params; sp; sp = sp->next) {
                dp = param_alloc();
                if (!dp) break;
                dp->name = mono_str_dup(sp->name);
                dp->annotation = copy_node(sp->annotation, param_names,
                                            concrete_types, num_params);
                dp->default_val = copy_node(sp->default_val, param_names,
                                              concrete_types, num_params);
                dp->is_star = sp->is_star;
                dp->is_double_star = sp->is_double_star;
                dp->next = 0;
                if (prev_p) prev_p->next = dp;
                else c->data.func_def.params = dp;
                prev_p = dp;
            }
        }
        c->data.func_def.return_type = copy_node(node->data.func_def.return_type,
                                                    param_names, concrete_types,
                                                    num_params);
        /* Copy body */
        c->data.func_def.body = 0;
        prev = 0;
        for (child = node->data.func_def.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.func_def.body = cpy;
                prev = cpy;
            }
        }
        /* Copy decorators */
        c->data.func_def.decorators = 0;
        prev = 0;
        for (child = node->data.func_def.decorators; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.func_def.decorators = cpy;
                prev = cpy;
            }
        }
        /* Copy func type params */
        c->data.func_def.type_param_names = node->data.func_def.type_param_names;
        c->data.func_def.num_type_params = node->data.func_def.num_type_params;
        break;

    case AST_CLASS_DEF:
        c->data.class_def.name = substitute_name(node->data.class_def.name,
                                                    param_names, concrete_types,
                                                    num_params);
        /* Copy bases */
        c->data.class_def.bases = 0;
        prev = 0;
        for (child = node->data.class_def.bases; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.class_def.bases = cpy;
                prev = cpy;
            }
        }
        /* Copy body */
        c->data.class_def.body = 0;
        prev = 0;
        for (child = node->data.class_def.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.class_def.body = cpy;
                prev = cpy;
            }
        }
        /* Copy decorators */
        c->data.class_def.decorators = 0;
        prev = 0;
        for (child = node->data.class_def.decorators; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.class_def.decorators = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_RETURN:
        c->data.ret.value = copy_node(node->data.ret.value, param_names,
                                        concrete_types, num_params);
        break;

    case AST_ASSIGN:
        /* Copy targets list */
        c->data.assign.targets = 0;
        prev = 0;
        for (child = node->data.assign.targets; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.assign.targets = cpy;
                prev = cpy;
            }
        }
        c->data.assign.value = copy_node(node->data.assign.value, param_names,
                                           concrete_types, num_params);
        break;

    case AST_ANN_ASSIGN:
        c->data.ann_assign.target = copy_node(node->data.ann_assign.target,
                                                param_names, concrete_types,
                                                num_params);
        c->data.ann_assign.annotation = copy_node(node->data.ann_assign.annotation,
                                                     param_names, concrete_types,
                                                     num_params);
        c->data.ann_assign.value = copy_node(node->data.ann_assign.value,
                                               param_names, concrete_types,
                                               num_params);
        break;

    case AST_AUG_ASSIGN:
        c->data.aug_assign.target = copy_node(node->data.aug_assign.target,
                                                param_names, concrete_types,
                                                num_params);
        c->data.aug_assign.value = copy_node(node->data.aug_assign.value,
                                               param_names, concrete_types,
                                               num_params);
        break;

    case AST_IF:
        c->data.if_stmt.condition = copy_node(node->data.if_stmt.condition,
                                                param_names, concrete_types,
                                                num_params);
        /* Copy body */
        c->data.if_stmt.body = 0;
        prev = 0;
        for (child = node->data.if_stmt.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.if_stmt.body = cpy;
                prev = cpy;
            }
        }
        /* Copy else_body */
        c->data.if_stmt.else_body = 0;
        prev = 0;
        for (child = node->data.if_stmt.else_body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.if_stmt.else_body = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_WHILE:
        c->data.while_stmt.condition = copy_node(node->data.while_stmt.condition,
                                                    param_names, concrete_types,
                                                    num_params);
        c->data.while_stmt.body = 0;
        prev = 0;
        for (child = node->data.while_stmt.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.while_stmt.body = cpy;
                prev = cpy;
            }
        }
        c->data.while_stmt.else_body = 0;
        prev = 0;
        for (child = node->data.while_stmt.else_body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.while_stmt.else_body = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_FOR:
        c->data.for_stmt.target = copy_node(node->data.for_stmt.target,
                                              param_names, concrete_types,
                                              num_params);
        c->data.for_stmt.iter = copy_node(node->data.for_stmt.iter,
                                            param_names, concrete_types,
                                            num_params);
        c->data.for_stmt.body = 0;
        prev = 0;
        for (child = node->data.for_stmt.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.for_stmt.body = cpy;
                prev = cpy;
            }
        }
        c->data.for_stmt.else_body = 0;
        prev = 0;
        for (child = node->data.for_stmt.else_body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.for_stmt.else_body = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_EXPR_STMT:
        c->data.expr_stmt.expr = copy_node(node->data.expr_stmt.expr,
                                             param_names, concrete_types,
                                             num_params);
        break;

    case AST_TRY:
        c->data.try_stmt.body = 0;
        prev = 0;
        for (child = node->data.try_stmt.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.try_stmt.body = cpy;
                prev = cpy;
            }
        }
        c->data.try_stmt.handlers = 0;
        prev = 0;
        for (child = node->data.try_stmt.handlers; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.try_stmt.handlers = cpy;
                prev = cpy;
            }
        }
        c->data.try_stmt.else_body = 0;
        prev = 0;
        for (child = node->data.try_stmt.else_body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.try_stmt.else_body = cpy;
                prev = cpy;
            }
        }
        c->data.try_stmt.finally_body = 0;
        prev = 0;
        for (child = node->data.try_stmt.finally_body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.try_stmt.finally_body = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_EXCEPT_HANDLER:
        c->data.handler.type = copy_node(node->data.handler.type, param_names,
                                           concrete_types, num_params);
        c->data.handler.name = mono_str_dup(node->data.handler.name);
        c->data.handler.body = 0;
        prev = 0;
        for (child = node->data.handler.body; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.handler.body = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_RAISE:
        c->data.raise_stmt.exc = copy_node(node->data.raise_stmt.exc, param_names,
                                              concrete_types, num_params);
        c->data.raise_stmt.cause = copy_node(node->data.raise_stmt.cause,
                                               param_names, concrete_types,
                                               num_params);
        break;

    case AST_BINOP:
        c->data.binop.left = copy_node(node->data.binop.left, param_names,
                                         concrete_types, num_params);
        c->data.binop.right = copy_node(node->data.binop.right, param_names,
                                          concrete_types, num_params);
        break;

    case AST_UNARYOP:
        c->data.unaryop.operand = copy_node(node->data.unaryop.operand,
                                              param_names, concrete_types,
                                              num_params);
        break;

    case AST_COMPARE:
        c->data.compare.left = copy_node(node->data.compare.left, param_names,
                                           concrete_types, num_params);
        /* Copy ops array */
        if (node->data.compare.ops && node->data.compare.num_ops > 0) {
            c->data.compare.ops = cmpop_alloc(node->data.compare.num_ops);
            if (c->data.compare.ops) {
                memcpy(c->data.compare.ops, node->data.compare.ops,
                       sizeof(CmpOp) * node->data.compare.num_ops);
            }
        }
        /* Copy comparators list */
        c->data.compare.comparators = 0;
        prev = 0;
        for (child = node->data.compare.comparators; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.compare.comparators = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_BOOLOP:
        c->data.boolop.values = 0;
        prev = 0;
        for (child = node->data.boolop.values; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.boolop.values = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_CALL:
        c->data.call.func = copy_node(node->data.call.func, param_names,
                                        concrete_types, num_params);
        c->data.call.args = 0;
        prev = 0;
        for (child = node->data.call.args; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.call.args = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_ATTR:
        c->data.attribute.object = copy_node(node->data.attribute.object,
                                               param_names, concrete_types,
                                               num_params);
        c->data.attribute.attr = mono_str_dup(node->data.attribute.attr);
        break;

    case AST_SUBSCRIPT:
        c->data.subscript.object = copy_node(node->data.subscript.object,
                                               param_names, concrete_types,
                                               num_params);
        c->data.subscript.index = copy_node(node->data.subscript.index,
                                              param_names, concrete_types,
                                              num_params);
        break;

    case AST_NAME:
        c->data.name.id = substitute_name(node->data.name.id, param_names,
                                            concrete_types, num_params);
        break;

    case AST_STR_LIT:
        c->data.str_lit.value = mono_str_dup(node->data.str_lit.value);
        break;

    case AST_LIST_EXPR:
    case AST_TUPLE_EXPR:
    case AST_SET_EXPR:
        c->data.collection.elts = 0;
        prev = 0;
        for (child = node->data.collection.elts; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.collection.elts = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_DICT_EXPR:
        c->data.dict.keys = 0;
        prev = 0;
        for (child = node->data.dict.keys; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.dict.keys = cpy;
                prev = cpy;
            }
        }
        c->data.dict.values = 0;
        prev = 0;
        for (child = node->data.dict.values; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.dict.values = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_LAMBDA:
        {
            Param *sp, *dp, *prev_p = 0;
            c->data.lambda.params = 0;
            for (sp = node->data.lambda.params; sp; sp = sp->next) {
                dp = param_alloc();
                if (!dp) break;
                dp->name = mono_str_dup(sp->name);
                dp->annotation = copy_node(sp->annotation, param_names,
                                             concrete_types, num_params);
                dp->default_val = copy_node(sp->default_val, param_names,
                                               concrete_types, num_params);
                dp->is_star = sp->is_star;
                dp->is_double_star = sp->is_double_star;
                dp->next = 0;
                if (prev_p) prev_p->next = dp;
                else c->data.lambda.params = dp;
                prev_p = dp;
            }
        }
        c->data.lambda.body = copy_node(node->data.lambda.body, param_names,
                                          concrete_types, num_params);
        break;

    case AST_IFEXPR:
        c->data.ifexpr.body = copy_node(node->data.ifexpr.body, param_names,
                                          concrete_types, num_params);
        c->data.ifexpr.test = copy_node(node->data.ifexpr.test, param_names,
                                          concrete_types, num_params);
        c->data.ifexpr.else_body = copy_node(node->data.ifexpr.else_body,
                                               param_names, concrete_types,
                                               num_params);
        break;

    case AST_WALRUS:
        c->data.walrus.target = copy_node(node->data.walrus.target, param_names,
                                            concrete_types, num_params);
        c->data.walrus.value = copy_node(node->data.walrus.value, param_names,
                                           concrete_types, num_params);
        break;

    case AST_STARRED:
        c->data.starred.value = copy_node(node->data.starred.value, param_names,
                                            concrete_types, num_params);
        break;

    case AST_FSTRING:
        c->data.fstring.parts = 0;
        prev = 0;
        for (child = node->data.fstring.parts; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.fstring.parts = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_TYPE_NAME:
        c->data.type_name.tname = substitute_name(node->data.type_name.tname,
                                                     param_names, concrete_types,
                                                     num_params);
        break;

    case AST_TYPE_GENERIC:
        c->data.type_generic.gname = substitute_name(
            node->data.type_generic.gname, param_names, concrete_types, num_params);
        c->data.type_generic.type_args = 0;
        prev = 0;
        for (child = node->data.type_generic.type_args; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.type_generic.type_args = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_TYPE_UNION:
        c->data.type_union.types = 0;
        prev = 0;
        for (child = node->data.type_union.types; child; child = child->next) {
            cpy = copy_node(child, param_names, concrete_types, num_params);
            if (cpy) {
                cpy->next = 0;
                if (prev) prev->next = cpy;
                else c->data.type_union.types = cpy;
                prev = cpy;
            }
        }
        break;

    case AST_SLICE:
        c->data.slice.lower = copy_node(node->data.slice.lower, param_names,
                                          concrete_types, num_params);
        c->data.slice.upper = copy_node(node->data.slice.upper, param_names,
                                          concrete_types, num_params);
        c->data.slice.step = copy_node(node->data.slice.step, param_names,
                                         concrete_types, num_params);
        break;

    case AST_KEYWORD_ARG:
        c->data.keyword_arg.key = mono_str_dup(node->data.keyword_arg.key);
        c->data.keyword_arg.kw_value = copy_node(node->data.keyword_arg.kw_value,
                                                    param_names, concrete_types,
                                                    num_params);
        break;

    case AST_YIELD_EXPR:
    case AST_YIELD_FROM_EXPR:
        c->data.yield_expr.value = copy_node(node->data.yield_expr.value,
                                               param_names, concrete_types,
                                               num_params);
        break;

    case AST_TYPE_ALIAS:
        c->data.type_alias.name = mono_str_dup(node->data.type_alias.name);
        c->data.type_alias.value = copy_node(node->data.type_alias.value,
                                               param_names, concrete_types,
                                               num_params);
        if (node->data.type_alias.num_type_params > 0) {
            int i;
            c->data.type_alias.type_param_names =
                (const char **)malloc(node->data.type_alias.num_type_params *
                                      sizeof(const char *));
            for (i = 0; i < node->data.type_alias.num_type_params; i++) {
                c->data.type_alias.type_param_names[i] =
                    mono_str_dup(node->data.type_alias.type_param_names[i]);
            }
        } else {
            c->data.type_alias.type_param_names = 0;
        }
        c->data.type_alias.num_type_params = node->data.type_alias.num_type_params;
        break;

    /* Leaf nodes that need no deep copy (scalars only) */
    case AST_INT_LIT:
    case AST_FLOAT_LIT:
    case AST_BOOL_LIT:
    case AST_NONE_LIT:
    case AST_PASS:
    case AST_BREAK:
    case AST_CONTINUE:
    case AST_YIELD_STMT:
        /* Nothing to deep copy */
        break;

    default:
        /* For any unhandled node kinds, the memcpy above preserves
         * all data. Pointers within may become stale but for kinds
         * not involved in generics this is acceptable. */
        break;
    }

    c->next = 0;
    return c;
}

ASTNode *Monomorphizer::specialize(ASTNode *generic_ast,
                                    const char **param_names,
                                    TypeInfo **concrete_types,
                                    int num_params)
{
    return copy_node(generic_ast, param_names, concrete_types, num_params);
}

/* ------------------------------------------------------------------ */
/* Helper: resolve a type argument AST node to a TypeInfo              */
/* ------------------------------------------------------------------ */
static TypeInfo *resolve_simple_type(ASTNode *node, SemanticAnalyzer *sa)
{
    const char *name = 0;

    if (!node) return type_any;

    if (node->kind == AST_TYPE_NAME) {
        name = node->data.type_name.tname;
    } else if (node->kind == AST_NAME) {
        name = node->data.name.id;
    }

    if (name) {
        if (strcmp(name, "int") == 0)    return type_int;
        if (strcmp(name, "float") == 0)  return type_float;
        if (strcmp(name, "str") == 0)    return type_str;
        if (strcmp(name, "bool") == 0)   return type_bool;
        if (strcmp(name, "None") == 0)   return type_none;
        if (strcmp(name, "bytes") == 0)  return type_bytes;
        if (strcmp(name, "dict") == 0)   return type_new_dict(type_any, type_any);
        if (strcmp(name, "list") == 0)   return type_new_list(type_any);
        if (strcmp(name, "tuple") == 0)  return type_new(TY_TUPLE, "tuple");
        if (strcmp(name, "set") == 0)    return type_new_set(type_any);
        if (strcmp(name, "object") == 0) return type_any;

        if (sa) {
            Symbol *sym = sa->lookup(name);
            if (sym) return sym->type;
        }
    }

    return type_any;
}

/* ------------------------------------------------------------------ */
/* Scan AST for generic instantiations                                 */
/* ------------------------------------------------------------------ */

void Monomorphizer::scan_for_instantiations(ASTNode *node, ASTNode *module)
{
    ASTNode *child;

    if (!node) return;

    /* Look for AST_TYPE_GENERIC nodes: explicit generic annotations */
    if (node->kind == AST_TYPE_GENERIC && node->data.type_generic.gname) {
        const char *generic_name = node->data.type_generic.gname;
        TypeInfo *args = 0;
        TypeInfo *prev_arg = 0;
        ASTNode *arg_node;
        int num_args = 0;

        for (arg_node = node->data.type_generic.type_args;
             arg_node; arg_node = arg_node->next) {
            TypeInfo *at = resolve_simple_type(arg_node, sema);
            TypeInfo *acopy = type_copy(at);
            if (acopy) {
                acopy->next = 0;
                if (prev_arg) prev_arg->next = acopy;
                else args = acopy;
                prev_arg = acopy;
            }
            num_args++;
        }

        if (!find_entry(generic_name, args, num_args)) {
            ASTNode *gen_def = find_generic_def(module, generic_name);
            if (gen_def) {
                /*
                 * Extract type parameter names. For now, we look for
                 * base class subscripts like Generic[T] in the class
                 * definition, or we synthesize names T0, T1, ...
                 */
                const char *pnames[16];
                TypeInfo *ptypes[16];
                int nparam = 0;

                /* Use actual type parameter names from AST if available,
                 * otherwise synthesize T0, T1, ... */
                {
                    TypeInfo *cur_arg = args;
                    int i;
                    int has_ast_params = (gen_def->kind == AST_CLASS_DEF &&
                                          gen_def->data.class_def.type_param_names &&
                                          gen_def->data.class_def.num_type_params > 0);
                    for (i = 0; i < num_args && i < 16; i++) {
                        if (has_ast_params && i < gen_def->data.class_def.num_type_params) {
                            pnames[i] = mono_str_dup(gen_def->data.class_def.type_param_names[i]);
                        } else {
                            char tname[8];
                            sprintf(tname, "T%d", i);
                            pnames[i] = mono_str_dup(tname);
                        }
                        ptypes[i] = cur_arg ? cur_arg : type_any;
                        if (cur_arg) cur_arg = cur_arg->next;
                        nparam++;
                    }
                }

                if (nparam > 0) {
                    ASTNode *specialized = specialize(gen_def, pnames,
                                                       ptypes, nparam);
                    if (specialized) {
                        MonoEntry *entry = (MonoEntry *)malloc(sizeof(MonoEntry));
                        if (entry) {
                            memset(entry, 0, sizeof(MonoEntry));
                            entry->generic_name = mono_str_dup(generic_name);
                            entry->type_args = args;
                            entry->num_type_args = num_args;
                            entry->specialized_ast = specialized;
                            mangle_name(entry->mangled_name,
                                       (int)sizeof(entry->mangled_name),
                                       generic_name, args, num_args);

                            /* Rename the specialized definition */
                            if (specialized->kind == AST_CLASS_DEF) {
                                specialized->data.class_def.name =
                                    mono_str_dup(entry->mangled_name);
                            } else if (specialized->kind == AST_FUNC_DEF) {
                                specialized->data.func_def.name =
                                    mono_str_dup(entry->mangled_name);
                            }

                            entry->next = entries;
                            entries = entry;

                            /* Re-run semantic analysis on specialized copy */
                            if (sema) {
                                sema->analyze(specialized);
                            }
                        }
                    }
                }
            }
        }
    }

    /* Also check AST_SUBSCRIPT on a class name (runtime generic usage) */
    if (node->kind == AST_SUBSCRIPT) {
        ASTNode *obj = node->data.subscript.object;
        if (obj && obj->kind == AST_NAME && obj->data.name.id) {
            Symbol *sym = sema ? sema->lookup(obj->data.name.id) : 0;
            if (sym && sym->kind == SYM_CLASS && sym->type &&
                sym->type->class_info && sym->type->class_info->is_generic) {
                ASTNode *idx = node->data.subscript.index;
                TypeInfo *args = 0;
                TypeInfo *prev_arg = 0;
                int num_args = 0;

                if (idx) {
                    if (idx->kind == AST_TUPLE_EXPR) {
                        ASTNode *elem;
                        for (elem = idx->data.collection.elts; elem;
                             elem = elem->next) {
                            TypeInfo *at = resolve_simple_type(elem, sema);
                            TypeInfo *acopy = type_copy(at);
                            if (acopy) {
                                acopy->next = 0;
                                if (prev_arg) prev_arg->next = acopy;
                                else args = acopy;
                                prev_arg = acopy;
                            }
                            num_args++;
                        }
                    } else {
                        args = type_copy(resolve_simple_type(idx, sema));
                        if (args) args->next = 0;
                        num_args = 1;
                    }
                }

                if (num_args > 0 &&
                    !find_entry(obj->data.name.id, args, num_args)) {
                    ASTNode *gen_def = find_generic_def(module,
                                                         obj->data.name.id);
                    if (gen_def) {
                        const char *pnames[16];
                        TypeInfo *ptypes[16];
                        int nparam = 0;
                        TypeInfo *cur_arg = args;
                        int i;
                        int has_ast_params = (gen_def->kind == AST_CLASS_DEF &&
                                              gen_def->data.class_def.type_param_names &&
                                              gen_def->data.class_def.num_type_params > 0);

                        for (i = 0; i < num_args && i < 16; i++) {
                            if (has_ast_params && i < gen_def->data.class_def.num_type_params) {
                                pnames[i] = mono_str_dup(gen_def->data.class_def.type_param_names[i]);
                            } else {
                                char tname[8];
                                sprintf(tname, "T%d", i);
                                pnames[i] = mono_str_dup(tname);
                            }
                            ptypes[i] = cur_arg ? cur_arg : type_any;
                            if (cur_arg) cur_arg = cur_arg->next;
                            nparam++;
                        }

                        if (nparam > 0) {
                            ASTNode *specialized = specialize(gen_def, pnames,
                                                               ptypes, nparam);
                            if (specialized) {
                                MonoEntry *entry =
                                    (MonoEntry *)malloc(sizeof(MonoEntry));
                                if (entry) {
                                    memset(entry, 0, sizeof(MonoEntry));
                                    entry->generic_name =
                                        mono_str_dup(obj->data.name.id);
                                    entry->type_args = args;
                                    entry->num_type_args = num_args;
                                    entry->specialized_ast = specialized;
                                    mangle_name(entry->mangled_name,
                                               (int)sizeof(entry->mangled_name),
                                               obj->data.name.id, args,
                                               num_args);

                                    if (specialized->kind == AST_CLASS_DEF) {
                                        specialized->data.class_def.name =
                                            mono_str_dup(entry->mangled_name);
                                    } else if (specialized->kind == AST_FUNC_DEF) {
                                        specialized->data.func_def.name =
                                            mono_str_dup(entry->mangled_name);
                                    }

                                    entry->next = entries;
                                    entries = entry;

                                    if (sema) {
                                        sema->analyze(specialized);
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    /* Recursively scan based on node kind */
    switch (node->kind) {
    case AST_MODULE:
        for (child = node->data.module.body; child; child = child->next) {
            scan_for_instantiations(child, module);
        }
        break;
    case AST_FUNC_DEF:
        for (child = node->data.func_def.body; child; child = child->next) {
            scan_for_instantiations(child, module);
        }
        /* Scan parameter annotations and return type */
        {
            Param *p;
            for (p = node->data.func_def.params; p; p = p->next) {
                if (p->annotation) scan_for_instantiations(p->annotation, module);
            }
        }
        if (node->data.func_def.return_type) {
            scan_for_instantiations(node->data.func_def.return_type, module);
        }
        break;
    case AST_CLASS_DEF:
        for (child = node->data.class_def.body; child; child = child->next) {
            scan_for_instantiations(child, module);
        }
        for (child = node->data.class_def.bases; child; child = child->next) {
            scan_for_instantiations(child, module);
        }
        break;
    case AST_IF:
        if (node->data.if_stmt.condition)
            scan_for_instantiations(node->data.if_stmt.condition, module);
        for (child = node->data.if_stmt.body; child; child = child->next)
            scan_for_instantiations(child, module);
        for (child = node->data.if_stmt.else_body; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_WHILE:
        if (node->data.while_stmt.condition)
            scan_for_instantiations(node->data.while_stmt.condition, module);
        for (child = node->data.while_stmt.body; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_FOR:
        if (node->data.for_stmt.iter)
            scan_for_instantiations(node->data.for_stmt.iter, module);
        for (child = node->data.for_stmt.body; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_ASSIGN:
        if (node->data.assign.value)
            scan_for_instantiations(node->data.assign.value, module);
        for (child = node->data.assign.targets; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_ANN_ASSIGN:
        if (node->data.ann_assign.annotation)
            scan_for_instantiations(node->data.ann_assign.annotation, module);
        if (node->data.ann_assign.value)
            scan_for_instantiations(node->data.ann_assign.value, module);
        break;
    case AST_AUG_ASSIGN:
        if (node->data.aug_assign.value)
            scan_for_instantiations(node->data.aug_assign.value, module);
        break;
    case AST_RETURN:
        if (node->data.ret.value)
            scan_for_instantiations(node->data.ret.value, module);
        break;
    case AST_EXPR_STMT:
        if (node->data.expr_stmt.expr)
            scan_for_instantiations(node->data.expr_stmt.expr, module);
        break;
    case AST_BINOP:
        scan_for_instantiations(node->data.binop.left, module);
        scan_for_instantiations(node->data.binop.right, module);
        break;
    case AST_UNARYOP:
        if (node->data.unaryop.operand)
            scan_for_instantiations(node->data.unaryop.operand, module);
        break;
    case AST_COMPARE:
        if (node->data.compare.left)
            scan_for_instantiations(node->data.compare.left, module);
        for (child = node->data.compare.comparators; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_BOOLOP:
        for (child = node->data.boolop.values; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_CALL:
        if (node->data.call.func)
            scan_for_instantiations(node->data.call.func, module);
        for (child = node->data.call.args; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_ATTR:
        if (node->data.attribute.object)
            scan_for_instantiations(node->data.attribute.object, module);
        break;
    case AST_SUBSCRIPT:
        if (node->data.subscript.object)
            scan_for_instantiations(node->data.subscript.object, module);
        if (node->data.subscript.index)
            scan_for_instantiations(node->data.subscript.index, module);
        break;
    case AST_LIST_EXPR:
    case AST_TUPLE_EXPR:
    case AST_SET_EXPR:
        for (child = node->data.collection.elts; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_DICT_EXPR:
        for (child = node->data.dict.keys; child; child = child->next)
            scan_for_instantiations(child, module);
        for (child = node->data.dict.values; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_TYPE_GENERIC:
        for (child = node->data.type_generic.type_args; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_TYPE_UNION:
        for (child = node->data.type_union.types; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_TRY:
        for (child = node->data.try_stmt.body; child; child = child->next)
            scan_for_instantiations(child, module);
        for (child = node->data.try_stmt.handlers; child; child = child->next)
            scan_for_instantiations(child, module);
        for (child = node->data.try_stmt.else_body; child; child = child->next)
            scan_for_instantiations(child, module);
        for (child = node->data.try_stmt.finally_body; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_EXCEPT_HANDLER:
        for (child = node->data.handler.body; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_RAISE:
        if (node->data.raise_stmt.exc)
            scan_for_instantiations(node->data.raise_stmt.exc, module);
        break;
    case AST_FSTRING:
        for (child = node->data.fstring.parts; child; child = child->next)
            scan_for_instantiations(child, module);
        break;
    case AST_IFEXPR:
        if (node->data.ifexpr.body)
            scan_for_instantiations(node->data.ifexpr.body, module);
        if (node->data.ifexpr.test)
            scan_for_instantiations(node->data.ifexpr.test, module);
        if (node->data.ifexpr.else_body)
            scan_for_instantiations(node->data.ifexpr.else_body, module);
        break;
    case AST_WALRUS:
        if (node->data.walrus.value)
            scan_for_instantiations(node->data.walrus.value, module);
        break;
    case AST_STARRED:
        if (node->data.starred.value)
            scan_for_instantiations(node->data.starred.value, module);
        break;
    case AST_TYPE_ALIAS:
        if (node->data.type_alias.value)
            scan_for_instantiations(node->data.type_alias.value, module);
        break;
    default:
        /* Leaf or unhandled node - no children to scan */
        break;
    }
}
