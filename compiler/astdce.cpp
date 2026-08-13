/*
 * astdce.cpp - Dead-code elimination for source-linked modules.
 *
 * Mark-and-sweep over the flattened module body.  Candidates for removal
 * are exclusively top-level AST_FUNC_DEF / AST_CLASS_DEF statements in
 * the linked-module region (everything before the main-module boundary)
 * that carry no decorators, no metaclass and no class keywords.
 *
 * Roots:
 *   - every statement of the main module (defs included, conservatively);
 *   - every non-candidate statement of the linked region (module-init
 *     side effects, decorated definitions, assignments, ...);
 *   - the --entry function name.
 *
 * References are collected by a full structural walk that emits every
 * identifier that can denote a top-level name: AST_NAME, annotation type
 * names, base-class expressions, decorator expressions, except-handler
 * types and PEP 695 bounds.  'from x import y as z' contributes an
 * alias mapping (z -> y) instead of acting as a root: AST_IMPORT_FROM
 * compiles to nothing (pirbld resolves it at link time), so importing a
 * name must not keep it alive on its own.
 *
 * Attribute names are deliberately not emitted: in the flattened
 * namespace 'obj.attr' can never denote a top-level definition (module
 * objects do not exist; 'import x' for x != sys is a compile error).
 */

#include <stdio.h>
#include <string.h>

#include "ast.h"
#include "astdce.h"
#include "pdstl.h"

int astdce_skip = 0;

/* ------------------------------------------------------------------ */
/* Candidate and alias tables                                          */
/* ------------------------------------------------------------------ */

#define DCE_BUCKETS 256

struct DceCandidate {
    const char *name;
    ASTNode *node;
    int marked;
    int bucket_next;         /* chain of candidate indices per bucket */
};

struct DceAlias {
    const char *alias;
    const char *original;
};

struct DceContext {
    PdVector<DceCandidate> candidates;
    PdVector<DceAlias> aliases;
    PdVector<ASTNode *> worklist;
    int buckets[DCE_BUCKETS];
};

