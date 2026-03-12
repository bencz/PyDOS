/*
 * ir.h - Three-address code intermediate representation
 *
 * PyDOS Python-to-8086 compiler IR layer. Translates AST nodes
 * into a linear IR of three-address instructions suitable for
 * optimization and 8086 code generation.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL - arrays, linked lists, manual memory only.
 */

#ifndef IR_H
#define IR_H

#include "ast.h"
#include "types.h"
#include "sema.h"

#include <stdio.h>

/* --------------------------------------------------------------- */
/* IR opcode enumeration                                            */
/* --------------------------------------------------------------- */
enum IROp {
    /* Constants */
    IR_CONST_INT,       /* dest = int constant (src1 = const index) */
    IR_CONST_STR,       /* dest = string constant (src1 = const index) */
    IR_CONST_FLOAT,     /* dest = float constant (src1 = const index) */
    IR_CONST_NONE,      /* dest = None */
    IR_CONST_BOOL,      /* dest = bool (src1 = 0 or 1) */

    /* Variables */
    IR_LOAD_LOCAL,      /* dest = local[src1] */
    IR_STORE_LOCAL,     /* local[dest] = src1 */
    IR_LOAD_GLOBAL,     /* dest = global[name] (src1 = name const index) */
    IR_STORE_GLOBAL,    /* global[name] = src1 (dest = name const index) */
    IR_LOAD_ATTR,       /* dest = src1.attr (src2 = attr name const index) */
    IR_STORE_ATTR,      /* src1.attr = src2 (dest = attr name const index) */
    IR_LOAD_SUBSCRIPT,  /* dest = src1[src2] */
    IR_STORE_SUBSCRIPT, /* src1[src2] = extra_src (dest = value temp) */
    IR_DEL_SUBSCRIPT,   /* del src1[src2] */
    IR_DEL_LOCAL,       /* DECREF local[src1], zero slot */
    IR_DEL_GLOBAL,      /* DECREF global[name], zero slot; dest=name CI */
    IR_DEL_ATTR,        /* del src1.attr; dest=attr name CI */

    /* Arithmetic */
    IR_ADD,             /* dest = src1 + src2 */
    IR_SUB,             /* dest = src1 - src2 */
    IR_MUL,             /* dest = src1 * src2 */
    IR_DIV,             /* dest = src1 / src2 (true div) */
    IR_FLOORDIV,        /* dest = src1 // src2 */
    IR_MOD,             /* dest = src1 % src2 */
    IR_POW,             /* dest = src1 ** src2 */
    IR_MATMUL,          /* dest = src1 @ src2 */
    IR_INPLACE,         /* dest = src1 iop src2; extra = op index */
    IR_NEG,             /* dest = -src1 */
    IR_POS,             /* dest = +src1 */
    IR_NOT,             /* dest = not src1 (logical) */
    IR_BITNOT,          /* dest = ~src1 */

    /* Bitwise */
    IR_BITAND,          /* dest = src1 & src2 */
    IR_BITOR,           /* dest = src1 | src2 */
    IR_BITXOR,          /* dest = src1 ^ src2 */
    IR_SHL,             /* dest = src1 << src2 */
    IR_SHR,             /* dest = src1 >> src2 */

    /* Comparison */
    IR_CMP_EQ,          /* dest = src1 == src2 */
    IR_CMP_NE,          /* dest = src1 != src2 */
    IR_CMP_LT,          /* dest = src1 < src2 */
    IR_CMP_LE,          /* dest = src1 <= src2 */
    IR_CMP_GT,          /* dest = src1 > src2 */
    IR_CMP_GE,          /* dest = src1 >= src2 */
    IR_IS,              /* dest = src1 is src2 */
    IR_IS_NOT,          /* dest = src1 is not src2 */
    IR_IN,              /* dest = src1 in src2 */
    IR_NOT_IN,          /* dest = src1 not in src2 */

    /* Control flow */
    IR_JUMP,            /* goto label (extra = label id) */
    IR_JUMP_IF_TRUE,    /* if src1: goto label (extra = label id) */
    IR_JUMP_IF_FALSE,   /* if not src1: goto label (extra = label id) */
    IR_LABEL,           /* label: (extra = label id) */

    /* Calls */
    IR_CALL,            /* dest = call src1(args...) extra=argc */
    IR_CALL_METHOD,     /* dest = src1.method(args...) src2=name, extra=argc */
    IR_PUSH_ARG,        /* push src1 as argument (for upcoming call) */
    IR_RETURN,          /* return src1 (or void if src1 == -1) */

