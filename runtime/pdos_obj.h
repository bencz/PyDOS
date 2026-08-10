/*
 * pydos_obj.h - Universal object type for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_OBJ_H
#define PDOS_OBJ_H

/* Calling convention for runtime API functions.
 * On Watcom: __cdecl with trailing underscore symbol naming.
 *   __cdecl = stack params (right-to-left), caller cleanup, return in AX/DX:AX.
 *   "*_" sets symbol naming to "func_" (trailing underscore, matches codegen).
 * On macOS: empty (flat memory, default convention).
 * PYDOS_API goes BETWEEN return type and function name. */
#ifdef __WATCOMC__
#pragma aux __cdecl "*_"
#define PYDOS_API __cdecl
#else
#ifndef PYDOS_API
#define PYDOS_API
#endif
#endif

/* ------------------------------------------------------------------ */
/* 32-bit protected mode (DOS/4GW) compatibility                       */
/* When PYDOS_32BIT is defined (wcc386 -dPYDOS_32BIT), the runtime    */
/* uses flat memory model: far pointers become near, _fmalloc/etc.    */
/* map to standard malloc/etc.                                         */
/* ------------------------------------------------------------------ */
#ifdef PYDOS_32BIT

#include <stdlib.h>
#include <string.h>

/* In flat model, far is a no-op (pointers are 4-byte linear) */
#ifdef far
#undef far
#endif
#define far

/* Far memory functions -> standard equivalents */
#define _fmalloc(s)         malloc(s)
#define _ffree(p)           free(p)
#define _frealloc(p,s)      realloc(p,s)
#define _fmemset(p,v,n)     memset(p,v,n)
#define _fmemcpy(d,s,n)     memcpy(d,s,n)
#define _fmemcmp(a,b,n)     memcmp(a,b,n)
#define _fstrlen(s)         strlen((const char*)(s))
#define _fstrcpy(d,s)       strcpy((char*)(d),(const char*)(s))
#define _fstrcmp(a,b)       strcmp((const char*)(a),(const char*)(b))
#define _fstrcat(d,s)       strcat((char*)(d),(const char*)(s))
#define _fmemmove(d,s,n)    memmove(d,s,n)

/* Far heap management - not needed in flat model */
#define _fheapgrow()        ((void)0)
#define _fheapshrink()      ((void)0)
#define _fmsize(p)          ((unsigned int)0)
#define _memavl()           ((unsigned int)0x7FFFFFFF)
#define _freect(s)          ((unsigned int)(0x7FFFFFFF/(s)))

#endif /* PYDOS_32BIT */

/* ------------------------------------------------------------------ */
/* Struct layout constants                                             */
/* ------------------------------------------------------------------ */

/* Offset of the value union 'v' in PyDosObj.
 * 16-bit: type(1) + flags(1) + refcount(2) = 4 bytes
 * 32-bit: type(1) + flags(1) + pad(2) + refcount(4) = 8 bytes */
#ifdef PYDOS_32BIT
#define PYOBJ_V_OFFSET      8
#else
#define PYOBJ_V_OFFSET      4
#endif

/* Maximum refcount value (overflow guard) */
#ifdef PYDOS_32BIT
#define REFCOUNT_MAX        0xFFFFFFFFU
#else
#define REFCOUNT_MAX        0xFFFFU
#endif

