/*
 * lexer.cpp - Full Python tokenizer for PyDOS Python-to-8086 compiler
 *
 * Handles: indentation tracking, string/f-string/raw/byte string lexing,
 * number lexing (dec/hex/oct/bin/float), keyword recognition, multi-char
 * operators, comment skipping, parenthesis-aware newline suppression.
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 */

#include "lexer.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Keyword table                                                       */
/* ------------------------------------------------------------------ */

struct KeywordEntry {
    const char *text;
    int len;
    TokenType type;
};

static const KeywordEntry keywords[] = {
    { "False",    5, TOK_FALSE },
    { "None",     4, TOK_NONE },
    { "True",     4, TOK_TRUE },
    { "and",      3, TOK_AND },
    { "as",       2, TOK_AS },
    { "assert",   6, TOK_ASSERT },
    { "async",    5, TOK_ASYNC },
    { "await",    5, TOK_AWAIT },
    { "break",    5, TOK_BREAK },
    { "case",     4, TOK_CASE },
    { "class",    5, TOK_CLASS },
    { "continue", 8, TOK_CONTINUE },
    { "def",      3, TOK_DEF },
    { "del",      3, TOK_DEL },
    { "elif",     4, TOK_ELIF },
    { "else",     4, TOK_ELSE },
    { "except",   6, TOK_EXCEPT },
    { "finally",  7, TOK_FINALLY },
    { "for",      3, TOK_FOR },
    { "from",     4, TOK_FROM },
    { "global",   6, TOK_GLOBAL },
    { "if",       2, TOK_IF },
    { "import",   6, TOK_IMPORT },
    { "in",       2, TOK_IN },
    { "is",       2, TOK_IS },
    { "lambda",   6, TOK_LAMBDA },
    { "match",    5, TOK_MATCH },
    { "nonlocal", 8, TOK_NONLOCAL },
    { "not",      3, TOK_NOT },
    { "or",       2, TOK_OR },
    { "pass",     4, TOK_PASS },
    { "raise",    5, TOK_RAISE },
    { "return",   6, TOK_RETURN },
    { "try",      3, TOK_TRY },
    { "type",     4, TOK_TYPE },
    { "while",    5, TOK_WHILE },
    { "with",     4, TOK_WITH },
    { "yield",    5, TOK_YIELD },
    { NULL,       0, TOK_ERROR }
};

static const int NUM_KEYWORDS = (int)(sizeof(keywords) / sizeof(keywords[0])) - 1;

/* ------------------------------------------------------------------ */
/* token_type_name()                                                   */
/* ------------------------------------------------------------------ */

const char *token_type_name(TokenType t) {
    switch (t) {
    case TOK_INT_LIT:           return "INT_LIT";
    case TOK_FLOAT_LIT:         return "FLOAT_LIT";
    case TOK_STRING_LIT:        return "STRING_LIT";
    case TOK_FSTRING_START:     return "FSTRING_START";
    case TOK_FSTRING_MID:       return "FSTRING_MID";
    case TOK_FSTRING_END:       return "FSTRING_END";
    case TOK_BYTES_LIT:         return "BYTES_LIT";
    case TOK_COMPLEX_LIT:       return "COMPLEX_LIT";
    case TOK_TRUE:              return "TRUE";
    case TOK_FALSE:             return "FALSE";
    case TOK_NONE:              return "NONE";
    case TOK_PLUS:              return "PLUS";
    case TOK_MINUS:             return "MINUS";
    case TOK_STAR:              return "STAR";
    case TOK_SLASH:             return "SLASH";
    case TOK_DOUBLESLASH:       return "DOUBLESLASH";
    case TOK_PERCENT:           return "PERCENT";
    case TOK_DOUBLESTAR:        return "DOUBLESTAR";
    case TOK_AT:                return "AT";
    case TOK_AMP:               return "AMP";
    case TOK_PIPE:              return "PIPE";
    case TOK_CARET:             return "CARET";
    case TOK_TILDE:             return "TILDE";
    case TOK_LSHIFT:            return "LSHIFT";
    case TOK_RSHIFT:            return "RSHIFT";
    case TOK_EQ:                return "EQ";
    case TOK_NE:                return "NE";
    case TOK_LT:                return "LT";
    case TOK_GT:                return "GT";
    case TOK_LE:                return "LE";
    case TOK_GE:                return "GE";
    case TOK_ASSIGN:            return "ASSIGN";
    case TOK_PLUS_ASSIGN:       return "PLUS_ASSIGN";
    case TOK_MINUS_ASSIGN:      return "MINUS_ASSIGN";
    case TOK_STAR_ASSIGN:       return "STAR_ASSIGN";
    case TOK_SLASH_ASSIGN:      return "SLASH_ASSIGN";
    case TOK_DOUBLESLASH_ASSIGN:return "DOUBLESLASH_ASSIGN";
    case TOK_PERCENT_ASSIGN:    return "PERCENT_ASSIGN";
    case TOK_DOUBLESTAR_ASSIGN: return "DOUBLESTAR_ASSIGN";
    case TOK_AMP_ASSIGN:        return "AMP_ASSIGN";
    case TOK_PIPE_ASSIGN:       return "PIPE_ASSIGN";
    case TOK_CARET_ASSIGN:      return "CARET_ASSIGN";
    case TOK_LSHIFT_ASSIGN:     return "LSHIFT_ASSIGN";
    case TOK_RSHIFT_ASSIGN:     return "RSHIFT_ASSIGN";
    case TOK_AT_ASSIGN:         return "AT_ASSIGN";
    case TOK_WALRUS:            return "WALRUS";
    case TOK_ARROW:             return "ARROW";
    case TOK_ELLIPSIS:          return "ELLIPSIS";
    case TOK_LPAREN:            return "LPAREN";
    case TOK_RPAREN:            return "RPAREN";
    case TOK_LBRACKET:          return "LBRACKET";
    case TOK_RBRACKET:          return "RBRACKET";
    case TOK_LBRACE:            return "LBRACE";
    case TOK_RBRACE:            return "RBRACE";
    case TOK_COMMA:             return "COMMA";
    case TOK_COLON:             return "COLON";
    case TOK_SEMICOLON:         return "SEMICOLON";
    case TOK_DOT:               return "DOT";
    case TOK_AND:               return "AND";
    case TOK_AS:                return "AS";
    case TOK_ASSERT:            return "ASSERT";
    case TOK_ASYNC:             return "ASYNC";
    case TOK_AWAIT:             return "AWAIT";
    case TOK_BREAK:             return "BREAK";
    case TOK_CASE:              return "CASE";
    case TOK_CLASS:             return "CLASS";
    case TOK_CONTINUE:          return "CONTINUE";
    case TOK_DEF:               return "DEF";
    case TOK_DEL:               return "DEL";
    case TOK_ELIF:              return "ELIF";
    case TOK_ELSE:              return "ELSE";
    case TOK_EXCEPT:            return "EXCEPT";
    case TOK_FINALLY:           return "FINALLY";
    case TOK_FOR:               return "FOR";
    case TOK_FROM:              return "FROM";
    case TOK_GLOBAL:            return "GLOBAL";
    case TOK_IF:                return "IF";
    case TOK_IMPORT:            return "IMPORT";
    case TOK_IN:                return "IN";
    case TOK_IS:                return "IS";
    case TOK_LAMBDA:            return "LAMBDA";
    case TOK_MATCH:             return "MATCH";
    case TOK_NONLOCAL:          return "NONLOCAL";
    case TOK_NOT:               return "NOT";
    case TOK_OR:                return "OR";
    case TOK_PASS:              return "PASS";
    case TOK_RAISE:             return "RAISE";
    case TOK_RETURN:            return "RETURN";
    case TOK_TRY:               return "TRY";
    case TOK_TYPE:              return "TYPE";
    case TOK_WHILE:             return "WHILE";
    case TOK_WITH:              return "WITH";
    case TOK_YIELD:             return "YIELD";
    case TOK_IDENTIFIER:        return "IDENTIFIER";
    case TOK_INDENT:            return "INDENT";
    case TOK_DEDENT:            return "DEDENT";
    case TOK_NEWLINE:           return "NEWLINE";
    case TOK_EOF:               return "EOF";
    case TOK_ERROR:             return "ERROR";
    }
    return "UNKNOWN";
}

