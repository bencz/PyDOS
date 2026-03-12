/*
 * t_arn.c - Unit tests for pdos_arn module (arena scope allocator)
 *
 * Tests arena scope enter/exit, object tracking, nested scopes,
 * and edge cases (NULL, immortal objects, depth limit).
 * C89 compatible.
 */

#include "testfw.h"
#include "../runtime/pdos_arn.h"
#include "../runtime/pdos_obj.h"

/* ------------------------------------------------------------------ */
/* arn_enter_exit: enter + exit without crash                           */
/* ------------------------------------------------------------------ */

TEST(arn_enter_exit)
{
    pydos_arena_scope_enter();
    pydos_arena_scope_exit();
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* arn_track_sets_flag: tracked obj has OBJ_FLAG_ARENA set              */
/* ------------------------------------------------------------------ */

TEST(arn_track_sets_flag)
{
    PyDosObj far *obj;

    pydos_arena_scope_enter();
    obj = pydos_obj_new_int(1000);
    ASSERT_NOT_NULL(obj);
    pydos_arena_scope_track(obj);
    ASSERT_TRUE((obj->flags & OBJ_FLAG_ARENA) != 0);
    pydos_arena_scope_exit();
    /* obj freed by scope_exit — do NOT deref */
}

/* ------------------------------------------------------------------ */
/* arn_exit_frees: tracked obj freed at exit (no crash)                 */
/* ------------------------------------------------------------------ */

TEST(arn_exit_frees)
{
    PyDosObj far *obj;

    pydos_arena_scope_enter();
    obj = pydos_obj_new_str((const char far *)"test", 4);
    ASSERT_NOT_NULL(obj);
    pydos_arena_scope_track(obj);
    pydos_arena_scope_exit();
    /* If we get here without crash, obj was freed successfully */
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* arn_nested_scopes: inner exit frees inner objs only                  */
/* ------------------------------------------------------------------ */

TEST(arn_nested_scopes)
{
    PyDosObj far *outer_obj;
    PyDosObj far *inner_obj;

    pydos_arena_scope_enter();
    outer_obj = pydos_obj_new_int(1000);
    pydos_arena_scope_track(outer_obj);

    pydos_arena_scope_enter();
    inner_obj = pydos_obj_new_int(2000);
    pydos_arena_scope_track(inner_obj);
    pydos_arena_scope_exit();
    /* inner_obj freed, outer_obj still valid */

    ASSERT_TRUE((outer_obj->flags & OBJ_FLAG_ARENA) != 0);
    pydos_arena_scope_exit();
    /* outer_obj freed now */
}

/* ------------------------------------------------------------------ */
/* arn_null_skipped: track(NULL) does not crash                         */
/* ------------------------------------------------------------------ */

TEST(arn_null_skipped)
{
    pydos_arena_scope_enter();
    pydos_arena_scope_track((PyDosObj far *)0);
    pydos_arena_scope_exit();
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* arn_immortal_skipped: track(None) does NOT set OBJ_FLAG_ARENA        */
/* ------------------------------------------------------------------ */

TEST(arn_immortal_skipped)
{
    PyDosObj far *none_obj;

    pydos_arena_scope_enter();
    none_obj = pydos_obj_new_none();
    pydos_arena_scope_track(none_obj);
    ASSERT_TRUE((none_obj->flags & OBJ_FLAG_ARENA) == 0);
    pydos_arena_scope_exit();
    /* None is immortal — not freed by arena */
}

/* ------------------------------------------------------------------ */
/* arn_multiple_objects: track 5 objects, all freed at exit              */
/* ------------------------------------------------------------------ */

TEST(arn_multiple_objects)
{
    int i;
    pydos_arena_scope_enter();
    for (i = 0; i < 5; i++) {
        PyDosObj far *obj = pydos_obj_new_str((const char far *)"x", 1);
        pydos_arena_scope_track(obj);
    }
    pydos_arena_scope_exit();
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* arn_scope_depth_limit: exceeding max depth is silently ignored        */
/* ------------------------------------------------------------------ */

TEST(arn_scope_depth_limit)
{
    int i;
    /* Enter ARENA_MAX_DEPTH + 1 times */
    for (i = 0; i < ARENA_MAX_DEPTH + 1; i++) {
        pydos_arena_scope_enter();
    }
    /* Exit all — the extra one should be harmless */
    for (i = 0; i < ARENA_MAX_DEPTH + 1; i++) {
        pydos_arena_scope_exit();
    }
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_arn_tests(void)
{
    SUITE("pdos_arn");
    RUN(arn_enter_exit);
    RUN(arn_track_sets_flag);
    RUN(arn_exit_frees);
    RUN(arn_nested_scopes);
    RUN(arn_null_skipped);
    RUN(arn_immortal_skipped);
    RUN(arn_multiple_objects);
    RUN(arn_scope_depth_limit);
}
