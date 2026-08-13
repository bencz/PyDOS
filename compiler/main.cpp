/*
 * main.cpp - CLI entry point for PyDOS Python-to-8086 compiler
 *
 * Drives the compilation pipeline:
 *   1. Lexing            (lexer.h)
 *   2. Parsing           (parser.h)
 *   3. Semantic analysis (sema.h)
 *   4. Monomorphization  (mono.h)
 *   5. PIR build         (pirbld.h)
 *   6. PIR optimization  (piropt.h)
 *   7. PIR lower         (pirlwr.h)
 *   8. IR optimization   (iropt.h)
 *   9. Code generation   (codegen.h)
 *
 * Usage: PYDOS.EXE input.py [-o output.asm] [-v] [-t 8086|386]
 *        [--dump-pir] [--no-pir-opt]
 *
 * C++98 compatible, Open Watcom targeting 8086 real-mode DOS.
 * No STL.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lexer.h"
#include "ast.h"
#include "parser.h"
#include "types.h"
#include "sema.h"
#include "mono.h"
#include "ir.h"
#include "iropt.h"
#include "codegen.h"
#include "error.h"
#include "pir.h"
#include "pirbld.h"
#include "pirprt.h"
#include "pirlwr.h"
#include "piropt.h"
#include "pirtyp.h"
#include "piresc.h"
#include "stdscan.h"
#include "stdbld.h"
#include "pirmrg.h"
#include "modpath.h"
#include "execmode.h"
#include "astdce.h"
#include "pbclwr.h"

/* --------------------------------------------------------------- */
/* Print usage banner                                               */
/* --------------------------------------------------------------- */

static void print_usage()
{
    printf("PyDOS Python Compiler\n");
    printf("Usage: PYDOS.EXE input.py [-o output] [-v] [-t 8086|386]\n");
    printf("  -o file       Output .asm for native mode or .pbc for VM mode\n");
    printf("  -v            Verbose: dump tokens, AST, PIR, IR\n");
    printf("  -t target     Target architecture: 8086 (default) or 386\n");
    printf("  --mode mode   Execution mode: auto, native, vm, or hybrid\n");
    printf("  --dump-pir    Dump PIR text and exit\n");
    printf("  --dump-types  Dump type inference results and exit\n");
    printf("  --dump-escape Dump escape analysis results and exit\n");
    printf("  --no-pir-opt  Skip PIR optimization passes\n");
    printf("  --no-hygiene  Keep compiler-generated metadata scaffolding\n");
    printf("  -m name       Override auto-derived module name\n");
    printf("  -M            Force main entry point (default)\n");
    printf("  -L            Library mode: no main entry point\n");
    printf("  --entry func  Call func() from entry point after __init__\n");
    printf("  --search-path dir  Add module search path (for imports)\n");
    printf("  --no-sccp     Skip SCCP optimization pass\n");
    printf("  --no-gvn      Skip GVN optimization pass\n");
    printf("  --no-licm     Skip LICM optimization pass\n");
    printf("  --no-specialize  Skip type specialization pass\n");
    printf("  --no-scope    Skip arena scope insertion pass\n");
    printf("  --no-mem2reg  Skip mem2reg optimization pass\n");
    printf("  --no-die      Skip dead instruction elimination pass\n");
    printf("  --no-devirt   Skip devirtualization pass\n");
    printf("  --no-dbe      Skip dead block elimination pass\n");
    printf("  --no-func-dedup  Skip identical generic function folding\n");
    printf("  --stdlib-idx f  Load stdlib index file for builtin lookup\n");
    printf("  --build-stdlib dir  Build stdlib.idx from dir (requires -o)\n");
    printf("  -h            Show this help\n");
}

/* --------------------------------------------------------------- */
/* Derive default output filename: replace .py with .asm            */
/* --------------------------------------------------------------- */

static void make_default_output(char *dest, int dest_size,
                                const char *input, const char *extension)
{
    char *dot;
    int extension_size = (int)strlen(extension);

    strncpy(dest, input, dest_size - extension_size - 1);
    dest[dest_size - extension_size - 1] = '\0';

    dot = strrchr(dest, '.');
    if (dot) {
        strcpy(dot, extension);
    } else {
        strcat(dest, extension);
    }
}

static int write_binary_output(const char *path,
                               const PBCU8 *data, PBCU32 size)
{
    FILE *output;
    if (path == 0 || data == 0 || size == 0) return 0;
    output = fopen(path, "wb");
    if (output == 0) return 0;
    if (fwrite(data, 1, (size_t)size, output) != (size_t)size) {
        fclose(output);
        return 0;
    }
    return fclose(output) == 0;
}

