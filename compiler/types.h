/*
 * types.h - Type system for PyDOS Python-to-8086 compiler
 *
 * Provides TypeInfo structures for representing Python types
 * at compile time, including primitives, containers, generics,
 * classes, unions, and optionals.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#ifndef TYPES_H
#define TYPES_H

/* Forward declarations */
struct ASTNode;

enum TypeKind {
    TY_INT, TY_FLOAT, TY_BOOL, TY_STR, TY_NONE,
    TY_LIST, TY_DICT, TY_TUPLE, TY_SET,
    TY_CLASS, TY_FUNC, TY_GENERIC_PARAM, TY_GENERIC_INST,
    TY_UNION, TY_OPTIONAL, TY_ANY, TY_BYTES, TY_RANGE,
    TY_FROZENSET,
    TY_COMPLEX,
    TY_BYTEARRAY,
    TY_ERROR  /* sentinel for type errors */
};

struct ClassInfo {
    const char *name;
    struct ClassInfo *base;     /* single inheritance primary base */
    struct ClassInfo **bases;   /* all bases for MRO */
    int num_bases;
    struct Symbol *members;     /* linked list of class members */
    struct TypeInfo *type_params; /* generic params if generic class */
    int num_type_params;
    int vtable_size;            /* number of methods */
    int is_generic;
};

struct TypeInfo {
    TypeKind kind;
    const char *name;           /* "int", "MyClass", "T", etc. */
    TypeInfo *type_args;        /* linked list: for generics list[int] -> type_args has int */
    int num_type_args;
    TypeInfo *params;           /* for TY_FUNC: param types linked list */
    int num_params;
    TypeInfo *ret_type;         /* for TY_FUNC: return type */
    ClassInfo *class_info;      /* for TY_CLASS: pointer to class metadata */
    TypeInfo *next;             /* for linked lists of types */
};

/* Built-in type singletons */
extern TypeInfo *type_int;
extern TypeInfo *type_float;
extern TypeInfo *type_bool;
extern TypeInfo *type_str;
extern TypeInfo *type_none;
extern TypeInfo *type_any;
extern TypeInfo *type_error;
extern TypeInfo *type_range;
extern TypeInfo *type_bytes;
extern TypeInfo *type_frozenset;
extern TypeInfo *type_complex;
extern TypeInfo *type_bytearray;

/* Type creation */
TypeInfo *type_new(TypeKind kind, const char *name);
TypeInfo *type_new_list(TypeInfo *elem_type);
TypeInfo *type_new_dict(TypeInfo *key_type, TypeInfo *val_type);
TypeInfo *type_new_tuple(TypeInfo *elem_types, int count);
TypeInfo *type_new_set(TypeInfo *elem_type);
TypeInfo *type_new_func(TypeInfo *params, int num_params, TypeInfo *ret);
TypeInfo *type_new_class(ClassInfo *ci);
TypeInfo *type_new_generic_param(const char *name);
TypeInfo *type_new_generic_inst(TypeInfo *base, TypeInfo *args, int num_args);
TypeInfo *type_new_union(TypeInfo *types);
TypeInfo *type_new_optional(TypeInfo *inner);

/* Type queries */
int type_equal(TypeInfo *a, TypeInfo *b);
int type_compatible(TypeInfo *target, TypeInfo *source); /* can source be assigned to target? */
int type_is_numeric(TypeInfo *t);
int type_is_container(TypeInfo *t);
int type_is_iterable(TypeInfo *t);
TypeInfo *type_common(TypeInfo *a, TypeInfo *b); /* find common type for binop results */
const char *type_to_string(TypeInfo *t); /* human-readable string, for error messages */
TypeInfo *type_copy(TypeInfo *t);

/* Type substitution for generics */
TypeInfo *type_substitute(TypeInfo *t, const char **param_names,
                          TypeInfo **concrete_types, int count);

/* Init/shutdown */
void types_init();
void types_shutdown();

#endif /* TYPES_H */
