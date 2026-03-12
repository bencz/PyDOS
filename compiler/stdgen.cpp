/*
 * stdlibgen.cpp - Stdlib index generator for PyDOS compiler
 *
 * Parses Python .py stub files in stdlib/builtins/ and produces a
 * binary index file (stdlib.idx) for the compiler's StdlibRegistry.
 *
 * Stub files use @internal_implementation("c_symbol") decorators
 * and dunder methods (__getitem__, etc.) as the single source of truth.
 *
 * Usage: stdlibgen <stdlib_dir> <output.idx>
 *
 * C++98 compatible, built with clang++ on macOS for development.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "stdscan.h"

/* ================================================================= */
/* TypeKind values from types.h                                       */
/* ================================================================= */

#define TK_INT   0
#define TK_FLOAT 1
#define TK_BOOL  2
#define TK_STR   3
#define TK_NONE  4
#define TK_LIST  5
#define TK_DICT  6
#define TK_TUPLE 7
#define TK_SET   8
#define TK_ANY  15
#define TK_RANGE 17
#define TK_FROZENSET 18
#define TK_COMPLEX   19
#define TK_BYTEARRAY 20

/* ================================================================= */
/* Language-level mappings (type name -> enum)                         */
/* ================================================================= */

static struct { const char *name; int kind; } type_name_map[] = {
    {"str",   TK_STR},   {"list",  TK_LIST},  {"dict",  TK_DICT},
    {"set",   TK_SET},   {"int",   TK_INT},   {"tuple", TK_TUPLE},
    {"frozenset", TK_FROZENSET},
    {"complex", TK_COMPLEX},
    {"bytearray", TK_BYTEARRAY},
    {0, 0}
};

static int type_kind_from_name(const char *s)
{
    int i;
    for (i = 0; type_name_map[i].name; i++) {
        if (strcmp(s, type_name_map[i].name) == 0)
            return type_name_map[i].kind;
    }
    return -1;
}

static int ret_type_from_str(const char *s)
{
    if (!s || !*s) return TK_ANY;
    if (strcmp(s, "int") == 0) return TK_INT;
    if (strcmp(s, "float") == 0) return TK_FLOAT;
    if (strcmp(s, "str") == 0) return TK_STR;
    if (strcmp(s, "bool") == 0) return TK_BOOL;
    if (strcmp(s, "list") == 0) return TK_LIST;
    if (strcmp(s, "dict") == 0) return TK_DICT;
    if (strcmp(s, "set") == 0) return TK_SET;
    if (strcmp(s, "tuple") == 0) return TK_TUPLE;
    if (strcmp(s, "None") == 0) return TK_NONE;
    if (strcmp(s, "range") == 0) return TK_RANGE;
    if (strcmp(s, "frozenset") == 0) return TK_FROZENSET;
    if (strcmp(s, "complex") == 0) return TK_COMPLEX;
    if (strcmp(s, "bytearray") == 0) return TK_BYTEARRAY;
    if (strcmp(s, "object") == 0) return TK_ANY;
    return TK_ANY;
}

/* ================================================================= */
/* Parsed data structures                                              */
/* ================================================================= */

#define MAX_PARSED_FUNCS   128
#define MAX_PARSED_TYPES   16
#define MAX_PARSED_METHODS 64

struct ParsedFunc {
    char py_name[STDLIB_NAME_LEN];
    char asm_name[STDLIB_ASM_LEN];
    int num_params;
    int ret_type;
    int is_exc;
    int exc_code;
};

struct ParsedMethod {
    char method_name[STDLIB_NAME_LEN];
    char asm_name[STDLIB_ASM_LEN];
    int num_params;     /* excluding self */
    int ret_type;
    int fast_argc;
};

struct ParsedType {
    char type_name[STDLIB_TYPE_NAME_LEN];
    int type_kind;
    char op_getitem[STDLIB_ASM_LEN];
    char op_setitem[STDLIB_ASM_LEN];
    char op_getslice[STDLIB_ASM_LEN];
    char op_contains[STDLIB_ASM_LEN];
    ParsedMethod methods[MAX_PARSED_METHODS];
    int num_methods;
};

static ParsedFunc parsed_funcs[MAX_PARSED_FUNCS];
static int num_parsed_funcs;
static ParsedType parsed_types[MAX_PARSED_TYPES];
static int num_parsed_types;

/* ================================================================= */
/* String helpers                                                      */
/* ================================================================= */