/* ------------------------------------------------------------------ */
/* Helper predicates                                                   */
/* ------------------------------------------------------------------ */

static int is_alpha(int c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_';
}

static int is_digit(int c) {
    return c >= '0' && c <= '9';
}

static int is_hex_digit(int c) {
    return is_digit(c) || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
}

static int is_oct_digit(int c) {
    return c >= '0' && c <= '7';
}

static int is_bin_digit(int c) {
    return c == '0' || c == '1';
}

static int is_alnum(int c) {
    return is_alpha(c) || is_digit(c);
}

static int hex_value(int c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + c - 'a';
    if (c >= 'A' && c <= 'F') return 10 + c - 'A';
    return 0;
}

/* ------------------------------------------------------------------ */
/* Constructor / Destructor                                            */
/* ------------------------------------------------------------------ */

Lexer::Lexer() {
    file_ptr = NULL;
    buf_pos = 0;
    buf_len = 0;
    at_eof = 0;
    has_pushback = 0;
    pushback_char = 0;
    line = 1;
    col = 0;
    indent_stack[0] = 0;
    indent_top = 0;
    pending_dedents = 0;
    at_line_start = 1;
    paren_depth = 0;
    emitted_final_newline = 0;
    has_peeked = 0;
    string_buf_pos = 0;
    fstring_depth = 0;
    memset(&peeked, 0, sizeof(peeked));
    memset(fstring_quote, 0, sizeof(fstring_quote));
    memset(fstring_triple, 0, sizeof(fstring_triple));
    memset(fstring_brace_depth, 0, sizeof(fstring_brace_depth));
}

Lexer::~Lexer() {
    if (file_ptr) {
        fclose((FILE *)file_ptr);
        file_ptr = NULL;
    }
}

/* ------------------------------------------------------------------ */
/* open()                                                              */
/* ------------------------------------------------------------------ */

int Lexer::open(const char *filename) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        fprintf(stderr, "Error: cannot open file '%s'\n", filename);
        return 0;
    }
    file_ptr = (void *)f;
    buf_pos = 0;
    buf_len = 0;
    at_eof = 0;
    line = 1;
    col = 0;
    at_line_start = 1;
    return 1;
}

/* ------------------------------------------------------------------ */
/* Low-level character I/O                                             */
/* ------------------------------------------------------------------ */

int Lexer::read_char() {
    if (has_pushback) {
        has_pushback = 0;
        int c = pushback_char;
        if (c == '\n') {
            line++;
            col = 0;
        } else {
            col++;
        }
        return c;
    }

    if (buf_pos >= buf_len) {
        if (at_eof) return -1;
        FILE *f = (FILE *)file_ptr;
        if (!f) return -1;
        buf_len = (int)fread(buffer, 1, sizeof(buffer), f);
        buf_pos = 0;
        if (buf_len <= 0) {
            at_eof = 1;
            return -1;
        }
    }

    int c = (unsigned char)buffer[buf_pos++];

    /* Normalize \r\n and bare \r to \n */
    if (c == '\r') {
        /* peek at next char for \r\n */
        if (buf_pos < buf_len) {
            if (buffer[buf_pos] == '\n') {
                buf_pos++;
            }
        } else if (!at_eof) {
            FILE *f = (FILE *)file_ptr;
            if (f) {
                buf_len = (int)fread(buffer, 1, sizeof(buffer), f);
                buf_pos = 0;
                if (buf_len <= 0) {
                    at_eof = 1;
                } else if (buffer[0] == '\n') {
                    buf_pos = 1;
                }
            }
        }
        c = '\n';
    }

    if (c == '\n') {
        line++;
        col = 0;
    } else {
        col++;
    }

    return c;
}

