/*
 * t_rt.c - Unit tests for pdos_rt module
 *
 * Tests runtime initialization state. Note that pydos_rt_init() has
 * already been called by main.c before these tests run.
 */

#include "testfw.h"
#include "../runtime/pdos_rt.h"
#include "../runtime/pdos_obj.h"

/* ------------------------------------------------------------------ */
/* Runtime state after init                                            */
/* ------------------------------------------------------------------ */

TEST(rt_globals_exist)
{
    /* The global namespace dict should have been created by init */
    ASSERT_NOT_NULL(pydos_globals);
}

TEST(rt_reinit)
{
    /* Calling init again should be idempotent and not crash */
    pydos_rt_init();
    ASSERT_TRUE(1);
}

TEST(rt_create_after_init)
{
    /* Verify that object creation works after init */
    PyDosObj far *obj = pydos_obj_new_int(999L);
    ASSERT_NOT_NULL(obj);
    ASSERT_EQ(obj->v.int_val, 999L);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_rt_tests(void)
{
    SUITE("pdos_rt");
    RUN(rt_globals_exist);
    RUN(rt_reinit);
    RUN(rt_create_after_init);
}
