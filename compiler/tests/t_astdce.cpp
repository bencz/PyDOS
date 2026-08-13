/*
 * t_astdce.cpp - Unit tests for the AST dead-code-elimination pass.
 *
 * Builds flattened module bodies by hand (linked region + main region)
 * and checks which top-level definitions survive ast_dce_run().
 */

#include "../ast.h"
#include "../astdce.h"
#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(expr) \
    do { \
        if (!(expr)) { \
            fprintf(stderr, "CHECK failed at %s:%d: %s\n", \
                    __FILE__, __LINE__, #expr); \
            failures++; \
        } \
    } while (0)

/* ---- tiny AST builders ------------------------------------------- */

static ASTNode *mk_name(const char *id)
{
    ASTNode *node = ast_alloc(AST_NAME, 1, 1);
    node->data.name.id = id;
    return node;
}

static ASTNode *mk_call_stmt(const char *fn_name)
{
    ASTNode *call = ast_alloc(AST_CALL, 1, 1);
    ASTNode *stmt = ast_alloc(AST_EXPR_STMT, 1, 1);
    call->data.call.func = mk_name(fn_name);
    stmt->data.expr_stmt.expr = call;
    return stmt;
}

static ASTNode *mk_func(const char *name, ASTNode *body)
{
    ASTNode *node = ast_alloc(AST_FUNC_DEF, 1, 1);
    node->data.func_def.name = name;
    node->data.func_def.body = body ? body : ast_alloc(AST_PASS, 1, 1);
    return node;
}

static ASTNode *mk_class(const char *name, ASTNode *bases)
{
    ASTNode *node = ast_alloc(AST_CLASS_DEF, 1, 1);
    node->data.class_def.name = name;
    node->data.class_def.bases = bases;
    node->data.class_def.body = ast_alloc(AST_PASS, 1, 1);
    return node;
}

static ASTNode *mk_import_alias(const char *module, const char *original,
                                const char *alias)
{
    ASTNode *stmt = ast_alloc(AST_IMPORT_FROM, 1, 1);
    ASTNode *name = ast_alloc(AST_IMPORT_NAME, 1, 1);
    stmt->data.import_from.module = module;
    name->data.import_name.imported_name = original;
    name->data.import_name.alias = alias;
    stmt->data.import_from.names = name;
    return stmt;
}

static ASTNode *chain(ASTNode *first, ASTNode *second)
{
    ASTNode *tail = first;
    while (tail->next) tail = tail->next;
    tail->next = second;
    return first;
}

static int body_has(ASTNode *body, const char *def_name)
{
    ASTNode *stmt;
    for (stmt = body; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_FUNC_DEF &&
            stmt->data.func_def.name &&
            strcmp(stmt->data.func_def.name, def_name) == 0)
            return 1;
        if (stmt->kind == AST_CLASS_DEF &&
            stmt->data.class_def.name &&
            strcmp(stmt->data.class_def.name, def_name) == 0)
            return 1;
    }
    return 0;
}

/* ---- cases -------------------------------------------------------- */

/* Unreferenced linked def removed; referenced def kept. */
static void test_basic_removal(void)
{
    ASTNode *linked = chain(mk_func("used", 0), mk_func("unused", 0));
    ASTNode *main_body = mk_call_stmt("used");
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, 0, 0);

    CHECK(removed == 1);
    CHECK(body_has(body, "used"));
    CHECK(!body_has(body, "unused"));
}

/* Import alias in the main region keeps the original alive. */
static void test_alias_keeps_original(void)
{
    ASTNode *linked = chain(mk_func("target", 0), mk_func("dead", 0));
    ASTNode *main_body = chain(
        mk_import_alias("helper", "target", "renamed"),
        mk_call_stmt("renamed"));
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, 0, 0);

    CHECK(removed == 1);
    CHECK(body_has(body, "target"));
    CHECK(!body_has(body, "dead"));
}