static char *skip_ws(char *s)
{
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static void strip_trailing(char *s)
{
    int len = (int)strlen(s);
    while (len > 0 && (s[len-1] == '\n' || s[len-1] == '\r' ||
                       s[len-1] == ' '  || s[len-1] == '\t'))
        s[--len] = '\0';
}

/* Extract quoted string: find first " then second ", copy content.
 * Returns pointer past the closing quote, or NULL on failure. */
static const char *extract_quoted(const char *s, char *out, int maxlen)
{
    const char *q1, *q2;
    int len;
    q1 = strchr(s, '"');
    if (!q1) return NULL;
    q2 = strchr(q1 + 1, '"');
    if (!q2) return NULL;
    len = (int)(q2 - q1 - 1);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, q1 + 1, len);
    out[len] = '\0';
    return q2 + 1;
}

/* Extract integer value after a keyword=, e.g. "exc_code=3" or "fast_argc=1".
 * Returns the integer value, or default_val if keyword not found. */
static int extract_kwarg_int(const char *s, const char *keyword, int default_val)
{
    const char *p = strstr(s, keyword);
    if (!p) return default_val;
    p += strlen(keyword);
    /* skip optional whitespace around = */
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return default_val;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

/* Count parameters in a parenthesized parameter list string.
 * Input: the content between '(' and ')' (exclusive).
 * skip_self: if 1, the first param named "self" is not counted.
 * Returns the parameter count. */
static int count_params(const char *params, int skip_self)
{
    const char *p;
    int count, paren_depth;

    /* skip leading whitespace */
    while (*params == ' ' || *params == '\t') params++;

    /* empty param list */
    if (*params == '\0') return 0;

    /* count commas at depth 0, then count = commas + 1 */
    count = 1;
    paren_depth = 0;
    for (p = params; *p; p++) {
        if (*p == '(') paren_depth++;
        else if (*p == ')') paren_depth--;
        else if (*p == ',' && paren_depth == 0) count++;
    }

    if (skip_self) {
        /* check if first param is "self" */
        p = params;
        while (*p == ' ' || *p == '\t' || *p == '*') p++;
        if (strncmp(p, "self", 4) == 0 &&
            (p[4] == ',' || p[4] == ')' || p[4] == '\0' ||
             p[4] == ' ' || p[4] == ':')) {
            count--;
        }
    }

    return count;
}

/* Extract return type from a def line.
 * Looks for "-> TYPE:" pattern. Writes type string to out.
 * Returns 1 if found, 0 if not (means TK_ANY). */
static int extract_return_type(const char *line, char *out, int maxlen)
{
    const char *arrow, *colon, *start;
    int len;

    arrow = strstr(line, "->");
    if (!arrow) { out[0] = '\0'; return 0; }
    start = arrow + 2;
    while (*start == ' ' || *start == '\t') start++;

    /* find the colon that ends the return type */
    colon = strchr(start, ':');
    if (!colon) { out[0] = '\0'; return 0; }

    len = (int)(colon - start);
    /* trim trailing whitespace */
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t'))
        len--;
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

/* Extract function/method name from a "def NAME(" line.
 * Input: pointer to the character after "def ".
 * Writes name to out, returns pointer to '(' or NULL. */
static const char *extract_def_name(const char *s, char *out, int maxlen)
{
    const char *paren;
    int len;

    while (*s == ' ' || *s == '\t') s++;
    paren = strchr(s, '(');
    if (!paren) return NULL;
    len = (int)(paren - s);
    /* trim trailing whitespace before ( */
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t'))
        len--;
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    return paren;
}

/* Extract parameter list content between ( and ).
 * Input: pointer to '('. Writes content to out.
 * Returns 1 on success, 0 on failure. */
static int extract_params(const char *open_paren, char *out, int maxlen)
{
    const char *close;
    int len;

    if (*open_paren != '(') return 0;
    close = strchr(open_paren, ')');
    if (!close) return 0;
    len = (int)(close - open_paren - 1);
    if (len < 0) len = 0;
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, open_paren + 1, len);
    out[len] = '\0';
    return 1;
}

/* ================================================================= */
/* Parser: funcs.py                                                */
/* ================================================================= */

static int parse_functions_file(const char *path)
{
    FILE *f;
    char line[1024];
    int pending_decorator = 0;
    char pending_asm[STDLIB_ASM_LEN];
    int pending_exc_code = -1;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return -1;
    }

    pending_asm[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *trimmed;
        strip_trailing(line);
        trimmed = skip_ws(line);

        /* @internal_implementation("...", exc_code=N) */
        if (strncmp(trimmed, "@internal_implementation(", 24) == 0) {
            const char *after;
            pending_asm[0] = '\0';
            after = extract_quoted(trimmed + 24, pending_asm, STDLIB_ASM_LEN);
            (void)after;
            pending_exc_code = extract_kwarg_int(trimmed, "exc_code", -1);
            pending_decorator = 1;
            continue;
        }

        /* def NAME(...) -> TYPE: ... */
        if (strncmp(trimmed, "def ", 4) == 0 && pending_decorator) {
            char name[STDLIB_NAME_LEN];
            char params[512];
            char ret_str[64];
            const char *paren;
            ParsedFunc *pf;

            if (num_parsed_funcs >= MAX_PARSED_FUNCS) {
                fprintf(stderr, "Error: too many functions (max %d)\n",
                        MAX_PARSED_FUNCS);
                fclose(f);
                return -1;
            }

            paren = extract_def_name(trimmed + 4, name, STDLIB_NAME_LEN);
            if (!paren) {
                fprintf(stderr, "Error: bad def line: %s\n", trimmed);
                fclose(f);
                return -1;
            }

            if (!extract_params(paren, params, sizeof(params))) {
                fprintf(stderr, "Error: bad params: %s\n", trimmed);
                fclose(f);
                return -1;
            }

            extract_return_type(trimmed, ret_str, sizeof(ret_str));

            pf = &parsed_funcs[num_parsed_funcs++];
            memset(pf, 0, sizeof(*pf));
            strncpy(pf->py_name, name, STDLIB_NAME_LEN - 1);
            strncpy(pf->asm_name, pending_asm, STDLIB_ASM_LEN - 1);
            pf->num_params = count_params(params, 0);
            pf->ret_type = ret_type_from_str(ret_str);
            pf->exc_code = pending_exc_code;
            pf->is_exc = (pending_exc_code >= 0) ? 1 : 0;

            pending_decorator = 0;
            pending_asm[0] = '\0';
            pending_exc_code = -1;
            continue;
        }
    }

    fclose(f);
    return 0;
}

