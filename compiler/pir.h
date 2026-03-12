/*
 * pir.h - Python Intermediate Representation (SSA-based)
 *
 * Full SSA IR with basic blocks, phi nodes, and typed values.
 * Sits between Sema/Mono and the existing IROpt/Codegen pipeline.
 * PIRLowerer converts PIR back to flat IR for the existing backends.
 *
 * C++98 compatible, Open Watcom wpp. Uses pdstl.h containers.
 */

#ifndef PIR_H
#define PIR_H

#include "pdstl.h"
#include <stdio.h>

struct TypeInfo; /* forward decl from types.h for type_hint */

/* --------------------------------------------------------------- */
/* PIR type system                                                   */
/* --------------------------------------------------------------- */
enum PIRTypeKind {
    PIR_TYPE_VOID,      /* No value (statements, terminators) */
    PIR_TYPE_PYOBJ,     /* Boxed PyDosObj* (generic Python value) */
    PIR_TYPE_I32,       /* Unboxed 32-bit integer */
    PIR_TYPE_F64,       /* Unboxed 64-bit float */
    PIR_TYPE_BOOL,      /* Unboxed boolean (0 or 1) */
    PIR_TYPE_PTR        /* Raw pointer (for alloca slots) */
};

const char *pir_type_name(PIRTypeKind t);

/* --------------------------------------------------------------- */
/* SSA Value — unique per definition point                           */
/* --------------------------------------------------------------- */
struct PIRValue {
    int id;             /* Globally unique per function (%0, %1, ...) */
    PIRTypeKind type;   /* Type of this value */
};

/* Invalid/absent value sentinel */
#define PIR_NO_VALUE_ID (-1)

inline int pir_value_valid(PIRValue v) { return v.id != PIR_NO_VALUE_ID; }
inline PIRValue pir_value_none() {
    PIRValue v;
    v.id = PIR_NO_VALUE_ID;
    v.type = PIR_TYPE_VOID;
    return v;
}
inline PIRValue pir_value_make(int id, PIRTypeKind t) {
    PIRValue v;
    v.id = id;
    v.type = t;
    return v;
}

/* --------------------------------------------------------------- */
/* PIR opcode enumeration                                            */
/* --------------------------------------------------------------- */
enum PIROp {
    /* Constants */
    PIR_CONST_INT,          /* result:i32 = int_val */
    PIR_CONST_FLOAT,        /* result:f64 = (double from const pool) */
    PIR_CONST_BOOL,         /* result:bool = int_val (0 or 1) */
    PIR_CONST_STR,          /* result:pyobj = str_val */
    PIR_CONST_NONE,         /* result:pyobj = None */

    /* Variables (pre-mem2reg: alloca/load/store) */
    PIR_ALLOCA,             /* result:ptr = stack slot for local variable */
    PIR_LOAD,               /* result:pyobj = *operands[0] (load from alloca) */
    PIR_STORE,              /* *operands[0] = operands[1] (store to alloca) */
    PIR_LOAD_GLOBAL,        /* result:pyobj = global[str_val] */
    PIR_STORE_GLOBAL,       /* global[str_val] = operands[0] */

    /* Generic Python arithmetic (boxed PyDosObj) */
    PIR_PY_ADD,             /* result:pyobj = operands[0] + operands[1] */
    PIR_PY_SUB,
    PIR_PY_MUL,
    PIR_PY_DIV,
    PIR_PY_FLOORDIV,
    PIR_PY_MOD,
    PIR_PY_POW,
    PIR_PY_MATMUL,          /* result:pyobj = operands[0] @ operands[1] */
    PIR_PY_INPLACE,         /* result:pyobj = operands[0] iop operands[1]; int_val = op */
    PIR_PY_NEG,             /* result:pyobj = -operands[0] */
    PIR_PY_POS,             /* result:pyobj = +operands[0] */
    PIR_PY_NOT,             /* result:pyobj = not operands[0] (logical) */

    /* Typed i32 arithmetic (unboxed) */
    PIR_ADD_I32,
    PIR_SUB_I32,
    PIR_MUL_I32,
    PIR_DIV_I32,
    PIR_MOD_I32,
    PIR_NEG_I32,

    /* Typed f64 arithmetic (unboxed) */
    PIR_ADD_F64,
    PIR_SUB_F64,
    PIR_MUL_F64,
    PIR_DIV_F64,
    PIR_NEG_F64,

    /* Generic Python comparison (boxed) */
    PIR_PY_CMP_EQ,
    PIR_PY_CMP_NE,
    PIR_PY_CMP_LT,
    PIR_PY_CMP_LE,
    PIR_PY_CMP_GT,
    PIR_PY_CMP_GE,

