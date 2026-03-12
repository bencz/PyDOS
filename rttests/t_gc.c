/*
 * t_gc.c - Unit tests for pdos_gc module
 *
 * Tests GC-tracked allocation, tracking/untracking,
 * root registration, collection, and statistics.
 */

#include "testfw.h"
#include "../runtime/pdos_gc.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_mem.h"

/* ------------------------------------------------------------------ */
/* GC allocation                                                       */
/* ------------------------------------------------------------------ */

TEST(gc_alloc)
{
    PyDosObj far *obj = pydos_gc_alloc();
    ASSERT_NOT_NULL(obj);
    /* Use container type so pydos_gc_track accepts it */
    obj->type = PYDT_LIST;
    obj->refcount = 1;
    obj->v.list.items = (PyDosObj far * far *)0;
    obj->v.list.len = 0;
    obj->v.list.cap = 0;
    pydos_gc_track(obj);
    pydos_gc_untrack(obj);
    /* Free the GC block (header + obj) directly */
    pydos_far_free(GC_HDR(obj));
}

/* ------------------------------------------------------------------ */
/* Tracking count                                                      */
/* ------------------------------------------------------------------ */

TEST(tracked_count)
{
    unsigned int before;
    unsigned int after_track;
    unsigned int after_untrack;
    PyDosObj far *obj;

    before = pydos_gc_tracked_count();

    obj = pydos_gc_alloc();
    ASSERT_NOT_NULL(obj);
    obj->type = PYDT_LIST;       /* container type — needed for tracking */
    obj->refcount = 1;
    obj->v.list.items = (PyDosObj far * far *)0;
    obj->v.list.len = 0;
    obj->v.list.cap = 0;
    pydos_gc_track(obj);          /* actually link into GC list */

    after_track = pydos_gc_tracked_count();
    ASSERT_EQ(after_track, before + 1);

    pydos_gc_untrack(obj);
    after_untrack = pydos_gc_tracked_count();
    ASSERT_EQ(after_untrack, before);

    /* Free the GC block (header + obj) directly */
    pydos_far_free(GC_HDR(obj));
}

/* ------------------------------------------------------------------ */
/* Collection with no garbage                                          */
/* ------------------------------------------------------------------ */

TEST(collect_no_garbage)
{
    PyDosObj far *obj;
    PyDosObj far * far *root_slot;
    unsigned int freed;

    /* Create a reachable object */
    obj = pydos_obj_new_int(123L);
    ASSERT_NOT_NULL(obj);

    /* Register it as a root so it is reachable */
    root_slot = &obj;
    pydos_gc_add_root(root_slot);

    freed = pydos_gc_collect();
    ASSERT_EQ(freed, 0);

    pydos_gc_remove_root(root_slot);
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Root management                                                     */
/* ------------------------------------------------------------------ */

TEST(add_root)
{
    PyDosObj far *obj;
    PyDosObj far * far *root_slot;
    int result;

    obj = pydos_obj_new_int(55L);
    ASSERT_NOT_NULL(obj);

    root_slot = &obj;
    result = pydos_gc_add_root(root_slot);
    ASSERT_EQ(result, 0);

    /* Run collection: object should survive since it is rooted */
    pydos_gc_collect();
    ASSERT_TRUE(obj->refcount > 0);

    pydos_gc_remove_root(root_slot);
    PYDOS_DECREF(obj);
}

TEST(remove_root)
{
    PyDosObj far *obj;
    PyDosObj far * far *root_slot;

    obj = pydos_obj_new_int(77L);
    ASSERT_NOT_NULL(obj);

    root_slot = &obj;
    pydos_gc_add_root(root_slot);
    pydos_gc_remove_root(root_slot);

    /* Should not crash; the root is simply removed */
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* Collections counter                                                 */
/* ------------------------------------------------------------------ */

TEST(collections_counter)
{
    unsigned long before;
    unsigned long after;

    before = pydos_gc_collections();
    pydos_gc_collect();
    after = pydos_gc_collections();
    ASSERT_TRUE(after > before);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_gc_tests(void)
{
    SUITE("pdos_gc");

    RUN(gc_alloc);
    RUN(tracked_count);
    RUN(collect_no_garbage);
    RUN(add_root);
    RUN(remove_root);
    RUN(collections_counter);
}
