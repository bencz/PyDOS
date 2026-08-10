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
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_cll.h"

/* ------------------------------------------------------------------ */
/* GC allocation                                                       */
/* ------------------------------------------------------------------ */

TEST(gc_alloc)
{
    unsigned int before = pydos_gc_tracked_count();
    PyDosObj far *obj = pydos_list_new(0);
    ASSERT_NOT_NULL(obj);
    ASSERT_TRUE(obj->flags & OBJ_FLAG_GC_TRACKED);
    ASSERT_EQ(pydos_gc_tracked_count(), before + 1);
    PYDOS_DECREF(obj);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
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

    obj = pydos_list_new(0);
    ASSERT_NOT_NULL(obj);

    after_track = pydos_gc_tracked_count();
    ASSERT_EQ(after_track, before + 1);

    PYDOS_DECREF(obj);
    after_untrack = pydos_gc_tracked_count();
    ASSERT_EQ(after_untrack, before);
}

TEST(cell_is_tracked)
{
    unsigned int before;
    PyDosObj far *cell;

    before = pydos_gc_tracked_count();
    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);
    ASSERT_TRUE(cell->flags & OBJ_FLAG_GC_TRACKED);
    ASSERT_EQ(pydos_gc_tracked_count(), before + 1);
    PYDOS_DECREF(cell);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

TEST(type_classification_matches_allocation)
{
    static const unsigned char tracked_types[] = {
        PYDT_LIST, PYDT_DICT, PYDT_TUPLE, PYDT_SET, PYDT_INSTANCE,
        PYDT_FUNCTION, PYDT_GENERATOR, PYDT_EXCEPTION, PYDT_CLASS,
        PYDT_CELL, PYDT_COROUTINE, PYDT_EXC_GROUP, PYDT_FROZENSET
    };
    static const unsigned char scalar_types[] = {
        PYDT_NONE, PYDT_BOOL, PYDT_INT, PYDT_FLOAT, PYDT_STR,
        PYDT_BYTES, PYDT_RANGE, PYDT_FILE, PYDT_COMPLEX,
        PYDT_BYTEARRAY, PYDT_NOTIMPLEMENTED
    };
    PyDosObj far *obj;
    unsigned int before;
    unsigned int i;

    before = pydos_gc_tracked_count();
    for (i = 0; i < sizeof(tracked_types); i++) {
        ASSERT_TRUE(pydos_gc_is_tracked_type(tracked_types[i]));
        obj = pydos_obj_alloc_type(tracked_types[i]);
        ASSERT_NOT_NULL(obj);
        ASSERT_TRUE(obj->flags & OBJ_FLAG_GC_TRACKED);
        PYDOS_DECREF(obj);
    }

    for (i = 0; i < sizeof(scalar_types); i++) {
        ASSERT_FALSE(pydos_gc_is_tracked_type(scalar_types[i]));
        obj = pydos_obj_alloc_type(scalar_types[i]);
        ASSERT_NOT_NULL(obj);
        ASSERT_FALSE(obj->flags & OBJ_FLAG_GC_TRACKED);
        PYDOS_DECREF(obj);
    }
    ASSERT_NULL(pydos_obj_alloc_type(PYDT_MAX));
    ASSERT_EQ(pydos_gc_tracked_count(), before);
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

TEST(control_state_and_thresholds)
{
    unsigned int old0;
    unsigned int old1;
    unsigned int old2;
    unsigned int value0;
    unsigned int value1;
    unsigned int value2;

    pydos_gc_get_thresholds(&old0, &old1, &old2);

    pydos_gc_disable();
    ASSERT_FALSE(pydos_gc_is_enabled());
    pydos_gc_enable();
    ASSERT_TRUE(pydos_gc_is_enabled());

    pydos_gc_set_thresholds(17U, 3U, 4U);
    pydos_gc_get_thresholds(&value0, &value1, &value2);
    ASSERT_EQ(value0, 17U);
    ASSERT_EQ(value1, 3U);
    ASSERT_EQ(value2, 4U);

    pydos_gc_set_thresholds(old0, old1, old2);
}

TEST(allocation_and_collection_statistics)
{
    PyDosObj far *cycle;
    PyDosObj far *result;
    unsigned long before_collected;
    unsigned long after_collected;

    pydos_gc_collect();
    ASSERT_EQ(pydos_gc_allocation_count(), 0U);

    cycle = pydos_list_new(1);
    ASSERT_NOT_NULL(cycle);
    result = pydos_list_append(cycle, cycle);
    PYDOS_DECREF(result);
    PYDOS_DECREF(cycle);
    ASSERT_TRUE(pydos_gc_allocation_count() > 0U);

    before_collected = pydos_gc_collected();
    ASSERT_TRUE(pydos_gc_collect() >= 1U);
    after_collected = pydos_gc_collected();
    ASSERT_TRUE(after_collected > before_collected);
    ASSERT_EQ(pydos_gc_allocation_count(), 0U);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_gc_tests(void)
{
    SUITE("pdos_gc");

    RUN(gc_alloc);
    RUN(tracked_count);
    RUN(cell_is_tracked);
    RUN(type_classification_matches_allocation);
    RUN(collect_no_garbage);
    RUN(add_root);
    RUN(remove_root);
    RUN(collections_counter);
    RUN(control_state_and_thresholds);
    RUN(allocation_and_collection_statistics);
}
