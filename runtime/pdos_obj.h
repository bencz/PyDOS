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
    PYDT_MAX        = 23
} PyDosType;

/* ------------------------------------------------------------------ */
/* Forward declarations                                                */
/* ------------------------------------------------------------------ */
struct PyDosObj;
typedef struct PyDosObj PyDosObj;

struct PyDosVTable;

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
} PyDosInstance;

typedef struct PyDosFunc {
    void     (far *code)(void);
    PyDosObj far       *defaults;
    PyDosObj far       *closure;
    const char far     *name;
} PyDosFunc;

typedef struct PyDosGen {
    void     (far *resume)(void);
    PyDosObj far       *state;
    int                 pc;
    PyDosObj far       *locals;
} PyDosGen;

typedef struct PyDosExc {
    int                 type_code;
    PyDosObj far       *message;
    PyDosObj far       *traceback;
    PyDosObj far       *cause;
} PyDosExc;

typedef struct PyDosClass {
    const char far             *name;
    struct PyDosVTable far     *vtable;
    PyDosObj far * far         *bases;
    unsigned char               num_bases;
    PyDosObj far               *class_attrs;
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
#define OBJ_FLAG_ARENA          0x08
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
            !((obj)->flags & (OBJ_FLAG_IMMORTAL | OBJ_FLAG_ARENA))) { \
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
PyDosObj far * PYDOS_API  pydos_obj_alloc(void);
void           PYDOS_API  pydos_obj_free(PyDosObj far *obj);
void           PYDOS_API  pydos_obj_release_data(PyDosObj far *obj);

PyDosObj far * PYDOS_API  pydos_obj_new_none(void);
PyDosObj far * PYDOS_API  pydos_obj_new_bool(int val);
PyDosObj far * PYDOS_API  pydos_obj_new_int(long val);
PyDosObj far * PYDOS_API  pydos_obj_new_float(double val);
PyDosObj far * PYDOS_API  pydos_obj_new_str(const char far *data, unsigned int len);

int            PYDOS_API  pydos_obj_is_truthy(PyDosObj far *obj);
int            PYDOS_API  pydos_obj_equal(PyDosObj far *a, PyDosObj far *b);
unsigned int   PYDOS_API  pydos_obj_hash(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_to_str(PyDosObj far *obj);
const char far * PYDOS_API pydos_obj_type_name(PyDosObj far *obj);

void           PYDOS_API  pydos_obj_init(void);
void           PYDOS_API  pydos_obj_shutdown(void);

void PYDOS_API pydos_incref(PyDosObj far *obj);
void PYDOS_API pydos_decref(PyDosObj far *obj);

PyDosObj far * PYDOS_API pydos_obj_get_attr(PyDosObj far *obj,
                                             const char far *attr_name);
void           PYDOS_API pydos_obj_set_attr(PyDosObj far *obj,
                                             const char far *attr_name,
                                             PyDosObj far *value);
void           PYDOS_API pydos_obj_del_attr(PyDosObj far *obj,
                                             const char far *attr_name);

void           PYDOS_API pydos_obj_set_vtable(PyDosObj far *obj,
                                              struct PyDosVTable far *vt);

/* Check if obj is an instance whose vtable matches target_vt (or inherits
 * from it via MRO chain).  Returns 1 if match, 0 otherwise. */
int            PYDOS_API pydos_obj_isinstance_vtable(PyDosObj far *obj,
                                              struct PyDosVTable far *target_vt);

PyDosObj far * PYDOS_API pydos_obj_add(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_sub(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_mul(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_matmul(PyDosObj far *a, PyDosObj far *b);
PyDosObj far * PYDOS_API pydos_obj_inplace(PyDosObj far *a, PyDosObj far *b,
                                            int op);

PyDosObj far * PYDOS_API pydos_obj_getitem(PyDosObj far *obj,
                                            PyDosObj far *key);
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

int            PYDOS_API  pydos_obj_contains(PyDosObj far *container,
                                              PyDosObj far *item);

int            PYDOS_API  pydos_obj_compare(PyDosObj far *a, PyDosObj far *b);

PyDosObj far * PYDOS_API  pydos_obj_neg(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_pos(PyDosObj far *obj);
PyDosObj far * PYDOS_API  pydos_obj_invert(PyDosObj far *obj);

PyDosObj far * PYDOS_API  pydos_func_new(void (far *code)(void),
                                          const char far *name);

#endif /* PDOS_OBJ_H */