/* --------------------------------------------------------------- */
/* Verbose token dump                                               */
/* --------------------------------------------------------------- */

static void dump_tokens(const char *filename)
{
    Lexer lex_dump;
    Token t;

    if (!lex_dump.open(filename)) {
        fprintf(stderr, "Cannot open file for token dump: %s\n", filename);
        return;
    }

    printf("=== Tokens ===\n");
    do {
        t = lex_dump.next_token();
        printf("  %d:%d  %-15s", t.line, t.col, token_type_name(t.type));
        if (t.text && t.text_len > 0) {
            int j;
            printf("  '");
            for (j = 0; j < t.text_len && j < 40; j++) {
                putchar(t.text[j]);
            }
            printf("'");
        }
        printf("\n");
    } while (t.type != TOK_EOF && t.type != TOK_ERROR);
    printf("\n");
}

/* --------------------------------------------------------------- */
/* Source-module linker                                             */
/* --------------------------------------------------------------- */

/* Upper bound on modules linked into one program.  Hitting it is a hard
 * error: silently skipping a module produced executables with missing
 * code, and the paired lexer table below must never be smaller than the
 * set of linked modules (AST token strings borrow Lexer storage). */
#define MAX_LINKED_MODULES 256

static Lexer *source_module_lexers[MAX_LINKED_MODULES];
static int source_module_lexer_count = 0;

enum SourceRequirement {
    SOURCE_REQ_NONE        = 0,
    SOURCE_REQ_FILE_IO     = 1,
    SOURCE_REQ_DESCRIPTORS = 2
};

struct SourceRequirementTrigger {
    const char *name;
    int length;
    unsigned int requirement;
};

/* Source-backed language services are linked only when referenced.  Keep
 * detection data-driven so adding another Python implementation does not
 * add another scanner and another global flag. */
static unsigned int scan_source_requirements(const char *filename)
{
    static const SourceRequirementTrigger triggers[] = {
        { "open", 4, SOURCE_REQ_FILE_IO },
        { "property", 8, SOURCE_REQ_DESCRIPTORS },
        { "classmethod", 11, SOURCE_REQ_DESCRIPTORS },
        { "staticmethod", 12, SOURCE_REQ_DESCRIPTORS },
        { "ABC", 3, SOURCE_REQ_DESCRIPTORS },
        { "ABCMeta", 7, SOURCE_REQ_DESCRIPTORS },
        { "abstractmethod", 14, SOURCE_REQ_DESCRIPTORS },
        { "abstractclassmethod", 19, SOURCE_REQ_DESCRIPTORS },
        { "abstractstaticmethod", 20, SOURCE_REQ_DESCRIPTORS },
        { "abstractproperty", 16, SOURCE_REQ_DESCRIPTORS },
        { 0, 0, SOURCE_REQ_NONE }
    };
    unsigned int requirements = SOURCE_REQ_NONE;
    Lexer scan;
    Token token;
    if (!scan.open(filename)) return requirements;
    do {
        token = scan.next_token();
        if (token.type == TOK_IDENTIFIER && token.text != 0) {
            int i;
            for (i = 0; triggers[i].name != 0; i++) {
                if (token.text_len == triggers[i].length &&
                    strncmp(token.text, triggers[i].name,
                            triggers[i].length) == 0) {
                    requirements |= triggers[i].requirement;
                    break;
                }
            }
        }
    } while (token.type != TOK_EOF && token.type != TOK_ERROR);
    return requirements;
}

static int linked_module_seen(const char *name,
                              char seen[][128], int seen_count)
{
    int i;
    for (i = 0; i < seen_count; i++) {
        if (strcmp(seen[i], name) == 0) return 1;
    }
    return 0;
}

static ASTNode *load_source_module(const char *module_name,
                                   const char **search_paths,
                                   int num_search_paths,
                                   unsigned int *requirements)
{
    char path[512];
    Lexer *module_lexer;
    Parser *module_parser;
    ASTNode *module_ast;

    if (!module_resolve_source(module_name, search_paths, num_search_paths,
                               path, sizeof(path)))
        return 0;
    if (requirements) *requirements |= scan_source_requirements(path);

    module_lexer = new Lexer();
    if (!module_lexer->open(path)) {
        delete module_lexer;
        return 0;
    }
    module_parser = new Parser();
    module_parser->init(module_lexer);
    module_ast = module_parser->parse_module();
    if (module_parser->get_error_count() > 0) module_ast = 0;
    delete module_parser;
    if (module_ast) {
        /* AST token strings currently borrow storage from Lexer, so the
         * lexer of every parsed module must stay alive.  Deleting it here
         * (the old behavior when the table overflowed) left the returned
         * AST pointing into freed memory. */
        if (source_module_lexer_count >= MAX_LINKED_MODULES) {
            error_fatal("Too many linked modules (limit %d)",
                        MAX_LINKED_MODULES);
        }
        source_module_lexers[source_module_lexer_count++] = module_lexer;
    } else {
        delete module_lexer;
    }
    return module_ast;
}

