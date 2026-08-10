/*
 * types.cpp - Type system implementation for PyDOS Python-to-8086 compiler
 *
 * All TypeInfo nodes are allocated from a pool (linked list of malloc'd
 * blocks). Built-in singletons are created at init time. Supports
 * equality, compatibility checking, common type resolution, and
 * generic type substitution.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "types.h"

/* ------------------------------------------------------------------ */
/* Pool allocator for TypeInfo nodes                                    */
/* ------------------------------------------------------------------ */

#define POOL_BLOCK_SIZE 64

struct TypePool {
    TypeInfo nodes[POOL_BLOCK_SIZE];
    int used;
    TypePool *next;
};

static TypePool *pool_head = 0;
static TypePool *pool_current = 0;

/* String buffer for type_to_string results */
static char string_buf[512];

/* Interned string table for type names */
#define INTERN_TABLE_SIZE 256
static const char *intern_table[INTERN_TABLE_SIZE];
static int intern_count = 0;

/* ------------------------------------------------------------------ */
/* Built-in type singletons                                            */
/* ------------------------------------------------------------------ */

TypeInfo *type_int   = 0;
TypeInfo *type_float = 0;
TypeInfo *type_bool  = 0;
TypeInfo *type_str   = 0;
TypeInfo *type_none  = 0;
TypeInfo *type_any   = 0;
TypeInfo *type_error = 0;
TypeInfo *type_range = 0;
TypeInfo *type_bytes = 0;
TypeInfo *type_frozenset = 0;
TypeInfo *type_complex = 0;
TypeInfo *type_bytearray = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static const char *intern_string(const char *s)
{
    int i;
    if (!s) return 0;
    for (i = 0; i < intern_count; i++) {
        if (strcmp(intern_table[i], s) == 0) {
            return intern_table[i];
        }
    }
    if (intern_count < INTERN_TABLE_SIZE) {
        int len = strlen(s);
        char *copy = (char *)malloc(len + 1);
        if (!copy) return s;
        strcpy(copy, s);
        intern_table[intern_count] = copy;
        intern_count++;
        return copy;
    }
    /* Table full, return a copy without interning */
    {
        int len = strlen(s);
        char *copy = (char *)malloc(len + 1);
        if (!copy) return s;
        strcpy(copy, s);
        return copy;
    }
}

static TypeInfo *pool_alloc()
{
    TypeInfo *result;

    if (!pool_current || pool_current->used >= POOL_BLOCK_SIZE) {
        TypePool *blk = (TypePool *)malloc(sizeof(TypePool));
        if (!blk) {
            fprintf(stderr, "types: out of memory\n");
            return 0;
        }
        blk->used = 0;
        blk->next = 0;
        if (pool_current) {
            pool_current->next = blk;
        }
        if (!pool_head) {
            pool_head = blk;
        }
        pool_current = blk;
    }

    result = &pool_current->nodes[pool_current->used];
    pool_current->used++;
    memset(result, 0, sizeof(TypeInfo));
    return result;
}

/* ------------------------------------------------------------------ */
/* Type creation                                                       */
/* ------------------------------------------------------------------ */

TypeInfo *type_new(TypeKind kind, const char *name)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = kind;
    t->name = intern_string(name);
    return t;
}

TypeInfo *type_new_list(TypeInfo *elem_type)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = TY_LIST;
    t->name = intern_string("list");
    if (elem_type) {
        TypeInfo *arg = type_copy(elem_type);
        if (arg) {
            arg->next = 0;
            t->type_args = arg;
            t->num_type_args = 1;
        }
    }
    return t;
}

TypeInfo *type_new_dict(TypeInfo *key_type, TypeInfo *val_type)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = TY_DICT;
    t->name = intern_string("dict");
    if (key_type && val_type) {
        TypeInfo *karg = type_copy(key_type);
        TypeInfo *varg = type_copy(val_type);
        if (karg && varg) {
            karg->next = varg;
            varg->next = 0;
            t->type_args = karg;
            t->num_type_args = 2;
        }
    }
    return t;
}

