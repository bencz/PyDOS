#ifndef TOKEN_H
#define TOKEN_H

/*
 * token.h - Token definitions for PyDOS Python-to-8086 compiler
 *
 * Defines all Python 3.11+ token types, the Token struct, and
 * a utility function for human-readable token names.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

enum TokenType {
    /* ---- Literals ---- */
    TOK_INT_LIT,
    TOK_FLOAT_LIT,
    TOK_STRING_LIT,
    TOK_FSTRING_START,
    TOK_FSTRING_MID,
    TOK_FSTRING_END,
    TOK_BYTES_LIT,
    TOK_COMPLEX_LIT,
    TOK_TRUE,
    TOK_FALSE,
    TOK_NONE,

    /* ---- Arithmetic operators ---- */
    TOK_PLUS,           /* +   */
    TOK_MINUS,          /* -   */
    TOK_STAR,           /* *   */
    TOK_SLASH,          /* /   */
    TOK_DOUBLESLASH,    /* //  */
    TOK_PERCENT,        /* %   */
    TOK_DOUBLESTAR,     /* **  */
    TOK_AT,             /* @   */

    /* ---- Bitwise operators ---- */
    TOK_AMP,            /* &   */
    TOK_PIPE,           /* |   */
    TOK_CARET,          /* ^   */
    TOK_TILDE,          /* ~   */
    TOK_LSHIFT,         /* <<  */
    TOK_RSHIFT,         /* >>  */

    /* ---- Comparison operators ---- */
    TOK_EQ,             /* ==  */
    TOK_NE,             /* !=  */
    TOK_LT,             /* <   */
    TOK_GT,             /* >   */
    TOK_LE,             /* <=  */
    TOK_GE,             /* >=  */

    /* ---- Assignment operators ---- */
    TOK_ASSIGN,              /* =    */
    TOK_PLUS_ASSIGN,         /* +=   */
    TOK_MINUS_ASSIGN,        /* -=   */
    TOK_STAR_ASSIGN,         /* *=   */
    TOK_SLASH_ASSIGN,        /* /=   */
    TOK_DOUBLESLASH_ASSIGN,  /* //=  */
    TOK_PERCENT_ASSIGN,      /* %=   */
    TOK_DOUBLESTAR_ASSIGN,   /* **=  */
    TOK_AMP_ASSIGN,          /* &=   */
    TOK_PIPE_ASSIGN,         /* |=   */
    TOK_CARET_ASSIGN,        /* ^=   */
    TOK_LSHIFT_ASSIGN,       /* <<=  */
    TOK_RSHIFT_ASSIGN,       /* >>=  */
    TOK_AT_ASSIGN,           /* @=   */

    /* ---- Special operators ---- */
    TOK_WALRUS,         /* :=  */
    TOK_ARROW,          /* ->  */
    TOK_ELLIPSIS,       /* ... */

    /* ---- Delimiters ---- */
    TOK_LPAREN,         /* (   */
    TOK_RPAREN,         /* )   */
    TOK_LBRACKET,       /* [   */
    TOK_RBRACKET,       /* ]   */
    TOK_LBRACE,         /* {   */
    TOK_RBRACE,         /* }   */
    TOK_COMMA,          /* ,   */
    TOK_COLON,          /* :   */
    TOK_SEMICOLON,      /* ;   */
    TOK_DOT,            /* .   */

    /* ---- Keywords (alphabetical) ---- */
    TOK_AND,
    TOK_AS,
    TOK_ASSERT,
    TOK_ASYNC,
    TOK_AWAIT,
    TOK_BREAK,
    TOK_CASE,
    TOK_CLASS,
    TOK_CONTINUE,
    TOK_DEF,
    TOK_DEL,
    TOK_ELIF,
    TOK_ELSE,
    TOK_EXCEPT,
    TOK_FINALLY,
    TOK_FOR,
    TOK_FROM,
    TOK_GLOBAL,
    TOK_IF,
    TOK_IMPORT,
    TOK_IN,
    TOK_IS,
    TOK_LAMBDA,
    TOK_MATCH,
    TOK_NONLOCAL,
    TOK_NOT,
    TOK_OR,
    TOK_PASS,
    TOK_RAISE,
    TOK_RETURN,
    TOK_TRY,
    TOK_TYPE,
    TOK_WHILE,
    TOK_WITH,
    TOK_YIELD,

    /* ---- Identifiers and special ---- */
    TOK_IDENTIFIER,
    TOK_INDENT,
    TOK_DEDENT,
    TOK_NEWLINE,
    TOK_EOF,
    TOK_ERROR
};

struct Token {
    TokenType type;
    const char *text;       /* pointer into source buffer (NOT owned) */
    int text_len;           /* length of token text */
    int line;
    int col;
    long int_value;         /* for INT_LIT */
    double float_value;     /* for FLOAT_LIT */
};

/* Returns human-readable name for a token type. */
const char *token_type_name(TokenType t);

#endif /* TOKEN_H */
