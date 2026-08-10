#ifndef LEXER_H
#define LEXER_H

/*
 * lexer.h - Python tokenizer for PyDOS Python-to-8086 compiler
 *
 * Full Python 3.11+ lexer with indentation tracking, string lexing
 * (including f-strings), number lexing, keyword recognition, and
 * multi-character operator support.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "token.h"

class Lexer {
public:
    Lexer();
    ~Lexer();
    int open(const char *filename);
    Token next_token();
    Token peek_token();
    int get_line() const;
    int get_col() const;

private:
    /* File reading */
    void *file_ptr;       /* FILE* stored as void* to avoid including stdio.h in header */
    char buffer[4096];
    int buf_pos;
    int buf_len;
    int at_eof;

    /* Pushback support */
    int pushback_char;
    int has_pushback;

    /* Source tracking */
    int line;
    int col;

    /* Indentation */
    int indent_stack[100];
    int indent_top;
    int pending_dedents;
    int at_line_start;
    int paren_depth;      /* suppress INDENT/DEDENT inside (), [], {} */

    /* Track whether we have emitted at least one NEWLINE at end */
    int emitted_final_newline;

    /* Peek support */
    Token peeked;
    int has_peeked;

    /* String storage for token text that needs to be owned */
    char string_buf[4096];
    int string_buf_pos;

    /* f-string nesting state */
    int fstring_depth;
    int fstring_quote[8];        /* quote char for each nesting level */
    int fstring_triple[8];       /* is triple-quoted for each level */
    int fstring_brace_depth[8];  /* brace depth within expression */

    /* Methods */
    int read_char();
    void unread_char(int c);
    int peek_char();
    void skip_comment();
    int count_indent();
    Token make_token(TokenType type, const char *text, int len);
    Token make_error(const char *msg);
    Token read_string(int quote_char, int is_fstring, int is_raw, int is_bytes);
    Token read_number(int first_char);
    Token read_identifier(int first_char);
    Token read_operator(int first_char);
    TokenType check_keyword(const char *text, int len);
    char *alloc_string(const char *src, int len);
    Token handle_indent();
    Token handle_fstring_continuation();
};

#endif /* LEXER_H */
