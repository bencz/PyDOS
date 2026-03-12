#ifndef PARSER_H
#define PARSER_H

/*
 * parser.h - Recursive descent parser for PyDOS Python-to-8086 compiler
 *
 * Parses Python 3.11+ source into an AST. Uses precedence climbing for
 * expressions. Handles: function/class definitions, control flow,
 * assignments (including annotated and augmented), type annotations,
 * list/dict/tuple/set literals, comprehensions, lambda, ternary, and more.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "lexer.h"
#include "ast.h"

class Parser {
public:
    Parser();
    ~Parser();
    void init(Lexer *lexer);
    ASTNode *parse_module();
    int get_error_count() const;

private:
    Lexer *lex;
    Token current;
    int error_count;

    /* Token management */
    void advance();
    Token peek();
    int match(TokenType type);
    void expect(TokenType type);
    int check(TokenType type);

    /* Statement parsing */
    ASTNode *parse_statement();
    ASTNode *parse_simple_stmt();
    ASTNode *parse_compound_stmt();
    ASTNode *parse_funcdef(ASTNode *decorators, int is_async);
    ASTNode *parse_classdef(ASTNode *decorators);
    ASTNode *parse_if();
    ASTNode *parse_while();
    ASTNode *parse_for();
    ASTNode *parse_try();
    ASTNode *parse_with();
    ASTNode *parse_match();
    ASTNode *parse_return();
    ASTNode *parse_raise();
    ASTNode *parse_import();
    ASTNode *parse_import_from();
    ASTNode *parse_assign_or_expr();
    ASTNode *parse_del();
    ASTNode *parse_global_or_nonlocal(ASTKind kind);
    ASTNode *parse_assert();
    ASTNode *parse_type_alias();
    ASTNode *parse_block();
    Param *parse_params();
    ASTNode *parse_decorators();

    /* Expression parsing (precedence climbing) */
    ASTNode *parse_expr();
    ASTNode *parse_or_expr();
    ASTNode *parse_and_expr();
    ASTNode *parse_not_expr();
    ASTNode *parse_comparison();
    ASTNode *parse_bitor_expr();
    ASTNode *parse_bitxor_expr();
    ASTNode *parse_bitand_expr();
    ASTNode *parse_shift_expr();
    ASTNode *parse_add_expr();
    ASTNode *parse_mul_expr();
    ASTNode *parse_unary_expr();
    ASTNode *parse_power_expr();
    ASTNode *parse_postfix_expr();
    ASTNode *parse_atom();
    ASTNode *parse_list_expr();
    ASTNode *parse_dict_or_set_expr();
    ASTNode *parse_tuple_or_paren();
    ASTNode *parse_lambda();
    ASTNode *parse_ifexpr(ASTNode *body);
    ASTNode *parse_fstring();
    ASTNode *parse_star_expr();

    /* Type annotation parsing */
    ASTNode *parse_type_annotation();
    ASTNode *parse_type_union();
    ASTNode *parse_type_primary();

    /* Error recovery */
    void error(const char *msg);
    void error_at(Token *tok, const char *msg);
    void synchronize();
};

#endif /* PARSER_H */
