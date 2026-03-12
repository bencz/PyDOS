/*
 * parser.cpp - Full recursive descent parser for PyDOS Python-to-8086 compiler
 *
 * Parses Python 3.11+ into an AST. Expression parsing uses precedence
 * climbing. Handles the complete Python statement grammar including:
 * function/class defs, if/elif/else, while, for, try/except/finally,
 * with, match/case, import, assignments, type annotations, decorators,
 * and comprehensive expression parsing.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Constructor / Destructor                                            */
/* ------------------------------------------------------------------ */

Parser::Parser() {
    lex = NULL;
    error_count = 0;
    memset(&current, 0, sizeof(current));
}

Parser::~Parser() {
}

void Parser::init(Lexer *lexer) {
    lex = lexer;
    error_count = 0;
    advance();
}

int Parser::get_error_count() const {
    return error_count;
}

/* ------------------------------------------------------------------ */
/* Token management                                                    */
/* ------------------------------------------------------------------ */

void Parser::advance() {
    current = lex->next_token();
}

Token Parser::peek() {
    return lex->peek_token();
}

int Parser::check(TokenType type) {
    return current.type == type;
}

int Parser::match(TokenType type) {
    if (current.type == type) {
        advance();
        return 1;
    }
    return 0;
}

void Parser::expect(TokenType type) {
    if (current.type == type) {
        advance();
    } else {
        char msg[128];
        sprintf(msg, "Expected %s but got %s",
                token_type_name(type), token_type_name(current.type));
        error(msg);
    }
}

/* ------------------------------------------------------------------ */
/* Error handling                                                      */
/* ------------------------------------------------------------------ */

void Parser::error(const char *msg) {
    fprintf(stderr, "%d:%d: error: %s\n", current.line, current.col, msg);
    error_count++;
}

void Parser::error_at(Token *tok, const char *msg) {
    fprintf(stderr, "%d:%d: error: %s\n", tok->line, tok->col, msg);
    error_count++;
}

void Parser::synchronize() {
    /* Skip tokens until we find a statement boundary */
    while (current.type != TOK_EOF) {
        if (current.type == TOK_NEWLINE) {
            advance();
            return;
        }
        switch (current.type) {
        case TOK_DEF:
        case TOK_CLASS:
        case TOK_IF:
        case TOK_WHILE:
        case TOK_FOR:
        case TOK_TRY:
        case TOK_WITH:
        case TOK_RETURN:
        case TOK_RAISE:
        case TOK_IMPORT:
        case TOK_FROM:
        case TOK_BREAK:
        case TOK_CONTINUE:
        case TOK_PASS:
        case TOK_DEDENT:
            return;
        default:
            advance();
            break;
        }
    }
}

/* ------------------------------------------------------------------ */
/* parse_module                                                        */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_module() {
    ASTNode *mod = ast_alloc(AST_MODULE, 1, 0);
    ASTNode *first = NULL;
    ASTNode *last = NULL;

    /* Skip any leading newlines */
    while (check(TOK_NEWLINE)) {
        advance();
    }

    while (!check(TOK_EOF)) {
        ASTNode *stmt = parse_statement();
        if (stmt) {
            if (!first) {
                first = stmt;
            } else {
                last->next = stmt;
            }
            /* Advance last to end of chain (stmt could be a chain from simple_stmt) */
            last = stmt;
            while (last->next) last = last->next;
        }
        /* Skip any extra newlines between statements */
        while (check(TOK_NEWLINE)) {
            advance();
        }
    }

    mod->data.module.body = first;
    return mod;
}

/* ------------------------------------------------------------------ */
/* parse_statement                                                     */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_statement() {
    /* Check for decorators */
    if (check(TOK_AT)) {
        ASTNode *decorators = parse_decorators();
        if (check(TOK_DEF)) {
            return parse_funcdef(decorators, 0);
        } else if (check(TOK_ASYNC)) {
            advance();
            if (check(TOK_DEF)) {
                return parse_funcdef(decorators, 1);
            }
            error("Expected 'def' after 'async'");
            synchronize();
            return NULL;
        } else if (check(TOK_CLASS)) {
            return parse_classdef(decorators);
        } else {
            error("Decorators must be followed by 'def' or 'class'");
            synchronize();
            return NULL;
        }
    }

    /* Compound statements */
    switch (current.type) {
    case TOK_DEF:
        return parse_funcdef(NULL, 0);
    case TOK_ASYNC: {
        Token async_tok = current;
        advance();
        if (check(TOK_DEF)) {
            return parse_funcdef(NULL, 1);
        } else if (check(TOK_FOR)) {
            /* async for - parse as regular for with a flag (simplified) */
            return parse_for();
        } else if (check(TOK_WITH)) {
            return parse_with();
        }
        error("Expected 'def', 'for', or 'with' after 'async'");
        synchronize();
        return NULL;
    }
    case TOK_CLASS:
        return parse_classdef(NULL);
    case TOK_IF:
        return parse_if();
    case TOK_WHILE:
        return parse_while();
    case TOK_FOR:
        return parse_for();
    case TOK_TRY:
        return parse_try();
    case TOK_WITH:
        return parse_with();
    case TOK_MATCH:
        return parse_match();
    default:
        break;
    }

    /* Simple statements */
    return parse_simple_stmt();
}

/* ------------------------------------------------------------------ */
/* parse_simple_stmt                                                   */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_simple_stmt() {
    ASTNode *stmt = NULL;

    switch (current.type) {
    case TOK_RETURN:
        stmt = parse_return();
        break;
    case TOK_RAISE:
        stmt = parse_raise();
        break;
    case TOK_IMPORT:
        stmt = parse_import();
        break;
    case TOK_FROM:
        stmt = parse_import_from();
        break;
    case TOK_BREAK:
        stmt = ast_alloc(AST_BREAK, current.line, current.col);
        advance();
        break;
    case TOK_CONTINUE:
        stmt = ast_alloc(AST_CONTINUE, current.line, current.col);
        advance();
        break;
    case TOK_PASS:
        stmt = ast_alloc(AST_PASS, current.line, current.col);
        advance();
        break;
    case TOK_DEL:
        stmt = parse_del();
        break;
    case TOK_GLOBAL:
        stmt = parse_global_or_nonlocal(AST_GLOBAL);
        break;
    case TOK_NONLOCAL:
        stmt = parse_global_or_nonlocal(AST_NONLOCAL);
        break;
    case TOK_ASSERT:
        stmt = parse_assert();
        break;
    case TOK_TYPE:
        stmt = parse_type_alias();
        break;
    case TOK_YIELD:
        stmt = parse_return(); /* yield handled specially below */
        break;
    default:
        stmt = parse_assign_or_expr();
        break;
    }

    /* Consume semicolons for multi-statement lines */
    while (match(TOK_SEMICOLON)) {
        /* Could parse another simple statement on same line, but for now skip */
        if (!check(TOK_NEWLINE) && !check(TOK_EOF)) {
            ASTNode *next_stmt = NULL;
            switch (current.type) {
            case TOK_RETURN:  next_stmt = parse_return(); break;
            case TOK_BREAK:   next_stmt = ast_alloc(AST_BREAK, current.line, current.col); advance(); break;
            case TOK_CONTINUE:next_stmt = ast_alloc(AST_CONTINUE, current.line, current.col); advance(); break;
            case TOK_PASS:    next_stmt = ast_alloc(AST_PASS, current.line, current.col); advance(); break;
            default:          next_stmt = parse_assign_or_expr(); break;
            }
            if (next_stmt && stmt) {
                /* Chain statements */
                ASTNode *tail = stmt;
                while (tail->next) tail = tail->next;
                tail->next = next_stmt;
            }
        }
    }

    /* Expect newline or EOF */
    if (!check(TOK_NEWLINE) && !check(TOK_EOF) && !check(TOK_DEDENT)) {
        /* Some compound statement parsers consume the newline themselves */
    }
    if (check(TOK_NEWLINE)) {
        advance();
    }

    return stmt;
}

/* ------------------------------------------------------------------ */
/* parse_decorators                                                    */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_decorators() {
    ASTNode *first = NULL;
    ASTNode *last = NULL;

    while (check(TOK_AT)) {
        advance(); /* consume @ */
        ASTNode *expr = parse_expr();
        if (expr) {
            if (!first) first = expr;
            else last->next = expr;
            last = expr;
        }
        if (check(TOK_NEWLINE)) advance();
    }

    return first;
}

/* ------------------------------------------------------------------ */
/* parse_block                                                         */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_block() {
    expect(TOK_NEWLINE);
    expect(TOK_INDENT);

    ASTNode *first = NULL;
    ASTNode *last = NULL;

    while (!check(TOK_DEDENT) && !check(TOK_EOF)) {
        /* Skip blank newlines inside block */
        if (check(TOK_NEWLINE)) {
            advance();
            continue;
        }

        ASTNode *stmt = parse_statement();
        if (stmt) {
            if (!first) {
                first = stmt;
            } else {
                last->next = stmt;
            }
            /* Advance last to end of chain */
            last = stmt;
            while (last->next) last = last->next;
        }
    }

    expect(TOK_DEDENT);
    return first;
}

