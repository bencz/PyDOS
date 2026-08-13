#ifndef ASTDCE_H
#define ASTDCE_H

/*
 * astdce.h - Dead-code elimination for source-linked modules.
 *
 * link_source_imports() concatenates the bodies of every module reached
 * through 'from x import y' in front of the main module.  Without this
 * pass every definition of every linked module is compiled into the
 * executable (~180 bytes of .EXE per Python line).
 *
 * The pass removes top-level function and class definitions in the
 * linked-module region that are unreachable from the main module.  It
 * never removes individual methods, decorated definitions, classes with
 * an explicit metaclass or class keywords, or anything in the main
 * module itself, so vtables and implicitly invoked dunders of surviving
 * classes stay intact by construction.
 *
 * Runs on the AST after linking and before semantic analysis, which is
 * also before monomorphization: unused generics disappear before any
 * specialization is generated.
 */

struct ASTNode;

/* When non-zero (--no-dead-code) the pass is a no-op. */
extern int astdce_skip;

/*
 * Remove unreferenced linked-region definitions from the flattened
 * module body.
 *
 *   body_ptr      - address of the module body head pointer; updated in
 *                   place when leading statements are removed.
 *   main_boundary - first statement of the original main-module body,
 *                   captured before link_source_imports() ran.  Every
 *                   statement before it belongs to linked modules.
 *   entry_func    - optional --entry function name kept as a root.
 *   verbose       - print a summary of what was removed.
 *
 * Returns the number of removed definitions.
 */
int ast_dce_run(ASTNode **body_ptr, ASTNode *main_boundary,
                const char *entry_func, int verbose);

#endif /* ASTDCE_H */
