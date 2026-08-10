/*
 * t_rt.c - Unit tests for pdos_rt module
 *
 * Tests runtime initialization state. Note that pydos_rt_init() has
 * already been called by main.c before these tests run.
 */

#include "testfw.h"
#include "../runtime/pdos_rt.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_gc.h"
#include "../runtime/pdos_lst.h"

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

TEST(rt_shutdown_releases_rooted_cycle)
{
    PyDosObj far *cycle;
    PyDosObj far *result;
    PyDosObj far * far *root_slot;

    cycle = pydos_list_new(1);
    ASSERT_NOT_NULL(cycle);
    result = pydos_list_append(cycle, cycle);
    PYDOS_DECREF(result);

    root_slot = &cycle;
    ASSERT_EQ(pydos_gc_add_root(root_slot), 0);
    PYDOS_DECREF(cycle);

    /* The final collect keeps this explicit root alive.  gc_shutdown must
     * still release the complete GCHeader + PyDosObj block safely. */
    pydos_rt_shutdown();
    cycle = (PyDosObj far *)0;

    pydos_rt_init();
    ASSERT_NOT_NULL(pydos_globals);
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
    RUN(rt_shutdown_releases_rooted_cycle);
}