static void link_source_imports(ASTNode **body_ptr,
                                const char **search_paths,
                                int num_search_paths,
                                char seen[][128], int *seen_count,
                                unsigned int *requirements)
{
    ASTNode *stmt;
    ASTNode *linked_first = 0;
    ASTNode *linked_last = 0;

    if (!body_ptr || !*body_ptr) return;
    for (stmt = *body_ptr; stmt; stmt = stmt->next) {
        const char *module_name;
        ASTNode *module_ast;
        ASTNode *module_body;
        ASTNode *tail;
        if (stmt->kind != AST_IMPORT_FROM) continue;
        module_name = stmt->data.import_from.module;
        if (!module_name || linked_module_seen(module_name, seen, *seen_count))
            continue;
        if (*seen_count >= MAX_LINKED_MODULES) {
            error_fatal("Too many linked modules at 'from %s import ...' "
                        "(limit %d)", module_name, MAX_LINKED_MODULES);
        }
        strncpy(seen[*seen_count], module_name, 127);
        seen[*seen_count][127] = '\0';
        (*seen_count)++;

        module_ast = load_source_module(module_name, search_paths,
                                        num_search_paths, requirements);
        if (!module_ast || module_ast->kind != AST_MODULE) continue;
        module_body = module_ast->data.module.body;
        link_source_imports(&module_body, search_paths, num_search_paths,
                            seen, seen_count, requirements);
        if (!module_body) continue;
        tail = module_body;
        while (tail->next) tail = tail->next;
        if (!linked_first) linked_first = module_body;
        else linked_last->next = module_body;
        linked_last = tail;
    }

    if (linked_last) {
        linked_last->next = *body_ptr;
        *body_ptr = linked_first;
    }
}

/* 'from x import y as z' binds z in the importing namespace.  The import
 * statement itself compiles to nothing (linking is by source), so the
 * binding is materialized here as an explicit top-level assignment
 * 'z = y' spliced right after each import — the flattened-namespace
 * equivalent of what CPython does.  Without it, call sites loaded a
 * never-initialized global named after the alias. */
static void bind_import_aliases(ASTNode *body)
{
    ASTNode *stmt;

    for (stmt = body; stmt; stmt = stmt->next) {
        ASTNode *name;
        ASTNode *last_binding = stmt;
        if (stmt->kind != AST_IMPORT_FROM) continue;
        for (name = stmt->data.import_from.names; name; name = name->next) {
            ASTNode *target;
            ASTNode *value;
            ASTNode *assign;
            if (name->kind != AST_IMPORT_NAME) continue;
            if (!name->data.import_name.alias ||
                !name->data.import_name.imported_name)
                continue;
            if (strcmp(name->data.import_name.alias,
                       name->data.import_name.imported_name) == 0)
                continue;

            target = ast_alloc(AST_NAME, stmt->line, stmt->col);
            target->data.name.id = name->data.import_name.alias;
            value = ast_alloc(AST_NAME, stmt->line, stmt->col);
            value->data.name.id = name->data.import_name.imported_name;
            assign = ast_alloc(AST_ASSIGN, stmt->line, stmt->col);
            assign->data.assign.targets = target;
            assign->data.assign.value = value;

            assign->next = last_binding->next;
            last_binding->next = assign;
            last_binding = assign;
        }
        stmt = last_binding;
    }
}

