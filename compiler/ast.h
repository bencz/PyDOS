#ifndef AST_H
#define AST_H

/*
 * ast.h - AST node hierarchy for PyDOS Python-to-8086 compiler
 *
 * Tagged-union style AST with arena allocation. Supports the full
 * Python 3.11+ statement and expression syntax relevant to compilation.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "token.h"

/* ------------------------------------------------------------------ */
/* Forward declaration                                                 */
/* ------------------------------------------------------------------ */

struct ASTNode;
struct Param;

/* ------------------------------------------------------------------ */
/* AST node kind                                                       */
/* ------------------------------------------------------------------ */

enum ASTKind {
    /* Statements */
    AST_MODULE,
    AST_FUNC_DEF,
    AST_CLASS_DEF,
    AST_RETURN,
    AST_ASSIGN,
    AST_ANN_ASSIGN,
    AST_AUG_ASSIGN,
    AST_IF,
    AST_WHILE,
    AST_FOR,
    AST_BREAK,
    AST_CONTINUE,
    AST_PASS,
    AST_EXPR_STMT,
    AST_TRY,
    AST_EXCEPT_HANDLER,
    AST_RAISE,
    AST_WITH,
    AST_WITH_ITEM,
    AST_MATCH,
    AST_MATCH_CASE,
    AST_IMPORT,
    AST_IMPORT_FROM,
    AST_IMPORT_NAME,
    AST_YIELD_STMT,
    AST_DELETE,
    AST_GLOBAL,
    AST_NONLOCAL,
    AST_ASSERT,
    AST_TYPE_ALIAS,

    /* Expressions */
    AST_BINOP,
    AST_UNARYOP,
    AST_COMPARE,
    AST_BOOLOP,
    AST_CALL,
    AST_KEYWORD_ARG,
    AST_ATTR,
    AST_SUBSCRIPT,
    AST_SLICE,
    AST_NAME,
    AST_INT_LIT,
    AST_FLOAT_LIT,
    AST_STR_LIT,
    AST_FSTRING,
    AST_BOOL_LIT,
    AST_NONE_LIT,
    AST_LIST_EXPR,
    AST_DICT_EXPR,
    AST_TUPLE_EXPR,
    AST_SET_EXPR,
    AST_LISTCOMP,
    AST_DICTCOMP,
    AST_SETCOMP,
    AST_GENEXPR,
    AST_COMP_GENERATOR,
    AST_LAMBDA,
    AST_IFEXPR,
    AST_WALRUS,
    AST_STARRED,
    AST_AWAIT,
    AST_COMPLEX_LIT,
    AST_YIELD_EXPR,
    AST_YIELD_FROM_EXPR,

    /* Type annotations */
    AST_TYPE_NAME,
    AST_TYPE_GENERIC,
    AST_TYPE_UNION,
    AST_TYPE_OPTIONAL,
    AST_TYPE_TUPLE,
    AST_TYPE_CALLABLE
};

/* ------------------------------------------------------------------ */
/* Operator enumerations                                               */
/* ------------------------------------------------------------------ */

enum BinOp {
    OP_ADD,
    OP_SUB,
    OP_MUL,
    OP_DIV,
    OP_FLOORDIV,
    OP_MOD,
    OP_POW,
    OP_LSHIFT,
    OP_RSHIFT,
    OP_BITOR,
    OP_BITXOR,
    OP_BITAND,
    OP_MATMUL
};

enum UnaryOp {
    UNARY_NEG,
    UNARY_POS,
    UNARY_NOT,
    UNARY_BITNOT
};

enum CmpOp {
    CMP_EQ,
    CMP_NE,
    CMP_LT,
    CMP_LE,
    CMP_GT,
    CMP_GE,
    CMP_IS,
    CMP_IS_NOT,
    CMP_IN,
    CMP_NOT_IN
};

enum BoolOp {
    BOOL_AND,
    BOOL_OR
};

/* ------------------------------------------------------------------ */
/* Parameter struct (for function definitions and lambdas)             */
/* ------------------------------------------------------------------ */

