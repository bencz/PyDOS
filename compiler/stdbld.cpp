/*
 * stdbld.cpp - Stdlib Builder implementation
 *
 * Orchestrates the --build-stdlib process:
 *   Pass 1: Scan funcs.py and type stubs for metadata (names, params,
 *           C symbols, exception codes) — same logic as stdgen.cpp.
 *   Pass 2: Compile pyfncs.py through Lexer→Parser→Sema→PIRBuilder
 *           to get PIR for Python-backed functions.
 *   Write:  Serialize metadata + PIR into stdlib.idx v3 format.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "stdbld.h"
#include "stdscan.h"
#include "pirsrlz.h"
#include "pir.h"
#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "types.h"
#include "sema.h"
#include "pirbld.h"
#include "error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ================================================================= */
/* TypeKind values (mirror types.h for standalone use)                 */
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

/* PYDT_* runtime type tags (mirror pdos_obj.h) */
static struct { const char *name; short tag; } pydt_tags[] = {
    {"int", 2}, {"bool", 1}, {"float", 3}, {"str", 4},
    {"list", 5}, {"dict", 6}, {"tuple", 7}, {"set", 8},
    {"bytes", 9}, {"range", 15}, {"frozenset", 20},
    {"complex", 21}, {"bytearray", 22},
    {0, 0}
};

static short find_pydt_tag(const char *name)
{
    int i;
    for (i = 0; pydt_tags[i].name; i++) {
        if (strcmp(name, pydt_tags[i].name) == 0)
            return pydt_tags[i].tag;
    }
    return -1;
}

/* ================================================================= */
/* Metadata parser (reimplemented from stdgen.cpp)                     */
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
    int is_pir;     /* 1 if function has no @internal_implementation */
};

struct ParsedMethod {
    char method_name[STDLIB_NAME_LEN];
    char asm_name[STDLIB_ASM_LEN];
    int num_params;
    int ret_type;
    int fast_argc;
    int is_pir;
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

static ParsedFunc g_funcs[MAX_PARSED_FUNCS];
static int g_num_funcs;
static ParsedType g_types[MAX_PARSED_TYPES];
static int g_num_types;

/* ================================================================= */
/* String helpers (same as stdgen.cpp)                                 */
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

static const char *extract_quoted(const char *s, char *out, int maxlen)
{
    const char *q1, *q2;
    int len;
    q1 = strchr(s, '"');
    if (!q1) return 0;
    q2 = strchr(q1 + 1, '"');
    if (!q2) return 0;
    len = (int)(q2 - q1 - 1);
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, q1 + 1, len);
    out[len] = '\0';
    return q2 + 1;
}

static int extract_kwarg_int(const char *s, const char *keyword, int default_val)
{
    const char *p = strstr(s, keyword);
    if (!p) return default_val;
    p += strlen(keyword);
    while (*p == ' ' || *p == '\t') p++;
    if (*p != '=') return default_val;
    p++;
    while (*p == ' ' || *p == '\t') p++;
    return atoi(p);
}