    /* Object operations */
    IR_ALLOC_OBJ,       /* dest = allocate new object of type extra */
    IR_INIT_VTABLE,     /* initialize vtable for class (extra = class_vtable_idx) */
    IR_SET_VTABLE,      /* set vtable on object: src1 = obj temp (extra = class_vtable_idx) */
    IR_CHECK_VTABLE,    /* dest = isinstance_vtable(src1, vtable[extra]) */
    IR_INCREF,          /* incref src1 */
    IR_DECREF,          /* decref src1 */

    /* Collection builders */
    IR_BUILD_LIST,      /* dest = build list from top 'extra' args */
    IR_BUILD_DICT,      /* dest = build dict from top 'extra' key-value pairs */
    IR_BUILD_TUPLE,     /* dest = build tuple from top 'extra' args */
    IR_BUILD_SET,       /* dest = build set from top 'extra' args */

    /* Iteration */
    IR_GET_ITER,        /* dest = iter(src1) */
    IR_FOR_ITER,        /* dest = next(src1), jump to extra if StopIteration */

    /* Exception handling */
    IR_SETUP_TRY,       /* push try frame, jump to extra on exception */
    IR_POP_TRY,         /* pop try frame */
    IR_RAISE,           /* raise src1 */
    IR_RERAISE,         /* re-raise current exception */
    IR_GET_EXCEPTION,   /* dest = current exception object */

    /* Generators */
    IR_YIELD,           /* yield src1, dest = sent value */
    IR_YIELD_FROM,      /* yield from src1 */

    /* First-class functions */
    IR_MAKE_FUNCTION,   /* dest = create func obj (src1 = name const, extra = func_label const) */

    /* Generator state machine */
    IR_MAKE_GENERATOR,  /* dest = create gen with resume func (src1 = name const, extra = num_locals) */
    IR_MAKE_COROUTINE,  /* dest = create cor with resume func (src1 = name const, extra = num_locals) */
    IR_COR_SET_RESULT,  /* cor->state = src2; src1 = cor temp */
    IR_GEN_LOAD_PC,     /* dest = gen->pc; src1 = gen temp */
    IR_GEN_SET_PC,      /* gen->pc = extra; src1 = gen temp */
    IR_GEN_LOAD_LOCAL,  /* dest = gen->locals[extra]; src1 = gen temp */
    IR_GEN_SAVE_LOCAL,  /* gen->locals[extra] = src2; src1 = gen temp */
    IR_GEN_CHECK_THROW, /* call pydos_gen_check_throw(src1); src1 = gen temp */
    IR_GEN_GET_SENT,    /* dest = pydos_gen_sent global; clear to NULL */

    /* Slicing */
    IR_LOAD_SLICE,      /* dest = src1[src2 : extra_src] (slice operation) */
    IR_EXC_MATCH,       /* dest = check if current exc matches type (src1=exc, src2=type_code) */

    /* Typed i32 arithmetic (unboxed, no runtime call) */
    IR_ADD_I32,         /* dest = src1 + src2 (native 32-bit) */
    IR_SUB_I32,         /* dest = src1 - src2 */
    IR_MUL_I32,         /* dest = src1 * src2 */
    IR_DIV_I32,         /* dest = src1 / src2 */
    IR_MOD_I32,         /* dest = src1 % src2 */
    IR_NEG_I32,         /* dest = -src1 */

    /* Typed i32 comparison (unboxed, result is bool) */
    IR_CMP_I32_EQ,      /* dest = (src1 == src2) */
    IR_CMP_I32_NE,
    IR_CMP_I32_LT,
    IR_CMP_I32_LE,
    IR_CMP_I32_GT,
    IR_CMP_I32_GE,

    /* Box/Unbox conversions (between native values and PyDosObj) */
    IR_BOX_INT,         /* dest:pyobj = pydos_obj_new_int(src1:i32) */
    IR_UNBOX_INT,       /* dest:i32 = src1->v.int_val */
    IR_BOX_BOOL,        /* dest:pyobj = pydos_obj_new_bool(src1:bool) */

    /* String join (batched f-string concatenation) */
    IR_STR_JOIN,        /* dest:pyobj = str_join_n(extra=count, args from PUSH_ARG) */