TypeInfo *type_new_tuple(TypeInfo *elem_types, int count)
{
    TypeInfo *t = pool_alloc();
    TypeInfo *prev, *cur, *cpy;
    int i;
    if (!t) return 0;
    t->kind = TY_TUPLE;
    t->name = intern_string("tuple");
    t->num_type_args = count;

    prev = 0;
    cur = elem_types;
    for (i = 0; i < count && cur; i++) {
        cpy = type_copy(cur);
        if (!cpy) break;
        cpy->next = 0;
        if (prev) {
            prev->next = cpy;
        } else {
            t->type_args = cpy;
        }
        prev = cpy;
        cur = cur->next;
    }
    return t;
}

TypeInfo *type_new_set(TypeInfo *elem_type)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = TY_SET;
    t->name = intern_string("set");
    if (elem_type) {
        TypeInfo *arg = type_copy(elem_type);
        if (arg) {
            arg->next = 0;
            t->type_args = arg;
            t->num_type_args = 1;
        }
    }
    return t;
}

TypeInfo *type_new_func(TypeInfo *params, int num_params, TypeInfo *ret)
{
    TypeInfo *t = pool_alloc();
    TypeInfo *prev, *cur, *cpy;
    int i;
    if (!t) return 0;
    t->kind = TY_FUNC;
    t->name = intern_string("function");
    t->num_params = num_params;

    /* Copy parameter type list */
    prev = 0;
    cur = params;
    for (i = 0; i < num_params && cur; i++) {
        cpy = type_copy(cur);
        if (!cpy) break;
        cpy->next = 0;
        if (prev) {
            prev->next = cpy;
        } else {
            t->params = cpy;
        }
        prev = cpy;
        cur = cur->next;
    }

    /* Copy return type */
    if (ret) {
        t->ret_type = type_copy(ret);
    } else {
        t->ret_type = type_none;
    }
    return t;
}

TypeInfo *type_new_class(ClassInfo *ci)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = TY_CLASS;
    t->name = ci ? intern_string(ci->name) : intern_string("object");
    t->class_info = ci;
    return t;
}

TypeInfo *type_new_generic_param(const char *name)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = TY_GENERIC_PARAM;
    t->name = intern_string(name);
    return t;
}

TypeInfo *type_new_generic_inst(TypeInfo *base, TypeInfo *args, int num_args)
{
    TypeInfo *t = pool_alloc();
    TypeInfo *prev, *cur, *cpy, *next;
    int i;
    if (!t) return 0;
    t->kind = TY_GENERIC_INST;
    t->name = base ? base->name : intern_string("generic");
    t->num_type_args = num_args;

    /* Copy type argument list.
     * Save cur->next before type_copy in case copy aliases cur. */
    prev = 0;
    cur = args;
    for (i = 0; i < num_args && cur; i++) {
        next = cur->next;
        cpy = type_copy(cur);
        if (!cpy) break;
        cpy->next = 0;
        if (prev) {
            prev->next = cpy;
        } else {
            t->type_args = cpy;
        }
        prev = cpy;
        cur = next;
    }

    /* Carry forward class info if base is a class */
    if (base && base->class_info) {
        t->class_info = base->class_info;
    }
    return t;
}

TypeInfo *type_new_union(TypeInfo *types)
{
    TypeInfo *t = pool_alloc();
    TypeInfo *prev, *cur, *cpy, *next;
    int count;
    if (!t) return 0;
    t->kind = TY_UNION;
    t->name = intern_string("Union");

    /* Copy and count the union member types.
     * Save cur->next before type_copy in case copy aliases cur. */
    prev = 0;
    cur = types;
    count = 0;
    while (cur) {
        next = cur->next;
        cpy = type_copy(cur);
        if (!cpy) break;
        cpy->next = 0;
        if (prev) {
            prev->next = cpy;
        } else {
            t->type_args = cpy;
        }
        prev = cpy;
        cur = next;
        count++;
    }
    t->num_type_args = count;
    return t;
}