    /* Typed i32 comparison (unboxed, result:bool) */
    PIR_CMP_I32_EQ,
    PIR_CMP_I32_NE,
    PIR_CMP_I32_LT,
    PIR_CMP_I32_LE,
    PIR_CMP_I32_GT,
    PIR_CMP_I32_GE,

    /* Bitwise (boxed PyDosObj) */
    PIR_PY_BIT_AND,
    PIR_PY_BIT_OR,
    PIR_PY_BIT_XOR,
    PIR_PY_BIT_NOT,
    PIR_PY_LSHIFT,
    PIR_PY_RSHIFT,

    /* Boolean operations */
    PIR_PY_IS,              /* result:pyobj = operands[0] is operands[1] */
    PIR_PY_IS_NOT,
    PIR_PY_IN,              /* result:pyobj = operands[0] in operands[1] */
    PIR_PY_NOT_IN,

    /* Box/Unbox conversions */
    PIR_BOX_INT,            /* result:pyobj = box(operands[0]:i32) */
    PIR_BOX_FLOAT,          /* result:pyobj = box(operands[0]:f64) */
    PIR_BOX_BOOL,           /* result:pyobj = box(operands[0]:bool) */
    PIR_UNBOX_INT,          /* result:i32 = unbox(operands[0]:pyobj) */
    PIR_UNBOX_FLOAT,        /* result:f64 = unbox(operands[0]:pyobj) */

    /* Collection builders */
    PIR_LIST_NEW,           /* result:pyobj = [] (empty list) */
    PIR_LIST_APPEND,        /* list_append(operands[0], operands[1]) */
    PIR_DICT_NEW,           /* result:pyobj = {} (empty dict) */
    PIR_DICT_SET,           /* dict_set(operands[0], operands[1], operands[2]) */
    PIR_TUPLE_NEW,          /* result:pyobj = tuple(int_val elements) */
    PIR_TUPLE_SET,          /* tuple_set(operands[0], int_val=index, operands[1]) */
    PIR_SET_NEW,            /* result:pyobj = set() */
    PIR_SET_ADD,            /* set_add(operands[0], operands[1]) */
    PIR_BUILD_LIST,         /* result:pyobj = build from int_val pushed args */
    PIR_BUILD_DICT,         /* result:pyobj = build from int_val key-val pairs */
    PIR_BUILD_TUPLE,        /* result:pyobj = build from int_val pushed args */
    PIR_BUILD_SET,          /* result:pyobj = build from int_val pushed args */

    /* Subscript/attribute access */
    PIR_SUBSCR_GET,         /* result:pyobj = operands[0][operands[1]] */
    PIR_SUBSCR_SET,         /* operands[0][operands[1]] = operands[2] */
    PIR_DEL_SUBSCR,         /* del operands[0][operands[1]] */
    PIR_SLICE,              /* result:pyobj = operands[0][operands[1]:operands[2]] */
    PIR_GET_ATTR,           /* result:pyobj = operands[0].str_val */
    PIR_SET_ATTR,           /* operands[0].str_val = operands[1] */
    PIR_DEL_ATTR,           /* del operands[0].str_val */
    PIR_DEL_NAME,           /* del local: operands[0]=alloca, str_val=name */
    PIR_DEL_GLOBAL,         /* del global[str_val] */

    /* Strings */
    PIR_STR_CONCAT,         /* result:pyobj = str_concat(operands[0], operands[1]) */
    PIR_STR_FORMAT,         /* result:pyobj = format(operands[0]) */
    PIR_STR_JOIN,           /* result:pyobj = join(int_val parts from PUSH_ARG) */

    /* Calls */
    PIR_PUSH_ARG,           /* push operands[0] as argument (before call) */
    PIR_CALL,               /* result:pyobj = call str_val(int_val args) */
    PIR_CALL_METHOD,        /* result:pyobj = operands[0].str_val(int_val args) */

    /* Control flow */
    PIR_BRANCH,             /* goto target_block */
    PIR_COND_BRANCH,        /* if operands[0]: goto true_block else goto false_block */
    PIR_RETURN,             /* return operands[0] */
    PIR_RETURN_NONE,        /* return None */

    /* SSA */
    PIR_PHI,                /* result = phi [val, pred_block], ... */

    /* Object operations */
    PIR_ALLOC_OBJ,          /* result:pyobj = new object of type int_val */
    PIR_INIT_VTABLE,        /* init vtable (int_val = class_vtable_idx) */
    PIR_SET_VTABLE,         /* set vtable on operands[0] (int_val = idx) */
    PIR_CHECK_VTABLE,       /* result:pyobj = isinstance_vtable(operands[0], vtable[int_val]) */
    PIR_INCREF,             /* incref operands[0] */
    PIR_DECREF,             /* decref operands[0] */