/* ------------------------------------------------------------------ */
/* parse_funcdef                                                       */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_funcdef(ASTNode *decorators, int is_async) {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_DEF);

    /* Function name */
    if (!check(TOK_IDENTIFIER)) {
        error("Expected function name after 'def'");
        synchronize();
        return NULL;
    }
    const char *name = current.text;
    int name_len = current.text_len;
    advance();

    /* Allocate a copy of the name */
    char *name_copy = (char *)malloc(name_len + 1);
    if (name_copy) {
        memcpy(name_copy, name, name_len);
        name_copy[name_len] = '\0';
    }

    /* PEP 695 type parameters: def func[T](...) */
    const char **type_param_names = NULL;
    int num_type_params = 0;
    if (match(TOK_LBRACKET)) {
        const char *tpnames[16];
        int tpcount = 0;
        while (!check(TOK_RBRACKET) && !check(TOK_EOF) && tpcount < 16) {
            if (!check(TOK_IDENTIFIER)) {
                error("Expected type parameter name");
                break;
            }
            int tlen = current.text_len;
            char *tpname = (char *)malloc(tlen + 1);
            if (tpname) {
                memcpy(tpname, current.text, tlen);
                tpname[tlen] = '\0';
            }
            tpnames[tpcount++] = tpname;
            advance();
            if (!match(TOK_COMMA)) break;
        }
        expect(TOK_RBRACKET);
        if (tpcount > 0) {
            type_param_names = name_array_alloc(tpcount);
            if (type_param_names) {
                for (int i = 0; i < tpcount; i++) {
                    type_param_names[i] = tpnames[i];
                }
            }
            num_type_params = tpcount;
        }
    }

    /* Parameters */
    expect(TOK_LPAREN);
    Param *params = NULL;
    if (!check(TOK_RPAREN)) {
        params = parse_params();
    }
    expect(TOK_RPAREN);

    /* Return type annotation */
    ASTNode *return_type = NULL;
    if (match(TOK_ARROW)) {
        return_type = parse_type_annotation();
    }

    expect(TOK_COLON);

    /* Body */
    ASTNode *body = parse_block();

    ASTNode *node = ast_alloc(AST_FUNC_DEF, start_line, start_col);
    node->data.func_def.name = name_copy;
    node->data.func_def.params = params;
    node->data.func_def.return_type = return_type;
    node->data.func_def.body = body;
    node->data.func_def.decorators = decorators;
    node->data.func_def.is_async = is_async;
    node->data.func_def.type_param_names = type_param_names;
    node->data.func_def.num_type_params = num_type_params;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_params                                                        */
/* ------------------------------------------------------------------ */

Param *Parser::parse_params() {
    Param *first = NULL;
    Param *last = NULL;
    int seen_slash = 0;

    for (;;) {
        if (check(TOK_RPAREN)) break;

        /* Check for / separator (positional-only marker) */
        if (check(TOK_SLASH)) {
            if (seen_slash) {
                error("only one / separator allowed in parameter list");
                break;
            }
            if (!first) {
                error("/ separator must come after at least one parameter");
                break;
            }
            seen_slash = 1;
            /* Mark all params parsed so far as positional-only */
            {
                Param *pp;
                for (pp = first; pp; pp = pp->next) {
                    pp->is_positional_only = 1;
                }
            }
            advance(); /* consume / */
            if (!match(TOK_COMMA)) break;
            if (check(TOK_RPAREN)) break;
            continue;
        }

        Param *p = param_alloc();

        /* Check for *args or **kwargs */
        if (match(TOK_STAR)) {
            p->is_star = 1;
            if (check(TOK_IDENTIFIER)) {
                p->name = current.text;
                advance();
            } else {
                /* bare * separator */
                p->name = "*";
            }
        } else if (match(TOK_DOUBLESTAR)) {
            p->is_double_star = 1;
            if (!check(TOK_IDENTIFIER)) {
                error("Expected parameter name after '**'");
                break;
            }
            p->name = current.text;
            advance();
        } else {
            /* Regular parameter */
            if (!check(TOK_IDENTIFIER)) {
                error("Expected parameter name");
                break;
            }
            p->name = current.text;
            advance();
        }

        /* Type annotation */
        if (match(TOK_COLON)) {
            p->annotation = parse_type_annotation();
        } else if (p->name && strcmp(p->name, "self") == 0) {
            /* 'self' parameter in class methods: annotation is optional */
            p->annotation = 0;
        } else if (p->name && strcmp(p->name, "cls") == 0) {
            /* 'cls' parameter in classmethods: annotation is optional */
            p->annotation = 0;
        } else {
            /* No annotation — sema resolves NULL as type_any */
            p->annotation = 0;
        }

        /* Default value */
        if (match(TOK_ASSIGN)) {
            p->default_val = parse_expr();
        }

        if (!first) first = p;
        else last->next = p;
        last = p;

        if (!match(TOK_COMMA)) break;

        /* Allow trailing comma */
        if (check(TOK_RPAREN)) break;
    }

    return first;
}