static int count_params(const char *params, int skip_self)
{
    const char *p;
    int count, paren_depth;
    while (*params == ' ' || *params == '\t') params++;
    if (*params == '\0') return 0;
    count = 1;
    paren_depth = 0;
    for (p = params; *p; p++) {
        if (*p == '(') paren_depth++;
        else if (*p == ')') paren_depth--;
        else if (*p == ',' && paren_depth == 0) count++;
    }
    if (skip_self) {
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

static int type_kind_from_name(const char *s)
{
    static struct { const char *name; int kind; } map[] = {
        {"str", TK_STR}, {"list", TK_LIST}, {"dict", TK_DICT},
        {"set", TK_SET}, {"int", TK_INT}, {"tuple", TK_TUPLE},
        {"frozenset", TK_FROZENSET}, {"complex", TK_COMPLEX},
        {"bytearray", TK_BYTEARRAY}, {0, 0}
    };
    int i;
    for (i = 0; map[i].name; i++) {
        if (strcmp(s, map[i].name) == 0) return map[i].kind;
    }
    return -1;
}

static int extract_return_type(const char *line, char *out, int maxlen)
{
    const char *arrow, *colon, *start;
    int len;
    arrow = strstr(line, "->");
    if (!arrow) { out[0] = '\0'; return 0; }
    start = arrow + 2;
    while (*start == ' ' || *start == '\t') start++;
    colon = strchr(start, ':');
    if (!colon) { out[0] = '\0'; return 0; }
    len = (int)(colon - start);
    while (len > 0 && (start[len-1] == ' ' || start[len-1] == '\t')) len--;
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, start, len);
    out[len] = '\0';
    return 1;
}

static const char *extract_def_name(const char *s, char *out, int maxlen)
{
    const char *paren;
    int len;
    while (*s == ' ' || *s == '\t') s++;
    paren = strchr(s, '(');
    if (!paren) return 0;
    len = (int)(paren - s);
    while (len > 0 && (s[len-1] == ' ' || s[len-1] == '\t')) len--;
    if (len >= maxlen) len = maxlen - 1;
    memcpy(out, s, len);
    out[len] = '\0';
    return paren;
}

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
/* Parse funcs.py for function metadata                                */
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
        fprintf(stderr, "stdbld: cannot open %s\n", path);
        return -1;
    }

    pending_asm[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *trimmed;
        strip_trailing(line);
        trimmed = skip_ws(line);

        /* @internal_implementation("...") */
        if (strncmp(trimmed, "@internal_implementation(", 24) == 0) {
            pending_asm[0] = '\0';
            extract_quoted(trimmed + 24, pending_asm, STDLIB_ASM_LEN);
            pending_exc_code = extract_kwarg_int(trimmed, "exc_code", -1);
            pending_decorator = 1;
            continue;
        }

        /* def NAME(...) -> TYPE: ... */
        if (strncmp(trimmed, "def ", 4) == 0) {
            char name[STDLIB_NAME_LEN];
            char params[512];
            char ret_str[64];
            const char *paren;
            ParsedFunc *pf;

            if (g_num_funcs >= MAX_PARSED_FUNCS) {
                fclose(f);
                return -1;
            }

            paren = extract_def_name(trimmed + 4, name, STDLIB_NAME_LEN);
            if (!paren) { pending_decorator = 0; continue; }
            if (!extract_params(paren, params, sizeof(params))) { pending_decorator = 0; continue; }
            extract_return_type(trimmed, ret_str, sizeof(ret_str));

            pf = &g_funcs[g_num_funcs++];
            memset(pf, 0, sizeof(*pf));
            strncpy(pf->py_name, name, STDLIB_NAME_LEN - 1);
            pf->num_params = count_params(params, 0);
            pf->ret_type = ret_type_from_str(ret_str);

            if (pending_decorator) {
                /* C-backed */
                strncpy(pf->asm_name, pending_asm, STDLIB_ASM_LEN - 1);
                pf->exc_code = pending_exc_code;
                pf->is_exc = (pending_exc_code >= 0) ? 1 : 0;
                pf->is_pir = 0;
            } else {
                /* Python-backed */
                pf->asm_name[0] = '\0';
                pf->exc_code = -1;
                pf->is_exc = 0;
                pf->is_pir = 1;
            }

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
/* Parse type stub files (same as stdgen.cpp)                          */
/* ================================================================= */

static int parse_type_file(const char *path)
{
    FILE *f;
    char line[1024];
    ParsedType *pt = 0;
    int pending_decorator = 0;
    char pending_asm[STDLIB_ASM_LEN];
    int pending_fast_argc = -1;

    f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "stdbld: cannot open %s\n", path);
        return -1;
    }

    pending_asm[0] = '\0';

    while (fgets(line, sizeof(line), f)) {
        char *trimmed;
        strip_trailing(line);
        trimmed = skip_ws(line);

        if (strncmp(trimmed, "class ", 6) == 0) {
            char class_name[STDLIB_TYPE_NAME_LEN];
            const char *p = trimmed + 6;
            int len, kind;
            const char *colon;

            if (g_num_types >= MAX_PARSED_TYPES) { fclose(f); return -1; }

            colon = strchr(p, ':');
            if (!colon) continue;

            len = (int)(colon - p);
            while (len > 0 && (p[len-1] == ' ' || p[len-1] == '\t')) len--;
            if (len >= STDLIB_TYPE_NAME_LEN) len = STDLIB_TYPE_NAME_LEN - 1;
            memcpy(class_name, p, len);
            class_name[len] = '\0';

            kind = type_kind_from_name(class_name);
            if (kind < 0) { fclose(f); return -1; }

            pt = &g_types[g_num_types++];
            memset(pt, 0, sizeof(*pt));
            strncpy(pt->type_name, class_name, STDLIB_TYPE_NAME_LEN - 1);
            pt->type_kind = kind;
            continue;
        }

        if (strncmp(trimmed, "@internal_implementation(", 24) == 0) {
            pending_asm[0] = '\0';
            extract_quoted(trimmed + 24, pending_asm, STDLIB_ASM_LEN);
            pending_fast_argc = extract_kwarg_int(trimmed, "fast_argc", -1);
            pending_decorator = 1;
            continue;
        }

        if (strncmp(trimmed, "def ", 4) == 0 && pt) {
            char name[STDLIB_NAME_LEN];
            char params[512];
            char ret_str[64];
            const char *paren;
            ParsedMethod *pm;

            if (pt->num_methods >= MAX_PARSED_METHODS) { fclose(f); return -1; }

            paren = extract_def_name(trimmed + 4, name, STDLIB_NAME_LEN);
            if (!paren) { pending_decorator = 0; continue; }

            if (pending_decorator) {
                /* Dunder -> op_* field mapping */
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
            }

            if (!extract_params(paren, params, sizeof(params))) { pending_decorator = 0; continue; }
            extract_return_type(trimmed, ret_str, sizeof(ret_str));

            pm = &pt->methods[pt->num_methods++];
            memset(pm, 0, sizeof(*pm));
            strncpy(pm->method_name, name, STDLIB_NAME_LEN - 1);
            pm->num_params = count_params(params, 1);
            pm->ret_type = ret_type_from_str(ret_str);

            if (pending_decorator) {
                /* C-backed method */
                strncpy(pm->asm_name, pending_asm, STDLIB_ASM_LEN - 1);
                pm->fast_argc = pending_fast_argc;
                pm->is_pir = 0;
            } else {
                /* No decorator = Python-backed method */
                pm->asm_name[0] = '\0';
                pm->fast_argc = -1;
                pm->is_pir = 1;
            }

            pending_decorator = 0;
            pending_asm[0] = '\0';
            pending_fast_argc = -1;
            continue;
        }
    }

    fclose(f);
    return 0;
}