    /* Functions */
    PIR_MAKE_FUNCTION,      /* result:pyobj = make_func(str_val=name, int_val=const) */
    PIR_MAKE_GENERATOR,     /* result:pyobj = make_gen(str_val=name, int_val=num_locals) */
    PIR_MAKE_COROUTINE,     /* result:pyobj = make_cor(str_val=name, int_val=num_locals) */
    PIR_COR_SET_RESULT,     /* gen->state = operands[1]; operands[0] = gen */

    /* Exception handling */
    PIR_SETUP_TRY,          /* push try frame, branch to handler on exception */
    PIR_POP_TRY,            /* pop try frame */
    PIR_RAISE,              /* raise operands[0] */
    PIR_RERAISE,            /* re-raise current exception */
    PIR_GET_EXCEPTION,      /* result:pyobj = current exception */
    PIR_EXC_MATCH,          /* result:pyobj = exc_match(operands[0], operands[1]) */

    /* Iteration */
    PIR_GET_ITER,           /* result:pyobj = iter(operands[0]) */
    PIR_FOR_ITER,           /* result:pyobj = next(operands[0]), branch on StopIteration */

    /* Generators */
    PIR_GEN_LOAD_PC,        /* result:i32 = gen->pc; operands[0] = gen */
    PIR_GEN_SET_PC,         /* gen->pc = int_val; operands[0] = gen */
    PIR_GEN_LOAD_LOCAL,     /* result:pyobj = gen->locals[int_val]; operands[0] = gen */
    PIR_GEN_SAVE_LOCAL,     /* gen->locals[int_val] = operands[1]; operands[0] = gen */
    PIR_YIELD,              /* yield operands[0], result = sent value */
    PIR_GEN_CHECK_THROW,    /* check throw flag on gen; operands[0] = gen */
    PIR_GEN_GET_SENT,       /* result:pyobj = sent value; operands[0] = gen */

    /* Cell objects (closures) */
    PIR_MAKE_CELL,          /* result:pyobj = new empty cell */
    PIR_CELL_GET,           /* result:pyobj = operands[0]->value (cell deref) */
    PIR_CELL_SET,           /* operands[0]->value = operands[1] (cell store) */
    PIR_LOAD_CLOSURE,       /* result:pyobj = pydos_active_closure */
    PIR_SET_CLOSURE,        /* pydos_active_closure = operands[0] */

    /* GC Scope */
    PIR_SCOPE_ENTER,        /* enter GC scope */
    PIR_SCOPE_TRACK,        /* track operands[0] in current scope */
    PIR_SCOPE_EXIT,         /* exit GC scope (bulk DECREF) */

    /* Import */
    PIR_IMPORT,             /* result:pyobj = import(str_val) */
    PIR_IMPORT_FROM,        /* result:pyobj = import_from(operands[0], str_val) */

    /* Misc */
    PIR_NOP,
    PIR_COMMENT,            /* str_val = comment text */

    PIR_OP_COUNT            /* sentinel — number of opcodes */
};

const char *pir_op_name(PIROp op);

/* --------------------------------------------------------------- */
/* Phi node entry                                                    */
/* --------------------------------------------------------------- */
struct PIRBlock; /* forward decl */

struct PIRPhiEntry {
    PIRValue value;
    PIRBlock *block;
};

/* --------------------------------------------------------------- */
/* PIR instruction                                                   */
/* --------------------------------------------------------------- */
struct PIRInst {
    PIROp op;
    PIRValue result;            /* SSA result value ({-1, VOID} if none) */

    PIRValue operands[3];       /* Fixed operands (most instructions use 0-3) */
    int num_operands;

    /* Variable-length data (mutually exclusive via union) */
    union {
        struct { PIRPhiEntry *entries; int count; } phi;
    } extra;

    long int_val;               /* Integer immediate (must be long for 16-bit DOS) */
    const char *str_val;        /* String immediate */

    /* Control flow targets (for terminators) */
    PIRBlock *target_block;     /* branch target / true target */
    PIRBlock *false_block;      /* cond_branch false target */
    PIRBlock *handler_block;    /* exception handler for SETUP_TRY / FOR_ITER */

    PIRInst *next;              /* Linked list within block */
    PIRInst *prev;

    int line;                   /* Source line number (for debug) */
    TypeInfo *type_hint;        /* Semantic type, propagated to flat IR */
};

/* --------------------------------------------------------------- */
/* Basic block — node in the CFG                                     */
/* --------------------------------------------------------------- */
struct PIRBlock {
    int id;
    const char *label;          /* Optional name (e.g. "entry", "then", "else") */

