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

/* --------------------------------------------------------------- */
/* Print usage banner                                               */
/* --------------------------------------------------------------- */

static void print_usage()
{
    printf("PyDOS Python Compiler\n");
    printf("Usage: PYDOS.EXE input.py [-o output.asm] [-v] [-t 8086|386]\n");
    printf("  -o file       Output assembly file (default: input with .asm extension)\n");
    printf("  -v            Verbose: dump tokens, AST, PIR, IR\n");
    printf("  -t target     Target architecture: 8086 (default) or 386\n");
    printf("  --dump-pir    Dump PIR text and exit\n");
    printf("  --dump-types  Dump type inference results and exit\n");
    printf("  --dump-escape Dump escape analysis results and exit\n");
    printf("  --no-pir-opt  Skip PIR optimization passes\n");
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
    printf("  --stdlib-idx f  Load stdlib index file for builtin lookup\n");
    printf("  --build-stdlib dir  Build stdlib.idx from dir (requires -o)\n");
    printf("  -h            Show this help\n");
}

/* --------------------------------------------------------------- */
/* Derive default output filename: replace .py with .asm            */
/* --------------------------------------------------------------- */

static void make_default_output(char *dest, int dest_size,
                                 const char *input)
{
    char *dot;

    strncpy(dest, input, dest_size - 5);
    dest[dest_size - 5] = '\0';

    dot = strrchr(dest, '.');
    if (dot) {
        strcpy(dot, ".asm");
    } else {
        strcat(dest, ".asm");
    }
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
/* main                                                             */
/* --------------------------------------------------------------- */

int main(int argc, char *argv[])
{
    const char *input_file;
    const char *output_file;
    int verbose;
    int target;
    int dump_pir;
    int dump_types;
    int dump_escape;
    int no_pir_opt;
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
    dump_pir = 0;
    dump_types = 0;
    dump_escape = 0;
    no_pir_opt = 0;
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
        } else if (strcmp(argv[i], "--dump-pir") == 0) {
            dump_pir = 1;
        } else if (strcmp(argv[i], "--dump-types") == 0) {
            dump_types = 1;
        } else if (strcmp(argv[i], "--dump-escape") == 0) {
            dump_escape = 1;
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
                            input_file);
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