static unsigned int dce_hash(const char *s)
{
    unsigned int h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

static void dce_add_candidate(DceContext *ctx, const char *name,
                              ASTNode *node)
{
    DceCandidate cand;
    unsigned int bucket = dce_hash(name) % DCE_BUCKETS;
    cand.name = name;
    cand.node = node;
    cand.marked = 0;
    cand.bucket_next = ctx->buckets[bucket];
    ctx->buckets[bucket] = ctx->candidates.size();
    ctx->candidates.push_back(cand);
}

/* Mark every candidate carrying this name (duplicate helper names in
 * different linked modules are all kept — conservative) and resolve
 * import aliases transitively.  The depth cap breaks pathological
 * alias cycles ('import y as z' + 'import z as y'). */
#define DCE_MAX_ALIAS_DEPTH 16

static void dce_mark_name_depth(DceContext *ctx, const char *name,
                                int depth)
{
    unsigned int bucket;
    int idx;
    int i;

    if (!name || depth > DCE_MAX_ALIAS_DEPTH) return;

    bucket = dce_hash(name) % DCE_BUCKETS;
    for (idx = ctx->buckets[bucket]; idx >= 0;
         idx = ctx->candidates[idx].bucket_next) {
        DceCandidate &cand = ctx->candidates[idx];
        if (!cand.marked && strcmp(cand.name, name) == 0) {
            cand.marked = 1;
            ctx->worklist.push_back(cand.node);
        }
    }

    for (i = 0; i < ctx->aliases.size(); i++) {
        if (strcmp(ctx->aliases[i].alias, name) == 0 &&
            strcmp(ctx->aliases[i].original, name) != 0) {
            dce_mark_name_depth(ctx, ctx->aliases[i].original, depth + 1);
        }
    }
}

static void dce_mark_name(DceContext *ctx, const char *name)
{
    dce_mark_name_depth(ctx, name, 0);
}

/* ------------------------------------------------------------------ */
/* Reference collection                                                */
/* ------------------------------------------------------------------ */

static void dce_collect(DceContext *ctx, ASTNode *node);

static void dce_collect_params(DceContext *ctx, Param *param)
{
    while (param) {
        dce_collect(ctx, param->annotation);
        dce_collect(ctx, param->default_val);
        param = param->next;
    }
}

static void dce_collect_bounds(DceContext *ctx, ASTNode **bounds, int count)
{
    int i;
    if (!bounds) return;
    for (i = 0; i < count; i++) {
        dce_collect(ctx, bounds[i]);
    }
}

/* Walk a subtree (including sibling chains of every child list) and
 * emit each identifier that can reference a top-level definition. */
static void dce_collect(DceContext *ctx, ASTNode *node)
{
    for (; node; node = node->next) {
        switch (node->kind) {
        case AST_MODULE:
            dce_collect(ctx, node->data.module.body);
            break;

        case AST_FUNC_DEF:
            dce_collect_params(ctx, node->data.func_def.params);
            dce_collect(ctx, node->data.func_def.return_type);
            dce_collect(ctx, node->data.func_def.body);
            dce_collect(ctx, node->data.func_def.decorators);
            dce_collect_bounds(ctx, node->data.func_def.type_param_bounds,
                               node->data.func_def.num_type_params);
            break;

        case AST_CLASS_DEF:
            dce_collect(ctx, node->data.class_def.bases);
            dce_collect(ctx, node->data.class_def.metaclass);
            dce_collect(ctx, node->data.class_def.keywords);
            dce_collect(ctx, node->data.class_def.body);
            dce_collect(ctx, node->data.class_def.decorators);
            dce_collect_bounds(ctx, node->data.class_def.type_param_bounds,
                               node->data.class_def.num_type_params);
            break;

        case AST_RETURN:
            dce_collect(ctx, node->data.ret.value);
            break;

        case AST_ASSIGN:
            dce_collect(ctx, node->data.assign.targets);
            dce_collect(ctx, node->data.assign.value);
            break;

        case AST_ANN_ASSIGN:
            dce_collect(ctx, node->data.ann_assign.target);
            dce_collect(ctx, node->data.ann_assign.annotation);
            dce_collect(ctx, node->data.ann_assign.value);
            break;

        case AST_AUG_ASSIGN:
            dce_collect(ctx, node->data.aug_assign.target);
            dce_collect(ctx, node->data.aug_assign.value);
            break;

        case AST_IF:
            dce_collect(ctx, node->data.if_stmt.condition);
            dce_collect(ctx, node->data.if_stmt.body);
            dce_collect(ctx, node->data.if_stmt.else_body);
            break;

        case AST_WHILE:
            dce_collect(ctx, node->data.while_stmt.condition);
            dce_collect(ctx, node->data.while_stmt.body);
            dce_collect(ctx, node->data.while_stmt.else_body);
            break;

        case AST_FOR:
            dce_collect(ctx, node->data.for_stmt.target);
            dce_collect(ctx, node->data.for_stmt.iter);
            dce_collect(ctx, node->data.for_stmt.body);
            dce_collect(ctx, node->data.for_stmt.else_body);
            break;

        case AST_BREAK:
        case AST_CONTINUE:
        case AST_PASS:
            break;

        case AST_EXPR_STMT:
            dce_collect(ctx, node->data.expr_stmt.expr);
            break;

        case AST_TRY:
            dce_collect(ctx, node->data.try_stmt.body);
            dce_collect(ctx, node->data.try_stmt.handlers);
            dce_collect(ctx, node->data.try_stmt.else_body);
            dce_collect(ctx, node->data.try_stmt.finally_body);
            break;

        case AST_EXCEPT_HANDLER:
            dce_collect(ctx, node->data.handler.type);
            dce_collect(ctx, node->data.handler.body);
            break;

        case AST_RAISE:
            dce_collect(ctx, node->data.raise_stmt.exc);
            dce_collect(ctx, node->data.raise_stmt.cause);
            break;

        case AST_WITH:
            dce_collect(ctx, node->data.with_stmt.items);
            dce_collect(ctx, node->data.with_stmt.body);
            break;

        case AST_WITH_ITEM:
            dce_collect(ctx, node->data.with_item.context_expr);
            dce_collect(ctx, node->data.with_item.optional_vars);
            break;

        case AST_MATCH:
            dce_collect(ctx, node->data.match_stmt.subject);
            dce_collect(ctx, node->data.match_stmt.cases);
            break;

        case AST_MATCH_CASE:
            dce_collect(ctx, node->data.match_case.pattern);
            dce_collect(ctx, node->data.match_case.guard);
            dce_collect(ctx, node->data.match_case.body);
            break;

        case AST_IMPORT:
        case AST_IMPORT_FROM:
        case AST_IMPORT_NAME:
            /* Imports bind names but compile to nothing; they are alias
             * metadata for this pass, never references. */
            break;

        case AST_YIELD_STMT:
            dce_collect(ctx, node->data.yield_expr.value);
            break;

        case AST_DELETE:
            dce_collect(ctx, node->data.del_stmt.targets);
            break;

        case AST_GLOBAL:
        case AST_NONLOCAL: {
            int i;
            for (i = 0; i < node->data.global_stmt.num_names; i++) {
                dce_mark_name(ctx, node->data.global_stmt.names[i]);
            }
            break;
        }

        case AST_ASSERT:
            dce_collect(ctx, node->data.assert_stmt.test);
            dce_collect(ctx, node->data.assert_stmt.msg);
            break;

        case AST_TYPE_ALIAS:
            dce_collect(ctx, node->data.type_alias.value);
            dce_collect_bounds(ctx, node->data.type_alias.type_param_bounds,
                               node->data.type_alias.num_type_params);
            break;

        case AST_BINOP:
            dce_collect(ctx, node->data.binop.left);
            dce_collect(ctx, node->data.binop.right);
            break;

        case AST_UNARYOP:
            dce_collect(ctx, node->data.unaryop.operand);
            break;

        case AST_COMPARE:
            dce_collect(ctx, node->data.compare.left);
            dce_collect(ctx, node->data.compare.comparators);
            break;

        case AST_BOOLOP:
            dce_collect(ctx, node->data.boolop.values);
            break;

        case AST_CALL:
            dce_collect(ctx, node->data.call.func);
            dce_collect(ctx, node->data.call.args);
            break;

        case AST_KEYWORD_ARG:
            dce_collect(ctx, node->data.keyword_arg.kw_value);
            break;

        case AST_ATTR:
            /* Only the object side: 'attr' cannot denote a top-level
             * name in the flattened namespace. */
            dce_collect(ctx, node->data.attribute.object);
            break;

        case AST_SUBSCRIPT:
            dce_collect(ctx, node->data.subscript.object);
            dce_collect(ctx, node->data.subscript.index);
            break;

        case AST_SLICE:
            dce_collect(ctx, node->data.slice.lower);
            dce_collect(ctx, node->data.slice.upper);
            dce_collect(ctx, node->data.slice.step);
            break;

        case AST_NAME:
            dce_mark_name(ctx, node->data.name.id);
            break;

        case AST_INT_LIT:
        case AST_FLOAT_LIT:
        case AST_COMPLEX_LIT:
        case AST_STR_LIT:
        case AST_BOOL_LIT:
        case AST_NONE_LIT:
            break;

        case AST_FSTRING:
            dce_collect(ctx, node->data.fstring.parts);
            break;

        case AST_LIST_EXPR:
        case AST_TUPLE_EXPR:
        case AST_SET_EXPR:
            dce_collect(ctx, node->data.collection.elts);
            break;

        case AST_DICT_EXPR:
            dce_collect(ctx, node->data.dict.keys);
            dce_collect(ctx, node->data.dict.values);
            break;

        case AST_LISTCOMP:
        case AST_SETCOMP:
        case AST_GENEXPR:
            dce_collect(ctx, node->data.listcomp.elt);
            dce_collect(ctx, node->data.listcomp.generators);
            break;

        case AST_DICTCOMP:
            dce_collect(ctx, node->data.dictcomp.key);
            dce_collect(ctx, node->data.dictcomp.value);
            dce_collect(ctx, node->data.dictcomp.generators);
            break;

        case AST_COMP_GENERATOR:
            dce_collect(ctx, node->data.comp_gen.target);
            dce_collect(ctx, node->data.comp_gen.iter);
            dce_collect(ctx, node->data.comp_gen.ifs);
            break;

        case AST_LAMBDA:
            dce_collect_params(ctx, node->data.lambda.params);
            dce_collect(ctx, node->data.lambda.body);
            break;

        case AST_IFEXPR:
            dce_collect(ctx, node->data.ifexpr.body);
            dce_collect(ctx, node->data.ifexpr.test);
            dce_collect(ctx, node->data.ifexpr.else_body);
            break;

        case AST_WALRUS:
            dce_collect(ctx, node->data.walrus.target);
            dce_collect(ctx, node->data.walrus.value);
            break;

        case AST_STARRED:
        case AST_AWAIT:
            dce_collect(ctx, node->data.starred.value);
            break;

        case AST_YIELD_EXPR:
        case AST_YIELD_FROM_EXPR:
            dce_collect(ctx, node->data.yield_expr.value);
            break;

        case AST_TYPE_NAME:
            /* Forward references in string annotations are normalized to
             * AST_TYPE_NAME by the parser, so this covers both forms. */
            dce_mark_name(ctx, node->data.type_name.tname);
            break;

        case AST_TYPE_GENERIC:
            dce_mark_name(ctx, node->data.type_generic.gname);
            dce_collect(ctx, node->data.type_generic.type_args);
            break;

        case AST_TYPE_UNION:
        case AST_TYPE_OPTIONAL:
        case AST_TYPE_TUPLE:
        case AST_TYPE_CALLABLE:
            dce_collect(ctx, node->data.type_union.types);
            break;
        }
    }
}

/* Walk exactly one statement, not its siblings. */
static void dce_collect_one(DceContext *ctx, ASTNode *node)
{
    ASTNode *saved_next;
    if (!node) return;
    saved_next = node->next;
    node->next = 0;
    dce_collect(ctx, node);
    node->next = saved_next;
}

/* ------------------------------------------------------------------ */
/* Candidate classification                                            */
/* ------------------------------------------------------------------ */

static int dce_is_candidate(ASTNode *stmt)
{
    if (!stmt) return 0;
    if (stmt->kind == AST_FUNC_DEF) {
        return stmt->data.func_def.name != 0 &&
               stmt->data.func_def.decorators == 0;
    }
    if (stmt->kind == AST_CLASS_DEF) {
        /* Decorators, metaclasses and class keywords can run arbitrary
         * registration code at class-creation time — never remove. */
        return stmt->data.class_def.name != 0 &&
               stmt->data.class_def.decorators == 0 &&
               stmt->data.class_def.metaclass == 0 &&
               stmt->data.class_def.keywords == 0;
    }
    return 0;
}

static void dce_add_aliases(DceContext *ctx, ASTNode *stmt)
{
    ASTNode *name;
    for (name = stmt->data.import_from.names; name; name = name->next) {
        if (name->kind == AST_IMPORT_NAME &&
            name->data.import_name.alias &&
            name->data.import_name.imported_name) {
            DceAlias alias;
            alias.alias = name->data.import_name.alias;
            alias.original = name->data.import_name.imported_name;
            ctx->aliases.push_back(alias);
        }
    }
}

/* ------------------------------------------------------------------ */
/* Entry point                                                         */
/* ------------------------------------------------------------------ */

int ast_dce_run(ASTNode **body_ptr, ASTNode *main_boundary,
                const char *entry_func, int verbose)
{
    DceContext ctx;
    ASTNode *stmt;
    ASTNode *prev;
    int total_candidates;
    int removed;
    int i;

    if (astdce_skip) return 0;
    if (!body_ptr || !*body_ptr || !main_boundary) return 0;
    if (*body_ptr == main_boundary) return 0;   /* nothing was linked */

    for (i = 0; i < DCE_BUCKETS; i++) ctx.buckets[i] = -1;

    /* Pass 1: candidates from the linked region, aliases from the whole
     * program (a main-module alias must keep its original alive). */
    for (stmt = *body_ptr; stmt && stmt != main_boundary;
         stmt = stmt->next) {
        if (dce_is_candidate(stmt)) {
            const char *name = stmt->kind == AST_FUNC_DEF
                               ? stmt->data.func_def.name
                               : stmt->data.class_def.name;
            dce_add_candidate(&ctx, name, stmt);
        } else if (stmt->kind == AST_IMPORT_FROM) {
            dce_add_aliases(&ctx, stmt);
        }
    }
    for (stmt = main_boundary; stmt; stmt = stmt->next) {
        if (stmt->kind == AST_IMPORT_FROM) {
            dce_add_aliases(&ctx, stmt);
        }
    }

    total_candidates = ctx.candidates.size();
    if (total_candidates == 0) {
        ctx.candidates.destroy();
        ctx.aliases.destroy();
        ctx.worklist.destroy();
        return 0;
    }

    /* Pass 2: seed the roots. */
    for (stmt = *body_ptr; stmt && stmt != main_boundary;
         stmt = stmt->next) {
        if (!dce_is_candidate(stmt)) {
            dce_collect_one(&ctx, stmt);
        }
    }
    for (stmt = main_boundary; stmt; stmt = stmt->next) {
        dce_collect_one(&ctx, stmt);
    }
    if (entry_func) {
        dce_mark_name(&ctx, entry_func);
    }

    /* Pass 3: transitive closure. */
    while (!ctx.worklist.empty()) {
        ASTNode *node = ctx.worklist.back();
        ctx.worklist.pop_back();
        dce_collect_one(&ctx, node);
    }

    /* Pass 4: unlink unmarked candidates from the linked region. */
    removed = 0;
    for (i = 0; i < ctx.candidates.size(); i++) {
        if (!ctx.candidates[i].marked) removed++;
    }
    if (removed > 0) {
        prev = 0;
        stmt = *body_ptr;
        while (stmt && stmt != main_boundary) {
            ASTNode *next = stmt->next;
            int drop = 0;
            if (dce_is_candidate(stmt)) {
                for (i = 0; i < ctx.candidates.size(); i++) {
                    if (ctx.candidates[i].node == stmt) {
                        drop = !ctx.candidates[i].marked;
                        break;
                    }
                }
            }
            if (drop) {
                if (prev) prev->next = next;
                else *body_ptr = next;
                /* Node storage stays in the arena; ast_free_all() owns it. */
            } else {
                prev = stmt;
            }
            stmt = next;
        }
    }

    if (verbose) {
        printf("Dead code: removed %d of %d top-level definitions "
               "from linked modules\n", removed, total_candidates);
    }

    ctx.candidates.destroy();
    ctx.aliases.destroy();
    ctx.worklist.destroy();
    return removed;
}