static int parse_all_stubs(const char *stdlib_dir)
{
    char path[512];
    int i;
    static const char *type_files[] = {
        "str.py", "list.py", "dict.py", "set.py", "int.py", "tuple.py",
        "frozenst.py", "complex.py", "bytearr.py", 0
    };

    snprintf(path, sizeof(path), "%s/builtins/funcs.py", stdlib_dir);
    if (parse_functions_file(path) < 0) return -1;

    for (i = 0; type_files[i]; i++) {
        snprintf(path, sizeof(path), "%s/builtins/%s", stdlib_dir, type_files[i]);
        if (parse_type_file(path) < 0) return -1;
    }

    return 0;
}

/* ================================================================= */
/* Pass 2: Compile pyfncs.py to PIR                                    */
/* ================================================================= */

static PIRModule *compile_pyfncs(const char *pyfncs_path,
                                  StdlibRegistry *temp_reg)
{
    Lexer *lexer;
    Parser *parser;
    SemanticAnalyzer *sema;
    PIRBuilder *pir_builder;
    ASTNode *module_ast;
    PIRModule *pir_mod;

    error_init(pyfncs_path);
    types_init();

    lexer = new Lexer();
    if (!lexer->open(pyfncs_path)) {
        fprintf(stderr, "stdbld: cannot open %s\n", pyfncs_path);
        delete lexer;
        types_shutdown();
        error_shutdown();
        return 0;
    }

    parser = new Parser();
    parser->init(lexer);
    module_ast = parser->parse_module();

    if (parser->get_error_count() > 0) {
        fprintf(stderr, "stdbld: %d parse error(s) in %s\n",
                parser->get_error_count(), pyfncs_path);
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 0;
    }

    sema = new SemanticAnalyzer();
    if (temp_reg) sema->set_stdlib(temp_reg);
    sema->analyze(module_ast);

    if (sema->get_error_count() > 0) {
        fprintf(stderr, "stdbld: %d sema error(s) in %s\n",
                sema->get_error_count(), pyfncs_path);
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 0;
    }

    pir_builder = new PIRBuilder();
    pir_builder->init(sema);
    if (temp_reg) pir_builder->set_stdlib(temp_reg);

    /* Build PIR in library mode (no main entry) */
    pir_mod = pir_builder->build(module_ast);
    if (!pir_mod) {
        fprintf(stderr, "stdbld: PIR build failed for %s\n", pyfncs_path);
    }

    /* Set library mode: no main entry point */
    if (pir_mod) {
        pir_mod->is_main_module = 0;
    }

    delete pir_builder;
    delete sema;
    delete parser;
    delete lexer;
    ast_free_all();
    types_shutdown();
    error_shutdown();

    return pir_mod;
}

