/*
 * ast.cpp - AST allocation, deallocation, and debug dumping
 *
 * Uses a simple arena allocator: all AST nodes are tracked via a linked
 * list of allocation records. ast_free_all() releases everything at once.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Arena allocator                                                     */
/* ------------------------------------------------------------------ */

struct ArenaBlock {
    void *ptr;
    ArenaBlock *next;
};

static ArenaBlock *arena_head = NULL;

static void *arena_malloc(int size) {
    void *p = malloc(size);
    if (!p) {
        fprintf(stderr, "Fatal: out of memory allocating %d bytes\n", size);
        exit(1);
    }
    memset(p, 0, size);
    ArenaBlock *blk = (ArenaBlock *)malloc(sizeof(ArenaBlock));
    if (!blk) {
        fprintf(stderr, "Fatal: out of memory for arena block\n");
        free(p);
        exit(1);
    }
    blk->ptr = p;
    blk->next = arena_head;
    arena_head = blk;
    return p;
}

/* ------------------------------------------------------------------ */
/* ast_alloc                                                           */
/* ------------------------------------------------------------------ */

ASTNode *ast_alloc(ASTKind kind, int line, int col) {
    ASTNode *node = (ASTNode *)arena_malloc(sizeof(ASTNode));
    node->kind = kind;
    node->line = line;
    node->col = col;
    node->next = NULL;
    return node;
}

/* ------------------------------------------------------------------ */
/* param_alloc                                                         */
/* ------------------------------------------------------------------ */

Param *param_alloc() {
    Param *p = (Param *)arena_malloc(sizeof(Param));
    return p;
}

/* ------------------------------------------------------------------ */
/* cmpop_alloc                                                         */
/* ------------------------------------------------------------------ */

CmpOp *cmpop_alloc(int count) {
    CmpOp *ops = (CmpOp *)arena_malloc(sizeof(CmpOp) * count);
    return ops;
}

/* ------------------------------------------------------------------ */
/* name_array_alloc                                                    */
/* ------------------------------------------------------------------ */

const char **name_array_alloc(int count) {
    const char **arr = (const char **)arena_malloc(sizeof(const char *) * count);
    return arr;
}

/* ------------------------------------------------------------------ */
/* ast_free - recursive free (no-op with arena, but clears linkage)    */
/* ------------------------------------------------------------------ */

void ast_free(ASTNode *node) {
    /* With arena allocation, individual free is a no-op.
       ast_free_all() handles bulk deallocation. */
    (void)node;
}

/* ------------------------------------------------------------------ */
/* ast_free_all - release entire arena                                 */
/* ------------------------------------------------------------------ */

void ast_free_all() {
    ArenaBlock *blk = arena_head;
    while (blk) {
        ArenaBlock *next = blk->next;
        free(blk->ptr);
        free(blk);
        blk = next;
    }
    arena_head = NULL;
}

/* ------------------------------------------------------------------ */
/* ast_kind_name                                                       */
/* ------------------------------------------------------------------ */