void Lexer::unread_char(int c) {
    if (c < 0) return;
    has_pushback = 1;
    pushback_char = c;
    /* Undo line/col tracking */
    if (c == '\n') {
        line--;
        /* col is indeterminate after unread of newline; set to a large value */
        col = 999;
    } else {
        col--;
    }
}

int Lexer::peek_char() {
    int c = read_char();
    if (c >= 0) unread_char(c);
    return c;
}

/* ------------------------------------------------------------------ */
/* get_line / get_col                                                  */
/* ------------------------------------------------------------------ */

int Lexer::get_line() const { return line; }
int Lexer::get_col() const { return col; }

/* ------------------------------------------------------------------ */
/* skip_comment                                                        */
/* ------------------------------------------------------------------ */

void Lexer::skip_comment() {
    int c;
    for (;;) {
        c = read_char();
        if (c < 0 || c == '\n') {
            if (c == '\n') unread_char(c);
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* count_indent - count leading spaces at start of line                */
/* Returns indent level. Handles spaces only (tabs counted as 1 each). */
/* ------------------------------------------------------------------ */

int Lexer::count_indent() {
    int spaces = 0;
    for (;;) {
        int c = read_char();
        if (c == ' ') {
            spaces++;
        } else if (c == '\t') {
            /* Round up to next multiple of 8 (Python default) */
            spaces = ((spaces / 8) + 1) * 8;
        } else {
            if (c >= 0) unread_char(c);
            break;
        }
    }
    return spaces;
}

/* ------------------------------------------------------------------ */
/* Token construction helpers                                          */
/* ------------------------------------------------------------------ */

Token Lexer::make_token(TokenType type, const char *text, int len) {
    Token tok;
    tok.type = type;
    tok.text = text;
    tok.text_len = len;
    tok.line = line;
    tok.col = col;
    tok.int_value = 0;
    tok.float_value = 0.0;
    return tok;
}

Token Lexer::make_error(const char *msg) {
    Token tok;
    tok.type = TOK_ERROR;
    tok.text = msg;
    tok.text_len = (int)strlen(msg);
    tok.line = line;
    tok.col = col;
    tok.int_value = 0;
    tok.float_value = 0.0;
    return tok;
}

/* ------------------------------------------------------------------ */
/* alloc_string - copy text into the string pool                       */
/* ------------------------------------------------------------------ */

char *Lexer::alloc_string(const char *src, int len) {
    /* If it fits in the internal pool, use that */
    if (string_buf_pos + len + 1 <= (int)sizeof(string_buf)) {
        char *dst = &string_buf[string_buf_pos];
        memcpy(dst, src, len);
        dst[len] = '\0';
        string_buf_pos += len + 1;
        return dst;
    }
    /* Fall back to malloc for very long strings */
    char *dst = (char *)malloc(len + 1);
    if (dst) {
        memcpy(dst, src, len);
        dst[len] = '\0';
    }
    return dst;
}

/* ------------------------------------------------------------------ */
/* handle_indent - process indentation at start of logical line        */
/* ------------------------------------------------------------------ */

Token Lexer::handle_indent() {
    /* If we have pending dedents to emit, do so */
    if (pending_dedents > 0) {
        pending_dedents--;
        return make_token(TOK_DEDENT, "<DEDENT>", 8);
    }

    /* Count indentation */
    int indent = count_indent();

    /* Check for blank line or comment-only line */
    int c = peek_char();
    if (c == '\n' || c == '#' || c < 0) {
        /* Blank line or comment line - skip; don't emit indent tokens */
        if (c == '#') skip_comment();
        if (c >= 0) {
            c = read_char(); /* consume the newline */
        }
        if (c < 0) {
            /* EOF - emit final newline + dedents */
            at_line_start = 0;
            goto emit_eof_dedents;
        }
        /* Still at line start, recurse */
        return handle_indent();
    }

    at_line_start = 0;

    /* Compare indent with current level */
    if (indent > indent_stack[indent_top]) {
        /* Push new indent level */
        if (indent_top < 99) {
            indent_top++;
            indent_stack[indent_top] = indent;
        }
        return make_token(TOK_INDENT, "<INDENT>", 8);
    } else if (indent < indent_stack[indent_top]) {
        /* Pop indent levels until we find a match */
        int dedents = 0;
        while (indent_top > 0 && indent < indent_stack[indent_top]) {
            indent_top--;
            dedents++;
        }
        if (indent != indent_stack[indent_top]) {
            return make_error("Indentation error: unindent does not match any outer level");
        }
        if (dedents > 1) {
            pending_dedents = dedents - 1;
        }
        return make_token(TOK_DEDENT, "<DEDENT>", 8);
    }

    /* indent == current level: no token needed, fall through to next_token logic */
    /* We need to actually return a real token now. Call next_token recursively
       by reading the first real character. But we've already consumed indent spaces
       and the next char is ready. So we just signal "no indent change" by returning
       with a special approach: we clear at_line_start and let next_token() proceed. */
    /* Actually, this path is taken when indent matches. We set at_line_start=0
       above and the main next_token loop will read the next char. Return a sentinel
       that next_token never actually returns to the user - actually let's restructure:
       we just return next_token() directly. */
    return next_token();

emit_eof_dedents:
    if (!emitted_final_newline) {
        emitted_final_newline = 1;
        /* We need to emit a NEWLINE before the dedents */
        /* Then the dedents, then EOF */
        /* Queue dedents */
        while (indent_top > 0) {
            indent_top--;
            pending_dedents++;
        }
        return make_token(TOK_NEWLINE, "<NEWLINE>", 9);
    }
    if (pending_dedents > 0) {
        pending_dedents--;
        return make_token(TOK_DEDENT, "<DEDENT>", 8);
    }
    return make_token(TOK_EOF, "<EOF>", 5);
}

/* ------------------------------------------------------------------ */
/* check_keyword - match identifier text against keyword table         */
/* ------------------------------------------------------------------ */

TokenType Lexer::check_keyword(const char *text, int len) {
    int i;
    for (i = 0; i < NUM_KEYWORDS; i++) {
        if (keywords[i].len == len && memcmp(keywords[i].text, text, len) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENTIFIER;
}

/* ------------------------------------------------------------------ */
/* read_identifier                                                     */
/* ------------------------------------------------------------------ */

Token Lexer::read_identifier(int first_char) {
    char id_buf[256];
    int len = 0;
    int start_line = line;
    int start_col = col;

    id_buf[len++] = (char)first_char;

    for (;;) {
        int c = read_char();
        if (c < 0) break;
        if (is_alnum(c)) {
            if (len < 255) id_buf[len++] = (char)c;
        } else {
            unread_char(c);
            break;
        }
    }

    id_buf[len] = '\0';

    /* Check for string prefix: f, r, b, rb, br, rf, fr before a quote */
    int next = peek_char();
    if (next == '\'' || next == '"') {
        int is_fstring = 0, is_raw = 0, is_bytes = 0;
        int is_prefix = 0;

        if (len == 1) {
            if (id_buf[0] == 'f' || id_buf[0] == 'F') { is_fstring = 1; is_prefix = 1; }
            else if (id_buf[0] == 'r' || id_buf[0] == 'R') { is_raw = 1; is_prefix = 1; }
            else if (id_buf[0] == 'b' || id_buf[0] == 'B') { is_bytes = 1; is_prefix = 1; }
        } else if (len == 2) {
            char c0 = id_buf[0], c1 = id_buf[1];
            if ((c0 == 'r' || c0 == 'R') && (c1 == 'b' || c1 == 'B')) { is_raw = 1; is_bytes = 1; is_prefix = 1; }
            else if ((c0 == 'b' || c0 == 'B') && (c1 == 'r' || c1 == 'R')) { is_raw = 1; is_bytes = 1; is_prefix = 1; }
            else if ((c0 == 'r' || c0 == 'R') && (c1 == 'f' || c1 == 'F')) { is_raw = 1; is_fstring = 1; is_prefix = 1; }
            else if ((c0 == 'f' || c0 == 'F') && (c1 == 'r' || c1 == 'R')) { is_raw = 1; is_fstring = 1; is_prefix = 1; }
        }

        if (is_prefix) {
            int quote = read_char();
            return read_string(quote, is_fstring, is_raw, is_bytes);
        }
    }

    /* Check if keyword */
    TokenType tt = check_keyword(id_buf, len);

    char *text_copy = alloc_string(id_buf, len);
    Token tok;
    tok.type = tt;
    tok.text = text_copy;
    tok.text_len = len;
    tok.line = start_line;
    tok.col = start_col;
    tok.int_value = 0;
    tok.float_value = 0.0;
    return tok;
}

/* ------------------------------------------------------------------ */
/* read_number                                                         */
/* ------------------------------------------------------------------ */

Token Lexer::read_number(int first_char) {
    char num_buf[128];
    int len = 0;
    int start_line = line;
    int start_col = col;
    int is_float = 0;
    int c;

    num_buf[len++] = (char)first_char;

    /* Check for 0x, 0o, 0b */
    if (first_char == '0') {
        c = peek_char();
        if (c == 'x' || c == 'X') {
            num_buf[len++] = (char)read_char();
            for (;;) {
                c = read_char();
                if (c < 0) break;
                if (is_hex_digit(c)) {
                    if (len < 126) num_buf[len++] = (char)c;
                } else if (c == '_') {
                    /* underscore separator - skip */
                } else {
                    unread_char(c);
                    break;
                }
            }
            num_buf[len] = '\0';
            long val = strtol(num_buf, NULL, 16);
            Token tok;
            tok.type = TOK_INT_LIT;
            tok.text = alloc_string(num_buf, len);
            tok.text_len = len;
            tok.line = start_line;
            tok.col = start_col;
            tok.int_value = val;
            tok.float_value = 0.0;
            return tok;
        }
        if (c == 'o' || c == 'O') {
            num_buf[len++] = (char)read_char();
            for (;;) {
                c = read_char();
                if (c < 0) break;
                if (is_oct_digit(c)) {
                    if (len < 126) num_buf[len++] = (char)c;
                } else if (c == '_') {
                    /* skip */
                } else {
                    unread_char(c);
                    break;
                }
            }
            num_buf[len] = '\0';
            long val = strtol(num_buf + 2, NULL, 8);
            Token tok;
            tok.type = TOK_INT_LIT;
            tok.text = alloc_string(num_buf, len);
            tok.text_len = len;
            tok.line = start_line;
            tok.col = start_col;
            tok.int_value = val;
            tok.float_value = 0.0;
            return tok;
        }
        if (c == 'b' || c == 'B') {
            num_buf[len++] = (char)read_char();
            for (;;) {
                c = read_char();
                if (c < 0) break;
                if (is_bin_digit(c)) {
                    if (len < 126) num_buf[len++] = (char)c;
                } else if (c == '_') {
                    /* skip */
                } else {
                    unread_char(c);
                    break;
                }
            }
            num_buf[len] = '\0';
            long val = strtol(num_buf + 2, NULL, 2);
            Token tok;
            tok.type = TOK_INT_LIT;
            tok.text = alloc_string(num_buf, len);
            tok.text_len = len;
            tok.line = start_line;
            tok.col = start_col;
            tok.int_value = val;
            tok.float_value = 0.0;
            return tok;
        }
    }

    /* Decimal integer or float */
    for (;;) {
        c = read_char();
        if (c < 0) break;
        if (is_digit(c)) {
            if (len < 126) num_buf[len++] = (char)c;
        } else if (c == '_') {
            /* underscore separator */
        } else if (c == '.') {
            /* Could be float or just a dot operator after an int */
            int next = peek_char();
            if (is_digit(next) || next == 'e' || next == 'E') {
                is_float = 1;
                if (len < 126) num_buf[len++] = '.';
            } else {
                /* It's a dot after an integer, like 10.method() or range literal */
                /* Check if we have already only digits - this IS a float like 10. */
                is_float = 1;
                if (len < 126) num_buf[len++] = '.';
                /* but unread the next char and stop */
                break;
            }
        } else if (c == 'e' || c == 'E') {
            is_float = 1;
            if (len < 126) num_buf[len++] = (char)c;
            c = peek_char();
            if (c == '+' || c == '-') {
                if (len < 126) num_buf[len++] = (char)read_char();
            }
        } else {
            unread_char(c);
            break;
        }
    }

    num_buf[len] = '\0';

    Token tok;
    if (is_float) {
        tok.type = TOK_FLOAT_LIT;
        tok.float_value = strtod(num_buf, NULL);
        tok.int_value = 0;
    } else {
        tok.type = TOK_INT_LIT;
        tok.int_value = strtol(num_buf, NULL, 10);
        tok.float_value = 0.0;
    }
    /* Check for complex suffix j/J (decimal only) */
    {
        int next_c = peek_char();
        if (next_c == 'j' || next_c == 'J') {
            read_char();
            tok.type = TOK_COMPLEX_LIT;
            if (!is_float) tok.float_value = (double)tok.int_value;
            tok.int_value = 0;
            if (len < 126) num_buf[len++] = (char)next_c;
            num_buf[len] = '\0';
        }
    }

    tok.text = alloc_string(num_buf, len);
    tok.text_len = len;
    tok.line = start_line;
    tok.col = start_col;
    return tok;
}

/* ------------------------------------------------------------------ */
/* read_string                                                         */
/* ------------------------------------------------------------------ */

Token Lexer::read_string(int quote_char, int is_fstring, int is_raw, int is_bytes) {
    int start_line = line;
    int start_col = col;
    char str_content[2048];
    int slen = 0;
    int is_triple = 0;

    /* Check for triple-quote */
    int c1 = peek_char();
    if (c1 == quote_char) {
        read_char(); /* consume second quote */
        int c2 = peek_char();
        if (c2 == quote_char) {
            read_char(); /* consume third quote - it's a triple-quoted string */
            is_triple = 1;
        } else {
            /* Empty string '' or "" */
            TokenType tt = is_bytes ? TOK_BYTES_LIT : (is_fstring ? TOK_FSTRING_START : TOK_STRING_LIT);
            if (is_fstring) {
                /* Empty f-string: emit FSTRING_START then FSTRING_END */
                tt = TOK_STRING_LIT;  /* just treat empty f"" as empty string */
            }
            Token tok;
            tok.type = tt;
            tok.text = alloc_string("", 0);
            tok.text_len = 0;
            tok.line = start_line;
            tok.col = start_col;
            tok.int_value = 0;
            tok.float_value = 0.0;
            return tok;
        }
    }

    /* If this is an f-string, emit FSTRING_START with content up to first { */
    if (is_fstring) {
        /* Save f-string state */
        if (fstring_depth < 8) {
            fstring_quote[fstring_depth] = quote_char;
            fstring_triple[fstring_depth] = is_triple;
            fstring_brace_depth[fstring_depth] = 0;
            fstring_depth++;
        }

        /* Read string content until { or end-of-string */
        slen = 0;
        for (;;) {
            int c = read_char();
            if (c < 0) {
                return make_error("Unterminated f-string");
            }

            if (c == '{') {
                int next = peek_char();
                if (next == '{') {
                    /* Escaped brace {{ -> literal { */
                    read_char();
                    if (slen < 2046) str_content[slen++] = '{';
                    continue;
                }
                /* Start of expression - emit FSTRING_START */
                str_content[slen] = '\0';
                Token tok;
                tok.type = TOK_FSTRING_START;
                tok.text = alloc_string(str_content, slen);
                tok.text_len = slen;
                tok.line = start_line;
                tok.col = start_col;
                tok.int_value = 0;
                tok.float_value = 0.0;
                /* The next call to next_token will read expression tokens.
                   When } is encountered in an fstring context, we handle it. */
                if (fstring_depth > 0) {
                    fstring_brace_depth[fstring_depth - 1] = 1;
                }
                return tok;
            }

            if (c == '}') {
                int next = peek_char();
                if (next == '}') {
                    read_char();
                    if (slen < 2046) str_content[slen++] = '}';
                    continue;
                }
                return make_error("Single '}' in f-string outside expression");
            }

            /* Check for end of string */
            if (c == quote_char && !is_triple) {
                /* End of single-line f-string with no expressions */
                str_content[slen] = '\0';
                /* Actually this is a complete f-string with no expressions,
                   just emit as STRING_LIT */
                if (fstring_depth > 0) fstring_depth--;
                Token tok;
                tok.type = TOK_STRING_LIT;
                tok.text = alloc_string(str_content, slen);
                tok.text_len = slen;
                tok.line = start_line;
                tok.col = start_col;
                tok.int_value = 0;
                tok.float_value = 0.0;
                return tok;
            }
            if (c == quote_char && is_triple) {
                int c2 = peek_char();
                if (c2 == quote_char) {
                    read_char();
                    int c3 = peek_char();
                    if (c3 == quote_char) {
                        read_char();
                        /* End of triple-quoted f-string with no expressions */
                        str_content[slen] = '\0';
                        if (fstring_depth > 0) fstring_depth--;
                        Token tok;
                        tok.type = TOK_STRING_LIT;
                        tok.text = alloc_string(str_content, slen);
                        tok.text_len = slen;
                        tok.line = start_line;
                        tok.col = start_col;
                        tok.int_value = 0;
                        tok.float_value = 0.0;
                        return tok;
                    }
                    /* Only two quotes, not three - add them as content */
                    if (slen < 2046) str_content[slen++] = (char)quote_char;
                    if (slen < 2046) str_content[slen++] = (char)quote_char;
                    continue;
                }
                /* Single quote in triple - just content */
                if (slen < 2046) str_content[slen++] = (char)c;
                continue;
            }

            /* Handle escape sequences */
            if (c == '\\' && !is_raw) {
                int esc = read_char();
                if (esc < 0) break;
                switch (esc) {
                case 'n':  if (slen < 2046) str_content[slen++] = '\n'; break;
                case 't':  if (slen < 2046) str_content[slen++] = '\t'; break;
                case '\\': if (slen < 2046) str_content[slen++] = '\\'; break;
                case '\'': if (slen < 2046) str_content[slen++] = '\''; break;
                case '\"': if (slen < 2046) str_content[slen++] = '\"'; break;
                case '0':  if (slen < 2046) str_content[slen++] = '\0'; break;
                case 'a':  if (slen < 2046) str_content[slen++] = '\a'; break;
                case 'b':  if (slen < 2046) str_content[slen++] = '\b'; break;
                case 'f':  if (slen < 2046) str_content[slen++] = '\f'; break;
                case 'r':  if (slen < 2046) str_content[slen++] = '\r'; break;
                case 'v':  if (slen < 2046) str_content[slen++] = '\v'; break;
                case 'x': {
                    int h1 = read_char(), h2 = read_char();
                    if (is_hex_digit(h1) && is_hex_digit(h2)) {
                        if (slen < 2046) str_content[slen++] = (char)(hex_value(h1) * 16 + hex_value(h2));
                    }
                    break;
                }
                case 'u': {
                    /* Unicode escape - read 4 hex digits, store as-is for DOS (limited) */
                    int val = 0;
                    int i;
                    for (i = 0; i < 4; i++) {
                        int h = read_char();
                        if (is_hex_digit(h)) val = val * 16 + hex_value(h);
                    }
                    if (val < 128 && slen < 2046) str_content[slen++] = (char)val;
                    break;
                }
                case 'U': {
                    int val = 0;
                    int i;
                    for (i = 0; i < 8; i++) {
                        int h = read_char();
                        if (is_hex_digit(h)) val = val * 16 + hex_value(h);
                    }
                    if (val < 128 && slen < 2046) str_content[slen++] = (char)val;
                    break;
                }
                case '\n':
                    /* Line continuation in string */
                    break;
                default:
                    /* Unknown escape - keep as-is */
                    if (slen < 2046) str_content[slen++] = '\\';
                    if (slen < 2046) str_content[slen++] = (char)esc;
                    break;
                }
                continue;
            }

            /* Regular character */
            if (c == '\n' && !is_triple) {
                return make_error("Unterminated string (newline in single-line string)");
            }
            if (slen < 2046) str_content[slen++] = (char)c;
        }

        return make_error("Unterminated f-string");
    }

    /* Regular string (not f-string) */
    slen = 0;
    for (;;) {
        int c = read_char();
        if (c < 0) {
            return make_error("Unterminated string");
        }

        /* Check for end of string */
        if (c == quote_char && !is_triple) {
            break;
        }
        if (c == quote_char && is_triple) {
            int c2 = peek_char();
            if (c2 == quote_char) {
                read_char();
                int c3 = peek_char();
                if (c3 == quote_char) {
                    read_char();
                    break; /* End of triple-quoted string */
                }
                /* Two quotes, not three */
                if (slen < 2046) str_content[slen++] = (char)quote_char;
                if (slen < 2046) str_content[slen++] = (char)quote_char;
                continue;
            }
            /* Single quote inside triple-quoted string */
            if (slen < 2046) str_content[slen++] = (char)c;
            continue;
        }

        /* Handle escape sequences */
        if (c == '\\' && !is_raw) {
            int esc = read_char();
            if (esc < 0) break;
            switch (esc) {
            case 'n':  if (slen < 2046) str_content[slen++] = '\n'; break;
            case 't':  if (slen < 2046) str_content[slen++] = '\t'; break;
            case '\\': if (slen < 2046) str_content[slen++] = '\\'; break;
            case '\'': if (slen < 2046) str_content[slen++] = '\''; break;
            case '\"': if (slen < 2046) str_content[slen++] = '\"'; break;
            case '0':  if (slen < 2046) str_content[slen++] = '\0'; break;
            case 'a':  if (slen < 2046) str_content[slen++] = '\a'; break;
            case 'b':  if (slen < 2046) str_content[slen++] = '\b'; break;
            case 'f':  if (slen < 2046) str_content[slen++] = '\f'; break;
            case 'r':  if (slen < 2046) str_content[slen++] = '\r'; break;
            case 'v':  if (slen < 2046) str_content[slen++] = '\v'; break;
            case 'x': {
                int h1 = read_char(), h2 = read_char();
                if (is_hex_digit(h1) && is_hex_digit(h2)) {
                    if (slen < 2046) str_content[slen++] = (char)(hex_value(h1) * 16 + hex_value(h2));
                }
                break;
            }
            case 'u': {
                int val = 0;
                int i;
                for (i = 0; i < 4; i++) {
                    int h = read_char();
                    if (is_hex_digit(h)) val = val * 16 + hex_value(h);
                }
                if (val < 128 && slen < 2046) str_content[slen++] = (char)val;
                break;
            }
            case 'U': {
                int val = 0;
                int i;
                for (i = 0; i < 8; i++) {
                    int h = read_char();
                    if (is_hex_digit(h)) val = val * 16 + hex_value(h);
                }
                if (val < 128 && slen < 2046) str_content[slen++] = (char)val;
                break;
            }
            case '\n':
                /* Line continuation */
                break;
            default:
                if (slen < 2046) str_content[slen++] = '\\';
                if (slen < 2046) str_content[slen++] = (char)esc;
                break;
            }
            continue;
        }

        /* In raw strings, backslash is literal */
        if (c == '\n' && !is_triple) {
            return make_error("Unterminated string (newline in single-line string)");
        }

        if (slen < 2046) str_content[slen++] = (char)c;
    }

    str_content[slen] = '\0';

    Token tok;
    tok.type = is_bytes ? TOK_BYTES_LIT : TOK_STRING_LIT;
    tok.text = alloc_string(str_content, slen);
    tok.text_len = slen;
    tok.line = start_line;
    tok.col = start_col;
    tok.int_value = 0;
    tok.float_value = 0.0;
    return tok;
}

/* ------------------------------------------------------------------ */
/* handle_fstring_continuation                                         */
/* After expression tokens inside an f-string, when we encounter },   */
/* continue reading the f-string content.                              */
/* ------------------------------------------------------------------ */

Token Lexer::handle_fstring_continuation() {
    if (fstring_depth <= 0) {
        /* Not in an fstring - this shouldn't happen */
        return make_token(TOK_RBRACE, "}", 1);
    }

    int level = fstring_depth - 1;
    int quote_char = fstring_quote[level];
    int is_triple = fstring_triple[level];
    int start_line = line;
    int start_col = col;

    char str_content[2048];
    int slen = 0;

    for (;;) {
        int c = read_char();
        if (c < 0) {
            fstring_depth--;
            return make_error("Unterminated f-string");
        }

        if (c == '{') {
            int next = peek_char();
            if (next == '{') {
                read_char();
                if (slen < 2046) str_content[slen++] = '{';
                continue;
            }
            /* New expression */
            str_content[slen] = '\0';
            fstring_brace_depth[level] = 1;
            Token tok;
            tok.type = TOK_FSTRING_MID;
            tok.text = alloc_string(str_content, slen);
            tok.text_len = slen;
            tok.line = start_line;
            tok.col = start_col;
            tok.int_value = 0;
            tok.float_value = 0.0;
            return tok;
        }

        if (c == '}') {
            int next = peek_char();
            if (next == '}') {
                read_char();
                if (slen < 2046) str_content[slen++] = '}';
                continue;
            }
            /* Should not get lone } in string portion */
            return make_error("Unexpected '}' in f-string");
        }

        /* Check for end of string */
        if (c == quote_char && !is_triple) {
            str_content[slen] = '\0';
            fstring_depth--;
            Token tok;
            tok.type = TOK_FSTRING_END;
            tok.text = alloc_string(str_content, slen);
            tok.text_len = slen;
            tok.line = start_line;
            tok.col = start_col;
            tok.int_value = 0;
            tok.float_value = 0.0;
            return tok;
        }
        if (c == quote_char && is_triple) {
            int c2 = peek_char();
            if (c2 == quote_char) {
                read_char();
                int c3 = peek_char();
                if (c3 == quote_char) {
                    read_char();
                    str_content[slen] = '\0';
                    fstring_depth--;
                    Token tok;
                    tok.type = TOK_FSTRING_END;
                    tok.text = alloc_string(str_content, slen);
                    tok.text_len = slen;
                    tok.line = start_line;
                    tok.col = start_col;
                    tok.int_value = 0;
                    tok.float_value = 0.0;
                    return tok;
                }
                if (slen < 2046) str_content[slen++] = (char)quote_char;
                if (slen < 2046) str_content[slen++] = (char)quote_char;
                continue;
            }
            if (slen < 2046) str_content[slen++] = (char)c;
            continue;
        }

        /* Escape sequences */
        if (c == '\\') {
            int esc = read_char();
            if (esc < 0) break;
            switch (esc) {
            case 'n':  if (slen < 2046) str_content[slen++] = '\n'; break;
            case 't':  if (slen < 2046) str_content[slen++] = '\t'; break;
            case '\\': if (slen < 2046) str_content[slen++] = '\\'; break;
            case '\'': if (slen < 2046) str_content[slen++] = '\''; break;
            case '\"': if (slen < 2046) str_content[slen++] = '\"'; break;
            case '{':  if (slen < 2046) str_content[slen++] = '{'; break;
            case '}':  if (slen < 2046) str_content[slen++] = '}'; break;
            default:
                if (slen < 2046) str_content[slen++] = '\\';
                if (slen < 2046) str_content[slen++] = (char)esc;
                break;
            }
            continue;
        }

        if (c == '\n' && !is_triple) {
            fstring_depth--;
            return make_error("Unterminated f-string (newline in single-line f-string)");
        }

        if (slen < 2046) str_content[slen++] = (char)c;
    }

    fstring_depth--;
    return make_error("Unterminated f-string");
}

/* ------------------------------------------------------------------ */
/* read_operator                                                       */
/* ------------------------------------------------------------------ */

Token Lexer::read_operator(int first_char) {
    int start_line = line;
    int start_col = col;
    int c2, c3;

    switch (first_char) {
    case '+':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_PLUS_ASSIGN, "+=", 2); }
        return make_token(TOK_PLUS, "+", 1);

    case '-':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_MINUS_ASSIGN, "-=", 2); }
        if (c2 == '>') { read_char(); return make_token(TOK_ARROW, "->", 2); }
        return make_token(TOK_MINUS, "-", 1);

    case '*':
        c2 = peek_char();
        if (c2 == '*') {
            read_char();
            c3 = peek_char();
            if (c3 == '=') { read_char(); return make_token(TOK_DOUBLESTAR_ASSIGN, "**=", 3); }
            return make_token(TOK_DOUBLESTAR, "**", 2);
        }
        if (c2 == '=') { read_char(); return make_token(TOK_STAR_ASSIGN, "*=", 2); }
        return make_token(TOK_STAR, "*", 1);

    case '/':
        c2 = peek_char();
        if (c2 == '/') {
            read_char();
            c3 = peek_char();
            if (c3 == '=') { read_char(); return make_token(TOK_DOUBLESLASH_ASSIGN, "//=", 3); }
            return make_token(TOK_DOUBLESLASH, "//", 2);
        }
        if (c2 == '=') { read_char(); return make_token(TOK_SLASH_ASSIGN, "/=", 2); }
        return make_token(TOK_SLASH, "/", 1);

    case '%':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_PERCENT_ASSIGN, "%=", 2); }
        return make_token(TOK_PERCENT, "%", 1);

    case '@':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_AT_ASSIGN, "@=", 2); }
        return make_token(TOK_AT, "@", 1);

    case '&':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_AMP_ASSIGN, "&=", 2); }
        return make_token(TOK_AMP, "&", 1);

    case '|':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_PIPE_ASSIGN, "|=", 2); }
        return make_token(TOK_PIPE, "|", 1);

    case '^':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_CARET_ASSIGN, "^=", 2); }
        return make_token(TOK_CARET, "^", 1);

    case '~':
        return make_token(TOK_TILDE, "~", 1);

    case '<':
        c2 = peek_char();
        if (c2 == '<') {
            read_char();
            c3 = peek_char();
            if (c3 == '=') { read_char(); return make_token(TOK_LSHIFT_ASSIGN, "<<=", 3); }
            return make_token(TOK_LSHIFT, "<<", 2);
        }
        if (c2 == '=') { read_char(); return make_token(TOK_LE, "<=", 2); }
        return make_token(TOK_LT, "<", 1);

    case '>':
        c2 = peek_char();
        if (c2 == '>') {
            read_char();
            c3 = peek_char();
            if (c3 == '=') { read_char(); return make_token(TOK_RSHIFT_ASSIGN, ">>=", 3); }
            return make_token(TOK_RSHIFT, ">>", 2);
        }
        if (c2 == '=') { read_char(); return make_token(TOK_GE, ">=", 2); }
        return make_token(TOK_GT, ">", 1);

    case '=':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_EQ, "==", 2); }
        return make_token(TOK_ASSIGN, "=", 1);

    case '!':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_NE, "!=", 2); }
        return make_error("Invalid character '!'");

    case ':':
        c2 = peek_char();
        if (c2 == '=') { read_char(); return make_token(TOK_WALRUS, ":=", 2); }
        return make_token(TOK_COLON, ":", 1);

    case '.':
        c2 = peek_char();
        if (c2 == '.') {
            read_char();
            c3 = peek_char();
            if (c3 == '.') {
                read_char();
                return make_token(TOK_ELLIPSIS, "...", 3);
            }
            /* Two dots is not valid Python */
            unread_char('.');
            return make_token(TOK_DOT, ".", 1);
        }
        /* Could be start of a float like .5 */
        if (is_digit(c2)) {
            /* Put back the dot and read as number */
            return read_number(first_char);
        }
        return make_token(TOK_DOT, ".", 1);
    }

    /* Should not reach here */
    return make_error("Unexpected operator character");
}