/* ------------------------------------------------------------------ */
/* parse_classdef                                                      */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_classdef(ASTNode *decorators) {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_CLASS);

    if (!check(TOK_IDENTIFIER)) {
        error("Expected class name after 'class'");
        synchronize();
        return NULL;
    }
    const char *name = current.text;
    int name_len = current.text_len;
    advance();

    char *name_copy = (char *)malloc(name_len + 1);
    if (name_copy) {
        memcpy(name_copy, name, name_len);
        name_copy[name_len] = '\0';
    }

    /* Type parameters: class Stack[T]: or class Pair[K, V]: */
    const char **type_param_names = NULL;
    int num_type_params = 0;
    if (match(TOK_LBRACKET)) {
        const char *tpnames[16];
        int tpcount = 0;
        while (!check(TOK_RBRACKET) && !check(TOK_EOF) && tpcount < 16) {
            if (!check(TOK_IDENTIFIER)) {
                error("Expected type parameter name");
                break;
            }
            int tlen = current.text_len;
            char *tpname = (char *)malloc(tlen + 1);
            if (tpname) {
                memcpy(tpname, current.text, tlen);
                tpname[tlen] = '\0';
            }
            tpnames[tpcount++] = tpname;
            advance();
            if (!match(TOK_COMMA)) break;
        }
        expect(TOK_RBRACKET);

        if (tpcount > 0) {
            type_param_names = name_array_alloc(tpcount);
            if (type_param_names) {
                for (int i = 0; i < tpcount; i++) {
                    type_param_names[i] = tpnames[i];
                }
            }
            num_type_params = tpcount;
        }
    }

    /* Base classes */
    ASTNode *bases = NULL;
    if (match(TOK_LPAREN)) {
        if (!check(TOK_RPAREN)) {
            ASTNode *first_base = NULL;
            ASTNode *last_base = NULL;
            for (;;) {
                ASTNode *base = parse_expr();
                if (base) {
                    if (!first_base) first_base = base;
                    else last_base->next = base;
                    last_base = base;
                    last_base->next = NULL;
                }
                if (!match(TOK_COMMA)) break;
                if (check(TOK_RPAREN)) break;
            }
            bases = first_base;
        }
        expect(TOK_RPAREN);
    }

    expect(TOK_COLON);

    ASTNode *body = parse_block();

    ASTNode *node = ast_alloc(AST_CLASS_DEF, start_line, start_col);
    node->data.class_def.name = name_copy;
    node->data.class_def.bases = bases;
    node->data.class_def.body = body;
    node->data.class_def.decorators = decorators;
    node->data.class_def.type_param_names = type_param_names;
    node->data.class_def.num_type_params = num_type_params;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_if                                                            */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_if() {
    int start_line = current.line;
    int start_col = current.col;

    /* Accept both 'if' and 'elif' - elif is parsed as a nested if */
    if (check(TOK_IF)) {
        advance();
    } else if (check(TOK_ELIF)) {
        advance();
    } else {
        expect(TOK_IF);
    }

    ASTNode *condition = parse_expr();
    expect(TOK_COLON);
    ASTNode *body = parse_block();

    ASTNode *else_body = NULL;

    if (check(TOK_ELIF)) {
        /* Parse elif as a nested if in the else branch */
        else_body = parse_if();
    } else if (match(TOK_ELSE)) {
        expect(TOK_COLON);
        else_body = parse_block();
    }

    ASTNode *node = ast_alloc(AST_IF, start_line, start_col);
    node->data.if_stmt.condition = condition;
    node->data.if_stmt.body = body;
    node->data.if_stmt.else_body = else_body;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_while                                                         */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_while() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_WHILE);
    ASTNode *condition = parse_expr();
    expect(TOK_COLON);
    ASTNode *body = parse_block();

    ASTNode *else_body = NULL;
    if (match(TOK_ELSE)) {
        expect(TOK_COLON);
        else_body = parse_block();
    }

    ASTNode *node = ast_alloc(AST_WHILE, start_line, start_col);
    node->data.while_stmt.condition = condition;
    node->data.while_stmt.body = body;
    node->data.while_stmt.else_body = else_body;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_for                                                           */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_for() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_FOR);

    /* Parse target(s) - could be a tuple of names.
     * Use parse_postfix_expr to avoid consuming 'in' as a comparison operator. */
    ASTNode *target = parse_postfix_expr();

    /* Handle tuple unpacking: for a, b in ... */
    if (check(TOK_COMMA)) {
        ASTNode *tuple_node = ast_alloc(AST_TUPLE_EXPR, target->line, target->col);
        ASTNode *first = target;
        ASTNode *last_elt = target;
        while (match(TOK_COMMA)) {
            if (check(TOK_IN)) break;
            ASTNode *elt = parse_postfix_expr();
            if (elt) {
                last_elt->next = elt;
                last_elt = elt;
                last_elt->next = NULL;
            }
        }
        tuple_node->data.collection.elts = first;
        target = tuple_node;
    }

    expect(TOK_IN);
    ASTNode *iter = parse_expr();
    expect(TOK_COLON);
    ASTNode *body = parse_block();

    ASTNode *else_body = NULL;
    if (match(TOK_ELSE)) {
        expect(TOK_COLON);
        else_body = parse_block();
    }

    ASTNode *node = ast_alloc(AST_FOR, start_line, start_col);
    node->data.for_stmt.target = target;
    node->data.for_stmt.iter = iter;
    node->data.for_stmt.body = body;
    node->data.for_stmt.else_body = else_body;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_try                                                           */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_try() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_TRY);
    expect(TOK_COLON);
    ASTNode *body = parse_block();

    ASTNode *handlers = NULL;
    ASTNode *last_handler = NULL;
    ASTNode *else_body = NULL;
    ASTNode *finally_body = NULL;

    while (check(TOK_EXCEPT)) {
        int h_line = current.line;
        int h_col = current.col;
        advance(); /* consume 'except' */

        ASTNode *handler = ast_alloc(AST_EXCEPT_HANDLER, h_line, h_col);

        /* Exception type (optional for bare except) */
        handler->data.handler.is_star = 0;
        if (!check(TOK_COLON)) {
            /* Check for except* (exception group) */
            if (check(TOK_STAR)) {
                advance(); /* consume * for except* */
                handler->data.handler.is_star = 1;
            }
            handler->data.handler.type = parse_expr();

            if (match(TOK_AS)) {
                if (check(TOK_IDENTIFIER)) {
                    handler->data.handler.name = current.text;
                    advance();
                } else {
                    error("Expected identifier after 'as'");
                }
            }
        }

        expect(TOK_COLON);
        handler->data.handler.body = parse_block();

        if (!handlers) handlers = handler;
        else last_handler->next = handler;
        last_handler = handler;
    }

    if (match(TOK_ELSE)) {
        expect(TOK_COLON);
        else_body = parse_block();
    }

    if (match(TOK_FINALLY)) {
        expect(TOK_COLON);
        finally_body = parse_block();
    }

    ASTNode *node = ast_alloc(AST_TRY, start_line, start_col);
    node->data.try_stmt.body = body;
    node->data.try_stmt.handlers = handlers;
    node->data.try_stmt.else_body = else_body;
    node->data.try_stmt.finally_body = finally_body;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_with                                                          */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_with() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_WITH);

    ASTNode *first_item = NULL;
    ASTNode *last_item = NULL;

    for (;;) {
        int item_line = current.line;
        int item_col = current.col;
        ASTNode *context = parse_expr();

        ASTNode *item = ast_alloc(AST_WITH_ITEM, item_line, item_col);
        item->data.with_item.context_expr = context;

        if (match(TOK_AS)) {
            item->data.with_item.optional_vars = parse_expr();
        }

        if (!first_item) first_item = item;
        else last_item->next = item;
        last_item = item;

        if (!match(TOK_COMMA)) break;
    }

    expect(TOK_COLON);
    ASTNode *body = parse_block();

    ASTNode *node = ast_alloc(AST_WITH, start_line, start_col);
    node->data.with_stmt.items = first_item;
    node->data.with_stmt.body = body;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_match                                                         */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_match() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_MATCH);
    ASTNode *subject = parse_expr();
    expect(TOK_COLON);
    expect(TOK_NEWLINE);
    expect(TOK_INDENT);

    ASTNode *first_case = NULL;
    ASTNode *last_case = NULL;

    while (check(TOK_CASE)) {
        int case_line = current.line;
        int case_col = current.col;
        advance(); /* consume 'case' */

        /* Parse pattern as an expression, but stop before 'if' (guard keyword).
           Use parse_or_expr to avoid consuming 'if' as a ternary.
           Handle *star patterns inline. */
        ASTNode *pattern;
        if (check(TOK_STAR)) {
            int sl2 = current.line, sc2 = current.col;
            advance();
            ASTNode *inner = parse_or_expr();
            pattern = ast_alloc(AST_STARRED, sl2, sc2);
            pattern->data.starred.value = inner;
        } else {
            pattern = parse_or_expr();
        }

        ASTNode *guard = NULL;
        if (match(TOK_IF)) {
            guard = parse_expr();
        }

        expect(TOK_COLON);
        ASTNode *case_body = parse_block();

        ASTNode *mc = ast_alloc(AST_MATCH_CASE, case_line, case_col);
        mc->data.match_case.pattern = pattern;
        mc->data.match_case.guard = guard;
        mc->data.match_case.body = case_body;

        if (!first_case) first_case = mc;
        else last_case->next = mc;
        last_case = mc;
    }

    expect(TOK_DEDENT);

    ASTNode *node = ast_alloc(AST_MATCH, start_line, start_col);
    node->data.match_stmt.subject = subject;
    node->data.match_stmt.cases = first_case;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_return                                                        */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_return() {
    int start_line = current.line;
    int start_col = current.col;

    /* Handle yield as well */
    if (check(TOK_YIELD)) {
        advance();
        ASTNode *value = NULL;
        int is_from = 0;
        if (match(TOK_FROM)) {
            is_from = 1;
        }
        if (!check(TOK_NEWLINE) && !check(TOK_EOF) && !check(TOK_SEMICOLON)) {
            value = parse_expr();
        }
        ASTNode *node = ast_alloc(is_from ? AST_YIELD_FROM_EXPR : AST_YIELD_EXPR, start_line, start_col);
        node->data.yield_expr.value = value;
        node->data.yield_expr.is_from = is_from;
        /* Wrap in expr stmt */
        ASTNode *stmt = ast_alloc(AST_EXPR_STMT, start_line, start_col);
        stmt->data.expr_stmt.expr = node;
        return stmt;
    }

    expect(TOK_RETURN);

    ASTNode *value = NULL;
    if (!check(TOK_NEWLINE) && !check(TOK_EOF) && !check(TOK_SEMICOLON)) {
        value = parse_expr();
    }

    ASTNode *node = ast_alloc(AST_RETURN, start_line, start_col);
    node->data.ret.value = value;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_raise                                                         */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_raise() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_RAISE);

    ASTNode *exc = NULL;
    ASTNode *cause = NULL;

    if (!check(TOK_NEWLINE) && !check(TOK_EOF)) {
        exc = parse_expr();

        if (match(TOK_FROM)) {
            cause = parse_expr();
        }
    }

    ASTNode *node = ast_alloc(AST_RAISE, start_line, start_col);
    node->data.raise_stmt.exc = exc;
    node->data.raise_stmt.cause = cause;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_import                                                        */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_import() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_IMPORT);

    /* Parse dotted name */
    char module_name[256];
    int mlen = 0;

    if (!check(TOK_IDENTIFIER)) {
        error("Expected module name after 'import'");
        synchronize();
        return NULL;
    }

    /* Build dotted module name */
    while (check(TOK_IDENTIFIER)) {
        int tlen = current.text_len;
        if (mlen + tlen + 1 < 255) {
            if (mlen > 0) module_name[mlen++] = '.';
            memcpy(&module_name[mlen], current.text, tlen);
            mlen += tlen;
        }
        advance();
        if (!match(TOK_DOT)) break;
    }
    module_name[mlen] = '\0';

    /* Copy module name */
    char *mod_copy = (char *)malloc(mlen + 1);
    if (mod_copy) memcpy(mod_copy, module_name, mlen + 1);

    const char *alias = NULL;
    if (match(TOK_AS)) {
        if (check(TOK_IDENTIFIER)) {
            alias = current.text;
            advance();
        } else {
            error("Expected identifier after 'as'");
        }
    }

    ASTNode *node = ast_alloc(AST_IMPORT, start_line, start_col);
    node->data.import_stmt.module = mod_copy;
    node->data.import_stmt.alias = alias;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_import_from                                                   */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_import_from() {
    int start_line = current.line;
    int start_col = current.col;

    expect(TOK_FROM);

    /* Count leading dots for relative imports */
    char module_name[256];
    int mlen = 0;

    while (check(TOK_DOT) || check(TOK_ELLIPSIS)) {
        if (check(TOK_ELLIPSIS)) {
            if (mlen + 3 < 255) {
                module_name[mlen++] = '.';
                module_name[mlen++] = '.';
                module_name[mlen++] = '.';
            }
            advance();
        } else {
            if (mlen < 255) module_name[mlen++] = '.';
            advance();
        }
    }

    /* Module name */
    while (check(TOK_IDENTIFIER)) {
        int tlen = current.text_len;
        if (mlen + tlen + 1 < 255) {
            if (mlen > 0 && module_name[mlen - 1] != '.') module_name[mlen++] = '.';
            memcpy(&module_name[mlen], current.text, tlen);
            mlen += tlen;
        }
        advance();
        if (!match(TOK_DOT)) break;
    }
    module_name[mlen] = '\0';

    char *mod_copy = (char *)malloc(mlen + 1);
    if (mod_copy) memcpy(mod_copy, module_name, mlen + 1);

    expect(TOK_IMPORT);

    /* Parse import names */
    ASTNode *names = NULL;
    ASTNode *last_name = NULL;

    if (match(TOK_STAR)) {
        /* from module import * */
        ASTNode *star = ast_alloc(AST_IMPORT_NAME, current.line, current.col);
        star->data.import_name.imported_name = "*";
        star->data.import_name.alias = NULL;
        names = star;
    } else {
        int has_paren = match(TOK_LPAREN);

        for (;;) {
            if (check(TOK_RPAREN) || check(TOK_NEWLINE) || check(TOK_EOF)) break;

            if (!check(TOK_IDENTIFIER)) {
                error("Expected name in 'from ... import ...'");
                break;
            }

            ASTNode *imp_name = ast_alloc(AST_IMPORT_NAME, current.line, current.col);
            imp_name->data.import_name.imported_name = current.text;
            imp_name->data.import_name.alias = NULL;
            advance();

            if (match(TOK_AS)) {
                if (check(TOK_IDENTIFIER)) {
                    imp_name->data.import_name.alias = current.text;
                    advance();
                }
            }

            if (!names) names = imp_name;
            else last_name->next = imp_name;
            last_name = imp_name;

            if (!match(TOK_COMMA)) break;
        }

        if (has_paren) expect(TOK_RPAREN);
    }

    ASTNode *node = ast_alloc(AST_IMPORT_FROM, start_line, start_col);
    node->data.import_from.module = mod_copy;
    node->data.import_from.names = names;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_del                                                           */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_del() {
    int start_line = current.line;
    int start_col = current.col;
    expect(TOK_DEL);

    ASTNode *first = NULL;
    ASTNode *last = NULL;

    for (;;) {
        ASTNode *target = parse_expr();
        if (target) {
            if (!first) first = target;
            else last->next = target;
            last = target;
            last->next = NULL;
        }
        if (!match(TOK_COMMA)) break;
    }

    ASTNode *node = ast_alloc(AST_DELETE, start_line, start_col);
    node->data.del_stmt.targets = first;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_global_or_nonlocal                                            */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_global_or_nonlocal(ASTKind kind) {
    int start_line = current.line;
    int start_col = current.col;
    advance(); /* consume 'global' or 'nonlocal' */

    const char *name_buf[64];
    int count = 0;

    for (;;) {
        if (!check(TOK_IDENTIFIER)) {
            error(kind == AST_GLOBAL ? "Expected name after 'global'"
                                     : "Expected name after 'nonlocal'");
            break;
        }
        if (count < 64) {
            name_buf[count++] = current.text;
        }
        advance();
        if (!match(TOK_COMMA)) break;
    }

    ASTNode *node = ast_alloc(kind, start_line, start_col);
    const char **names = name_array_alloc(count);
    int i;
    for (i = 0; i < count; i++) {
        names[i] = name_buf[i];
    }
    node->data.global_stmt.names = names;
    node->data.global_stmt.num_names = count;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_assert                                                        */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_assert() {
    int start_line = current.line;
    int start_col = current.col;
    expect(TOK_ASSERT);

    ASTNode *test = parse_expr();
    ASTNode *msg = NULL;

    if (match(TOK_COMMA)) {
        msg = parse_expr();
    }

    ASTNode *node = ast_alloc(AST_ASSERT, start_line, start_col);
    node->data.assert_stmt.test = test;
    node->data.assert_stmt.msg = msg;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_type_alias                                                    */
/* Parses: type Name[T, U] = TypeExpr                                  */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_type_alias() {
    int start_line = current.line;
    int start_col = current.col;
    expect(TOK_TYPE);

    /* Name */
    if (!check(TOK_IDENTIFIER)) {
        error("expected type alias name after 'type'");
        synchronize();
        return NULL;
    }
    int name_len = current.text_len;
    char *name = (char *)malloc(name_len + 1);
    if (name) {
        memcpy(name, current.text, name_len);
        name[name_len] = '\0';
    }
    advance();

    /* Optional type params: [T, U, ...] */
    const char *param_buf[16];
    int num_params = 0;
    if (match(TOK_LBRACKET)) {
        while (!check(TOK_RBRACKET) && !check(TOK_EOF)) {
            if (num_params > 0) expect(TOK_COMMA);
            if (!check(TOK_IDENTIFIER)) {
                error("expected type parameter name");
                break;
            }
            int tlen = current.text_len;
            char *tpname = (char *)malloc(tlen + 1);
            if (tpname) {
                memcpy(tpname, current.text, tlen);
                tpname[tlen] = '\0';
            }
            param_buf[num_params++] = tpname;
            advance();
            if (num_params >= 16) break;
        }
        expect(TOK_RBRACKET);
    }

    /* = value */
    expect(TOK_ASSIGN);
    ASTNode *value = parse_type_annotation();

    /* Build node */
    ASTNode *node = ast_alloc(AST_TYPE_ALIAS, start_line, start_col);
    node->data.type_alias.name = name;
    node->data.type_alias.value = value;
    if (num_params > 0) {
        const char **params = name_array_alloc(num_params);
        if (params) {
            int i;
            for (i = 0; i < num_params; i++) {
                params[i] = param_buf[i];
            }
        }
        node->data.type_alias.type_param_names = params;
    } else {
        node->data.type_alias.type_param_names = NULL;
    }
    node->data.type_alias.num_type_params = num_params;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_assign_or_expr                                                */
/* Parses: expr, assignment (=), annotated assignment (:), or aug assign */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_assign_or_expr() {
    int start_line = current.line;
    int start_col = current.col;

    ASTNode *expr = parse_star_expr();
    if (!expr) return NULL;

    /* Check for tuple (comma-separated) on left side */
    if (check(TOK_COMMA) && !check(TOK_ASSIGN)) {
        /* Could be a tuple expression or multi-target assignment */
        ASTNode *tuple_node = ast_alloc(AST_TUPLE_EXPR, start_line, start_col);
        ASTNode *first = expr;
        ASTNode *last_elt = expr;
        while (match(TOK_COMMA)) {
            if (check(TOK_ASSIGN) || check(TOK_NEWLINE) || check(TOK_EOF) ||
                check(TOK_SEMICOLON) || check(TOK_COLON)) {
                break;
            }
            ASTNode *elt = parse_star_expr();
            if (elt) {
                last_elt->next = elt;
                last_elt = elt;
                last_elt->next = NULL;
            }
        }
        tuple_node->data.collection.elts = first;
        expr = tuple_node;
    }

    /* Annotated assignment: target : type [= value] */
    if (check(TOK_COLON) && (expr->kind == AST_NAME ||
                              expr->kind == AST_ATTR ||
                              expr->kind == AST_SUBSCRIPT)) {
        advance(); /* consume : */
        ASTNode *annotation = parse_type_annotation();
        ASTNode *value = NULL;
        if (match(TOK_ASSIGN)) {
            value = parse_expr();
        }
        ASTNode *node = ast_alloc(AST_ANN_ASSIGN, start_line, start_col);
        node->data.ann_assign.target = expr;
        node->data.ann_assign.annotation = annotation;
        node->data.ann_assign.value = value;
        return node;
    }

    /* Regular assignment: target = value */
    if (match(TOK_ASSIGN)) {
        /* Could be chained: a = b = c = value */
        ASTNode *targets = expr;
        ASTNode *last_target = expr;

        /* Parse value, which might itself be followed by = for chaining */
        ASTNode *value = parse_star_expr();

        /* Handle tuple on right side */
        if (check(TOK_COMMA)) {
            ASTNode *rtuple = ast_alloc(AST_TUPLE_EXPR, value->line, value->col);
            ASTNode *rfirst = value;
            ASTNode *rlast = value;
            while (match(TOK_COMMA)) {
                if (check(TOK_NEWLINE) || check(TOK_EOF) || check(TOK_SEMICOLON)) break;
                ASTNode *relt = parse_star_expr();
                if (relt) {
                    rlast->next = relt;
                    rlast = relt;
                    rlast->next = NULL;
                }
            }
            rtuple->data.collection.elts = rfirst;
            value = rtuple;
        }

        while (match(TOK_ASSIGN)) {
            /* Chained assignment: previous value becomes a target */
            last_target->next = value;
            last_target = value;
            value = parse_star_expr();

            /* Handle tuple on right side of chained */
            if (check(TOK_COMMA)) {
                ASTNode *rtuple = ast_alloc(AST_TUPLE_EXPR, value->line, value->col);
                ASTNode *rfirst = value;
                ASTNode *rlast = value;
                while (match(TOK_COMMA)) {
                    if (check(TOK_NEWLINE) || check(TOK_EOF) || check(TOK_SEMICOLON)) break;
                    ASTNode *relt = parse_star_expr();
                    if (relt) {
                        rlast->next = relt;
                        rlast = relt;
                        rlast->next = NULL;
                    }
                }
                rtuple->data.collection.elts = rfirst;
                value = rtuple;
            }
        }

        /* Detach targets chain: last_target->next should be NULL */
        /* targets is a linked list; value is separate */
        /* Actually with chaining, targets forms the chain and value is the final RHS */
        /* Clear the next pointer on the last target */
        last_target->next = NULL;

        ASTNode *node = ast_alloc(AST_ASSIGN, start_line, start_col);
        node->data.assign.targets = targets;
        node->data.assign.value = value;
        return node;
    }

    /* Augmented assignment: target op= value */
    {
        BinOp aug_op;
        int is_aug = 0;
        switch (current.type) {
        case TOK_PLUS_ASSIGN:         aug_op = OP_ADD; is_aug = 1; break;
        case TOK_MINUS_ASSIGN:        aug_op = OP_SUB; is_aug = 1; break;
        case TOK_STAR_ASSIGN:         aug_op = OP_MUL; is_aug = 1; break;
        case TOK_SLASH_ASSIGN:        aug_op = OP_DIV; is_aug = 1; break;
        case TOK_DOUBLESLASH_ASSIGN:  aug_op = OP_FLOORDIV; is_aug = 1; break;
        case TOK_PERCENT_ASSIGN:      aug_op = OP_MOD; is_aug = 1; break;
        case TOK_DOUBLESTAR_ASSIGN:   aug_op = OP_POW; is_aug = 1; break;
        case TOK_AMP_ASSIGN:          aug_op = OP_BITAND; is_aug = 1; break;
        case TOK_PIPE_ASSIGN:         aug_op = OP_BITOR; is_aug = 1; break;
        case TOK_CARET_ASSIGN:        aug_op = OP_BITXOR; is_aug = 1; break;
        case TOK_LSHIFT_ASSIGN:       aug_op = OP_LSHIFT; is_aug = 1; break;
        case TOK_RSHIFT_ASSIGN:       aug_op = OP_RSHIFT; is_aug = 1; break;
        case TOK_AT_ASSIGN:           aug_op = OP_MATMUL; is_aug = 1; break;
        default: is_aug = 0; aug_op = OP_ADD; break;
        }

        if (is_aug) {
            advance();
            ASTNode *value = parse_expr();
            ASTNode *node = ast_alloc(AST_AUG_ASSIGN, start_line, start_col);
            node->data.aug_assign.target = expr;
            node->data.aug_assign.op = aug_op;
            node->data.aug_assign.value = value;
            return node;
        }
    }

    /* Plain expression statement */
    ASTNode *node = ast_alloc(AST_EXPR_STMT, start_line, start_col);
    node->data.expr_stmt.expr = expr;
    return node;
}

/* ------------------------------------------------------------------ */
/* Expression parsing - precedence climbing                            */
/* ------------------------------------------------------------------ */

/* parse_star_expr: handles *expr for starred expressions and walrus */
ASTNode *Parser::parse_star_expr() {
    if (check(TOK_STAR)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *inner = parse_or_expr();
        ASTNode *node = ast_alloc(AST_STARRED, sl, sc);
        node->data.starred.value = inner;
        return node;
    }
    return parse_expr();
}

/* parse_expr: top-level expression - walrus and ternary */
ASTNode *Parser::parse_expr() {
    /* Check for lambda */
    if (check(TOK_LAMBDA)) {
        return parse_lambda();
    }

    ASTNode *node = parse_or_expr();
    if (!node) return NULL;

    /* Walrus operator := */
    if (check(TOK_WALRUS)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *value = parse_expr();
        ASTNode *walrus = ast_alloc(AST_WALRUS, sl, sc);
        walrus->data.walrus.target = node;
        walrus->data.walrus.value = value;
        return walrus;
    }

    /* Ternary: expr if condition else expr */
    if (check(TOK_IF)) {
        return parse_ifexpr(node);
    }

    return node;
}

/* parse_ifexpr: ternary conditional */
ASTNode *Parser::parse_ifexpr(ASTNode *body) {
    int sl = current.line, sc = current.col;
    expect(TOK_IF);
    ASTNode *test = parse_or_expr();
    expect(TOK_ELSE);
    ASTNode *else_body = parse_expr();

    ASTNode *node = ast_alloc(AST_IFEXPR, sl, sc);
    node->data.ifexpr.body = body;
    node->data.ifexpr.test = test;
    node->data.ifexpr.else_body = else_body;
    return node;
}

/* parse_or_expr: left-assoc 'or' */
ASTNode *Parser::parse_or_expr() {
    ASTNode *left = parse_and_expr();
    while (check(TOK_OR)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_and_expr();
        ASTNode *node = ast_alloc(AST_BOOLOP, sl, sc);
        node->data.boolop.op = BOOL_OR;
        /* Chain values as linked list */
        left->next = right;
        node->data.boolop.values = left;
        left = node;
    }
    return left;
}

/* parse_and_expr: left-assoc 'and' */
ASTNode *Parser::parse_and_expr() {
    ASTNode *left = parse_not_expr();
    while (check(TOK_AND)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_not_expr();
        ASTNode *node = ast_alloc(AST_BOOLOP, sl, sc);
        node->data.boolop.op = BOOL_AND;
        left->next = right;
        node->data.boolop.values = left;
        left = node;
    }
    return left;
}

/* parse_not_expr: 'not' prefix */
ASTNode *Parser::parse_not_expr() {
    if (check(TOK_NOT)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *operand = parse_not_expr();
        ASTNode *node = ast_alloc(AST_UNARYOP, sl, sc);
        node->data.unaryop.op = UNARY_NOT;
        node->data.unaryop.operand = operand;
        return node;
    }
    return parse_comparison();
}

/* parse_comparison: chained comparisons */
ASTNode *Parser::parse_comparison() {
    ASTNode *left = parse_bitor_expr();

    /* Check if this is a comparison */
    CmpOp ops[16];
    ASTNode *comparators_first = NULL;
    ASTNode *comparators_last = NULL;
    int num_ops = 0;

    for (;;) {
        CmpOp op;
        int is_cmp = 0;

        switch (current.type) {
        case TOK_EQ: op = CMP_EQ; is_cmp = 1; break;
        case TOK_NE: op = CMP_NE; is_cmp = 1; break;
        case TOK_LT: op = CMP_LT; is_cmp = 1; break;
        case TOK_GT: op = CMP_GT; is_cmp = 1; break;
        case TOK_LE: op = CMP_LE; is_cmp = 1; break;
        case TOK_GE: op = CMP_GE; is_cmp = 1; break;
        case TOK_IS: {
            advance();
            if (check(TOK_NOT)) {
                advance();
                op = CMP_IS_NOT;
            } else {
                op = CMP_IS;
            }
            is_cmp = 2; /* already advanced */
            break;
        }
        case TOK_IN:
            op = CMP_IN; is_cmp = 1; break;
        case TOK_NOT: {
            /* peek for 'not in' */
            Token p = peek();
            if (p.type == TOK_IN) {
                advance(); /* consume 'not' */
                advance(); /* consume 'in' */
                op = CMP_NOT_IN;
                is_cmp = 2;
            } else {
                is_cmp = 0;
            }
            break;
        }
        default:
            is_cmp = 0;
            break;
        }

        if (!is_cmp) break;

        if (is_cmp == 1) advance();

        if (num_ops < 16) {
            ops[num_ops++] = op;
        }

        ASTNode *right = parse_bitor_expr();
        if (right) {
            right->next = NULL;
            if (!comparators_first) comparators_first = right;
            else comparators_last->next = right;
            comparators_last = right;
        }
    }

    if (num_ops == 0) return left;

    /* Build compare node */
    ASTNode *node = ast_alloc(AST_COMPARE, left->line, left->col);
    node->data.compare.left = left;
    node->data.compare.num_ops = num_ops;

    CmpOp *ops_copy = cmpop_alloc(num_ops);
    int i;
    for (i = 0; i < num_ops; i++) ops_copy[i] = ops[i];
    node->data.compare.ops = ops_copy;
    node->data.compare.comparators = comparators_first;
    return node;
}

/* parse_bitor_expr: | */
ASTNode *Parser::parse_bitor_expr() {
    ASTNode *left = parse_bitxor_expr();
    while (check(TOK_PIPE)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_bitxor_expr();
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = OP_BITOR;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_bitxor_expr: ^ */
ASTNode *Parser::parse_bitxor_expr() {
    ASTNode *left = parse_bitand_expr();
    while (check(TOK_CARET)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_bitand_expr();
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = OP_BITXOR;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_bitand_expr: & */
ASTNode *Parser::parse_bitand_expr() {
    ASTNode *left = parse_shift_expr();
    while (check(TOK_AMP)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_shift_expr();
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = OP_BITAND;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_shift_expr: << >> */
ASTNode *Parser::parse_shift_expr() {
    ASTNode *left = parse_add_expr();
    while (check(TOK_LSHIFT) || check(TOK_RSHIFT)) {
        int sl = current.line, sc = current.col;
        BinOp op = check(TOK_LSHIFT) ? OP_LSHIFT : OP_RSHIFT;
        advance();
        ASTNode *right = parse_add_expr();
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = op;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_add_expr: + - */
ASTNode *Parser::parse_add_expr() {
    ASTNode *left = parse_mul_expr();
    while (check(TOK_PLUS) || check(TOK_MINUS)) {
        int sl = current.line, sc = current.col;
        BinOp op = check(TOK_PLUS) ? OP_ADD : OP_SUB;
        advance();
        ASTNode *right = parse_mul_expr();
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = op;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_mul_expr: * / // % @ */
ASTNode *Parser::parse_mul_expr() {
    ASTNode *left = parse_unary_expr();
    for (;;) {
        BinOp op;
        int is_mul = 0;
        switch (current.type) {
        case TOK_STAR:        op = OP_MUL; is_mul = 1; break;
        case TOK_SLASH:       op = OP_DIV; is_mul = 1; break;
        case TOK_DOUBLESLASH: op = OP_FLOORDIV; is_mul = 1; break;
        case TOK_PERCENT:     op = OP_MOD; is_mul = 1; break;
        case TOK_AT:          op = OP_MATMUL; is_mul = 1; break;
        default: is_mul = 0; op = OP_MUL; break;
        }
        if (!is_mul) break;
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_unary_expr();
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = op;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_unary_expr: - + ~ */
ASTNode *Parser::parse_unary_expr() {
    if (check(TOK_MINUS) || check(TOK_PLUS) || check(TOK_TILDE)) {
        int sl = current.line, sc = current.col;
        UnaryOp op;
        switch (current.type) {
        case TOK_MINUS: op = UNARY_NEG; break;
        case TOK_PLUS:  op = UNARY_POS; break;
        case TOK_TILDE: op = UNARY_BITNOT; break;
        default:        op = UNARY_NEG; break;
        }
        advance();
        ASTNode *operand = parse_unary_expr();
        ASTNode *node = ast_alloc(AST_UNARYOP, sl, sc);
        node->data.unaryop.op = op;
        node->data.unaryop.operand = operand;
        return node;
    }

    /* await expression */
    if (check(TOK_AWAIT)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *operand = parse_unary_expr();
        ASTNode *node = ast_alloc(AST_AWAIT, sl, sc);
        node->data.starred.value = operand; /* reuses starred.value layout */
        return node;
    }

    return parse_power_expr();
}

/* parse_power_expr: ** (right-associative) */
ASTNode *Parser::parse_power_expr() {
    ASTNode *left = parse_postfix_expr();
    if (check(TOK_DOUBLESTAR)) {
        int sl = current.line, sc = current.col;
        advance();
        ASTNode *right = parse_unary_expr(); /* right-associative: recurse into unary */
        ASTNode *node = ast_alloc(AST_BINOP, sl, sc);
        node->data.binop.op = OP_POW;
        node->data.binop.left = left;
        node->data.binop.right = right;
        left = node;
    }
    return left;
}

/* parse_postfix_expr: calls (), attr .name, subscript [index] */
ASTNode *Parser::parse_postfix_expr() {
    ASTNode *node = parse_atom();

    for (;;) {
        if (check(TOK_LPAREN)) {
            /* Function call */
            int sl = current.line, sc = current.col;
            advance(); /* consume ( */

            ASTNode *args_first = NULL;
            ASTNode *args_last = NULL;
            int num_args = 0;

            while (!check(TOK_RPAREN) && !check(TOK_EOF)) {
                ASTNode *arg = NULL;

                /* Check for **kwargs */
                if (check(TOK_DOUBLESTAR)) {
                    int kl = current.line, kc = current.col;
                    advance();
                    ASTNode *val = parse_expr();
                    arg = ast_alloc(AST_KEYWORD_ARG, kl, kc);
                    arg->data.keyword_arg.key = NULL; /* NULL key = **kwargs */
                    arg->data.keyword_arg.kw_value = val;
                }
                /* Check for *args */
                else if (check(TOK_STAR)) {
                    int kl = current.line, kc = current.col;
                    advance();
                    ASTNode *val = parse_expr();
                    arg = ast_alloc(AST_STARRED, kl, kc);
                    arg->data.starred.value = val;
                }
                /* Check for keyword=value */
                else if (check(TOK_IDENTIFIER)) {
                    Token id_tok = current;
                    Token p = peek();
                    if (p.type == TOK_ASSIGN) {
                        /* keyword argument */
                        int kl = current.line, kc = current.col;
                        const char *key = current.text;
                        advance(); /* consume identifier */
                        advance(); /* consume = */
                        ASTNode *val = parse_expr();
                        arg = ast_alloc(AST_KEYWORD_ARG, kl, kc);
                        arg->data.keyword_arg.key = key;
                        arg->data.keyword_arg.kw_value = val;
                    } else {
                        arg = parse_expr();
                    }
                } else {
                    arg = parse_expr();
                }

                /* Generator expression as call argument: func(expr for x in ...) */
                if (arg && check(TOK_FOR)) {
                    int gel = arg->line, gec = arg->col;
                    ASTNode *gen = ast_alloc(AST_GENEXPR, gel, gec);
                    gen->data.listcomp.elt = arg;

                    ASTNode *gf = NULL, *gl2 = NULL;
                    while (check(TOK_FOR)) {
                        int gll = current.line, glc = current.col;
                        advance();
                        ASTNode *tgt = NULL;
                        if (check(TOK_IDENTIFIER)) {
                            tgt = ast_alloc(AST_NAME, current.line, current.col);
                            tgt->data.name.id = current.text;
                            advance();
                        } else {
                            tgt = parse_atom();
                        }
                        if (check(TOK_COMMA)) {
                            ASTNode *tup = ast_alloc(AST_TUPLE_EXPR, tgt->line, tgt->col);
                            ASTNode *ttf = tgt, *ttl = tgt;
                            while (match(TOK_COMMA)) {
                                if (check(TOK_IN)) break;
                                if (check(TOK_IDENTIFIER)) {
                                    ASTNode *te = ast_alloc(AST_NAME, current.line, current.col);
                                    te->data.name.id = current.text;
                                    advance();
                                    te->next = NULL;
                                    ttl->next = te; ttl = te;
                                }
                            }
                            tup->data.collection.elts = ttf;
                            tgt = tup;
                        }
                        expect(TOK_IN);
                        ASTNode *itr = parse_or_expr();
                        ASTNode *cg = ast_alloc(AST_COMP_GENERATOR, gll, glc);
                        cg->data.comp_gen.target = tgt;
                        cg->data.comp_gen.iter = itr;
                        cg->data.comp_gen.ifs = NULL;
                        cg->data.comp_gen.is_async = 0;
                        ASTNode *cifs_f = NULL, *cifs_l = NULL;
                        while (check(TOK_IF)) {
                            advance();
                            ASTNode *cond = parse_or_expr();
                            if (cond) { cond->next = NULL; if (!cifs_f) cifs_f = cond; else cifs_l->next = cond; cifs_l = cond; }
                        }
                        cg->data.comp_gen.ifs = cifs_f;
                        if (!gf) gf = cg; else gl2->next = cg;
                        gl2 = cg;
                    }
                    gen->data.listcomp.generators = gf;
                    arg = gen;
                }

                if (arg) {
                    arg->next = NULL;
                    if (!args_first) args_first = arg;
                    else args_last->next = arg;
                    args_last = arg;
                    num_args++;
                }

                if (!match(TOK_COMMA)) break;
                /* Allow trailing comma */
                if (check(TOK_RPAREN)) break;
            }

            expect(TOK_RPAREN);

            ASTNode *call = ast_alloc(AST_CALL, sl, sc);
            call->data.call.func = node;
            call->data.call.args = args_first;
            call->data.call.num_args = num_args;
            node = call;
        }
        else if (check(TOK_DOT)) {
            /* Attribute access */
            int sl = current.line, sc = current.col;
            advance(); /* consume . */
            if (!check(TOK_IDENTIFIER)) {
                error("Expected attribute name after '.'");
                break;
            }
            const char *attr = current.text;
            advance();

            ASTNode *attr_node = ast_alloc(AST_ATTR, sl, sc);
            attr_node->data.attribute.object = node;
            attr_node->data.attribute.attr = attr;
            node = attr_node;
        }
        else if (check(TOK_LBRACKET)) {
            /* Subscript */
            int sl = current.line, sc = current.col;
            advance(); /* consume [ */

            ASTNode *index = NULL;

            /* Check for slice */
            if (check(TOK_COLON)) {
                /* Slice starting with : (no lower bound) */
                ASTNode *slice_node = ast_alloc(AST_SLICE, sl, sc);
                slice_node->data.slice.lower = NULL;
                advance(); /* consume : */
                if (!check(TOK_RBRACKET) && !check(TOK_COLON)) {
                    slice_node->data.slice.upper = parse_expr();
                }
                if (match(TOK_COLON)) {
                    if (!check(TOK_RBRACKET)) {
                        slice_node->data.slice.step = parse_expr();
                    }
                }
                index = slice_node;
            } else {
                ASTNode *first_expr = parse_expr();
                if (check(TOK_COLON)) {
                    /* It's a slice */
                    ASTNode *slice_node = ast_alloc(AST_SLICE, sl, sc);
                    slice_node->data.slice.lower = first_expr;
                    advance(); /* consume : */
                    if (!check(TOK_RBRACKET) && !check(TOK_COLON)) {
                        slice_node->data.slice.upper = parse_expr();
                    }
                    if (match(TOK_COLON)) {
                        if (!check(TOK_RBRACKET)) {
                            slice_node->data.slice.step = parse_expr();
                        }
                    }
                    index = slice_node;
                } else {
                    index = first_expr;
                }
            }

            expect(TOK_RBRACKET);

            ASTNode *sub = ast_alloc(AST_SUBSCRIPT, sl, sc);
            sub->data.subscript.object = node;
            sub->data.subscript.index = index;
            node = sub;
        }
        else {
            break;
        }
    }

    return node;
}

/* Slice parsing is handled inline within parse_postfix_expr's subscript section */

/* ------------------------------------------------------------------ */
/* parse_atom                                                          */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_atom() {
    int sl = current.line, sc = current.col;

    switch (current.type) {
    case TOK_INT_LIT: {
        ASTNode *node = ast_alloc(AST_INT_LIT, sl, sc);
        node->data.int_lit.value = current.int_value;
        advance();
        return node;
    }
    case TOK_FLOAT_LIT: {
        ASTNode *node = ast_alloc(AST_FLOAT_LIT, sl, sc);
        node->data.float_lit.value = current.float_value;
        advance();
        return node;
    }
    case TOK_COMPLEX_LIT: {
        ASTNode *node = ast_alloc(AST_COMPLEX_LIT, sl, sc);
        node->data.complex_lit.imag = current.float_value;
        advance();
        return node;
    }
    case TOK_STRING_LIT: {
        ASTNode *node = ast_alloc(AST_STR_LIT, sl, sc);
        node->data.str_lit.value = current.text;
        node->data.str_lit.len = current.text_len;
        advance();

        /* Handle implicit string concatenation: "a" "b" -> "ab" */
        while (check(TOK_STRING_LIT)) {
            /* For simplicity, we don't actually concatenate here -
               we just create another STR_LIT. A later pass can handle it. */
            /* Actually let's create a proper concat by chaining with binop */
            ASTNode *right = ast_alloc(AST_STR_LIT, current.line, current.col);
            right->data.str_lit.value = current.text;
            right->data.str_lit.len = current.text_len;
            advance();

            ASTNode *concat = ast_alloc(AST_BINOP, sl, sc);
            concat->data.binop.op = OP_ADD;
            concat->data.binop.left = node;
            concat->data.binop.right = right;
            node = concat;
        }
        return node;
    }
    case TOK_BYTES_LIT: {
        ASTNode *node = ast_alloc(AST_STR_LIT, sl, sc);
        node->data.str_lit.value = current.text;
        node->data.str_lit.len = current.text_len;
        advance();
        return node;
    }
    case TOK_FSTRING_START: {
        return parse_fstring();
    }
    case TOK_TRUE: {
        ASTNode *node = ast_alloc(AST_BOOL_LIT, sl, sc);
        node->data.bool_lit.value = 1;
        advance();
        return node;
    }
    case TOK_FALSE: {
        ASTNode *node = ast_alloc(AST_BOOL_LIT, sl, sc);
        node->data.bool_lit.value = 0;
        advance();
        return node;
    }
    case TOK_NONE: {
        ASTNode *node = ast_alloc(AST_NONE_LIT, sl, sc);
        advance();
        return node;
    }
    case TOK_IDENTIFIER: {
        ASTNode *node = ast_alloc(AST_NAME, sl, sc);
        node->data.name.id = current.text;
        advance();
        return node;
    }
    case TOK_LPAREN: {
        return parse_tuple_or_paren();
    }
    case TOK_LBRACKET: {
        return parse_list_expr();
    }
    case TOK_LBRACE: {
        return parse_dict_or_set_expr();
    }
    case TOK_ELLIPSIS: {
        ASTNode *node = ast_alloc(AST_NONE_LIT, sl, sc); /* Ellipsis as special constant */
        advance();
        return node;
    }
    case TOK_YIELD: {
        advance();
        int is_from = 0;
        if (match(TOK_FROM)) is_from = 1;
        ASTNode *value = NULL;
        if (!check(TOK_NEWLINE) && !check(TOK_EOF) && !check(TOK_RPAREN) &&
            !check(TOK_RBRACKET) && !check(TOK_COMMA)) {
            value = parse_expr();
        }
        ASTNode *node = ast_alloc(is_from ? AST_YIELD_FROM_EXPR : AST_YIELD_EXPR, sl, sc);
        node->data.yield_expr.value = value;
        node->data.yield_expr.is_from = is_from;
        return node;
    }
    default:
        error("Expected expression");
        synchronize();
        return ast_alloc(AST_NONE_LIT, sl, sc); /* return a dummy node */
    }
}

/* ------------------------------------------------------------------ */
/* parse_tuple_or_paren                                                */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_tuple_or_paren() {
    int sl = current.line, sc = current.col;
    expect(TOK_LPAREN);

    /* Empty tuple */
    if (check(TOK_RPAREN)) {
        advance();
        ASTNode *node = ast_alloc(AST_TUPLE_EXPR, sl, sc);
        node->data.collection.elts = NULL;
        return node;
    }

    /* Parse first expression */
    ASTNode *first = parse_star_expr();

    /* Generator expression: (expr for ...) */
    if (check(TOK_FOR)) {
        ASTNode *gen = ast_alloc(AST_GENEXPR, sl, sc);
        gen->data.listcomp.elt = first;

        ASTNode *gens_first = NULL;
        ASTNode *gens_last = NULL;

        while (check(TOK_FOR)) {
            int gl = current.line, gc = current.col;
            advance(); /* consume 'for' */

            /* Parse target: just a name or comma-separated names */
            ASTNode *target = NULL;
            if (check(TOK_IDENTIFIER)) {
                target = ast_alloc(AST_NAME, current.line, current.col);
                target->data.name.id = current.text;
                advance();
            } else {
                target = parse_atom();
            }

            /* Handle tuple target */
            if (check(TOK_COMMA)) {
                ASTNode *tup = ast_alloc(AST_TUPLE_EXPR, target->line, target->col);
                ASTNode *tf = target;
                ASTNode *tl = target;
                while (match(TOK_COMMA)) {
                    if (check(TOK_IN)) break;
                    if (check(TOK_IDENTIFIER)) {
                        ASTNode *te = ast_alloc(AST_NAME, current.line, current.col);
                        te->data.name.id = current.text;
                        advance();
                        te->next = NULL;
                        tl->next = te; tl = te;
                    }
                }
                tup->data.collection.elts = tf;
                target = tup;
            }

            expect(TOK_IN);
            ASTNode *iter = parse_or_expr();

            ASTNode *comp_gen = ast_alloc(AST_COMP_GENERATOR, gl, gc);
            comp_gen->data.comp_gen.target = target;
            comp_gen->data.comp_gen.iter = iter;
            comp_gen->data.comp_gen.ifs = NULL;
            comp_gen->data.comp_gen.is_async = 0;

            /* Parse if clauses */
            ASTNode *ifs_first = NULL;
            ASTNode *ifs_last = NULL;
            while (check(TOK_IF)) {
                advance();
                ASTNode *cond = parse_or_expr();
                if (cond) {
                    cond->next = NULL;
                    if (!ifs_first) ifs_first = cond;
                    else ifs_last->next = cond;
                    ifs_last = cond;
                }
            }
            comp_gen->data.comp_gen.ifs = ifs_first;

            if (!gens_first) gens_first = comp_gen;
            else gens_last->next = comp_gen;
            gens_last = comp_gen;
        }

        gen->data.listcomp.generators = gens_first;
        expect(TOK_RPAREN);
        return gen;
    }

    /* Check if it's a tuple (has comma) or just parenthesized expr */
    if (check(TOK_COMMA)) {
        ASTNode *tuple_node = ast_alloc(AST_TUPLE_EXPR, sl, sc);
        ASTNode *last = first;

        while (match(TOK_COMMA)) {
            if (check(TOK_RPAREN)) break; /* trailing comma */
            ASTNode *elt = parse_star_expr();
            if (elt) {
                last->next = elt;
                last = elt;
                last->next = NULL;
            }
        }
        tuple_node->data.collection.elts = first;
        expect(TOK_RPAREN);
        return tuple_node;
    }

    /* Just a parenthesized expression */
    expect(TOK_RPAREN);
    return first;
}

/* ------------------------------------------------------------------ */
/* parse_list_expr - list literal or list comprehension                */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_list_expr() {
    int sl = current.line, sc = current.col;
    expect(TOK_LBRACKET);

    if (check(TOK_RBRACKET)) {
        advance();
        ASTNode *node = ast_alloc(AST_LIST_EXPR, sl, sc);
        node->data.collection.elts = NULL;
        return node;
    }

    ASTNode *first = parse_star_expr();

    /* List comprehension: [expr for x in iter] */
    if (check(TOK_FOR)) {
        ASTNode *comp = ast_alloc(AST_LISTCOMP, sl, sc);
        comp->data.listcomp.elt = first;

        ASTNode *gens_first = NULL;
        ASTNode *gens_last = NULL;

        while (check(TOK_FOR)) {
            int gl = current.line, gc = current.col;
            advance();

            /* Parse target: just a name or comma-separated names,
             * NOT a full expression (to avoid consuming 'in' as
             * a comparison operator). */
            ASTNode *target = NULL;
            if (check(TOK_IDENTIFIER)) {
                target = ast_alloc(AST_NAME, current.line, current.col);
                target->data.name.id = current.text;
                advance();
            } else {
                target = parse_atom();
            }

            if (check(TOK_COMMA)) {
                ASTNode *tup = ast_alloc(AST_TUPLE_EXPR, target->line, target->col);
                ASTNode *tf = target;
                ASTNode *tl = target;
                while (match(TOK_COMMA)) {
                    if (check(TOK_IN)) break;
                    if (check(TOK_IDENTIFIER)) {
                        ASTNode *te = ast_alloc(AST_NAME, current.line, current.col);
                        te->data.name.id = current.text;
                        advance();
                        te->next = NULL;
                        tl->next = te; tl = te;
                    }
                }
                tup->data.collection.elts = tf;
                target = tup;
            }

            expect(TOK_IN);
            ASTNode *iter = parse_or_expr();

            ASTNode *comp_gen = ast_alloc(AST_COMP_GENERATOR, gl, gc);
            comp_gen->data.comp_gen.target = target;
            comp_gen->data.comp_gen.iter = iter;
            comp_gen->data.comp_gen.ifs = NULL;
            comp_gen->data.comp_gen.is_async = 0;

            ASTNode *ifs_first = NULL;
            ASTNode *ifs_last = NULL;
            while (check(TOK_IF)) {
                advance();
                ASTNode *cond = parse_or_expr();
                if (cond) {
                    cond->next = NULL;
                    if (!ifs_first) ifs_first = cond;
                    else ifs_last->next = cond;
                    ifs_last = cond;
                }
            }
            comp_gen->data.comp_gen.ifs = ifs_first;

            if (!gens_first) gens_first = comp_gen;
            else gens_last->next = comp_gen;
            gens_last = comp_gen;
        }

        comp->data.listcomp.generators = gens_first;
        expect(TOK_RBRACKET);
        return comp;
    }

    /* Regular list */
    ASTNode *list_node = ast_alloc(AST_LIST_EXPR, sl, sc);
    ASTNode *last = first;

    while (match(TOK_COMMA)) {
        if (check(TOK_RBRACKET)) break;
        ASTNode *elt = parse_star_expr();
        if (elt) {
            last->next = elt;
            last = elt;
            last->next = NULL;
        }
    }

    list_node->data.collection.elts = first;
    expect(TOK_RBRACKET);
    return list_node;
}

/* ------------------------------------------------------------------ */
/* parse_dict_or_set_expr                                              */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_dict_or_set_expr() {
    int sl = current.line, sc = current.col;
    expect(TOK_LBRACE);

    /* Empty dict */
    if (check(TOK_RBRACE)) {
        advance();
        ASTNode *node = ast_alloc(AST_DICT_EXPR, sl, sc);
        node->data.dict.keys = NULL;
        node->data.dict.values = NULL;
        return node;
    }

    /* Check for **unpack in dict */
    if (check(TOK_DOUBLESTAR)) {
        /* Dict with unpacking - treat first element as key=NULL, val=expr */
        goto parse_as_dict;
    }

    {
        ASTNode *first = parse_expr();

        /* Dict comprehension or dict literal: key: value */
        if (check(TOK_COLON)) {
            advance(); /* consume : */
            ASTNode *first_val = parse_expr();

            /* Dict comprehension: {key: value for ...} */
            if (check(TOK_FOR)) {
                ASTNode *dc = ast_alloc(AST_DICTCOMP, sl, sc);
                dc->data.dictcomp.key = first;
                dc->data.dictcomp.value = first_val;

                ASTNode *gens_first = NULL;
                ASTNode *gens_last = NULL;

                while (check(TOK_FOR)) {
                    int gl = current.line, gc = current.col;
                    advance();
                    /* Use parse_postfix_expr to avoid consuming 'in' as comparison */
                    ASTNode *target = parse_postfix_expr();
                    if (check(TOK_COMMA)) {
                        ASTNode *tup = ast_alloc(AST_TUPLE_EXPR, target->line, target->col);
                        ASTNode *tf = target, *tl = target;
                        while (match(TOK_COMMA)) {
                            if (check(TOK_IN)) break;
                            ASTNode *te = parse_postfix_expr();
                            if (te) { tl->next = te; tl = te; tl->next = NULL; }
                        }
                        tup->data.collection.elts = tf;
                        target = tup;
                    }
                    expect(TOK_IN);
                    ASTNode *iter = parse_or_expr();
                    ASTNode *cg = ast_alloc(AST_COMP_GENERATOR, gl, gc);
                    cg->data.comp_gen.target = target;
                    cg->data.comp_gen.iter = iter;
                    cg->data.comp_gen.ifs = NULL;

                    ASTNode *ifs_f = NULL, *ifs_l = NULL;
                    while (check(TOK_IF)) {
                        advance();
                        ASTNode *cond = parse_or_expr();
                        if (cond) { cond->next = NULL; if (!ifs_f) ifs_f = cond; else ifs_l->next = cond; ifs_l = cond; }
                    }
                    cg->data.comp_gen.ifs = ifs_f;

                    if (!gens_first) gens_first = cg;
                    else gens_last->next = cg;
                    gens_last = cg;
                }

                dc->data.dictcomp.generators = gens_first;
                expect(TOK_RBRACE);
                return dc;
            }

            /* Regular dict literal */
            ASTNode *dict_node = ast_alloc(AST_DICT_EXPR, sl, sc);
            ASTNode *keys_first = first;
            ASTNode *keys_last = first;
            ASTNode *vals_first = first_val;
            ASTNode *vals_last = first_val;
            keys_last->next = NULL;
            vals_last->next = NULL;

            while (match(TOK_COMMA)) {
                if (check(TOK_RBRACE)) break;

                ASTNode *key;
                if (check(TOK_DOUBLESTAR)) {
                    /* ** dict unpacking - key is NULL conceptually but we use a special marker */
                    advance();
                    key = ast_alloc(AST_NONE_LIT, current.line, current.col);
                } else {
                    key = parse_expr();
                }

                ASTNode *val;
                if (key->kind != AST_NONE_LIT || check(TOK_COLON)) {
                    /* Has a colon for regular key:val or we need to parse value for ** */
                    if (check(TOK_COLON)) {
                        advance();
                        val = parse_expr();
                    } else {
                        /* **expr case - the 'key' is actually the unpacked expr */
                        val = key;
                        key = ast_alloc(AST_NONE_LIT, current.line, current.col);
                    }
                } else {
                    val = parse_expr();
                }

                key->next = NULL;
                val->next = NULL;
                keys_last->next = key;
                keys_last = key;
                vals_last->next = val;
                vals_last = val;
            }

            dict_node->data.dict.keys = keys_first;
            dict_node->data.dict.values = vals_first;
            expect(TOK_RBRACE);
            return dict_node;
        }

        /* Set literal or set comprehension */
        if (check(TOK_FOR)) {
            /* Set comprehension: {expr for ...} */
            ASTNode *sc_node = ast_alloc(AST_SETCOMP, sl, sc);
            sc_node->data.listcomp.elt = first;

            ASTNode *gens_first = NULL;
            ASTNode *gens_last = NULL;

            while (check(TOK_FOR)) {
                int gl = current.line, gc = current.col;
                advance();
                /* Use parse_postfix_expr to avoid consuming 'in' as comparison */
                ASTNode *target = parse_postfix_expr();
                if (check(TOK_COMMA)) {
                    ASTNode *tup = ast_alloc(AST_TUPLE_EXPR, target->line, target->col);
                    ASTNode *tf = target, *tl = target;
                    while (match(TOK_COMMA)) {
                        if (check(TOK_IN)) break;
                        ASTNode *te = parse_postfix_expr();
                        if (te) { tl->next = te; tl = te; tl->next = NULL; }
                    }
                    tup->data.collection.elts = tf;
                    target = tup;
                }
                expect(TOK_IN);
                ASTNode *iter = parse_or_expr();
                ASTNode *cg = ast_alloc(AST_COMP_GENERATOR, gl, gc);
                cg->data.comp_gen.target = target;
                cg->data.comp_gen.iter = iter;
                cg->data.comp_gen.ifs = NULL;

                ASTNode *ifs_f = NULL, *ifs_l = NULL;
                while (check(TOK_IF)) {
                    advance();
                    ASTNode *cond = parse_or_expr();
                    if (cond) { cond->next = NULL; if (!ifs_f) ifs_f = cond; else ifs_l->next = cond; ifs_l = cond; }
                }
                cg->data.comp_gen.ifs = ifs_f;

                if (!gens_first) gens_first = cg;
                else gens_last->next = cg;
                gens_last = cg;
            }

            sc_node->data.listcomp.generators = gens_first;
            expect(TOK_RBRACE);
            return sc_node;
        }

        /* Regular set literal */
        ASTNode *set_node = ast_alloc(AST_SET_EXPR, sl, sc);
        ASTNode *last = first;
        first->next = NULL;

        while (match(TOK_COMMA)) {
            if (check(TOK_RBRACE)) break;
            ASTNode *elt = parse_star_expr();
            if (elt) {
                last->next = elt;
                last = elt;
                last->next = NULL;
            }
        }

        set_node->data.collection.elts = first;
        expect(TOK_RBRACE);
        return set_node;
    }

parse_as_dict:
    {
        /* Dict starting with ** unpacking */
        ASTNode *dict_node = ast_alloc(AST_DICT_EXPR, sl, sc);
        ASTNode *keys_first = NULL, *keys_last = NULL;
        ASTNode *vals_first = NULL, *vals_last = NULL;

        for (;;) {
            if (check(TOK_RBRACE)) break;

            ASTNode *key;
            ASTNode *val;

            if (match(TOK_DOUBLESTAR)) {
                key = ast_alloc(AST_NONE_LIT, current.line, current.col);
                val = parse_expr();
            } else {
                key = parse_expr();
                expect(TOK_COLON);
                val = parse_expr();
            }

            key->next = NULL;
            val->next = NULL;
            if (!keys_first) { keys_first = key; vals_first = val; }
            else { keys_last->next = key; vals_last->next = val; }
            keys_last = key;
            vals_last = val;

            if (!match(TOK_COMMA)) break;
        }

        dict_node->data.dict.keys = keys_first;
        dict_node->data.dict.values = vals_first;
        expect(TOK_RBRACE);
        return dict_node;
    }
}

/* ------------------------------------------------------------------ */
/* parse_lambda                                                        */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_lambda() {
    int sl = current.line, sc = current.col;
    expect(TOK_LAMBDA);

    Param *params = NULL;
    Param *last_param = NULL;

    /* Parse lambda parameters (simplified: no type annotations required) */
    while (!check(TOK_COLON) && !check(TOK_EOF)) {
        Param *p = param_alloc();

        if (match(TOK_STAR)) {
            p->is_star = 1;
            if (check(TOK_IDENTIFIER)) { p->name = current.text; advance(); }
            else p->name = "*";
        } else if (match(TOK_DOUBLESTAR)) {
            p->is_double_star = 1;
            if (check(TOK_IDENTIFIER)) { p->name = current.text; advance(); }
        } else if (check(TOK_IDENTIFIER)) {
            p->name = current.text;
            advance();
        } else {
            break;
        }

        if (match(TOK_ASSIGN)) {
            p->default_val = parse_expr();
        }

        if (!params) params = p;
        else last_param->next = p;
        last_param = p;

        if (!match(TOK_COMMA)) break;
    }

    expect(TOK_COLON);
    ASTNode *body = parse_expr();

    ASTNode *node = ast_alloc(AST_LAMBDA, sl, sc);
    node->data.lambda.params = params;
    node->data.lambda.body = body;
    return node;
}

/* ------------------------------------------------------------------ */
/* parse_fstring                                                       */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_fstring() {
    int sl = current.line, sc = current.col;
    ASTNode *fstr = ast_alloc(AST_FSTRING, sl, sc);
    ASTNode *parts_first = NULL;
    ASTNode *parts_last = NULL;

    /* current token is FSTRING_START */
    if (current.text_len > 0) {
        ASTNode *text_part = ast_alloc(AST_STR_LIT, sl, sc);
        text_part->data.str_lit.value = current.text;
        text_part->data.str_lit.len = current.text_len;
        parts_first = text_part;
        parts_last = text_part;
    }
    advance(); /* consume FSTRING_START */

    /* Now parse expression tokens until FSTRING_MID or FSTRING_END */
    for (;;) {
        /* Parse the expression inside {} */
        if (!check(TOK_FSTRING_MID) && !check(TOK_FSTRING_END) && !check(TOK_EOF)) {
            ASTNode *expr = parse_expr();
            if (expr) {
                expr->next = NULL;
                if (!parts_first) parts_first = expr;
                else { parts_last->next = expr; }
                parts_last = expr;
            }
        }

        if (check(TOK_FSTRING_MID)) {
            if (current.text_len > 0) {
                ASTNode *mid = ast_alloc(AST_STR_LIT, current.line, current.col);
                mid->data.str_lit.value = current.text;
                mid->data.str_lit.len = current.text_len;
                mid->next = NULL;
                if (!parts_first) parts_first = mid;
                else parts_last->next = mid;
                parts_last = mid;
            }
            advance();
            continue; /* more expressions to come */
        }

        if (check(TOK_FSTRING_END)) {
            if (current.text_len > 0) {
                ASTNode *end_part = ast_alloc(AST_STR_LIT, current.line, current.col);
                end_part->data.str_lit.value = current.text;
                end_part->data.str_lit.len = current.text_len;
                end_part->next = NULL;
                if (!parts_first) parts_first = end_part;
                else parts_last->next = end_part;
                parts_last = end_part;
            }
            advance();
            break;
        }

        if (check(TOK_EOF)) {
            error("Unterminated f-string");
            break;
        }

        /* Shouldn't reach here normally */
        advance();
    }

    fstr->data.fstring.parts = parts_first;
    return fstr;
}

/* ------------------------------------------------------------------ */
/* Type annotation parsing                                             */
/* ------------------------------------------------------------------ */

ASTNode *Parser::parse_type_annotation() {
    return parse_type_union();
}

/* parse_type_union: type1 | type2 | type3 */
ASTNode *Parser::parse_type_union() {
    ASTNode *first = parse_type_primary();
    if (!first) return NULL;

    if (!check(TOK_PIPE)) return first;

    ASTNode *union_node = ast_alloc(AST_TYPE_UNION, first->line, first->col);
    ASTNode *last = first;

    while (match(TOK_PIPE)) {
        ASTNode *next_type = parse_type_primary();
        if (next_type) {
            last->next = next_type;
            last = next_type;
            last->next = NULL;
        }
    }

    union_node->data.type_union.types = first;
    return union_node;
}

/* parse_type_primary: name, name[args], None, ... */
ASTNode *Parser::parse_type_primary() {
    int sl = current.line, sc = current.col;

    if (check(TOK_NONE)) {
        advance();
        ASTNode *node = ast_alloc(AST_TYPE_NAME, sl, sc);
        node->data.type_name.tname = "None";
        return node;
    }

    if (check(TOK_IDENTIFIER)) {
        const char *name = current.text;
        advance();

        /* Check for generic: name[...] */
        if (check(TOK_LBRACKET)) {
            advance(); /* consume [ */

            ASTNode *args_first = NULL;
            ASTNode *args_last = NULL;
            int num_args = 0;

            /* Special case: Callable[[param_types], return_type] */
            if (check(TOK_LBRACKET)) {
                advance(); /* consume inner [ */
                ASTNode *param_types = NULL;
                ASTNode *ptl = NULL;
                while (!check(TOK_RBRACKET) && !check(TOK_EOF)) {
                    ASTNode *pt = parse_type_annotation();
                    if (pt) {
                        pt->next = NULL;
                        if (!param_types) param_types = pt;
                        else ptl->next = pt;
                        ptl = pt;
                    }
                    if (!match(TOK_COMMA)) break;
                }
                expect(TOK_RBRACKET);

                /* Wrap param types in a tuple type node */
                ASTNode *ptuple = ast_alloc(AST_TYPE_TUPLE, sl, sc);
                ptuple->data.type_union.types = param_types;
                args_first = ptuple;
                args_last = ptuple;
                num_args = 1;

                if (match(TOK_COMMA)) {
                    ASTNode *ret_type = parse_type_annotation();
                    if (ret_type) {
                        args_last->next = ret_type;
                        args_last = ret_type;
                        ret_type->next = NULL;
                        num_args++;
                    }
                }
            } else {
                for (;;) {
                    if (check(TOK_RBRACKET)) break;
                    ASTNode *arg = parse_type_annotation();
                    if (arg) {
                        arg->next = NULL;
                        if (!args_first) args_first = arg;
                        else args_last->next = arg;
                        args_last = arg;
                        num_args++;
                    }
                    if (!match(TOK_COMMA)) break;
                }
            }

            expect(TOK_RBRACKET);

            ASTNode *node = ast_alloc(AST_TYPE_GENERIC, sl, sc);
            node->data.type_generic.gname = name;
            node->data.type_generic.type_args = args_first;
            node->data.type_generic.num_type_args = num_args;
            return node;
        }

        /* Simple type name */
        ASTNode *node = ast_alloc(AST_TYPE_NAME, sl, sc);
        node->data.type_name.tname = name;
        return node;
    }

    /* Parenthesized type */
    if (check(TOK_LPAREN)) {
        advance();
        ASTNode *inner = parse_type_annotation();
        expect(TOK_RPAREN);
        return inner;
    }

    /* String literal type (forward reference) */
    if (check(TOK_STRING_LIT)) {
        ASTNode *node = ast_alloc(AST_TYPE_NAME, sl, sc);
        node->data.type_name.tname = current.text;
        advance();
        return node;
    }

    error("Expected type annotation");
    return NULL;
}