const char *ast_kind_name(ASTKind kind) {
    switch (kind) {
    case AST_MODULE:         return "Module";
    case AST_FUNC_DEF:       return "FuncDef";
    case AST_CLASS_DEF:      return "ClassDef";
    case AST_RETURN:         return "Return";
    case AST_ASSIGN:         return "Assign";
    case AST_ANN_ASSIGN:     return "AnnAssign";
    case AST_AUG_ASSIGN:     return "AugAssign";
    case AST_IF:             return "If";
    case AST_WHILE:          return "While";
    case AST_FOR:            return "For";
    case AST_BREAK:          return "Break";
    case AST_CONTINUE:       return "Continue";
    case AST_PASS:           return "Pass";
    case AST_EXPR_STMT:      return "ExprStmt";
    case AST_TRY:            return "Try";
    case AST_EXCEPT_HANDLER: return "ExceptHandler";
    case AST_RAISE:          return "Raise";
    case AST_WITH:           return "With";
    case AST_WITH_ITEM:      return "WithItem";
    case AST_MATCH:          return "Match";
    case AST_MATCH_CASE:     return "MatchCase";
    case AST_IMPORT:         return "Import";
    case AST_IMPORT_FROM:    return "ImportFrom";
    case AST_IMPORT_NAME:    return "ImportName";
    case AST_YIELD_STMT:     return "YieldStmt";
    case AST_DELETE:         return "Delete";
    case AST_GLOBAL:         return "Global";
    case AST_NONLOCAL:       return "Nonlocal";
    case AST_ASSERT:         return "Assert";
    case AST_TYPE_ALIAS:     return "TypeAlias";
    case AST_BINOP:          return "BinOp";
    case AST_UNARYOP:        return "UnaryOp";
    case AST_COMPARE:        return "Compare";
    case AST_BOOLOP:         return "BoolOp";
    case AST_CALL:           return "Call";
    case AST_KEYWORD_ARG:    return "KeywordArg";
    case AST_ATTR:           return "Attribute";
    case AST_SUBSCRIPT:      return "Subscript";
    case AST_SLICE:          return "Slice";
    case AST_NAME:           return "Name";
    case AST_INT_LIT:        return "IntLit";
    case AST_FLOAT_LIT:      return "FloatLit";
    case AST_COMPLEX_LIT:    return "ComplexLit";
    case AST_STR_LIT:        return "StrLit";
    case AST_FSTRING:        return "FString";
    case AST_BOOL_LIT:       return "BoolLit";
    case AST_NONE_LIT:       return "NoneLit";
    case AST_LIST_EXPR:      return "ListExpr";
    case AST_DICT_EXPR:      return "DictExpr";
    case AST_TUPLE_EXPR:     return "TupleExpr";
    case AST_SET_EXPR:       return "SetExpr";
    case AST_LISTCOMP:       return "ListComp";
    case AST_DICTCOMP:       return "DictComp";
    case AST_SETCOMP:        return "SetComp";
    case AST_GENEXPR:        return "GenExpr";
    case AST_COMP_GENERATOR: return "CompGenerator";
    case AST_LAMBDA:         return "Lambda";
    case AST_IFEXPR:         return "IfExpr";
    case AST_WALRUS:         return "Walrus";
    case AST_STARRED:        return "Starred";
    case AST_AWAIT:          return "Await";
    case AST_YIELD_EXPR:     return "YieldExpr";
    case AST_YIELD_FROM_EXPR:return "YieldFromExpr";
    case AST_TYPE_NAME:      return "TypeName";
    case AST_TYPE_GENERIC:   return "TypeGeneric";
    case AST_TYPE_UNION:     return "TypeUnion";
    case AST_TYPE_OPTIONAL:  return "TypeOptional";
    case AST_TYPE_TUPLE:     return "TypeTuple";
    case AST_TYPE_CALLABLE:  return "TypeCallable";
    }
    return "Unknown";
}

/* ------------------------------------------------------------------ */
/* binop_name                                                          */
/* ------------------------------------------------------------------ */