TypeInfo *type_new_optional(TypeInfo *inner)
{
    TypeInfo *t = pool_alloc();
    if (!t) return 0;
    t->kind = TY_OPTIONAL;
    t->name = intern_string("Optional");
    if (inner) {
        TypeInfo *arg = type_copy(inner);
        if (arg) {
            arg->next = 0;
            t->type_args = arg;
            t->num_type_args = 1;
        }
    }
    return t;
}

/* ------------------------------------------------------------------ */
/* Type copy                                                           */
/* ------------------------------------------------------------------ */

TypeInfo *type_copy(TypeInfo *t)
{
    TypeInfo *c;
    TypeInfo *prev, *cur, *cpy, *next;

    if (!t) return 0;

    /* Singletons: return a cheap shallow copy (one allocation, no recursion).
     * We must NOT return the singleton itself because callers often modify
     * ->next on the copy, which would corrupt the global singleton. */
    if (t == type_int || t == type_float || t == type_bool ||
        t == type_str || t == type_none || t == type_any ||
        t == type_error || t == type_range || t == type_bytes ||
        t == type_frozenset || t == type_complex ||
        t == type_bytearray) {
        c = pool_alloc();
        if (!c) return 0;
        c->kind = t->kind;
        c->name = t->name;
        c->next = 0;
        return c;
    }

    c = pool_alloc();
    if (!c) return 0;
    c->kind = t->kind;
    c->name = t->name; /* already interned */
    c->num_type_args = t->num_type_args;
    c->num_params = t->num_params;
    c->class_info = t->class_info;
    c->next = 0;

    /* Deep copy type_args linked list.
     * Save cur->next before type_copy/cpy->next=0 in case cpy aliases cur. */
    prev = 0;
    cur = t->type_args;
    while (cur) {
        next = cur->next;
        cpy = type_copy(cur);
        if (!cpy) break;
        cpy->next = 0;
        if (prev) {
            prev->next = cpy;
        } else {
            c->type_args = cpy;
        }
        prev = cpy;
        cur = next;
    }

    /* Deep copy params linked list (for TY_FUNC) */
    prev = 0;
    cur = t->params;
    while (cur) {
        next = cur->next;
        cpy = type_copy(cur);
        if (!cpy) break;
        cpy->next = 0;
        if (prev) {
            prev->next = cpy;
        } else {
            c->params = cpy;
        }
        prev = cpy;
        cur = next;
    }

    /* Copy return type */
    if (t->ret_type) {
        c->ret_type = type_copy(t->ret_type);
    }

    return c;
}

/* ------------------------------------------------------------------ */
/* Type equality                                                       */
/* ------------------------------------------------------------------ */