    PIRInst *first;
    PIRInst *last;
    int inst_count;

    PdVector<PIRBlock *> preds; /* Predecessor blocks */
    PdVector<PIRBlock *> succs; /* Successor blocks */

    int sealed;                 /* All predecessors are known */
    int filled;                 /* Block has a terminator instruction */
};

/* --------------------------------------------------------------- */
/* Type inference results (populated by pirtyp.cpp)                  */
/* --------------------------------------------------------------- */
struct ValueTypeInfo {
    int value_id;
    int proven_type;       /* TY_INT, TY_FLOAT, TY_STR, TY_BOOL, TY_NONE, -1=unknown */
    int is_constant;       /* 1 if value is provably constant */
};

struct FuncTypeResult {
    ValueTypeInfo *values;  /* Array indexed by value_id */
    int count;              /* Size of values array (== next_value_id) */
};

/* --------------------------------------------------------------- */
/* Escape analysis results (populated by piresc.cpp)                 */
/* --------------------------------------------------------------- */
enum EscapeKind {
    ESC_NO_ESCAPE    = 0,   /* Value stays local to function */
    ESC_ARG_ESCAPE   = 1,   /* Value escapes via argument to a call */
    ESC_GLOBAL_ESCAPE = 2   /* Value escapes via return, global store, or container */
};

struct ValueEscapeInfo {
    int value_id;
    int escape;             /* EscapeKind */
};

struct FuncEscapeResult {
    ValueEscapeInfo *values; /* Array indexed by value_id */
    int count;               /* Size of values array (== next_value_id) */
    int can_use_arena;       /* 1 if function has NoEscape locals */
};

/* --------------------------------------------------------------- */
/* PIR function                                                      */
/* --------------------------------------------------------------- */
struct PIRFunction {
    const char *name;

    PdVector<PIRBlock *> blocks;
    PIRBlock *entry_block;

    int next_value_id;
    int next_block_id;

    PdVector<PIRValue> params;  /* Function parameters */
    int num_locals;             /* For lowerer's local slot allocation */
    int num_params;
    int is_generator;
    int is_coroutine;

    /* Analysis results (attached by passes, may be 0) */
    struct FuncTypeResult *type_info;
    struct FuncEscapeResult *escape_info;
};

/* --------------------------------------------------------------- */
/* PIR vtable info (mirrors ClassVTableInfo in ir.h)                 */
/* --------------------------------------------------------------- */
struct PIRVTableMethod {
    const char *python_name;    /* "speak", "__init__" */
    const char *mangled_name;   /* "Dog__speak", "Dog____init__" */
};

struct PIRVTableInfo {
    const char *class_name;
    const char *base_class_name;    /* 0 if no base */
    const char *extra_bases[7];
    int num_extra_bases;
    PIRVTableMethod methods[64];
    int num_methods;
};

/* --------------------------------------------------------------- */
/* PIR module (top-level container)                                  */
/* --------------------------------------------------------------- */
struct PIRModule {
    PdVector<PIRFunction *> functions;
    PIRFunction *init_func;     /* Module-level init code */

    /* Constant pool (shared with lowering to IRModule) */
    PdVector<const char *> string_constants;

    /* Class vtable info (populated by PIRBuilder, used by lowerer) */
    PIRVTableInfo vtables[32];
    int num_vtables;

    /* Module qualification (for multi-file compilation) */
    const char *module_name;    /* NULL = unqualified (default) */
    int is_main_module;         /* 1 = emit main_ entry point */
    int has_main_func;          /* 1 = call entry func from main */
    const char *entry_func;     /* name of entry function (--entry) */
};

/* --------------------------------------------------------------- */
/* Allocation / deallocation                                         */
/* --------------------------------------------------------------- */
PIRModule    *pir_module_new();
void          pir_module_free(PIRModule *mod);

PIRFunction  *pir_func_new(const char *name);
void          pir_func_free(PIRFunction *func);
PIRBlock     *pir_block_new(PIRFunction *func, const char *label);

PIRInst      *pir_inst_new(PIROp op);
void          pir_block_append(PIRBlock *block, PIRInst *inst);

/* Add edge to CFG */
void          pir_block_add_edge(PIRBlock *from, PIRBlock *to);

/* Allocate a new SSA value ID within a function */
PIRValue      pir_func_alloc_value(PIRFunction *func, PIRTypeKind type);

/* --------------------------------------------------------------- */
/* Debug utilities                                                   */
/* --------------------------------------------------------------- */
void pir_dump_module(PIRModule *mod, FILE *out);

#endif /* PIR_H */