static int prepend_source_module(ASTNode **body_ptr, const char *module_name,
                                 const char **search_paths,
                                 int num_search_paths,
                                 char seen[][128], int *seen_count,
                                 unsigned int *requirements)
{
    ASTNode *module_ast;
    ASTNode *module_body;
    ASTNode *tail;

    if (linked_module_seen(module_name, seen, *seen_count)) return 1;
    if (*seen_count >= MAX_LINKED_MODULES) {
        error_fatal("Too many linked modules at builtin module '%s' "
                    "(limit %d)", module_name, MAX_LINKED_MODULES);
    }

    module_ast = load_source_module(module_name, search_paths,
                                    num_search_paths, requirements);
    if (!module_ast || module_ast->kind != AST_MODULE) return 0;

    strncpy(seen[*seen_count], module_name, 127);
    seen[*seen_count][127] = '\0';
    (*seen_count)++;

    module_body = module_ast->data.module.body;
    link_source_imports(&module_body, search_paths, num_search_paths,
                        seen, seen_count, requirements);
    if (!module_body) return 1;

    tail = module_body;
    while (tail->next) tail = tail->next;
    tail->next = *body_ptr;
    *body_ptr = module_body;
    return 1;
}

/* --------------------------------------------------------------- */
/* main                                                             */
/* --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *input_file;
    const char *output_file;
    int verbose;
    int target;
    ExecutionMode requested_mode;
    ExecutionPlan execution_plan;
    int dump_pir;
    int dump_types;
    int dump_escape;
    int no_pir_opt;
    int split_count;
    const char *module_name;
    int is_main_module;
    const char *entry_func;
    const char *search_paths[32];
    int num_search_paths;
    const char *stdlib_idx_path;
    const char *build_stdlib_dir;
    int i;
    char default_output[256];
    char auto_module_name[64];

    /* Defaults */
    input_file = 0;
    output_file = 0;
    verbose = 0;
    target = TARGET_8086;
    requested_mode = EXEC_MODE_AUTO;
    dump_pir = 0;
    dump_types = 0;
    dump_escape = 0;
    no_pir_opt = 0;
    split_count = 1;
    module_name = 0;
    is_main_module = 1;
    entry_func = 0;
    num_search_paths = 0;
    stdlib_idx_path = 0;
    build_stdlib_dir = 0;

    /* Parse command line */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_file = argv[++i];
        } else if (strcmp(argv[i], "-t") == 0 && i + 1 < argc) {
            i++;
            if (strcmp(argv[i], "8086") == 0) {
                target = TARGET_8086;
            } else if (strcmp(argv[i], "386") == 0) {
                target = TARGET_386;
            } else {
                fprintf(stderr, "Unknown target: %s (use 8086 or 386)\n",
                        argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "--mode") == 0 && i + 1 < argc) {
            i++;
            if (!execution_mode_parse(argv[i], &requested_mode)) {
                fprintf(stderr,
                        "Unknown execution mode: %s "
                        "(use auto, native, vm, or hybrid)\n",
                        argv[i]);
                return 1;
            }
        } else if (strcmp(argv[i], "--dump-pir") == 0) {
            dump_pir = 1;
        } else if (strcmp(argv[i], "--dump-types") == 0) {
            dump_types = 1;
        } else if (strcmp(argv[i], "--dump-escape") == 0) {
            dump_escape = 1;
        } else if (strcmp(argv[i], "--split") == 0 && i + 1 < argc) {
            split_count = atoi(argv[++i]);
            if (split_count < 1) split_count = 1;
        } else if (strcmp(argv[i], "--no-pir-opt") == 0) {
            no_pir_opt = 1;
        } else if (strcmp(argv[i], "--no-sccp") == 0) {
            piropt_skip_sccp = 1;
        } else if (strcmp(argv[i], "--no-gvn") == 0) {
            piropt_skip_gvn = 1;
        } else if (strcmp(argv[i], "--no-licm") == 0) {
            piropt_skip_licm = 1;
        } else if (strcmp(argv[i], "--no-specialize") == 0) {
            piropt_skip_specialize = 1;
        } else if (strcmp(argv[i], "--no-scope") == 0) {
            piropt_skip_scope = 1;
        } else if (strcmp(argv[i], "--no-mem2reg") == 0) {
            piropt_skip_mem2reg = 1;
        } else if (strcmp(argv[i], "--no-die") == 0) {
            piropt_skip_die = 1;
        } else if (strcmp(argv[i], "--no-devirt") == 0) {
            piropt_skip_devirt = 1;
        } else if (strcmp(argv[i], "--no-dbe") == 0) {
            piropt_skip_dbe = 1;
        } else if (strcmp(argv[i], "--no-func-dedup") == 0) {
            piropt_skip_func_dedup = 1;
        } else if (strcmp(argv[i], "--no-hygiene") == 0) {
            piropt_skip_hygiene = 1;
        } else if (strcmp(argv[i], "--no-dead-code") == 0) {
            astdce_skip = 1;
        } else if (strcmp(argv[i], "-m") == 0 && i + 1 < argc) {
            module_name = argv[++i];
            /* -m just overrides auto-derived name; use -L for library mode */
        } else if (strcmp(argv[i], "-M") == 0) {
            is_main_module = 1;  /* force main entry point */
        } else if (strcmp(argv[i], "-L") == 0) {
            is_main_module = 0;  /* library mode: no main entry point */
        } else if (strcmp(argv[i], "--entry") == 0 && i + 1 < argc) {
            entry_func = argv[++i];
        } else if (strcmp(argv[i], "--stdlib-idx") == 0 && i + 1 < argc) {
            stdlib_idx_path = argv[++i];
        } else if (strcmp(argv[i], "--build-stdlib") == 0 && i + 1 < argc) {
            build_stdlib_dir = argv[++i];
        } else if (strcmp(argv[i], "--search-path") == 0 && i + 1 < argc) {
            if (num_search_paths < 32) {
                search_paths[num_search_paths++] = argv[++i];
            } else {
                i++;
            }
        } else if (strcmp(argv[i], "-h") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            print_usage();
            return 0;
        } else if (argv[i][0] != '-') {
            input_file = argv[i];
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return 1;
        }
    }

    /* --build-stdlib mode: generate stdlib.idx and exit */
    if (build_stdlib_dir) {
        if (!output_file) {
            fprintf(stderr, "--build-stdlib requires -o <output_path>\n");
            return 1;
        }
        return stdlib_build(build_stdlib_dir, 0, output_file);
    }

    if (!input_file) {
        print_usage();
        return 1;
    }

    /* The execution policy is explicit even while only the native engine is
     * available.  Unsupported modes fail here instead of silently producing
     * a native executable with different behavior from the request. */
    if (!execution_plan_resolve(requested_mode, EXEC_MODE_NATIVE,
                                EXEC_CAP_NATIVE | EXEC_CAP_VM,
                                &execution_plan)) {
        fprintf(stderr,
                "Execution mode '%s' is not available for target %s yet\n",
                execution_mode_name(requested_mode),
                target == TARGET_8086 ? "8086" : "386");
        return 1;
    }
    if (verbose) {
        printf("Execution mode: requested=%s effective=%s\n",
               execution_mode_name(execution_plan.requested),
               execution_mode_name(execution_plan.effective));
    }

    /* Pure-Python standard-library modules are source-linked just like user
     * modules.  Keep stdlib as the lowest-priority implicit search path. */
    {
        int has_stdlib = 0;
        for (i = 0; i < num_search_paths; i++) {
            if (strcmp(search_paths[i], "stdlib") == 0) has_stdlib = 1;
        }
        if (!has_stdlib && num_search_paths < 32)
            search_paths[num_search_paths++] = "stdlib";
    }

    /* Auto-derive module name from input filename if not set by -m */
    if (!module_name && input_file) {
        const char *base = strrchr(input_file, '/');
        const char *base2 = strrchr(input_file, '\\');
        const char *dot;
        int len;
        if (base2 && (!base || base2 > base)) base = base2;
        if (base) base++; else base = input_file;
        dot = strrchr(base, '.');
        len = dot ? (int)(dot - base) : (int)strlen(base);
        if (len > 63) len = 63;
        strncpy(auto_module_name, base, len);
        auto_module_name[len] = '\0';
        module_name = auto_module_name;
    }

    /* Derive default output filename if not specified */
    if (!output_file) {
        make_default_output(default_output, sizeof(default_output),
                            input_file,
                            execution_plan.effective == EXEC_MODE_VM
                            ? ".pbc" : ".asm");
        output_file = default_output;
    }

    /* --------------------------------------------------------------- */
    /* Initialize subsystems                                            */
    /* --------------------------------------------------------------- */

    error_init(input_file);
    types_init();

    /* Load stdlib index: explicit path or auto-detect next to binary */
    StdlibRegistry *stdlib_reg = 0;
    {
        const char *idx_path = stdlib_idx_path;
        char auto_idx[256];
        if (!idx_path) {
            /* Try stdlib.idx next to the compiler binary (argv[0]) */
            const char *slash = 0;
            const char *p;
            for (p = argv[0]; *p; p++) {
                if (*p == '/' || *p == '\\') slash = p;
            }
            if (slash) {
                int dirlen = (int)(slash - argv[0]) + 1;
                if (dirlen + 10 < 256) {
                    memcpy(auto_idx, argv[0], dirlen);
                    memcpy(auto_idx + dirlen, "stdlib.idx", 11);
                    idx_path = auto_idx;
                }
            } else {
                idx_path = "stdlib.idx";
            }
        }
        if (idx_path) {
            stdlib_reg = new StdlibRegistry();
            if (!stdlib_reg->load_idx(idx_path)) {
                /* Silent fail for auto-detect, warn for explicit */
                if (stdlib_idx_path) {
                    fprintf(stderr, "Warning: failed to load stdlib index: %s\n",
                            stdlib_idx_path);
                }
                delete stdlib_reg;
                stdlib_reg = 0;
            }
        }
    }

    /* --------------------------------------------------------------- */
    /* Phase 1: Lexing                                                  */
    /* --------------------------------------------------------------- */

    if (verbose) {
        dump_tokens(input_file);
    }

    Lexer *lexer = new Lexer();
    if (!lexer->open(input_file)) {
        delete lexer;
        error_fatal("Cannot open input file: %s", input_file);
    }

    /* --------------------------------------------------------------- */
    /* Phase 2: Parsing                                                 */
    /* --------------------------------------------------------------- */

    if (verbose) printf("=== Parser ===\n");

    Parser *parser = new Parser();
    ASTNode *module_ast;

    parser->init(lexer);
    module_ast = parser->parse_module();

    if (parser->get_error_count() > 0) {
        fprintf(stderr, "%d parse error(s)\n", parser->get_error_count());
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }

    if (module_ast && module_ast->kind == AST_MODULE) {
        /* 256 entries * 128 bytes = 32 KB: too large for the stack of the
         * DOS-hosted compiler build, so the table lives on the heap. */
        char (*linked_modules)[128];
        int linked_module_count = 0;
        unsigned int requirements = scan_source_requirements(input_file);
        /* Everything link_source_imports() prepends before this original
         * head belongs to linked modules — the boundary the dead-code
         * pass uses to tell linked code from the user's program. */
        ASTNode *user_body = module_ast->data.module.body;
        linked_modules = (char (*)[128])malloc(
            (size_t)MAX_LINKED_MODULES * 128);
        if (!linked_modules)
            error_fatal("Out of memory for the linked-module table");
        memset(linked_modules, 0, (size_t)MAX_LINKED_MODULES * 128);
        link_source_imports(&module_ast->data.module.body,
                            search_paths, num_search_paths,
                            linked_modules, &linked_module_count,
                            &requirements);

        /* Materialize alias bindings before DCE runs: the injected
         * 'z = y' assignments both make aliases work at runtime and act
         * as conservative liveness roots for their originals. */
        bind_import_aliases(module_ast->data.module.body);

        /* Drop linked definitions the program never reaches, before the
         * requirement modules below are prepended (they stay exempt: the
         * compiler may invoke property/open/... implicitly).  Library
         * builds keep everything — every export is a potential root. */
        if (is_main_module && linked_module_count > 0) {
            ast_dce_run(&module_ast->data.module.body, user_body,
                        entry_func, verbose);
        }

        /* Link ordinary Python open() first.  Its TextFile class uses
         * property, so the descriptor module discovered while loading it is
         * prepended afterwards and therefore executes before the I/O code. */
        if ((requirements & SOURCE_REQ_FILE_IO) != 0) {
            if (!prepend_source_module(&module_ast->data.module.body,
                                       "pydos.io.files", search_paths,
                                       num_search_paths, linked_modules,
                                       &linked_module_count, &requirements))
                error_fatal("Cannot load Python file I/O builtins");
        }

        /* Descriptor definitions must precede imported modules such as abc
         * that subclass or call them.  Link imports first, then put this
         * language-builtin dependency at the absolute front. */
        if ((requirements & SOURCE_REQ_DESCRIPTORS) != 0) {
            if (!prepend_source_module(&module_ast->data.module.body,
                                       "builtins.descriptors", search_paths,
                                       num_search_paths, linked_modules,
                                       &linked_module_count, &requirements))
                error_fatal("Cannot load Python descriptor builtins");
        }
        free(linked_modules);
    }

    if (verbose) {
        printf("AST:\n");
        ast_dump(module_ast, 0);
        printf("\n");
    }

    /* --------------------------------------------------------------- */
    /* Phase 3: Semantic analysis                                       */
    /* --------------------------------------------------------------- */

    if (verbose) printf("=== Semantic Analysis ===\n");

    SemanticAnalyzer *sema = new SemanticAnalyzer();
    if (stdlib_reg) sema->set_stdlib(stdlib_reg);
    if (num_search_paths > 0) {
        sema->set_search_paths(search_paths, num_search_paths);
    }
    sema->analyze(module_ast);

    if (sema->get_error_count() > 0) {
        fprintf(stderr, "%d semantic error(s)\n", sema->get_error_count());
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }

    /* --------------------------------------------------------------- */
    /* Phase 4: Monomorphization                                        */
    /* --------------------------------------------------------------- */

    if (verbose) printf("=== Monomorphization ===\n");

    Monomorphizer *mono = new Monomorphizer();
    mono->init(sema);
    mono->process(module_ast);

    if (mono->get_error_count() > 0) {
        fprintf(stderr, "%d monomorphization error(s)\n",
                mono->get_error_count());
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }

    /* --------------------------------------------------------------- */
    /* Phase 5: PIR build (AST -> PIR)                                  */
    /* --------------------------------------------------------------- */

    IRModule *ir_mod = 0;
    PIRBuilder *pir_builder = 0;
    PIRModule *pir_mod = 0;
    PIRLowerer *pir_lowerer = 0;

    if (verbose) printf("=== PIR Build ===\n");

    pir_builder = new PIRBuilder();
    pir_builder->init(sema);
    if (stdlib_reg) pir_builder->set_stdlib(stdlib_reg);
    pir_mod = pir_builder->build(module_ast);

    if (pir_builder->get_error_count() > 0) {
        fprintf(stderr, "%d PIR build error(s)\n",
                pir_builder->get_error_count());
        delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }

    /* Set module qualification fields */
    pir_mod->module_name = module_name;
    pir_mod->is_main_module = is_main_module;

    /* Set explicit entry function if specified with --entry */
    pir_mod->has_main_func = (entry_func != 0) ? 1 : 0;
    pir_mod->entry_func = entry_func;

    /* Merge Python-backed stdlib functions into the PIR module */
    if (stdlib_reg) {
        int merged = pir_merge_stdlib(pir_mod, stdlib_reg);
        if (verbose && merged > 0) {
            printf("Merged %d stdlib PIR function(s)\n", merged);
        }
    }

    if (dump_pir) {
        /* --dump-pir: print PIR text and exit */
        pir_print_module(pir_mod, stdout);
        pir_module_free(pir_mod);
        delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 0;
    }

    if (verbose) {
        printf("PIR (before optimization):\n");
        pir_print_module(pir_mod, stdout);
        printf("\n");
    }

    /* --------------------------------------------------------------- */
    /* Phase 6: PIR optimization                                        */
    /* --------------------------------------------------------------- */

    if (!no_pir_opt) {
        if (verbose) printf("=== PIR Optimization ===\n");

        PIROptimizer *piropt = new PIROptimizer();
        if (stdlib_reg) piropt->set_stdlib(stdlib_reg);
        piropt->optimize(pir_mod);
        if (verbose && (piropt->get_hygiene_instruction_count() > 0 ||
                        piropt->get_hygiene_exception_fast_path_count() > 0)) {
            printf("Hygiene removed %lu metadata bundle(s), "
                   "%lu PIR instruction(s), linked %lu cold exception "
                   "edge(s), observed mask 0x%04X\n",
                   piropt->get_hygiene_bundle_count(),
                   piropt->get_hygiene_instruction_count(),
                   piropt->get_hygiene_exception_fast_path_count(),
                   piropt->get_hygiene_feature_mask());
        }
        if (verbose && piropt->get_deduplicated_function_count() > 0) {
            printf("Shared %d identical generic function body/bodies\n",
                   piropt->get_deduplicated_function_count());
        }
        delete piropt;

        if (verbose) {
            printf("PIR (after optimization):\n");
            pir_print_module(pir_mod, stdout);
            printf("\n");
        }
    }

    /* Dump type inference / escape analysis if requested */
    if (dump_types || dump_escape) {
        int fi;
        for (fi = 0; fi < pir_mod->functions.size(); fi++) {
            PIRFunction *f = pir_mod->functions[fi];
            if (dump_types) pir_dump_types(f, stdout);
            if (dump_escape) pir_dump_escape(f, stdout);
        }
        if (pir_mod->init_func) {
            if (dump_types) pir_dump_types(pir_mod->init_func, stdout);
            if (dump_escape) pir_dump_escape(pir_mod->init_func, stdout);
        }
        pir_module_free(pir_mod);
        delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 0;
    }

    if (execution_plan.effective == EXEC_MODE_VM) {
        PBCPIRLowerer pbc_lowerer;
        PBCWriter pbc_writer;
        int pbc_ok;
        if (verbose) printf("=== PBC Lower ===\n");
        pbc_ok = pbc_lowerer.lower(pir_mod, pbc_writer);
        if (!pbc_ok) {
            fprintf(stderr, "PBC lowering failed");
            if (pbc_lowerer.get_error_function() != 0)
                fprintf(stderr, " in %s", pbc_lowerer.get_error_function());
            if (pbc_lowerer.get_error() != 0)
                fprintf(stderr, ": %s", pbc_lowerer.get_error());
            if (pbc_lowerer.get_error_function() != 0)
                fprintf(stderr, " (PIR %s, line %d)",
                        pir_op_name(pbc_lowerer.get_error_op()),
                        pbc_lowerer.get_error_line());
            fprintf(stderr, "\n");
        } else if (!write_binary_output(output_file, pbc_writer.data(),
                                        pbc_writer.size())) {
            fprintf(stderr, "Cannot write PBC output: %s\n", output_file);
            pbc_ok = 0;
        }
        if (pbc_ok)
            printf("Compiled %s -> %s\n", input_file, output_file);
        pir_module_free(pir_mod);
        delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        if (stdlib_reg) delete stdlib_reg;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return pbc_ok ? 0 : 1;
    }

    /* --------------------------------------------------------------- */
    /* Phase 7: PIR lower (PIR -> flat IR)                              */
    /* --------------------------------------------------------------- */

    if (verbose) printf("=== PIR Lower ===\n");

    pir_lowerer = new PIRLowerer();
    ir_mod = pir_lowerer->lower(pir_mod);

    if (pir_lowerer->get_error_count() > 0) {
        fprintf(stderr, "%d PIR lowering error(s)\n",
                pir_lowerer->get_error_count());
        pir_module_free(pir_mod);
        delete pir_lowerer;
        delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }

    if (verbose) {
        printf("IR (before optimization):\n");
        ir_dump(ir_mod, stdout);
        printf("\n");
    }

    /* Free PIR module — no longer needed after lowering to flat IR.
       Critical for 16-bit builds: reclaims hundreds of KB before codegen. */
    pir_module_free(pir_mod);
    pir_mod = 0;
    delete pir_builder;
    pir_builder = 0;

    /* --------------------------------------------------------------- */
    /* Phase 8: IR optimization                                         */
    /* --------------------------------------------------------------- */

    if (verbose) printf("=== IR Optimization ===\n");

    IROptimizer *iropt = new IROptimizer();
    iropt->optimize(ir_mod);

    if (verbose) {
        printf("IR (after optimization):\n");
        ir_dump(ir_mod, stdout);
        printf("\n");
    }

    /* --------------------------------------------------------------- */
    /* Phase 9: Code generation                                         */
    /* --------------------------------------------------------------- */

    if (verbose) printf("=== Code Generation ===\n");

    CodeGeneratorBase *codegen = create_codegen(target);
    if (!codegen) {
        fprintf(stderr, "Failed to create code generator for target\n");
        delete iropt;
        if (pir_lowerer) delete pir_lowerer;
        if (pir_mod) pir_module_free(pir_mod);
        if (pir_builder) delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }
    codegen->set_verbose(verbose);
    if (stdlib_reg) codegen->set_stdlib(stdlib_reg);
    if (split_count > 1) {
        if (target != TARGET_386) {
            fprintf(stderr,
                    "--split is only supported for the 386 target\n");
            delete codegen;
            return 1;
        }
        codegen->set_split(split_count);
    }

    if (!codegen->generate(ir_mod, output_file)) {
        fprintf(stderr, "Code generation failed\n");
        delete codegen;
        delete iropt;
        if (pir_lowerer) delete pir_lowerer;
        if (pir_mod) pir_module_free(pir_mod);
        if (pir_builder) delete pir_builder;
        delete mono;
        delete sema;
        delete parser;
        delete lexer;
        ast_free_all();
        types_shutdown();
        error_shutdown();
        return 1;
    }

    printf("Compiled %s -> %s\n", input_file, output_file);

    /* --------------------------------------------------------------- */
    /* Cleanup                                                          */
    /* --------------------------------------------------------------- */

    delete codegen;
    delete iropt;
    if (pir_lowerer) delete pir_lowerer;
    if (pir_mod) pir_module_free(pir_mod);
    if (pir_builder) delete pir_builder;
    delete mono;
    delete sema;
    delete parser;
    delete lexer;
    if (stdlib_reg) delete stdlib_reg;
    ast_free_all();
    types_shutdown();
    error_shutdown();

    return 0;
}