/* ================================================================= */
/* Parser: type stub files                                             */
/* ================================================================= */

static int parse_type_file(const char *path)
{
    FILE *f;
    char line[1024];
    ParsedType *pt = NULL;
    int pending_decorator = 0;
    char pending_asm[STDLIB_ASM_LEN];
    int pending_fast_argc = -1;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "Error: cannot open %s\n", path);
        return -1;
    }

    pending_asm[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *trimmed;
        strip_trailing(line);
        trimmed = skip_ws(line);

        /* class NAME: */
        if (strncmp(trimmed, "class ", 6) == 0) {
            char class_name[STDLIB_TYPE_NAME_LEN];
            const char *p = trimmed + 6;
            int len;
            const char *colon;
            int kind;

            if (num_parsed_types >= MAX_PARSED_TYPES) {
                fprintf(stderr, "Error: too many types (max %d)\n",
                        MAX_PARSED_TYPES);
                fclose(f);
                return -1;
            }

            colon = strchr(p, ':');
            if (!colon) {
                fprintf(stderr, "Error: bad class line: %s\n", trimmed);
                fclose(f);
                return -1;
            }

            len = (int)(colon - p);
            while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t'))
                len--;
            if (len >= STDLIB_TYPE_NAME_LEN) len = STDLIB_TYPE_NAME_LEN - 1;
            memcpy(class_name, p, len);
            class_name[len] = '\0';

            kind = type_kind_from_name(class_name);
            if (kind < 0) {
                fprintf(stderr, "Warning: unknown type '%s' in %s\n",
                        class_name, path);
                fclose(f);
                return -1;
            }

            pt = &parsed_types[num_parsed_types++];
            memset(pt, 0, sizeof(*pt));
            strncpy(pt->type_name, class_name, STDLIB_TYPE_NAME_LEN - 1);
            pt->type_kind = kind;

            continue;
        }

        /* @internal_implementation("...", fast_argc=N) */
        if (strncmp(trimmed, "@internal_implementation(", 24) == 0) {
            pending_asm[0] = '\0';
            extract_quoted(trimmed + 24, pending_asm, STDLIB_ASM_LEN);
            pending_fast_argc = extract_kwarg_int(trimmed, "fast_argc", -1);
            pending_decorator = 1;
            continue;
        }

        /* def NAME(self, ...) -> TYPE: ... */
        if (strncmp(trimmed, "def ", 4) == 0 && pending_decorator && pt) {
            char name[STDLIB_NAME_LEN];
            char params[512];
            char ret_str[64];
            const char *paren;
            ParsedMethod *pm;

            if (pt->num_methods >= MAX_PARSED_METHODS) {
                fprintf(stderr, "Error: too many methods for %s (max %d)\n",
                        pt->type_name, MAX_PARSED_METHODS);
                fclose(f);
                return -1;
            }

            paren = extract_def_name(trimmed + 4, name, STDLIB_NAME_LEN);
            if (!paren) {
                fprintf(stderr, "Error: bad def line: %s\n", trimmed);
                fclose(f);
                return -1;
            }

            /* Dunder -> op_* field mapping (not added as regular method) */
            if (strcmp(name, "__getitem__") == 0) {
                strncpy(pt->op_getitem, pending_asm, STDLIB_ASM_LEN - 1);
                pending_decorator = 0; pending_asm[0] = '\0'; pending_fast_argc = -1;
                continue;
            }
            if (strcmp(name, "__setitem__") == 0) {
                strncpy(pt->op_setitem, pending_asm, STDLIB_ASM_LEN - 1);
                pending_decorator = 0; pending_asm[0] = '\0'; pending_fast_argc = -1;
                continue;
            }
            if (strcmp(name, "__getslice__") == 0) {
                strncpy(pt->op_getslice, pending_asm, STDLIB_ASM_LEN - 1);
                pending_decorator = 0; pending_asm[0] = '\0'; pending_fast_argc = -1;
                continue;
            }
            if (strcmp(name, "__contains__") == 0) {
                strncpy(pt->op_contains, pending_asm, STDLIB_ASM_LEN - 1);
                pending_decorator = 0; pending_asm[0] = '\0'; pending_fast_argc = -1;
                continue;
            }

            if (!extract_params(paren, params, sizeof(params))) {
                fprintf(stderr, "Error: bad params: %s\n", trimmed);
                fclose(f);
                return -1;
            }

            extract_return_type(trimmed, ret_str, sizeof(ret_str));

            pm = &pt->methods[pt->num_methods++];
            memset(pm, 0, sizeof(*pm));
            strncpy(pm->method_name, name, STDLIB_NAME_LEN - 1);
            strncpy(pm->asm_name, pending_asm, STDLIB_ASM_LEN - 1);
            pm->num_params = count_params(params, 1);  /* skip self */
            pm->ret_type = ret_type_from_str(ret_str);
            pm->fast_argc = pending_fast_argc;

            pending_decorator = 0;
            pending_asm[0] = '\0';
            pending_fast_argc = -1;
            continue;
        }
    }

    fclose(f);
    return 0;
}