struct Param {
    const char *name;
    ASTNode *annotation;     /* type annotation (may be NULL) */
    ASTNode *default_val;    /* default value or NULL */
    int is_star;             /* 1 if *args */
    int is_double_star;      /* 1 if **kwargs */
    int is_positional_only;  /* 1 if param appears before / separator */
    Param *next;
};

/* ------------------------------------------------------------------ */
/* ASTNode struct                                                      */
/* ------------------------------------------------------------------ */

struct ASTNode {
    ASTKind kind;
    int line;
    int col;
    ASTNode *next;           /* sibling linked list */

    union {
        /* Module */
        struct { ASTNode *body; } module;

        /* Function definition */
        struct {
            const char *name;
            Param *params;
            ASTNode *return_type;    /* type annotation or NULL */
            ASTNode *body;
            ASTNode *decorators;     /* linked list of decorator expressions */
            int is_async;
            const char **type_param_names; /* PEP 695 type params e.g. ["T"] */
            int num_type_params;
            /* Closure info (populated by sema) */
            const char **cell_var_names;  /* vars captured by inner funcs */
            int num_cell_vars;
            const char **free_var_names;  /* nonlocal vars from outer scope */
            int num_free_vars;
        } func_def;

        /* Class definition */
        struct {
            const char *name;
            ASTNode *bases;          /* linked list of base class expressions */
            ASTNode *body;
            ASTNode *decorators;
            const char **type_param_names; /* type parameter names e.g. ["T"] */
            int num_type_params;
        } class_def;

        /* Return statement */
        struct { ASTNode *value; } ret;

        /* Assignment: targets = value */
        struct { ASTNode *targets; ASTNode *value; } assign;

        /* Annotated assignment: target: annotation = value */
        struct { ASTNode *target; ASTNode *annotation; ASTNode *value; } ann_assign;

        /* Augmented assignment: target op= value */
        struct { ASTNode *target; BinOp op; ASTNode *value; } aug_assign;

        /* If / elif / else */
        struct { ASTNode *condition; ASTNode *body; ASTNode *else_body; } if_stmt;

        /* While */
        struct { ASTNode *condition; ASTNode *body; ASTNode *else_body; } while_stmt;

        /* For */
        struct { ASTNode *target; ASTNode *iter; ASTNode *body; ASTNode *else_body; } for_stmt;

        /* Expression statement */
        struct { ASTNode *expr; } expr_stmt;

        /* Try / except / else / finally */
        struct { ASTNode *body; ASTNode *handlers; ASTNode *else_body; ASTNode *finally_body; } try_stmt;

        /* Exception handler */
        struct { ASTNode *type; const char *name; ASTNode *body; int is_star; } handler;

        /* Raise */
        struct { ASTNode *exc; ASTNode *cause; } raise_stmt;

        /* With statement */
        struct { ASTNode *items; ASTNode *body; } with_stmt;

        /* With item */
        struct { ASTNode *context_expr; ASTNode *optional_vars; } with_item;

        /* Import */
        struct { const char *module; const char *alias; } import_stmt;

        /* Import from */
        struct { const char *module; ASTNode *names; } import_from;

        /* Import name (used inside import_from) */
        struct { const char *imported_name; const char *alias; } import_name;

        /* Match statement */
        struct { ASTNode *subject; ASTNode *cases; } match_stmt;

        /* Match case */
        struct { ASTNode *pattern; ASTNode *guard; ASTNode *body; } match_case;

        /* Delete */
        struct { ASTNode *targets; } del_stmt;

        /* Global / nonlocal */
        struct { const char **names; int num_names; } global_stmt;

        /* Assert */
        struct { ASTNode *test; ASTNode *msg; } assert_stmt;

        /* Type alias: type X = Y */
        struct {
            const char *name;
            ASTNode *value;             /* RHS type expression */
            const char **type_param_names;  /* [T, U, ...] or NULL */
            int num_type_params;
        } type_alias;

