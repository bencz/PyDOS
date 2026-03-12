/*
 * modscan.cpp - Module scanner implementation
 *
 * Scans imported .py files to extract top-level symbol information.
 * Uses Lexer + Parser to build an AST, then walks top-level statements
 * to find functions, classes, and global assignments.
 *
 * No sema/PIR/codegen — just symbol extraction.
 *
 * C++98 compatible, Open Watcom wpp.
 */

#include "modscan.h"
#include "lexer.h"
#include "parser.h"
#include "ast.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --------------------------------------------------------------- */
/* Utility: strdup (local copy)                                      */
/* --------------------------------------------------------------- */
static char *mscan_str_dup(const char *s)
{
    int len;
    char *d;
    if (!s) return 0;
    len = (int)strlen(s);
    d = (char *)malloc(len + 1);
    if (d) memcpy(d, s, len + 1);
    return d;
}

/* --------------------------------------------------------------- */
/* Count parameters in a Param linked list                           */
/* --------------------------------------------------------------- */
static int count_params(Param *params)
{
    int n = 0;
    Param *p;
    for (p = params; p; p = p->next) {
        /* Skip bare * separator (keyword-only marker) */
        if (p->is_star && p->name && strcmp(p->name, "*") == 0)
            continue;
        n++;
    }
    return n;
}

/* --------------------------------------------------------------- */
/* Walk top-level AST to extract symbols                             */
/* --------------------------------------------------------------- */
static void scan_toplevel(ASTNode *body, ModuleInfo *out)
{
    ASTNode *stmt;

    for (stmt = body; stmt; stmt = stmt->next) {
        switch (stmt->kind) {

        case AST_FUNC_DEF:
            if (out->num_functions < 128) {
                int idx = out->num_functions++;
                out->functions[idx].name =
                    mscan_str_dup(stmt->data.func_def.name);
                out->functions[idx].num_params =
                    count_params(stmt->data.func_def.params);
            }
            break;

        case AST_CLASS_DEF:
            if (out->num_classes < 32) {
                int idx = out->num_classes++;
                out->classes[idx].name =
                    mscan_str_dup(stmt->data.class_def.name);
                /* Extract first base class name if present */
                out->classes[idx].base_name = 0;
                if (stmt->data.class_def.bases &&
                    stmt->data.class_def.bases->kind == AST_NAME) {
                    out->classes[idx].base_name =
                        mscan_str_dup(stmt->data.class_def.bases->data.name.id);
                }
            }
            break;

        case AST_ASSIGN:
            /* Record simple name assignments as globals */
            if (stmt->data.assign.targets &&
                stmt->data.assign.targets->kind == AST_NAME) {
                if (out->num_globals < 128) {
                    out->globals[out->num_globals++] =
                        mscan_str_dup(stmt->data.assign.targets->data.name.id);
                }
            }
            break;

        case AST_ANN_ASSIGN:
            /* Annotated assignment: x: int = 5 */
            if (stmt->data.ann_assign.target &&
                stmt->data.ann_assign.target->kind == AST_NAME) {
                if (out->num_globals < 128) {
                    out->globals[out->num_globals++] =
                        mscan_str_dup(stmt->data.ann_assign.target->data.name.id);
                }
            }
            break;

        default:
            break;
        }
    }
}

/* --------------------------------------------------------------- */
/* module_scan - scan a Python file for top-level symbols            */
/* --------------------------------------------------------------- */
int module_scan(const char *module_name,
                const char **search_paths, int num_search_paths,
                ModuleInfo *out)
{
    char filepath[512];
    int i;
    Lexer *lexer;
    Parser *parser;
    ASTNode *module_ast;

    if (!module_name || !out) return 0;

    /* Initialize output */
    memset(out, 0, sizeof(ModuleInfo));
    out->module_name = mscan_str_dup(module_name);

    /* Try to find module_name.py in each search path */
    lexer = 0;
    for (i = 0; i < num_search_paths; i++) {
        int plen = (int)strlen(search_paths[i]);
        int mlen = (int)strlen(module_name);

        if (plen + mlen + 5 >= 512) continue; /* path too long */

        strcpy(filepath, search_paths[i]);
        /* Ensure trailing separator */
        if (plen > 0 && filepath[plen - 1] != '/' &&
            filepath[plen - 1] != '\\') {
            filepath[plen] = '/';
            filepath[plen + 1] = '\0';
        }
        strcat(filepath, module_name);
        strcat(filepath, ".py");

        lexer = new Lexer();
        if (lexer->open(filepath)) {
            break; /* found it */
        }
        delete lexer;
        lexer = 0;
    }

    /* Also try current directory: module_name.py */
    if (!lexer) {
        int mlen = (int)strlen(module_name);
        if (mlen + 4 < 512) {
            strcpy(filepath, module_name);
            strcat(filepath, ".py");
            lexer = new Lexer();
            if (!lexer->open(filepath)) {
                delete lexer;
                return 0;
            }
        } else {
            return 0;
        }
    }

    /* Parse */
    parser = new Parser();
    parser->init(lexer);
    module_ast = parser->parse_module();

    if (parser->get_error_count() > 0) {
        /* Parse errors — still try to extract what we got */
    }

    /* Walk top-level AST */
    if (module_ast && module_ast->kind == AST_MODULE) {
        scan_toplevel(module_ast->data.module.body, out);
    } else if (module_ast) {
        scan_toplevel(module_ast, out);
    }

    out->valid = 1;

    /* Cleanup — note: AST is arena-allocated, freed globally by ast_free_all()
     * in main.cpp. We must NOT call ast_free_all() here as it would destroy
     * the main module's AST too. The scanned AST nodes will be freed when
     * main.cpp calls ast_free_all() at the end. */
    delete parser;
    delete lexer;

    return 1;
}