/* ================================================================= */
/* Top-level stub parser                                               */
/* ================================================================= */

static int parse_all_stubs(const char *stdlib_dir)
{
    char path[512];
    int i;

    /* Which type stub files to parse */
    static const char *type_files[] = {
        "str.py", "list.py", "dict.py", "set.py", "int.py", "tuple.py",
        "frozenst.py", "complex.py", "bytearr.py", 0
    };

    /* Parse builtin functions */
    snprintf(path, sizeof(path), "%s/builtins/funcs.py", stdlib_dir);
    if (parse_functions_file(path) < 0) return -1;

    /* Parse type stubs */
    for (i = 0; type_files[i]; i++) {
        snprintf(path, sizeof(path), "%s/builtins/%s", stdlib_dir, type_files[i]);
        if (parse_type_file(path) < 0) return -1;
    }

    return 0;
}

/* ================================================================= */
/* Index file writer                                                   */
/* ================================================================= */

static void write_short(FILE *f, short val)
{
    fwrite(&val, sizeof(short), 1, f);
}

int main(int argc, char *argv[])
{
    FILE *out;
    const char *stdlib_dir;
    const char *output_path;
    int i, j;
    StdlibIdxHeader hdr;

    if (argc < 3) {
        fprintf(stderr, "Usage: stdlibgen <stdlib_dir> <output.idx>\n");
        fprintf(stderr, "  Parses stdlib/ stubs and generates stdlib.idx.\n");
        return 1;
    }

    stdlib_dir = argv[1];
    output_path = argv[2];

    /* Parse all stub files */
    memset(parsed_funcs, 0, sizeof(parsed_funcs));
    memset(parsed_types, 0, sizeof(parsed_types));
    num_parsed_funcs = 0;
    num_parsed_types = 0;

    if (parse_all_stubs(stdlib_dir) < 0) {
        fprintf(stderr, "Error: failed to parse stubs in %s\n", stdlib_dir);
        return 1;
    }

    printf("stdlibgen: %d builtins, %d types\n",
           num_parsed_funcs, num_parsed_types);

    if (num_parsed_funcs > STDLIB_MAX_FUNCS) {
        fprintf(stderr, "Error: too many builtins (%d > %d)\n",
                num_parsed_funcs, STDLIB_MAX_FUNCS);
        return 1;
    }
    if (num_parsed_types > STDLIB_MAX_TYPES) {
        fprintf(stderr, "Error: too many types (%d > %d)\n",
                num_parsed_types, STDLIB_MAX_TYPES);
        return 1;
    }

    out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "Error: cannot open %s for writing\n", output_path);
        return 1;
    }

    /* Write header */
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, STDLIB_IDX_MAGIC, 4);
    hdr.version = STDLIB_IDX_VERSION;
    hdr.num_funcs = (short)num_parsed_funcs;
    hdr.num_types = (short)num_parsed_types;
    fwrite(&hdr, sizeof(hdr), 1, out);

    /* Write function entries */
    for (i = 0; i < num_parsed_funcs; i++) {
        BuiltinFuncEntry entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.py_name, parsed_funcs[i].py_name, STDLIB_NAME_LEN - 1);
        strncpy(entry.asm_name, parsed_funcs[i].asm_name, STDLIB_ASM_LEN - 1);
        entry.num_params = (short)parsed_funcs[i].num_params;
        entry.ret_type_kind = (short)parsed_funcs[i].ret_type;
        entry.is_exception = (char)parsed_funcs[i].is_exc;
        entry.padding = 0;
        entry.exc_code = (short)parsed_funcs[i].exc_code;
        fwrite(&entry, sizeof(entry), 1, out);

        printf("  func: %s -> %s (params=%d, ret=%d%s)\n",
               entry.py_name, entry.asm_name, entry.num_params,
               entry.ret_type_kind,
               entry.is_exception ? ", exc" : "");
    }

    /* Write type entries */
    for (i = 0; i < num_parsed_types; i++) {
        int nm = parsed_types[i].num_methods;
        if (nm > STDLIB_MAX_METHODS) {
            fprintf(stderr, "Error: too many methods for %s (%d > %d)\n",
                    parsed_types[i].type_name, nm, STDLIB_MAX_METHODS);
            fclose(out);
            return 1;
        }

        /* Write type header: type_name + type_kind + num_methods */
        {
            char type_name_buf[STDLIB_TYPE_NAME_LEN];
            short type_kind = (short)parsed_types[i].type_kind;
            short num_methods = (short)nm;

            memset(type_name_buf, 0, sizeof(type_name_buf));
            strncpy(type_name_buf, parsed_types[i].type_name,
                    STDLIB_TYPE_NAME_LEN - 1);
            fwrite(type_name_buf, STDLIB_TYPE_NAME_LEN, 1, out);
            write_short(out, type_kind);
            write_short(out, num_methods);
        }

        /* Write operator function names (v2) */
        {
            char op_buf[STDLIB_ASM_LEN];

            memset(op_buf, 0, sizeof(op_buf));
            strncpy(op_buf, parsed_types[i].op_getitem, STDLIB_ASM_LEN - 1);
            fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

            memset(op_buf, 0, sizeof(op_buf));
            strncpy(op_buf, parsed_types[i].op_setitem, STDLIB_ASM_LEN - 1);
            fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

            memset(op_buf, 0, sizeof(op_buf));
            strncpy(op_buf, parsed_types[i].op_getslice, STDLIB_ASM_LEN - 1);
            fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

            memset(op_buf, 0, sizeof(op_buf));
            strncpy(op_buf, parsed_types[i].op_contains, STDLIB_ASM_LEN - 1);
            fwrite(op_buf, STDLIB_ASM_LEN, 1, out);
        }

        /* Write method entries */
        for (j = 0; j < nm; j++) {
            BuiltinMethodEntry mentry;
            memset(&mentry, 0, sizeof(mentry));
            strncpy(mentry.method_name,
                    parsed_types[i].methods[j].method_name,
                    STDLIB_NAME_LEN - 1);
            strncpy(mentry.asm_name,
                    parsed_types[i].methods[j].asm_name,
                    STDLIB_ASM_LEN - 1);
            mentry.num_params = (short)parsed_types[i].methods[j].num_params;
            mentry.ret_type_kind = (short)parsed_types[i].methods[j].ret_type;
            mentry.fast_argc = (short)parsed_types[i].methods[j].fast_argc;
            mentry.reserved = 0;
            fwrite(&mentry, sizeof(mentry), 1, out);
        }

        printf("  type: %s (kind=%d, %d methods)\n",
               parsed_types[i].type_name, parsed_types[i].type_kind, nm);
    }

    fclose(out);
    printf("Wrote %s (%d funcs, %d types)\n",
           output_path, num_parsed_funcs, num_parsed_types);
    return 0;
}