/* ================================================================= */
/* Build temporary StdlibRegistry from parsed metadata                 */
/* ================================================================= */

static StdlibRegistry *build_temp_registry()
{
    /* Write a temporary v2 .idx from already-parsed g_funcs/g_types,
     * then load it into a StdlibRegistry. This gives sema and pirbld
     * access to all builtin names and types when compiling pyfncs.py.
     * Without this, sema errors on calls to builtins like len(). */
    FILE *tmp;
    StdlibIdxHeader hdr;
    StdlibRegistry *reg;
    int i, j;
#ifdef __WATCOMC__
    const char *tmp_path = "_PDSTMP.IDX";
#else
    const char *tmp_path = "/tmp/_pydos_tmp.idx";
#endif

    tmp = fopen(tmp_path, "wb");
    if (!tmp) return 0;

    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, STDLIB_IDX_MAGIC, 4);
    hdr.version = STDLIB_IDX_VERSION_2;
    hdr.num_funcs = (short)g_num_funcs;
    hdr.num_types = (short)g_num_types;
    hdr.num_pir_funcs = 0;
    fwrite(&hdr, sizeof(hdr), 1, tmp);

    for (i = 0; i < g_num_funcs; i++) {
        BuiltinFuncEntry entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.py_name, g_funcs[i].py_name, STDLIB_NAME_LEN - 1);
        strncpy(entry.asm_name, g_funcs[i].asm_name, STDLIB_ASM_LEN - 1);
        entry.num_params = (short)g_funcs[i].num_params;
        entry.ret_type_kind = (short)g_funcs[i].ret_type;
        entry.is_exception = (char)g_funcs[i].is_exc;
        entry.is_pir = 0;  /* v2: no PIR info */
        entry.exc_code = (short)g_funcs[i].exc_code;
        fwrite(&entry, sizeof(entry), 1, tmp);
    }

    for (i = 0; i < g_num_types; i++) {
        int nm = g_types[i].num_methods;
        char buf[STDLIB_TYPE_NAME_LEN];
        short sk, sn;
        char op_buf[STDLIB_ASM_LEN];

        memset(buf, 0, sizeof(buf));
        strncpy(buf, g_types[i].type_name, STDLIB_TYPE_NAME_LEN - 1);
        fwrite(buf, STDLIB_TYPE_NAME_LEN, 1, tmp);
        sk = (short)g_types[i].type_kind;
        sn = (short)nm;
        fwrite(&sk, sizeof(short), 1, tmp);
        fwrite(&sn, sizeof(short), 1, tmp);
        {
            short rtag = find_pydt_tag(g_types[i].type_name);
            short rpad = 0;
            fwrite(&rtag, sizeof(short), 1, tmp);
            fwrite(&rpad, sizeof(short), 1, tmp);
        }

        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_getitem, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, tmp);
        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_setitem, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, tmp);
        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_getslice, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, tmp);
        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_contains, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, tmp);

        for (j = 0; j < nm; j++) {
            BuiltinMethodEntry mentry;
            memset(&mentry, 0, sizeof(mentry));
            strncpy(mentry.method_name, g_types[i].methods[j].method_name,
                    STDLIB_NAME_LEN - 1);
            strncpy(mentry.asm_name, g_types[i].methods[j].asm_name,
                    STDLIB_ASM_LEN - 1);
            mentry.num_params = (short)g_types[i].methods[j].num_params;
            mentry.ret_type_kind = (short)g_types[i].methods[j].ret_type;
            mentry.fast_argc = (short)g_types[i].methods[j].fast_argc;
            mentry.is_pir = 0;
            fwrite(&mentry, sizeof(mentry), 1, tmp);
        }
    }

    fclose(tmp);

    reg = new StdlibRegistry();
    if (!reg->load_idx(tmp_path)) {
        delete reg;
        remove(tmp_path);
        return 0;
    }
    remove(tmp_path);
    return reg;
}