/* ------------------------------------------------------------------ */
/* Type tag enumeration                                                */
/* ------------------------------------------------------------------ */
typedef enum {
    PYDT_NONE       = 0,
    PYDT_BOOL       = 1,
    PYDT_INT        = 2,
    PYDT_FLOAT      = 3,
    PYDT_STR        = 4,
    PYDT_LIST       = 5,
    PYDT_DICT       = 6,
    PYDT_TUPLE      = 7,
    PYDT_SET        = 8,
    PYDT_BYTES      = 9,
    PYDT_INSTANCE   = 10,
    PYDT_FUNCTION   = 11,
    PYDT_GENERATOR  = 12,
    PYDT_EXCEPTION  = 13,
    PYDT_CLASS      = 14,
    PYDT_RANGE      = 15,
    PYDT_FILE       = 16,
    PYDT_CELL       = 17,
    PYDT_COROUTINE  = 18,
    PYDT_EXC_GROUP  = 19,
    PYDT_FROZENSET  = 20,
    PYDT_COMPLEX    = 21,
    PYDT_BYTEARRAY  = 22,
    PYDT_NOTIMPLEMENTED = 23,
    PYDT_TRACEBACK  = 24,
    PYDT_SLICE      = 25,
    PYDT_TYPE_PARAM = 26,
    PYDT_TYPE_ALIAS = 27,
    PYDT_GENERIC_ALIAS = 28,
    PYDT_CODE       = 29,
    PYDT_SUPER      = 30,
    PYDT_MEMORYVIEW = 31,
    PYDT_MAX        = 32
} PyDosType;

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
struct PyDosObj;
typedef struct PyDosObj PyDosObj;

struct PyDosVTable;
struct PyDosCodeRef;
struct PyDosVMModule;

/* Compact immutable call binder metadata.  Parameter kinds use one nibble
 * each and names are stored as one comma-separated byte string immediately
 * after this header.  Bound methods share the block through refcounting. */
typedef struct PyDosParamSpec {
    unsigned int        refcount;
    unsigned int        names_len;
    unsigned long       flags;
    char                names[1];
} PyDosParamSpec;

/* ------------------------------------------------------------------ */
/* Sub-structures for compound types                                   */
/* ------------------------------------------------------------------ */

typedef struct PyDosStr {
    char far           *data;
    unsigned int        len;
    unsigned int        hash;
} PyDosStr;

typedef struct PyDosList {
    PyDosObj far * far *items;
    unsigned int        len;
    unsigned int        cap;
} PyDosList;

typedef struct PyDosDictEntry {
    PyDosObj far       *key;
    PyDosObj far       *value;
    unsigned int        hash;
    unsigned int        insert_order;
} PyDosDictEntry;

typedef struct PyDosDict {
    PyDosDictEntry far *entries;
    unsigned int        size;
    unsigned int        used;
    unsigned int        next_order;
} PyDosDict;

typedef struct PyDosTuple {
    PyDosObj far * far *items;
    unsigned int        len;
} PyDosTuple;

typedef struct PyDosInstance {
    PyDosObj far               *attrs;
    struct PyDosVTable far     *vtable;
    PyDosObj far               *cls;
    /* Primitive payload owned by subclasses of mutable built-in types.
     * Keeping it separate from attrs preserves the distinction between
     * mapping items and normal Python attributes. */
    PyDosObj far               *native_storage;
} PyDosInstance;

typedef struct PyDosFunc {
    struct PyDosCodeRef far *code_ref;
    PyDosObj far       *defaults;
    PyDosObj far       *closure;
    const char far     *name;
    PyDosObj far       *bound_self;
    PyDosObj far       *attrs;
    PyDosParamSpec far *param_spec;
    PyDosObj far       *code_obj;
    unsigned char       arg_count;
    unsigned char       signature_known;
} PyDosFunc;

typedef struct PyDosGen {
    void     (far *resume)(void);
    PyDosObj far       *state;
    int                 pc;
    PyDosObj far       *locals;
    PyDosObj far       *vm_stack;
    PyDosObj far       *vm_closure;
    struct PyDosCodeRef far *code_ref;
    unsigned char       vm_suspended;
} PyDosGen;

typedef struct PyDosExc {
    int                 type_code;
    PyDosObj far       *message;
    PyDosObj far       *value;       /* StopIteration return value */
    PyDosObj far       *traceback;
    PyDosObj far       *cause;
} PyDosExc;

typedef struct PyDosTraceback {
    PyDosObj far       *next;
    const char far     *function_name;
    unsigned int        line;
} PyDosTraceback;

typedef struct PyDosSlice {
    long                start;
    long                stop;
    long                step;
    unsigned char       has_start;
    unsigned char       has_stop;
    unsigned char       has_step;
} PyDosSlice;