        /* Binary operation */
        struct { BinOp op; ASTNode *left; ASTNode *right; } binop;

        /* Unary operation */
        struct { UnaryOp op; ASTNode *operand; } unaryop;

        /* Comparison (chained: a < b < c) */
        struct { ASTNode *left; CmpOp *ops; ASTNode *comparators; int num_ops; } compare;

        /* Boolean operation */
        struct { BoolOp op; ASTNode *values; } boolop;

        /* Function call */
        struct { ASTNode *func; ASTNode *args; int num_args; } call;

        /* Keyword argument in a call */
        struct { const char *key; ASTNode *kw_value; } keyword_arg;

        /* Attribute access: object.attr */
        struct { ASTNode *object; const char *attr; } attribute;

        /* Subscript: object[index] */
        struct { ASTNode *object; ASTNode *index; } subscript;

        /* Slice: lower:upper:step */
        struct { ASTNode *lower; ASTNode *upper; ASTNode *step; } slice;

        /* Name (variable reference) */
        struct { const char *id; } name;

        /* Integer literal */
        struct { long value; } int_lit;

        /* Float literal */
        struct { double value; } float_lit;

        /* Complex literal (imaginary part only; real part added via binop) */
        struct { double imag; } complex_lit;

        /* String literal */
        struct { const char *value; int len; } str_lit;

        /* F-string */
        struct { ASTNode *parts; } fstring;

        /* Boolean literal */
        struct { int value; } bool_lit;

        /* List / tuple / set expression */
        struct { ASTNode *elts; } collection;

        /* Dict expression */
        struct { ASTNode *keys; ASTNode *values; } dict;

        /* List/set comprehension */
        struct { ASTNode *elt; ASTNode *generators; } listcomp;

        /* Dict comprehension */
        struct { ASTNode *key; ASTNode *value; ASTNode *generators; } dictcomp;

        /* Comprehension generator (for ... in ... if ...) */
        struct { ASTNode *target; ASTNode *iter; ASTNode *ifs; int is_async; } comp_gen;

        /* Lambda */
        struct { Param *params; ASTNode *body; } lambda;

        /* Conditional expression: body if test else else_body */
        struct { ASTNode *body; ASTNode *test; ASTNode *else_body; } ifexpr;

        /* Walrus operator := */
        struct { ASTNode *target; ASTNode *value; } walrus;

        /* Starred expression *expr */
        struct { ASTNode *value; } starred;

        /* Yield expression */
        struct { ASTNode *value; int is_from; } yield_expr;

        /* Type name annotation */
        struct { const char *tname; } type_name;

        /* Generic type: list[int], dict[str, int] */
        struct { const char *gname; ASTNode *type_args; int num_type_args; } type_generic;

        /* Union type: int | str */
        struct { ASTNode *types; } type_union;
    } data;
};

/* ------------------------------------------------------------------ */
/* AST allocation and lifecycle                                        */
/* ------------------------------------------------------------------ */

/* Allocate a new AST node. All nodes are tracked in an internal arena. */
ASTNode *ast_alloc(ASTKind kind, int line, int col);

/* Allocate a Param struct */
Param *param_alloc();

/* Allocate an array of CmpOp */
CmpOp *cmpop_alloc(int count);

/* Allocate an array of const char* */
const char **name_array_alloc(int count);

/* Free a single node and all its children recursively */
void ast_free(ASTNode *node);

/* Free all allocated AST nodes at once (arena reset) */
void ast_free_all();

/* ------------------------------------------------------------------ */
/* Debug dumping                                                       */
/* ------------------------------------------------------------------ */

/* Print AST tree to stdout with indentation */
void ast_dump(ASTNode *node, int indent);

/* Return human-readable name for an ASTKind */
const char *ast_kind_name(ASTKind kind);

/* Return human-readable name for a BinOp */
const char *binop_name(BinOp op);

/* Return human-readable name for a CmpOp */
const char *cmpop_name(CmpOp op);

#endif /* AST_H */