/* ================================================================= */
/* Write v3 stdlib.idx                                                 */
/* ================================================================= */

static void write_short(FILE *f, short val)
{
    fwrite(&val, sizeof(short), 1, f);
}

/* Helper: find PIR function by name in module */
static PIRFunction *find_pir_func(PIRModule *mod, const char *name)
{
    int fi;
    if (!mod || !name) return 0;
    for (fi = 0; fi < mod->functions.size(); fi++) {
        if (mod->functions[fi]->name &&
            strcmp(mod->functions[fi]->name, name) == 0)
            return mod->functions[fi];
    }
    return 0;
}

/* Collect all PIR functions to serialize: start from is_pir builtins,
 * then transitively follow MAKE_GENERATOR/MAKE_COROUTINE/MAKE_FUNCTION
 * references to include internal resume functions. */
static int collect_pir_funcs(PIRModule *pir_mod,
                              PIRFunction **out_funcs, int out_cap)
{
    int count = 0;
    int wi, bi;

    if (!pir_mod) return 0;

    /* Seed: all is_pir builtin functions that exist in the PIR module */
    {
        int i;
        for (i = 0; i < g_num_funcs; i++) {
            if (!g_funcs[i].is_pir) continue;
            PIRFunction *f = find_pir_func(pir_mod, g_funcs[i].py_name);
            if (f && count < out_cap)
                out_funcs[count++] = f;
        }
    }

    /* Seed: all is_pir methods (named TYPE_METHOD in PIR module) */
    {
        int ti, mi;
        for (ti = 0; ti < g_num_types; ti++) {
            for (mi = 0; mi < g_types[ti].num_methods; mi++) {
                char pir_name[STDLIB_NAME_LEN];
                PIRFunction *f;
                if (!g_types[ti].methods[mi].is_pir) continue;
                snprintf(pir_name, sizeof(pir_name), "%s_%s",
                         g_types[ti].type_name, g_types[ti].methods[mi].method_name);
                f = find_pir_func(pir_mod, pir_name);
                if (f && count < out_cap)
                    out_funcs[count++] = f;
            }
        }
    }

    /* Worklist: scan each collected function for references to more functions */
    for (wi = 0; wi < count; wi++) {
        PIRFunction *func = out_funcs[wi];
        for (bi = 0; bi < func->blocks.size(); bi++) {
            PIRBlock *block = func->blocks[bi];
            PIRInst *inst;
            for (inst = block->first; inst; inst = inst->next) {
                PIRFunction *ref;
                if (inst->op != PIR_MAKE_GENERATOR &&
                    inst->op != PIR_MAKE_COROUTINE &&
                    inst->op != PIR_MAKE_FUNCTION)
                    continue;
                if (!inst->str_val) continue;
                ref = find_pir_func(pir_mod, inst->str_val);
                if (!ref) continue;
                /* Check if already collected */
                {
                    int ci, found = 0;
                    for (ci = 0; ci < count; ci++) {
                        if (out_funcs[ci] == ref) { found = 1; break; }
                    }
                    if (!found && count < out_cap)
                        out_funcs[count++] = ref;
                }
            }
        }
    }

    return count;
}