    /* Cell objects (closures) */
    IR_MAKE_CELL,       /* dest = new cell object */
    IR_CELL_GET,        /* dest = cell->value; src1 = cell temp */
    IR_CELL_SET,        /* cell->value = src2; src1 = cell temp */
    IR_LOAD_CLOSURE,    /* dest = pydos_active_closure */
    IR_SET_CLOSURE,     /* pydos_active_closure = src1 */

    /* Arena scope (bulk deallocation for no-escape objects) */
    IR_SCOPE_ENTER,     /* push arena scope */
    IR_SCOPE_TRACK,     /* track src1 in current arena scope */
    IR_SCOPE_EXIT,      /* pop scope, bulk-free tracked objects */

    /* Misc */
    IR_NOP,             /* no operation */
    IR_COMMENT          /* comment (for debug, src1 = string const index) */
};

/* --------------------------------------------------------------- */
/* Constant pool entry                                              */
/* --------------------------------------------------------------- */
struct IRConst {
    enum ConstKind { CONST_INT, CONST_FLOAT, CONST_STR };
    ConstKind kind;
    union {
        long int_val;
        double float_val;
        struct { const char *data; int len; } str_val;
    };
};

/* --------------------------------------------------------------- */
/* Single IR instruction (doubly-linked list node)                  */
/* --------------------------------------------------------------- */
struct IRInstr {
    IROp        op;
    int         dest;           /* destination temp register (virtual) */
    int         src1;           /* source operand 1 */
    int         src2;           /* source operand 2 */
    int         extra;          /* label id, argc, type tag, etc. */
    TypeInfo   *type_hint;      /* type of dest, used by codegen */
    IRInstr    *next;           /* linked list forward */
    IRInstr    *prev;           /* linked list backward */
};

/* --------------------------------------------------------------- */
/* Local variable info (for frame layout and debugging)             */
/* --------------------------------------------------------------- */
struct IRLocalInfo {
    const char *name;
    TypeInfo   *type;
    int         slot;
};

/* --------------------------------------------------------------- */
/* IR function (one per Python function + one for module init)      */
/* --------------------------------------------------------------- */
struct IRFunc {
    const char  *name;
    IRInstr     *first;         /* first instruction */
    IRInstr     *last;          /* last instruction */
    int          num_temps;     /* count of virtual temp registers used */
    int          num_locals;    /* count of local variables */
    int          num_params;    /* count of parameters */
    TypeInfo    *ret_type;      /* return type */
    IRFunc      *next;          /* linked list of functions */
    int          is_generator;  /* 1 if this function is a generator resume func */
    int          is_coroutine;  /* 1 if this function is a coroutine resume func */

    IRLocalInfo  locals[256];   /* max 256 locals per function */
    int          locals_count;
};

/* --------------------------------------------------------------- */
/* Class vtable info for code generation                            */
/* --------------------------------------------------------------- */
struct ClassMethodEntry {
    const char *python_name;    /* "__add__", "__str__", etc. */
    const char *mangled_name;   /* "Complex____add__" (assembly: _Complex____add__) */
};

struct ClassVTableInfo {
    const char *class_name;
    const char *base_class_name;  /* NULL if no base (first base) */
    const char *extra_bases[7];   /* additional bases for multiple inheritance */
    int num_extra_bases;
    ClassMethodEntry methods[64];
    int num_methods;
};

/* --------------------------------------------------------------- */
/* IR module (top-level container for an entire compilation unit)   */
/* --------------------------------------------------------------- */
struct IRModule {
    IRFunc  *functions;         /* linked list of all functions */
    IRFunc  *init_func;         /* module-level code (runs at startup) */
    IRConst *constants;         /* constant pool */
    int      num_constants;
    int      max_constants;

    ClassVTableInfo class_vtables[32];
    int             num_class_vtables;

    /* Module qualification (for multi-file compilation) */
    const char *module_name;    /* NULL = unqualified (default) */
    int         is_main_module; /* 1 = emit main_ entry point */
    int         has_main_func;  /* 1 = call entry func from main */
    const char *entry_func;     /* name of entry function (--entry) */
};

/* --------------------------------------------------------------- */
/* Debug / dump utilities                                           */
/* --------------------------------------------------------------- */
void        ir_dump(IRModule *mod, FILE *out);
void        ir_dump_func(IRFunc *func, FILE *out);
const char *irop_name(IROp op);

#endif /* IR_H */