const char *binop_name(BinOp op) {
    switch (op) {
    case OP_ADD:      return "+";
    case OP_SUB:      return "-";
    case OP_MUL:      return "*";
    case OP_DIV:      return "/";
    case OP_FLOORDIV: return "//";
    case OP_MOD:      return "%";
    case OP_POW:      return "**";
    case OP_LSHIFT:   return "<<";
    case OP_RSHIFT:   return ">>";
    case OP_BITOR:    return "|";
    case OP_BITXOR:   return "^";
    case OP_BITAND:   return "&";
    case OP_MATMUL:   return "@";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* cmpop_name                                                          */
/* ------------------------------------------------------------------ */

const char *cmpop_name(CmpOp op) {
    switch (op) {
    case CMP_EQ:      return "==";
    case CMP_NE:      return "!=";
    case CMP_LT:      return "<";
    case CMP_LE:      return "<=";
    case CMP_GT:      return ">";
    case CMP_GE:      return ">=";
    case CMP_IS:      return "is";
    case CMP_IS_NOT:  return "is not";
    case CMP_IN:      return "in";
    case CMP_NOT_IN:  return "not in";
    }
    return "?";
}

/* ------------------------------------------------------------------ */
/* Indentation helper                                                  */
/* ------------------------------------------------------------------ */

static void print_indent(int indent) {
    int i;
    for (i = 0; i < indent; i++) {
        printf("  ");
    }
}

/* ------------------------------------------------------------------ */
/* ast_dump - recursive AST printer                                    */
/* ------------------------------------------------------------------ */

void ast_dump(ASTNode *node, int indent) {
    if (!node) return;

    print_indent(indent);
    printf("%s", ast_kind_name(node->kind));
    printf(" [line %d, col %d]", node->line, node->col);

    switch (node->kind) {
    case AST_MODULE:
        printf("\n");
        ast_dump(node->data.module.body, indent + 1);
        break;

    case AST_FUNC_DEF:
        printf(" name='%s'", node->data.func_def.name ? node->data.func_def.name : "?");
        if (node->data.func_def.is_async) printf(" async");
        printf("\n");
        if (node->data.func_def.decorators) {
            print_indent(indent + 1);
            printf("decorators:\n");
            ast_dump(node->data.func_def.decorators, indent + 2);
        }
        {
            Param *p = node->data.func_def.params;
            if (p) {
                print_indent(indent + 1);
                printf("params:\n");
                while (p) {
                    print_indent(indent + 2);
                    printf("'%s'", p->name ? p->name : "?");
                    if (p->is_star) printf(" (*args)");
                    if (p->is_double_star) printf(" (**kwargs)");
                    printf("\n");
                    if (p->annotation) {
                        print_indent(indent + 3);
                        printf("annotation:\n");
                        ast_dump(p->annotation, indent + 4);
                    }
                    if (p->default_val) {
                        print_indent(indent + 3);
                        printf("default:\n");
                        ast_dump(p->default_val, indent + 4);
                    }
                    p = p->next;
                }
            }
        }
        if (node->data.func_def.return_type) {
            print_indent(indent + 1);
            printf("return_type:\n");
            ast_dump(node->data.func_def.return_type, indent + 2);
        }
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.func_def.body, indent + 2);
        break;

    case AST_CLASS_DEF:
        printf(" name='%s'\n", node->data.class_def.name ? node->data.class_def.name : "?");
        if (node->data.class_def.decorators) {
            print_indent(indent + 1);
            printf("decorators:\n");
            ast_dump(node->data.class_def.decorators, indent + 2);
        }
        if (node->data.class_def.bases) {
            print_indent(indent + 1);
            printf("bases:\n");
            ast_dump(node->data.class_def.bases, indent + 2);
        }
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.class_def.body, indent + 2);
        break;

    case AST_RETURN:
        printf("\n");
        if (node->data.ret.value) {
            ast_dump(node->data.ret.value, indent + 1);
        }
        break;

    case AST_ASSIGN:
        printf("\n");
        print_indent(indent + 1);
        printf("targets:\n");
        ast_dump(node->data.assign.targets, indent + 2);
        print_indent(indent + 1);
        printf("value:\n");
        ast_dump(node->data.assign.value, indent + 2);
        break;

    case AST_ANN_ASSIGN:
        printf("\n");
        print_indent(indent + 1);
        printf("target:\n");
        ast_dump(node->data.ann_assign.target, indent + 2);
        print_indent(indent + 1);
        printf("annotation:\n");
        ast_dump(node->data.ann_assign.annotation, indent + 2);
        if (node->data.ann_assign.value) {
            print_indent(indent + 1);
            printf("value:\n");
            ast_dump(node->data.ann_assign.value, indent + 2);
        }
        break;

    case AST_AUG_ASSIGN:
        printf(" op='%s'\n", binop_name(node->data.aug_assign.op));
        print_indent(indent + 1);
        printf("target:\n");
        ast_dump(node->data.aug_assign.target, indent + 2);
        print_indent(indent + 1);
        printf("value:\n");
        ast_dump(node->data.aug_assign.value, indent + 2);
        break;

    case AST_IF:
        printf("\n");
        print_indent(indent + 1);
        printf("condition:\n");
        ast_dump(node->data.if_stmt.condition, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.if_stmt.body, indent + 2);
        if (node->data.if_stmt.else_body) {
            print_indent(indent + 1);
            printf("else:\n");
            ast_dump(node->data.if_stmt.else_body, indent + 2);
        }
        break;

    case AST_WHILE:
        printf("\n");
        print_indent(indent + 1);
        printf("condition:\n");
        ast_dump(node->data.while_stmt.condition, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.while_stmt.body, indent + 2);
        if (node->data.while_stmt.else_body) {
            print_indent(indent + 1);
            printf("else:\n");
            ast_dump(node->data.while_stmt.else_body, indent + 2);
        }
        break;

    case AST_FOR:
        printf("\n");
        print_indent(indent + 1);
        printf("target:\n");
        ast_dump(node->data.for_stmt.target, indent + 2);
        print_indent(indent + 1);
        printf("iter:\n");
        ast_dump(node->data.for_stmt.iter, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.for_stmt.body, indent + 2);
        if (node->data.for_stmt.else_body) {
            print_indent(indent + 1);
            printf("else:\n");
            ast_dump(node->data.for_stmt.else_body, indent + 2);
        }
        break;

    case AST_BREAK:
    case AST_CONTINUE:
    case AST_PASS:
    case AST_NONE_LIT:
        printf("\n");
        break;

    case AST_EXPR_STMT:
        printf("\n");
        ast_dump(node->data.expr_stmt.expr, indent + 1);
        break;

    case AST_TRY:
        printf("\n");
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.try_stmt.body, indent + 2);
        if (node->data.try_stmt.handlers) {
            print_indent(indent + 1);
            printf("handlers:\n");
            ast_dump(node->data.try_stmt.handlers, indent + 2);
        }
        if (node->data.try_stmt.else_body) {
            print_indent(indent + 1);
            printf("else:\n");
            ast_dump(node->data.try_stmt.else_body, indent + 2);
        }
        if (node->data.try_stmt.finally_body) {
            print_indent(indent + 1);
            printf("finally:\n");
            ast_dump(node->data.try_stmt.finally_body, indent + 2);
        }
        break;

    case AST_EXCEPT_HANDLER:
        if (node->data.handler.name) printf(" as '%s'", node->data.handler.name);
        printf("\n");
        if (node->data.handler.type) {
            print_indent(indent + 1);
            printf("type:\n");
            ast_dump(node->data.handler.type, indent + 2);
        }
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.handler.body, indent + 2);
        break;

    case AST_RAISE:
        printf("\n");
        if (node->data.raise_stmt.exc) {
            ast_dump(node->data.raise_stmt.exc, indent + 1);
        }
        if (node->data.raise_stmt.cause) {
            print_indent(indent + 1);
            printf("from:\n");
            ast_dump(node->data.raise_stmt.cause, indent + 2);
        }
        break;

    case AST_WITH:
        printf("\n");
        print_indent(indent + 1);
        printf("items:\n");
        ast_dump(node->data.with_stmt.items, indent + 2);
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.with_stmt.body, indent + 2);
        break;

    case AST_WITH_ITEM:
        printf("\n");
        print_indent(indent + 1);
        printf("context:\n");
        ast_dump(node->data.with_item.context_expr, indent + 2);
        if (node->data.with_item.optional_vars) {
            print_indent(indent + 1);
            printf("as:\n");
            ast_dump(node->data.with_item.optional_vars, indent + 2);
        }
        break;

    case AST_IMPORT:
        printf(" module='%s'", node->data.import_stmt.module ? node->data.import_stmt.module : "?");
        if (node->data.import_stmt.alias) printf(" as '%s'", node->data.import_stmt.alias);
        printf("\n");
        break;

    case AST_IMPORT_FROM:
        printf(" module='%s'\n", node->data.import_from.module ? node->data.import_from.module : "?");
        if (node->data.import_from.names) {
            print_indent(indent + 1);
            printf("names:\n");
            ast_dump(node->data.import_from.names, indent + 2);
        }
        break;

    case AST_IMPORT_NAME:
        printf(" name='%s'", node->data.import_name.imported_name ? node->data.import_name.imported_name : "?");
        if (node->data.import_name.alias) printf(" as '%s'", node->data.import_name.alias);
        printf("\n");
        break;

    case AST_DELETE:
        printf("\n");
        ast_dump(node->data.del_stmt.targets, indent + 1);
        break;

    case AST_GLOBAL:
    case AST_NONLOCAL:
        {
            int i;
            for (i = 0; i < node->data.global_stmt.num_names; i++) {
                printf(" '%s'", node->data.global_stmt.names[i]);
            }
            printf("\n");
        }
        break;

    case AST_ASSERT:
        printf("\n");
        print_indent(indent + 1);
        printf("test:\n");
        ast_dump(node->data.assert_stmt.test, indent + 2);
        if (node->data.assert_stmt.msg) {
            print_indent(indent + 1);
            printf("msg:\n");
            ast_dump(node->data.assert_stmt.msg, indent + 2);
        }
        break;

    case AST_TYPE_ALIAS:
        printf(" name='%s'", node->data.type_alias.name);
        if (node->data.type_alias.num_type_params > 0) {
            int i;
            printf(" params=[");
            for (i = 0; i < node->data.type_alias.num_type_params; i++) {
                if (i > 0) printf(", ");
                printf("%s", node->data.type_alias.type_param_names[i]);
            }
            printf("]");
        }
        printf("\n");
        if (node->data.type_alias.value)
            ast_dump(node->data.type_alias.value, indent + 1);
        break;

    case AST_BINOP:
        printf(" op='%s'\n", binop_name(node->data.binop.op));
        print_indent(indent + 1);
        printf("left:\n");
        ast_dump(node->data.binop.left, indent + 2);
        print_indent(indent + 1);
        printf("right:\n");
        ast_dump(node->data.binop.right, indent + 2);
        break;

    case AST_UNARYOP:
        {
            const char *opname = "?";
            switch (node->data.unaryop.op) {
            case UNARY_NEG:    opname = "-"; break;
            case UNARY_POS:    opname = "+"; break;
            case UNARY_NOT:    opname = "not"; break;
            case UNARY_BITNOT: opname = "~"; break;
            }
            printf(" op='%s'\n", opname);
        }
        ast_dump(node->data.unaryop.operand, indent + 1);
        break;

    case AST_COMPARE:
        printf("\n");
        print_indent(indent + 1);
        printf("left:\n");
        ast_dump(node->data.compare.left, indent + 2);
        {
            int i;
            ASTNode *comp = node->data.compare.comparators;
            for (i = 0; i < node->data.compare.num_ops; i++) {
                print_indent(indent + 1);
                printf("op '%s':\n", cmpop_name(node->data.compare.ops[i]));
                if (comp) {
                    ast_dump(comp, indent + 2);
                    comp = comp->next;
                }
            }
        }
        break;

    case AST_BOOLOP:
        printf(" op='%s'\n", node->data.boolop.op == BOOL_AND ? "and" : "or");
        ast_dump(node->data.boolop.values, indent + 1);
        break;

    case AST_CALL:
        printf(" num_args=%d\n", node->data.call.num_args);
        print_indent(indent + 1);
        printf("func:\n");
        ast_dump(node->data.call.func, indent + 2);
        if (node->data.call.args) {
            print_indent(indent + 1);
            printf("args:\n");
            ast_dump(node->data.call.args, indent + 2);
        }
        break;

    case AST_KEYWORD_ARG:
        printf(" key='%s'\n", node->data.keyword_arg.key ? node->data.keyword_arg.key : "**");
        ast_dump(node->data.keyword_arg.kw_value, indent + 1);
        break;

    case AST_ATTR:
        printf(" attr='%s'\n", node->data.attribute.attr ? node->data.attribute.attr : "?");
        ast_dump(node->data.attribute.object, indent + 1);
        break;

    case AST_SUBSCRIPT:
        printf("\n");
        print_indent(indent + 1);
        printf("object:\n");
        ast_dump(node->data.subscript.object, indent + 2);
        print_indent(indent + 1);
        printf("index:\n");
        ast_dump(node->data.subscript.index, indent + 2);
        break;

    case AST_SLICE:
        printf("\n");
        if (node->data.slice.lower) {
            print_indent(indent + 1);
            printf("lower:\n");
            ast_dump(node->data.slice.lower, indent + 2);
        }
        if (node->data.slice.upper) {
            print_indent(indent + 1);
            printf("upper:\n");
            ast_dump(node->data.slice.upper, indent + 2);
        }
        if (node->data.slice.step) {
            print_indent(indent + 1);
            printf("step:\n");
            ast_dump(node->data.slice.step, indent + 2);
        }
        break;

    case AST_NAME:
        printf(" id='%s'\n", node->data.name.id ? node->data.name.id : "?");
        break;

    case AST_INT_LIT:
        printf(" value=%ld\n", node->data.int_lit.value);
        break;

    case AST_FLOAT_LIT:
        printf(" value=%g\n", node->data.float_lit.value);
        break;

    case AST_COMPLEX_LIT:
        printf(" imag=%g\n", node->data.complex_lit.imag);
        break;

    case AST_STR_LIT:
        printf(" value=\"%s\" len=%d\n",
               node->data.str_lit.value ? node->data.str_lit.value : "",
               node->data.str_lit.len);
        break;

    case AST_FSTRING:
        printf("\n");
        ast_dump(node->data.fstring.parts, indent + 1);
        break;

    case AST_BOOL_LIT:
        printf(" value=%s\n", node->data.bool_lit.value ? "True" : "False");
        break;

    case AST_LIST_EXPR:
    case AST_TUPLE_EXPR:
    case AST_SET_EXPR:
        printf("\n");
        ast_dump(node->data.collection.elts, indent + 1);
        break;

    case AST_DICT_EXPR:
        printf("\n");
        {
            ASTNode *k = node->data.dict.keys;
            ASTNode *v = node->data.dict.values;
            while (k && v) {
                print_indent(indent + 1);
                printf("key:\n");
                ast_dump(k, indent + 2);
                print_indent(indent + 1);
                printf("value:\n");
                ast_dump(v, indent + 2);
                k = k->next;
                v = v->next;
            }
        }
        break;

    case AST_LISTCOMP:
    case AST_SETCOMP:
    case AST_GENEXPR:
        printf("\n");
        print_indent(indent + 1);
        printf("elt:\n");
        ast_dump(node->data.listcomp.elt, indent + 2);
        print_indent(indent + 1);
        printf("generators:\n");
        ast_dump(node->data.listcomp.generators, indent + 2);
        break;

    case AST_DICTCOMP:
        printf("\n");
        print_indent(indent + 1);
        printf("key:\n");
        ast_dump(node->data.dictcomp.key, indent + 2);
        print_indent(indent + 1);
        printf("value:\n");
        ast_dump(node->data.dictcomp.value, indent + 2);
        print_indent(indent + 1);
        printf("generators:\n");
        ast_dump(node->data.dictcomp.generators, indent + 2);
        break;

    case AST_COMP_GENERATOR:
        printf("%s\n", node->data.comp_gen.is_async ? " async" : "");
        print_indent(indent + 1);
        printf("target:\n");
        ast_dump(node->data.comp_gen.target, indent + 2);
        print_indent(indent + 1);
        printf("iter:\n");
        ast_dump(node->data.comp_gen.iter, indent + 2);
        if (node->data.comp_gen.ifs) {
            print_indent(indent + 1);
            printf("ifs:\n");
            ast_dump(node->data.comp_gen.ifs, indent + 2);
        }
        break;

    case AST_LAMBDA:
        printf("\n");
        {
            Param *p = node->data.lambda.params;
            if (p) {
                print_indent(indent + 1);
                printf("params:\n");
                while (p) {
                    print_indent(indent + 2);
                    printf("'%s'\n", p->name ? p->name : "?");
                    p = p->next;
                }
            }
        }
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.lambda.body, indent + 2);
        break;

    case AST_IFEXPR:
        printf("\n");
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.ifexpr.body, indent + 2);
        print_indent(indent + 1);
        printf("test:\n");
        ast_dump(node->data.ifexpr.test, indent + 2);
        print_indent(indent + 1);
        printf("else:\n");
        ast_dump(node->data.ifexpr.else_body, indent + 2);
        break;

    case AST_WALRUS:
        printf("\n");
        print_indent(indent + 1);
        printf("target:\n");
        ast_dump(node->data.walrus.target, indent + 2);
        print_indent(indent + 1);
        printf("value:\n");
        ast_dump(node->data.walrus.value, indent + 2);
        break;

    case AST_STARRED:
        printf("\n");
        ast_dump(node->data.starred.value, indent + 1);
        break;

    case AST_YIELD_EXPR:
    case AST_YIELD_FROM_EXPR:
        printf("\n");
        if (node->data.yield_expr.value) {
            ast_dump(node->data.yield_expr.value, indent + 1);
        }
        break;

    case AST_YIELD_STMT:
        printf("\n");
        break;

    case AST_MATCH:
        printf("\n");
        print_indent(indent + 1);
        printf("subject:\n");
        ast_dump(node->data.match_stmt.subject, indent + 2);
        print_indent(indent + 1);
        printf("cases:\n");
        ast_dump(node->data.match_stmt.cases, indent + 2);
        break;

    case AST_MATCH_CASE:
        printf("\n");
        print_indent(indent + 1);
        printf("pattern:\n");
        ast_dump(node->data.match_case.pattern, indent + 2);
        if (node->data.match_case.guard) {
            print_indent(indent + 1);
            printf("guard:\n");
            ast_dump(node->data.match_case.guard, indent + 2);
        }
        print_indent(indent + 1);
        printf("body:\n");
        ast_dump(node->data.match_case.body, indent + 2);
        break;

    case AST_AWAIT:
        printf("\n");
        ast_dump(node->data.starred.value, indent + 1); /* reuses starred layout */
        break;

    case AST_TYPE_NAME:
        printf(" name='%s'\n", node->data.type_name.tname ? node->data.type_name.tname : "?");
        break;

    case AST_TYPE_GENERIC:
        printf(" name='%s'\n", node->data.type_generic.gname ? node->data.type_generic.gname : "?");
        if (node->data.type_generic.type_args) {
            print_indent(indent + 1);
            printf("type_args:\n");
            ast_dump(node->data.type_generic.type_args, indent + 2);
        }
        break;

    case AST_TYPE_UNION:
        printf("\n");
        ast_dump(node->data.type_union.types, indent + 1);
        break;

    case AST_TYPE_OPTIONAL:
    case AST_TYPE_TUPLE:
    case AST_TYPE_CALLABLE:
        printf("\n");
        break;
    }

    /* Dump siblings */
    if (node->next) {
        ast_dump(node->next, indent);
    }
}