static int write_idx_v3(const char *output_path, PIRModule *pir_mod)
{
    FILE *out;
    StdlibIdxHeader hdr;
    int i, j;
    int num_pir_funcs = 0;
    PIRFunction *pir_to_write[128];

    /* Collect all PIR functions (user-facing + internal like _genresume_*) */
    if (pir_mod) {
        num_pir_funcs = collect_pir_funcs(pir_mod, pir_to_write, 128);
    }

    out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "stdbld: cannot open %s for writing\n", output_path);
        return -1;
    }

    /* Write header */
    memset(&hdr, 0, sizeof(hdr));
    memcpy(hdr.magic, STDLIB_IDX_MAGIC, 4);
    hdr.version = STDLIB_IDX_VERSION_3;
    hdr.num_funcs = (short)g_num_funcs;
    hdr.num_types = (short)g_num_types;
    hdr.num_pir_funcs = (short)num_pir_funcs;
    fwrite(&hdr, sizeof(hdr), 1, out);

    /* Write function entries */
    for (i = 0; i < g_num_funcs; i++) {
        BuiltinFuncEntry entry;
        memset(&entry, 0, sizeof(entry));
        strncpy(entry.py_name, g_funcs[i].py_name, STDLIB_NAME_LEN - 1);
        strncpy(entry.asm_name, g_funcs[i].asm_name, STDLIB_ASM_LEN - 1);
        entry.num_params = (short)g_funcs[i].num_params;
        entry.ret_type_kind = (short)g_funcs[i].ret_type;
        entry.is_exception = (char)g_funcs[i].is_exc;
        entry.is_pir = (char)g_funcs[i].is_pir;
        entry.exc_code = (short)g_funcs[i].exc_code;
        fwrite(&entry, sizeof(entry), 1, out);
    }

    /* Write type entries */
    for (i = 0; i < g_num_types; i++) {
        int nm = g_types[i].num_methods;
        char type_name_buf[STDLIB_TYPE_NAME_LEN];
        short type_kind = (short)g_types[i].type_kind;
        short num_methods = (short)nm;
        char op_buf[STDLIB_ASM_LEN];

        memset(type_name_buf, 0, sizeof(type_name_buf));
        strncpy(type_name_buf, g_types[i].type_name, STDLIB_TYPE_NAME_LEN - 1);
        fwrite(type_name_buf, STDLIB_TYPE_NAME_LEN, 1, out);
        write_short(out, type_kind);
        write_short(out, num_methods);
        write_short(out, find_pydt_tag(g_types[i].type_name));
        write_short(out, 0); /* reserved_pad */

        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_getitem, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_setitem, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_getslice, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

        memset(op_buf, 0, sizeof(op_buf));
        strncpy(op_buf, g_types[i].op_contains, STDLIB_ASM_LEN - 1);
        fwrite(op_buf, STDLIB_ASM_LEN, 1, out);

        for (j = 0; j < nm; j++) {
            BuiltinMethodEntry mentry;
            memset(&mentry, 0, sizeof(mentry));
            strncpy(mentry.method_name, g_types[i].methods[j].method_name, STDLIB_NAME_LEN - 1);
            strncpy(mentry.asm_name, g_types[i].methods[j].asm_name, STDLIB_ASM_LEN - 1);
            mentry.num_params = (short)g_types[i].methods[j].num_params;
            mentry.ret_type_kind = (short)g_types[i].methods[j].ret_type;
            mentry.fast_argc = (short)g_types[i].methods[j].fast_argc;
            mentry.is_pir = (short)g_types[i].methods[j].is_pir;
            fwrite(&mentry, sizeof(mentry), 1, out);
        }
    }

    /* Write PIR section */
    if (num_pir_funcs > 0 && pir_mod) {
        PirStringTable strtab;
        int pi;

        pir_strtab_init(&strtab);

        /* Collect strings from all PIR functions to be serialized */
        for (pi = 0; pi < num_pir_funcs; pi++) {
            pir_collect_strings(pir_to_write[pi], &strtab);
        }

        /* Write string table */
        pir_strtab_write(&strtab, out);

        /* Write PIR functions */
        for (pi = 0; pi < num_pir_funcs; pi++) {
            pir_serialize_func(pir_to_write[pi], out, &strtab);
        }

        pir_strtab_destroy(&strtab);
    }

    fclose(out);
    return 0;
}