typedef struct PyDosTypeParam {
    PyDosObj far       *name;
    PyDosObj far       *bound;
    PyDosObj far       *constraints;
    PyDosObj far       *bound_thunk;
    unsigned char       kind;
} PyDosTypeParam;

typedef struct PyDosTypeAlias {
    PyDosObj far       *name;
    PyDosObj far       *type_params;
    PyDosObj far       *value;
} PyDosTypeAlias;

typedef struct PyDosGenericAlias {
    PyDosObj far       *origin;
    PyDosObj far       *args;
} PyDosGenericAlias;

typedef struct PyDosCode {
    const char far     *name;
    PyDosObj far       *freevars;
    PyDosObj far       *consts;
    struct PyDosCodeRef far *code_ref;
} PyDosCode;

typedef struct PyDosSuper {
    PyDosObj far       *start_type;
    PyDosObj far       *bound_obj;
} PyDosSuper;

typedef struct PyDosMemoryView {
    PyDosObj far       *source;
    PyDosObj far       *exporter;
    unsigned char       released;
} PyDosMemoryView;

typedef struct PyDosClass {
    const char far             *name;
    struct PyDosVTable far     *vtable;
    PyDosObj far * far         *bases;
    PyDosObj far * far         *mro;   /* borrowed refs, includes self */
    unsigned char               num_bases;
    unsigned char               mro_len;
    signed char                 runtime_type_tag; /* -1 for user classes */
    PyDosObj far               *class_attrs;
    PyDosObj far               *metaclass;
} PyDosClass;

typedef struct PyDosRange {
    long    start;
    long    stop;
    long    step;
    long    current;
} PyDosRange;

typedef struct PyDosFile {
    int             handle;
    unsigned char   mode;
    char far       *buffer;
} PyDosFile;

typedef struct PyDosCell {
    PyDosObj far   *value;
} PyDosCell;

typedef struct PyDosExcGroup {
    PyDosObj far       *message;      /* group message string */
    PyDosObj far * far *exceptions;   /* array of exception objects */
    unsigned int        count;        /* number of exceptions */
} PyDosExcGroup;

typedef struct PyDosComplex {
    double real;
    double imag;
} PyDosComplex;

typedef struct PyDosByteArray {
    unsigned char far  *data;    /* byte buffer */
    unsigned int        len;     /* current length */
    unsigned int        cap;     /* allocated capacity */
} PyDosByteArray;

typedef struct PyDosFrozenSet {
    PyDosObj far * far *items;   /* sorted, deduplicated array */
    unsigned int        len;     /* element count */
    unsigned int        hash;    /* cached XOR of element hashes */
} PyDosFrozenSet;

/* ------------------------------------------------------------------ */
/* Main PyDosObj structure                                             */
/* ------------------------------------------------------------------ */
struct PyDosObj {
    unsigned char   type;       /* PyDosType tag */
    unsigned char   flags;
    unsigned int    refcount;

    union {
        int             bool_val;
        long            int_val;
        double          float_val;
        PyDosStr        str;
        PyDosList       list;
        PyDosDict       dict;
        PyDosTuple      tuple;
        PyDosInstance   instance;
        PyDosFunc       func;
        PyDosGen        gen;
        PyDosExc        exc;
        PyDosTraceback  traceback;
        PyDosSlice      slice;
        PyDosTypeParam  type_param;
        PyDosTypeAlias  type_alias;
        PyDosGenericAlias generic_alias;
        PyDosCode       code;
        PyDosSuper      super_obj;
        PyDosMemoryView memoryview;
        PyDosClass      cls;
        PyDosRange      range;
        PyDosFile       file;
        PyDosCell       cell;
        PyDosExcGroup   excgroup;
        PyDosFrozenSet  frozenset;
        PyDosComplex    complex_val;
        PyDosByteArray  bytearray;
    } v;
};