/* ------------------------------------------------------------------ */
/* next_token - main tokenizer entry point                             */
/* ------------------------------------------------------------------ */

Token Lexer::next_token() {
    /* Return peeked token if available */
    if (has_peeked) {
        has_peeked = 0;
        return peeked;
    }

    /* Emit pending dedents */
    if (pending_dedents > 0) {
        pending_dedents--;
        return make_token(TOK_DEDENT, "<DEDENT>", 8);
    }

    /* Handle indentation at start of line */
    if (at_line_start && paren_depth == 0) {
        return handle_indent();
    }

    /* Main tokenization loop */
    for (;;) {
        int c = read_char();

        if (c < 0) {
            /* EOF */
            if (!emitted_final_newline && indent_top > 0) {
                emitted_final_newline = 1;
                while (indent_top > 0) {
                    indent_top--;
                    pending_dedents++;
                }
                /* Emit NEWLINE first, then dedents will follow */
                return make_token(TOK_NEWLINE, "<NEWLINE>", 9);
            }
            if (pending_dedents > 0) {
                pending_dedents--;
                return make_token(TOK_DEDENT, "<DEDENT>", 8);
            }
            return make_token(TOK_EOF, "<EOF>", 5);
        }

        /* Skip spaces and tabs within a line */
        if (c == ' ' || c == '\t') {
            continue;
        }

        /* Line continuation */
        if (c == '\\') {
            int next = read_char();
            if (next == '\n') {
                /* Line continuation - next line is part of current logical line */
                continue;
            }
            if (next >= 0) unread_char(next);
            return make_error("Unexpected '\\' (not followed by newline)");
        }

        /* Newline */
        if (c == '\n') {
            if (paren_depth > 0) {
                /* Inside parens/brackets/braces - implicit line continuation */
                continue;
            }
            at_line_start = 1;
            return make_token(TOK_NEWLINE, "<NEWLINE>", 9);
        }

        /* Comment */
        if (c == '#') {
            skip_comment();
            continue;
        }

        /* f-string expression: if we're inside an f-string and hit }, handle continuation */
        if (c == '}' && fstring_depth > 0) {
            int level = fstring_depth - 1;
            if (fstring_brace_depth[level] > 0) {
                fstring_brace_depth[level] = 0;
                return handle_fstring_continuation();
            }
        }

        /* Delimiters */
        if (c == '(') { paren_depth++; return make_token(TOK_LPAREN, "(", 1); }
        if (c == ')') { if (paren_depth > 0) paren_depth--; return make_token(TOK_RPAREN, ")", 1); }
        if (c == '[') { paren_depth++; return make_token(TOK_LBRACKET, "[", 1); }
        if (c == ']') { if (paren_depth > 0) paren_depth--; return make_token(TOK_RBRACKET, "]", 1); }
        if (c == '{') { paren_depth++; return make_token(TOK_LBRACE, "{", 1); }
        if (c == '}') { if (paren_depth > 0) paren_depth--; return make_token(TOK_RBRACE, "}", 1); }
        if (c == ',') return make_token(TOK_COMMA, ",", 1);
        if (c == ';') return make_token(TOK_SEMICOLON, ";", 1);

        /* Strings */
        if (c == '\'' || c == '"') {
            return read_string(c, 0, 0, 0);
        }

        /* Numbers */
        if (is_digit(c)) {
            return read_number(c);
        }

        /* Dot (could be operator, float, or ellipsis) */
        if (c == '.') {
            int next = peek_char();
            if (is_digit(next)) {
                /* Float starting with . */
                return read_number(c);
            }
            return read_operator(c);
        }

        /* Identifiers and keywords */
        if (is_alpha(c)) {
            return read_identifier(c);
        }

        /* Operators */
        if (c == '+' || c == '-' || c == '*' || c == '/' || c == '%' ||
            c == '@' || c == '&' || c == '|' || c == '^' || c == '~' ||
            c == '<' || c == '>' || c == '=' || c == '!' || c == ':') {
            return read_operator(c);
        }

        /* Unknown character */
        {
            char err_msg[64];
            sprintf(err_msg, "Invalid character '%c' (0x%02x)", c, c);
            return make_error(err_msg);
        }
    }
}

/* ------------------------------------------------------------------ */
/* peek_token                                                          */
/* ------------------------------------------------------------------ */

Token Lexer::peek_token() {
    if (!has_peeked) {
        peeked = next_token();
        has_peeked = 1;
    }
    return peeked;
}