/* A decorated definition is never a candidate, even when unreferenced. */
static void test_decorated_kept(void)
{
    ASTNode *decorated = mk_func("registered", 0);
    ASTNode *linked;
    ASTNode *main_body;
    ASTNode *body;
    int removed;

    decorated->data.func_def.decorators = mk_name("register");
    linked = chain(decorated, mk_func("plain_dead", 0));
    main_body = ast_alloc(AST_PASS, 1, 1);
    body = chain(linked, main_body);
    removed = ast_dce_run(&body, main_body, 0, 0);

    CHECK(removed == 1);
    CHECK(body_has(body, "registered"));
    CHECK(!body_has(body, "plain_dead"));
}

/* A class referenced only as a base class of a live class survives. */
static void test_base_class_kept(void)
{
    ASTNode *linked = chain(
        mk_class("Base", 0),
        chain(mk_class("Sub", mk_name("Base")), mk_class("Isolated", 0)));
    ASTNode *main_body = mk_call_stmt("Sub");
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, 0, 0);

    CHECK(removed == 1);
    CHECK(body_has(body, "Base"));
    CHECK(body_has(body, "Sub"));
    CHECK(!body_has(body, "Isolated"));
}

/* Liveness is transitive through marked bodies, and only there. */
static void test_transitive_marking(void)
{
    ASTNode *fa = mk_func("alpha", mk_call_stmt("beta"));
    ASTNode *fb = mk_func("beta", mk_call_stmt("gamma"));
    ASTNode *fc = mk_func("gamma", 0);
    ASTNode *fd = mk_func("delta", mk_call_stmt("gamma"));
    ASTNode *linked = chain(fa, chain(fb, chain(fc, fd)));
    ASTNode *main_body = mk_call_stmt("alpha");
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, 0, 0);

    /* delta is dead even though it references live gamma */
    CHECK(removed == 1);
    CHECK(body_has(body, "alpha"));
    CHECK(body_has(body, "beta"));
    CHECK(body_has(body, "gamma"));
    CHECK(!body_has(body, "delta"));
}

/* Definitions in the main region are never candidates. */
static void test_main_region_untouched(void)
{
    ASTNode *linked = mk_func("linked_dead", 0);
    ASTNode *main_body = mk_func("main_unreferenced", 0);
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, 0, 0);

    CHECK(removed == 1);
    CHECK(!body_has(body, "linked_dead"));
    CHECK(body_has(body, "main_unreferenced"));
}

/* --entry names a root. */
static void test_entry_func_root(void)
{
    ASTNode *linked = chain(mk_func("entry_point", 0), mk_func("dead", 0));
    ASTNode *main_body = ast_alloc(AST_PASS, 1, 1);
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, "entry_point", 0);

    CHECK(removed == 1);
    CHECK(body_has(body, "entry_point"));
    CHECK(!body_has(body, "dead"));
}

/* --no-dead-code disables the pass entirely. */
static void test_skip_flag(void)
{
    ASTNode *linked = mk_func("kept_by_flag", 0);
    ASTNode *main_body = ast_alloc(AST_PASS, 1, 1);
    ASTNode *body = chain(linked, main_body);
    int removed;

    astdce_skip = 1;
    removed = ast_dce_run(&body, main_body, 0, 0);
    astdce_skip = 0;

    CHECK(removed == 0);
    CHECK(body_has(body, "kept_by_flag"));
}

/* Removing the first statement updates the body head pointer. */
static void test_head_removal(void)
{
    ASTNode *linked = chain(mk_func("first_dead", 0), mk_func("used", 0));
    ASTNode *main_body = mk_call_stmt("used");
    ASTNode *body = chain(linked, main_body);
    int removed = ast_dce_run(&body, main_body, 0, 0);

    CHECK(removed == 1);
    CHECK(body->kind == AST_FUNC_DEF);
    CHECK(strcmp(body->data.func_def.name, "used") == 0);
}

int main()
{
    test_basic_removal();
    test_alias_keeps_original();
    test_decorated_kept();
    test_base_class_kept();
    test_transitive_marking();
    test_main_region_untouched();
    test_entry_func_root();
    test_skip_flag();
    test_head_removal();

    ast_free_all();

    if (failures != 0) {
        fprintf(stderr, "%d astdce test failure(s)\n", failures);
        return 1;
    }
    printf("ast dead-code elimination tests passed\n");
    return 0;
}