/* ------------------------------------------------------------------ */
/* Object flags                                                        */
/* ------------------------------------------------------------------ */
#define OBJ_FLAG_IMMORTAL       0x01
#define OBJ_FLAG_MARKED         0x02
#define OBJ_FLAG_GC_TRACKED     0x04
#define OBJ_FLAG_GC_SCANNED     0x08
#define OBJ_FLAG_GEN_THROW     0x10   /* generator has pending throw */

/* ------------------------------------------------------------------ */
/* Reference counting macros                                           */
/* ------------------------------------------------------------------ */
#define PYDOS_INCREF(obj) \
    do { \
        if ((obj) != (PyDosObj far *)0 && \
            !((obj)->flags & OBJ_FLAG_IMMORTAL)) { \
            if ((obj)->refcount < REFCOUNT_MAX) { \
                (obj)->refcount++; \
            } \
        } \
    } while (0)

#define PYDOS_DECREF(obj) \
    do { \
        if ((obj) != (PyDosObj far *)0 && \
            !((obj)->flags & OBJ_FLAG_IMMORTAL)) { \
            if ((obj)->refcount > 0) { \
                (obj)->refcount--; \
            } \
            if ((obj)->refcount == 0) { \
                pydos_obj_free(obj); \
            } \
        } \
    } while (0)

/* ------------------------------------------------------------------ */
/* Function declarations                                               */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API  pydos_obj_alloc_type(unsigned int type);
void           PYDOS_API  pydos_obj_free(PyDosObj far *obj);
void           PYDOS_API  pydos_obj_release_data(PyDosObj far *obj);

PyDosObj far * PYDOS_API  pydos_obj_new_none(void);
PyDosObj far * PYDOS_API  pydos_obj_new_notimplemented(void);
PyDosObj far * PYDOS_API  pydos_obj_new_empty_tuple(void);
PyDosObj far * PYDOS_API  pydos_obj_new_bool(int val);
PyDosObj far * PYDOS_API  pydos_obj_new_int(long val);
PyDosObj far * PYDOS_API  pydos_obj_new_float(double val);
PyDosObj far * PYDOS_API  pydos_obj_new_str(const char far *data, unsigned int len);
PyDosObj far * PYDOS_API  pydos_obj_new_slice(PyDosObj far *start,
                                               PyDosObj far *stop,
                                               PyDosObj far *step);
PyDosObj far * PYDOS_API  pydos_type_param_new(PyDosObj far *name,
                                               PyDosObj far *kind,
                                               PyDosObj far *bound,
                                               PyDosObj far *constraints,
                                               PyDosObj far *bound_thunk);
PyDosObj far * PYDOS_API  pydos_type_alias_new(PyDosObj far *name,
                                               PyDosObj far *type_params);
PyDosObj far * PYDOS_API  pydos_type_alias_set_value(PyDosObj far *alias,
                                                     PyDosObj far *value);
PyDosObj far * PYDOS_API  pydos_generic_alias_new(PyDosObj far *origin,
                                                  PyDosObj far *args);
PyDosObj far * PYDOS_API  pydos_func_set_code_metadata(
    PyDosObj far *func, PyDosObj far *name, PyDosObj far *freevars,
    PyDosObj far *consts);
PyDosObj far * PYDOS_API  pydos_super_new(PyDosObj far *start_type,
                                          PyDosObj far *bound_obj);
PyDosObj far * PYDOS_API  pydos_super_get_attr(PyDosObj far *super_obj,
                                               const char far *attr_name);
PyDosObj far * PYDOS_API  pydos_instance_new(PyDosObj far *cls);
PyDosObj far * PYDOS_API  pydos_memoryview_new(PyDosObj far *source,
                                               PyDosObj far *exporter);