int type_equal(TypeInfo *a, TypeInfo *b)
{
    if (a == b) return 1;
    if (!a || !b) return 0;
    if (a->kind != b->kind) return 0;

    switch (a->kind) {
    case TY_INT:
    case TY_FLOAT:
    case TY_BOOL:
    case TY_STR:
    case TY_NONE:
    case TY_ANY:
    case TY_BYTES:
    case TY_RANGE:
    case TY_FROZENSET:
    case TY_COMPLEX:
    case TY_BYTEARRAY:
    case TY_ERROR:
        return 1; /* same kind is sufficient for primitives */

    case TY_LIST:
    case TY_SET:
        /* Compare element type */
        if (a->num_type_args != b->num_type_args) return 0;
        if (a->num_type_args == 0) return 1;
        return type_equal(a->type_args, b->type_args);

    case TY_DICT:
        if (a->num_type_args != b->num_type_args) return 0;
        if (a->num_type_args < 2) return 1;
        return type_equal(a->type_args, b->type_args) &&
               type_equal(a->type_args->next, b->type_args->next);

    case TY_TUPLE: {
        TypeInfo *ca, *cb;
        if (a->num_type_args != b->num_type_args) return 0;
        ca = a->type_args;
        cb = b->type_args;
        while (ca && cb) {
            if (!type_equal(ca, cb)) return 0;
            ca = ca->next;
            cb = cb->next;
        }
        return (ca == 0 && cb == 0) ? 1 : 0;
    }

    case TY_FUNC: {
        TypeInfo *pa, *pb;
        if (a->num_params != b->num_params) return 0;
        if (!type_equal(a->ret_type, b->ret_type)) return 0;
        pa = a->params;
        pb = b->params;
        while (pa && pb) {
            if (!type_equal(pa, pb)) return 0;
            pa = pa->next;
            pb = pb->next;
        }
        return (pa == 0 && pb == 0) ? 1 : 0;
    }

    case TY_CLASS:
        return a->class_info == b->class_info;

    case TY_GENERIC_PARAM:
        if (a->name && b->name) return strcmp(a->name, b->name) == 0;
        return 0;

    case TY_GENERIC_INST: {
        TypeInfo *aa, *ba;
        if (!a->name || !b->name || strcmp(a->name, b->name) != 0) return 0;
        if (a->num_type_args != b->num_type_args) return 0;
        aa = a->type_args;
        ba = b->type_args;
        while (aa && ba) {
            if (!type_equal(aa, ba)) return 0;
            aa = aa->next;
            ba = ba->next;
        }
        return (aa == 0 && ba == 0) ? 1 : 0;
    }

    case TY_UNION: {
        /*
         * Unions are equal if they contain the same set of types.
         * Simple approach: check that every type in a is in b and
         * vice versa, and counts match.
         */
        TypeInfo *ua, *ub;
        int found;
        if (a->num_type_args != b->num_type_args) return 0;
        for (ua = a->type_args; ua; ua = ua->next) {
            found = 0;
            for (ub = b->type_args; ub; ub = ub->next) {
                if (type_equal(ua, ub)) { found = 1; break; }
            }
            if (!found) return 0;
        }
        return 1;
    }

    case TY_OPTIONAL:
        if (a->num_type_args != b->num_type_args) return 0;
        if (a->num_type_args == 0) return 1;
        return type_equal(a->type_args, b->type_args);
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Type compatibility (can source be assigned to target?)              */
/* ------------------------------------------------------------------ */

static int is_subclass_of(ClassInfo *derived, ClassInfo *base)
{
    int i;
    if (!derived || !base) return 0;
    if (derived == base) return 1;
    /* Check primary base */
    if (derived->base && is_subclass_of(derived->base, base)) return 1;
    /* Check all bases for MRO */
    for (i = 0; i < derived->num_bases; i++) {
        if (derived->bases[i] && is_subclass_of(derived->bases[i], base)) return 1;
    }
    return 0;
}

int type_compatible(TypeInfo *target, TypeInfo *source)
{
    TypeInfo *member;

    if (!target || !source) return 0;

    /* Equal types are always compatible */
    if (type_equal(target, source)) return 1;

    /* Any matches everything */
    if (target->kind == TY_ANY || source->kind == TY_ANY) return 1;

    /* Error type is compatible with anything (to avoid cascading errors) */
    if (target->kind == TY_ERROR || source->kind == TY_ERROR) return 1;

    /* int -> float promotion */
    if (target->kind == TY_FLOAT && source->kind == TY_INT) return 1;

    /* bool -> int promotion */
    if (target->kind == TY_INT && source->kind == TY_BOOL) return 1;

    /* bool -> float promotion */
    if (target->kind == TY_FLOAT && source->kind == TY_BOOL) return 1;

    /* None -> Optional[X] */
    if (target->kind == TY_OPTIONAL && source->kind == TY_NONE) return 1;

    /* T -> Optional[T] */
    if (target->kind == TY_OPTIONAL && target->type_args) {
        if (type_compatible(target->type_args, source)) return 1;
    }

    /* Optional[T] -> Optional[T] with compatible inner */
    if (target->kind == TY_OPTIONAL && source->kind == TY_OPTIONAL) {
        if (target->type_args && source->type_args) {
            return type_compatible(target->type_args, source->type_args);
        }
        return 1;
    }

    /* Union target: source must be compatible with at least one member */
    if (target->kind == TY_UNION) {
        for (member = target->type_args; member; member = member->next) {
            if (type_compatible(member, source)) return 1;
        }
        return 0;
    }

    /* Union source: all members must be compatible with target */
    if (source->kind == TY_UNION) {
        for (member = source->type_args; member; member = member->next) {
            if (!type_compatible(target, member)) return 0;
        }
        return 1;
    }

    /* Subclass -> base class */
    if (target->kind == TY_CLASS && source->kind == TY_CLASS) {
        if (target->class_info && source->class_info) {
            return is_subclass_of(source->class_info, target->class_info);
        }
    }

    /* Generic instance <-> base class: TypedList() assignable to TypedList[str] */
    if (target->kind == TY_GENERIC_INST && source->kind == TY_CLASS) {
        if (target->name && source->name && strcmp(target->name, source->name) == 0)
            return 1;
        if (target->class_info && source->class_info)
            return is_subclass_of(source->class_info, target->class_info);
    }
    if (target->kind == TY_CLASS && source->kind == TY_GENERIC_INST) {
        if (target->name && source->name && strcmp(target->name, source->name) == 0)
            return 1;
        if (target->class_info && source->class_info)
            return is_subclass_of(source->class_info, target->class_info);
    }

    /* Container covariance: list[Derived] -> list[Base] */
    if (target->kind == TY_LIST && source->kind == TY_LIST) {
        if (target->type_args && source->type_args) {
            return type_compatible(target->type_args, source->type_args);
        }
        return 1; /* unparameterized list is compatible */
    }

    if (target->kind == TY_SET && source->kind == TY_SET) {
        if (target->type_args && source->type_args) {
            return type_compatible(target->type_args, source->type_args);
        }
        return 1;
    }

    if (target->kind == TY_DICT && source->kind == TY_DICT) {
        if (target->type_args && source->type_args &&
            target->type_args->next && source->type_args->next) {
            return type_compatible(target->type_args, source->type_args) &&
                   type_compatible(target->type_args->next, source->type_args->next);
        }
        return 1;
    }

    /* Tuple compatibility: unparameterized tuple matches any tuple */
    if (target->kind == TY_TUPLE && source->kind == TY_TUPLE) {
        if (!target->type_args || !source->type_args) {
            return 1; /* unparameterized tuple compatible with any tuple */
        }
        /* Same-length tuples with compatible element types */
        if (target->num_type_args == source->num_type_args) {
            TypeInfo *ta = target->type_args;
            TypeInfo *sa = source->type_args;
            while (ta && sa) {
                if (!type_compatible(ta, sa)) return 0;
                ta = ta->next;
                sa = sa->next;
            }
            return 1;
        }
        return 0;
    }

    /* Generic instance compatibility (e.g. Stack[int] == Stack[int]) */
    if (target->kind == TY_GENERIC_INST && source->kind == TY_GENERIC_INST) {
        if (target->name && source->name && strcmp(target->name, source->name) == 0) {
            /* Same generic class — check type args */
            TypeInfo *ta = target->type_args;
            TypeInfo *sa = source->type_args;
            while (ta && sa) {
                if (!type_compatible(ta, sa)) return 0;
                ta = ta->next;
                sa = sa->next;
            }
            return (ta == 0 && sa == 0) ? 1 : 0;
        }
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Type queries                                                        */
/* ------------------------------------------------------------------ */

int type_is_numeric(TypeInfo *t)
{
    if (!t) return 0;
    return t->kind == TY_INT || t->kind == TY_FLOAT || t->kind == TY_BOOL ||
           t->kind == TY_COMPLEX;
}

int type_is_container(TypeInfo *t)
{
    if (!t) return 0;
    return t->kind == TY_LIST || t->kind == TY_DICT ||
           t->kind == TY_TUPLE || t->kind == TY_SET ||
           t->kind == TY_FROZENSET ||
           t->kind == TY_BYTEARRAY;
}

int type_is_iterable(TypeInfo *t)
{
    if (!t) return 0;
    return t->kind == TY_LIST || t->kind == TY_DICT ||
           t->kind == TY_TUPLE || t->kind == TY_SET ||
           t->kind == TY_STR || t->kind == TY_RANGE ||
           t->kind == TY_BYTES || t->kind == TY_FROZENSET ||
           t->kind == TY_BYTEARRAY;
}

/* ------------------------------------------------------------------ */
/* Common type for binary operations                                   */
/* ------------------------------------------------------------------ */

TypeInfo *type_common(TypeInfo *a, TypeInfo *b)
{
    if (!a || !b) return type_error;

    /* Error propagation */
    if (a->kind == TY_ERROR) return a;
    if (b->kind == TY_ERROR) return b;

    /* Any absorbs */
    if (a->kind == TY_ANY) return type_any;
    if (b->kind == TY_ANY) return type_any;

    /* Same kind */
    if (type_equal(a, b)) return a;

    /* Numeric promotions */
    if (type_is_numeric(a) && type_is_numeric(b)) {
        /* complex wins over everything */
        if (a->kind == TY_COMPLEX || b->kind == TY_COMPLEX) return type_complex;
        /* float wins over int and bool */
        if (a->kind == TY_FLOAT || b->kind == TY_FLOAT) return type_float;
        /* int wins over bool */
        if (a->kind == TY_INT || b->kind == TY_INT) return type_int;
        return type_bool;
    }

    /* str + str = str */
    if (a->kind == TY_STR && b->kind == TY_STR) return type_str;

    /* list + list = list (use left operand's element type) */
    if (a->kind == TY_LIST && b->kind == TY_LIST) return a;

    /* tuple + tuple = tuple */
    if (a->kind == TY_TUPLE && b->kind == TY_TUPLE) return a;

    /* Optional: unwrap and find common, then re-wrap */
    if (a->kind == TY_OPTIONAL && b->kind == TY_OPTIONAL) {
        if (a->type_args && b->type_args) {
            TypeInfo *inner = type_common(a->type_args, b->type_args);
            return type_new_optional(inner);
        }
    }

    /* If one is None and other is a real type, result is Optional */
    if (a->kind == TY_NONE) return type_new_optional(b);
    if (b->kind == TY_NONE) return type_new_optional(a);

    /* Fall back to a union of the two */
    {
        TypeInfo *acpy = type_copy(a);
        TypeInfo *bcpy = type_copy(b);
        if (acpy && bcpy) {
            acpy->next = bcpy;
            bcpy->next = 0;
            return type_new_union(acpy);
        }
    }
    return type_error;
}

/* ------------------------------------------------------------------ */
/* type_to_string - human-readable representation                      */
/* ------------------------------------------------------------------ */

static int str_append(char *buf, int pos, int max, const char *s)
{
    while (*s && pos < max - 1) {
        buf[pos++] = *s++;
    }
    buf[pos] = '\0';
    return pos;
}

static int type_to_string_impl(TypeInfo *t, char *buf, int pos, int max)
{
    if (!t) {
        return str_append(buf, pos, max, "<null>");
    }

    switch (t->kind) {
    case TY_INT:   return str_append(buf, pos, max, "int");
    case TY_FLOAT: return str_append(buf, pos, max, "float");
    case TY_BOOL:  return str_append(buf, pos, max, "bool");
    case TY_STR:   return str_append(buf, pos, max, "str");
    case TY_NONE:  return str_append(buf, pos, max, "None");
    case TY_ANY:   return str_append(buf, pos, max, "Any");
    case TY_BYTES: return str_append(buf, pos, max, "bytes");
    case TY_RANGE: return str_append(buf, pos, max, "range");
    case TY_COMPLEX: return str_append(buf, pos, max, "complex");
    case TY_BYTEARRAY: return str_append(buf, pos, max, "bytearray");
    case TY_ERROR: return str_append(buf, pos, max, "<error>");

    case TY_LIST:
        pos = str_append(buf, pos, max, "list");
        if (t->type_args) {
            pos = str_append(buf, pos, max, "[");
            pos = type_to_string_impl(t->type_args, buf, pos, max);
            pos = str_append(buf, pos, max, "]");
        }
        return pos;

    case TY_DICT:
        pos = str_append(buf, pos, max, "dict");
        if (t->type_args) {
            pos = str_append(buf, pos, max, "[");
            pos = type_to_string_impl(t->type_args, buf, pos, max);
            if (t->type_args->next) {
                pos = str_append(buf, pos, max, ", ");
                pos = type_to_string_impl(t->type_args->next, buf, pos, max);
            }
            pos = str_append(buf, pos, max, "]");
        }
        return pos;

    case TY_TUPLE: {
        TypeInfo *cur;
        int first;
        pos = str_append(buf, pos, max, "tuple");
        if (t->type_args) {
            pos = str_append(buf, pos, max, "[");
            first = 1;
            for (cur = t->type_args; cur; cur = cur->next) {
                if (!first) pos = str_append(buf, pos, max, ", ");
                pos = type_to_string_impl(cur, buf, pos, max);
                first = 0;
            }
            pos = str_append(buf, pos, max, "]");
        }
        return pos;
    }

    case TY_SET:
        pos = str_append(buf, pos, max, "set");
        if (t->type_args) {
            pos = str_append(buf, pos, max, "[");
            pos = type_to_string_impl(t->type_args, buf, pos, max);
            pos = str_append(buf, pos, max, "]");
        }
        return pos;

    case TY_FROZENSET:
        return str_append(buf, pos, max, "frozenset");

    case TY_FUNC: {
        TypeInfo *p;
        int first;
        pos = str_append(buf, pos, max, "(");
        first = 1;
        for (p = t->params; p; p = p->next) {
            if (!first) pos = str_append(buf, pos, max, ", ");
            pos = type_to_string_impl(p, buf, pos, max);
            first = 0;
        }
        pos = str_append(buf, pos, max, ") -> ");
        pos = type_to_string_impl(t->ret_type, buf, pos, max);
        return pos;
    }

    case TY_CLASS:
        if (t->name) return str_append(buf, pos, max, t->name);
        return str_append(buf, pos, max, "<class>");

    case TY_GENERIC_PARAM:
        if (t->name) return str_append(buf, pos, max, t->name);
        return str_append(buf, pos, max, "T");

    case TY_GENERIC_INST: {
        TypeInfo *a;
        int first;
        if (t->name) pos = str_append(buf, pos, max, t->name);
        else pos = str_append(buf, pos, max, "<generic>");
        if (t->type_args) {
            pos = str_append(buf, pos, max, "[");
            first = 1;
            for (a = t->type_args; a; a = a->next) {
                if (!first) pos = str_append(buf, pos, max, ", ");
                pos = type_to_string_impl(a, buf, pos, max);
                first = 0;
            }
            pos = str_append(buf, pos, max, "]");
        }
        return pos;
    }

    case TY_UNION: {
        TypeInfo *m;
        int first;
        first = 1;
        for (m = t->type_args; m; m = m->next) {
            if (!first) pos = str_append(buf, pos, max, " | ");
            pos = type_to_string_impl(m, buf, pos, max);
            first = 0;
        }
        return pos;
    }

    case TY_OPTIONAL:
        if (t->type_args) {
            pos = type_to_string_impl(t->type_args, buf, pos, max);
            pos = str_append(buf, pos, max, " | None");
        } else {
            pos = str_append(buf, pos, max, "Optional");
        }
        return pos;
    }

    return str_append(buf, pos, max, "<?>");
}

const char *type_to_string(TypeInfo *t)
{
    string_buf[0] = '\0';
    type_to_string_impl(t, string_buf, 0, sizeof(string_buf));
    return string_buf;
}

/* ------------------------------------------------------------------ */
/* Type substitution for generics                                      */
/* ------------------------------------------------------------------ */

TypeInfo *type_substitute(TypeInfo *t, const char **param_names,
                          TypeInfo **concrete_types, int count)
{
    TypeInfo *result;
    TypeInfo *prev, *cur, *sub;
    int i;

    if (!t) return 0;

    /* If this is a generic param, look for substitution */
    if (t->kind == TY_GENERIC_PARAM && t->name) {
        for (i = 0; i < count; i++) {
            if (param_names[i] && strcmp(t->name, param_names[i]) == 0) {
                return type_copy(concrete_types[i]);
            }
        }
        /* No substitution found, return copy */
        return type_copy(t);
    }

    /* Singletons don't need substitution */
    if (t == type_int || t == type_float || t == type_bool ||
        t == type_str || t == type_none || t == type_any ||
        t == type_error || t == type_range || t == type_bytes ||
        t == type_frozenset || t == type_complex ||
        t == type_bytearray) {
        return t;
    }

    /* Deep copy with recursive substitution */
    result = pool_alloc();
    if (!result) return 0;
    result->kind = t->kind;
    result->name = t->name;
    result->num_type_args = t->num_type_args;
    result->num_params = t->num_params;
    result->class_info = t->class_info;
    result->next = 0;

    /* Substitute in type_args */
    prev = 0;
    for (cur = t->type_args; cur; cur = cur->next) {
        sub = type_substitute(cur, param_names, concrete_types, count);
        if (!sub) continue;
        sub->next = 0;
        if (prev) {
            prev->next = sub;
        } else {
            result->type_args = sub;
        }
        prev = sub;
    }

    /* Substitute in params (for TY_FUNC) */
    prev = 0;
    for (cur = t->params; cur; cur = cur->next) {
        sub = type_substitute(cur, param_names, concrete_types, count);
        if (!sub) continue;
        sub->next = 0;
        if (prev) {
            prev->next = sub;
        } else {
            result->params = sub;
        }
        prev = sub;
    }

    /* Substitute in return type */
    if (t->ret_type) {
        result->ret_type = type_substitute(t->ret_type, param_names,
                                           concrete_types, count);
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void types_init()
{
    /* Reset pool state */
    pool_head = 0;
    pool_current = 0;
    intern_count = 0;
    memset(intern_table, 0, sizeof(intern_table));

    /* Create built-in type singletons */
    type_int   = type_new(TY_INT,   "int");
    type_float = type_new(TY_FLOAT, "float");
    type_bool  = type_new(TY_BOOL,  "bool");
    type_str   = type_new(TY_STR,   "str");
    type_none  = type_new(TY_NONE,  "None");
    type_any   = type_new(TY_ANY,   "Any");
    type_error = type_new(TY_ERROR, "<error>");
    type_range = type_new(TY_RANGE, "range");
    type_bytes = type_new(TY_BYTES, "bytes");
    type_frozenset = type_new(TY_FROZENSET, "frozenset");
    type_complex = type_new(TY_COMPLEX, "complex");
    type_bytearray = type_new(TY_BYTEARRAY, "bytearray");
}

void types_shutdown()
{
    TypePool *blk, *next;
    int i;

    /* Free all pool blocks */
    blk = pool_head;
    while (blk) {
        next = blk->next;
        free(blk);
        blk = next;
    }
    pool_head = 0;
    pool_current = 0;

    /* Free interned strings */
    for (i = 0; i < intern_count; i++) {
        free((void *)intern_table[i]);
        intern_table[i] = 0;
    }
    intern_count = 0;

    /* Clear singleton pointers */
    type_int   = 0;
    type_float = 0;
    type_bool  = 0;
    type_str   = 0;
    type_none  = 0;
    type_any   = 0;
    type_error = 0;
    type_range = 0;
    type_bytes = 0;
    type_frozenset = 0;
    type_complex = 0;
    type_bytearray = 0;
}