/* ================================================================= */
/* Public entry point                                                  */
/* ================================================================= */

int stdlib_build(const char *stdlib_dir, const char *pyfncs_path,
                 const char *output_path)
{
    char pyfncs_buf[512];
    PIRModule *pir_mod = 0;
    int has_pir_funcs = 0;
    int i;

    /* Initialize */
    memset(g_funcs, 0, sizeof(g_funcs));
    memset(g_types, 0, sizeof(g_types));
    g_num_funcs = 0;
    g_num_types = 0;

    /* Pass 1: parse metadata from all stub files */
    if (parse_all_stubs(stdlib_dir) < 0) {
        fprintf(stderr, "stdbld: failed to parse stubs in %s\n", stdlib_dir);
        return 1;
    }

    printf("stdbld: %d builtins, %d types\n", g_num_funcs, g_num_types);

    /* Check if there are any Python-backed functions */
    for (i = 0; i < g_num_funcs; i++) {
        if (g_funcs[i].is_pir) {
            has_pir_funcs = 1;
            printf("  pir: %s (params=%d)\n", g_funcs[i].py_name, g_funcs[i].num_params);
        }
    }

    /* Pass 2: compile pyfncs.py if there are Python-backed functions */
    if (has_pir_funcs) {
        if (!pyfncs_path) {
            snprintf(pyfncs_buf, sizeof(pyfncs_buf),
                     "%s/builtins/pyfncs.py", stdlib_dir);
            pyfncs_path = pyfncs_buf;
        }

        /* Check if pyfncs.py exists */
        {
            FILE *test = fopen(pyfncs_path, "r");
            if (test) {
                fclose(test);
                StdlibRegistry *temp_reg = build_temp_registry();
                pir_mod = compile_pyfncs(pyfncs_path, temp_reg);
                if (temp_reg) delete temp_reg;

                if (!pir_mod) {
                    fprintf(stderr, "stdbld: failed to compile %s\n", pyfncs_path);
                    return 1;
                }

                printf("stdbld: compiled %d PIR functions from %s\n",
                       pir_mod->functions.size(), pyfncs_path);
            } else {
                fprintf(stderr, "stdbld: warning: %s not found, "
                        "PIR functions will be empty\n", pyfncs_path);
            }
        }
    }

    /* Write v3 stdlib.idx */
    if (write_idx_v3(output_path, pir_mod) < 0) {
        if (pir_mod) pir_module_free(pir_mod);
        return 1;
    }

    printf("stdbld: wrote %s (v3, %d funcs, %d types",
           output_path, g_num_funcs, g_num_types);
    if (pir_mod) {
        int pc = 0;
        for (i = 0; i < g_num_funcs; i++) {
            if (g_funcs[i].is_pir) pc++;
        }
        printf(", %d pir", pc);
    }
    printf(")\n");

    if (pir_mod) pir_module_free(pir_mod);
    return 0;
}