PyDosObj far * PYDOS_API  pydos_memoryview_tobytes(PyDosObj far *view);
PyDosObj far * PYDOS_API  pydos_memoryview_release(PyDosObj far *view);
int            PYDOS_API  pydos_obj_is_truthy(PyDosObj far *obj);
int            PYDOS_API  pydos_obj_equal(PyDosObj far *a, PyDosObj far *b);
unsigned int   PYDOS_API  pydos_obj_hash(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_to_str(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_repr(PyDosObj far *obj);
const char far * PYDOS_API pydos_obj_type_name(PyDosObj far *obj);

void           PYDOS_API  pydos_obj_init(void);
void           PYDOS_API  pydos_obj_shutdown(void);

void PYDOS_API pydos_incref(PyDosObj far *obj);
void PYDOS_API pydos_decref(PyDosObj far *obj);

PyDosObj far * PYDOS_API pydos_obj_get_attr(PyDosObj far *obj,
                                             const char far *attr_name);
int            PYDOS_API pydos_obj_has_attr(PyDosObj far *obj,
                                             const char far *attr_name);
void           PYDOS_API pydos_obj_set_attr(PyDosObj far *obj,
                                             const char far *attr_name,
                                             PyDosObj far *value);
void           PYDOS_API pydos_obj_del_attr(PyDosObj far *obj,
                                             const char far *attr_name);

void           PYDOS_API pydos_obj_set_vtable(PyDosObj far *obj,
                                              struct PyDosVTable far *vt);
void           PYDOS_API pydos_obj_set_class(PyDosObj far *obj,
                                              PyDosObj far *cls);
PyDosObj far * PYDOS_API pydos_class_new(const char far *name,
                                         struct PyDosVTable far *vtable);
PyDosObj far * PYDOS_API pydos_builtin_type_object(unsigned int type_tag);
PyDosObj far * PYDOS_API pydos_class_set_names(PyDosObj far *cls);
void           PYDOS_API pydos_class_add_base(PyDosObj far *cls,
                                              PyDosObj far *base);
void           PYDOS_API pydos_class_add_object_base(PyDosObj far *cls);
int            PYDOS_API pydos_class_is_subclass(PyDosObj far *cls,
                                                 PyDosObj far *base);
PyDosObj far * PYDOS_API pydos_class_apply_inherited_hook(
    PyDosObj far *cls);
PyDosObj far * PYDOS_API pydos_class_call_init_subclass(
    PyDosObj far *cls, PyDosObj far *keywords);
PyDosObj far * PYDOS_API pydos_class_apply_metaclass(
    PyDosObj far *cls, PyDosObj far *metaclass);
PyDosObj far * PYDOS_API pydos_class_apply_metaclass_protocol(
    PyDosObj far *cls, PyDosObj far *metaclass, PyDosObj far *keywords);
PyDosObj far * PYDOS_API pydos_class_apply_inherited_metaclass(
    PyDosObj far *cls);
PyDosObj far * PYDOS_API pydos_class_apply_inherited_metaclass_protocol(
    PyDosObj far *cls, PyDosObj far *keywords);

/* Check if obj is an instance whose vtable matches target_vt (or inherits
 * from it via MRO chain).  Returns 1 if match, 0 otherwise. */
int            PYDOS_API pydos_obj_isinstance_vtable(PyDosObj far *obj,
                                              struct PyDosVTable far *target_vt);

PyDosObj far * PYDOS_API pydos_obj_add(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_sub(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_mul(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_floordiv(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_truediv(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_mod(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_pow(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_matmul(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_inplace(PyDosObj far *a, PyDosObj far *b,
                                            int op);

PyDosObj far * PYDOS_API pydos_obj_getitem(PyDosObj far *obj,
                                            PyDosObj far *key);
PyDosObj far * PYDOS_API pydos_match_class_arg(PyDosObj far *obj,
                                               PyDosObj far *index);
PyDosObj far * PYDOS_API pydos_match_sequence(PyDosObj far *obj);
PyDosObj far * PYDOS_API pydos_match_mapping(PyDosObj far *obj);
PyDosObj far * PYDOS_API pydos_obj_slice(PyDosObj far *obj,
                                          long start, long stop, long step);
void           PYDOS_API pydos_obj_setitem(PyDosObj far *obj,
                                            PyDosObj far *key,
                                            PyDosObj far *value);
void           PYDOS_API pydos_obj_delitem(PyDosObj far *obj,
                                            PyDosObj far *key);

PyDosObj far * PYDOS_API pydos_obj_get_iter(PyDosObj far *obj);
PyDosObj far * PYDOS_API pydos_obj_iter_next(PyDosObj far *iter);

PyDosObj far * PYDOS_API pydos_obj_call_method(
    const char far *method_name,
    unsigned int argc,
    PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_obj_call_method_guarded(
    const char far *method_name,
    void (far *expected_func)(void),
    unsigned int argc,
    PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_obj_call(PyDosObj far *callable,
                                         unsigned int argc,
                                         PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_obj_call_ex(PyDosObj far *callable,
                                            PyDosObj far *positional,
                                            PyDosObj far *keywords);
PyDosObj far * PYDOS_API pydos_call_pos_append(PyDosObj far *positional,
                                                PyDosObj far *value);
PyDosObj far * PYDOS_API pydos_call_pos_extend(PyDosObj far *positional,
                                                PyDosObj far *iterable);
PyDosObj far * PYDOS_API pydos_call_kw_set(PyDosObj far *keywords,
                                           PyDosObj far *name,
                                           PyDosObj far *value);
PyDosObj far * PYDOS_API pydos_call_kw_update(PyDosObj far *keywords,
                                              PyDosObj far *mapping);
PyDosObj far * PYDOS_API pydos_import_module(PyDosObj far *name);

int            PYDOS_API  pydos_obj_contains(PyDosObj far *container,
                                              PyDosObj far *item);

int            PYDOS_API  pydos_obj_compare(PyDosObj far *a, PyDosObj far *b);

PyDosObj far * PYDOS_API  pydos_obj_neg(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_pos(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_invert(PyDosObj far *obj);

PyDosObj far * PYDOS_API  pydos_func_new(void (far *code)(void),
                                         const char far *name);
PyDosObj far * PYDOS_API  pydos_func_new_builtin(void (far *code)(void),
                                                 const char far *name);
PyDosObj far * PYDOS_API  pydos_func_new_from_code_ref(
                                         struct PyDosCodeRef far *code_ref,
                                         const char far *name);
PyDosObj far * PYDOS_API  pydos_func_new_pbc(
                                const struct PyDosVMModule far *module,
                                unsigned short function_index,
                                const char far *name);
void           PYDOS_API  pydos_func_set_arg_count(PyDosObj far *func,
                                                    unsigned int arg_count);
void           PYDOS_API  pydos_func_set_signature(PyDosObj far *func,
                                                    unsigned int arg_count,
                                                    const char far *signature);
void           PYDOS_API  pydos_func_set_defaults(PyDosObj far *func,
                                                   PyDosObj far *defaults);
void           PYDOS_API  pydos_func_set_closure(PyDosObj far *func,
                                                  PyDosObj far *closure);
PyDosObj far * PYDOS_API  pydos_func_set_parameters(PyDosObj far *func,
                                                     PyDosObj far *names,
                                                     PyDosObj far *flags);
PyDosObj far * PYDOS_API  pydos_func_set_param_spec(PyDosObj far *func,
                                                    PyDosObj far *names,
                                                    PyDosObj far *flags);
PyDosObj far * PYDOS_API  pydos_class_set_method_defaults(
                                                   PyDosObj far *cls,
                                                   PyDosObj far *method_name,
                                                   PyDosObj far *defaults);
PyDosObj far * PYDOS_API  pydos_bound_method_new(void (far *code)(void),
                                                  PyDosObj far *self,
                                                  const char far *name);
PyDosObj far * PYDOS_API  pydos_bound_method_new_from_code_ref(
                                         struct PyDosCodeRef far *code_ref,
                                         PyDosObj far *self,
                                         const char far *name);
PyDosObj far * PYDOS_API  pydos_func_bind(PyDosObj far *func,
                                           PyDosObj far *self);

#endif /* PDOS_OBJ_H */
