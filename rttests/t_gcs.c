/*
 * t_gcs.c - GC stress tests for PyDOS runtime
 *
 * Exercises the mark-and-sweep garbage collector under load:
 * bulk allocation/tracking, mixed container types, rooted vs
 * unrooted survival, repeated collection cycles, auto-collect
 * threshold trigger, self-referencing cycles, and memory stability.
 *
 * These tests complement t_gc.c (basic unit tests) by verifying
 * GC behavior under realistic pressure conditions.
 */

#include "testfw.h"
#include "../runtime/pdos_gc.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_mem.h"

/* ------------------------------------------------------------------ */
/* Constants                                                            */
/* ------------------------------------------------------------------ */

#define GCS_BULK_N              80
#define GCS_SWEEP_N             50
#define GCS_ROOTED_N            10
#define GCS_DEAD_N              40
#define GCS_REPEAT_ROUNDS       30
#define GCS_OBJS_PER_ROUND      15
#define GCS_MIX_N               30
#define GCS_PRESSURE_ROUNDS     20
#define GCS_PRESSURE_OBJS       25

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/*
 * Allocate a GC-tracked empty list via the GC allocator.
 * The returned object has refcount=1, type=PYDT_LIST,
 * and is already linked into the GC tracking list.
 */
static PyDosObj far *make_gc_list(void)
{
    PyDosObj far *obj;
    obj = pydos_gc_alloc();
    if (obj == (PyDosObj far *)0) return obj;
    obj->type = PYDT_LIST;
    obj->refcount = 1;
    obj->v.list.items = (PyDosObj far * far *)0;
    obj->v.list.len = 0;
    obj->v.list.cap = 0;
    pydos_gc_track(obj);
    return obj;
}

/*
 * Allocate a GC-tracked empty dict via the GC allocator.
 */
static PyDosObj far *make_gc_dict(void)
{
    PyDosObj far *obj;
    obj = pydos_gc_alloc();
    if (obj == (PyDosObj far *)0) return obj;
    obj->type = PYDT_DICT;
    obj->refcount = 1;
    obj->v.dict.entries = (PyDosDictEntry far *)0;
    obj->v.dict.size = 0;
    obj->v.dict.used = 0;
    pydos_gc_track(obj);
    return obj;
}

/* ------------------------------------------------------------------ */
/* Test 1: Bulk track and untrack                                       */
/*                                                                      */
/* Verify tracking list integrity when many objects are tracked          */
/* and then untracked in sequence.                                      */
/* ------------------------------------------------------------------ */

TEST(bulk_track_untrack)
{
    PyDosObj far *objs[GCS_BULK_N];
    unsigned int before;
    unsigned int after;
    int i;

    before = pydos_gc_tracked_count();

    for (i = 0; i < GCS_BULK_N; i++) {
        objs[i] = make_gc_list();
        ASSERT_NOT_NULL(objs[i]);
    }

    after = pydos_gc_tracked_count();
    ASSERT_EQ(after, before + GCS_BULK_N);

    /* Untrack and free all */
    for (i = 0; i < GCS_BULK_N; i++) {
        pydos_gc_untrack(objs[i]);
        pydos_far_free(GC_HDR(objs[i]));
    }

    after = pydos_gc_tracked_count();
    ASSERT_EQ(after, before);
}

/* ------------------------------------------------------------------ */
/* Test 2: Sweep unreachable objects                                    */
/*                                                                      */
/* Create objects with refcount=0 (simulating cycle-broken state)       */
/* and verify the sweep phase frees them all.                           */
/* ------------------------------------------------------------------ */

TEST(sweep_unreachable)
{
    unsigned int before;
    unsigned int freed;
    int i;

    before = pydos_gc_tracked_count();

    for (i = 0; i < GCS_SWEEP_N; i++) {
        PyDosObj far *obj;
        obj = make_gc_list();
        ASSERT_NOT_NULL(obj);
        /*
         * Set refcount to 0 to simulate an object that is only
         * kept alive by cyclic references (which a proper cycle
         * collector would have decremented to zero).  Phase 2b
         * of collect() will NOT mark it (refcount == 0), so the
         * sweep will free it.
         */
        obj->refcount = 0;
    }

    ASSERT_EQ(pydos_gc_tracked_count(), before + GCS_SWEEP_N);

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= (unsigned int)GCS_SWEEP_N);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 3: Rooted objects survive, unrooted objects die                  */
/*                                                                      */
/* Mix of rooted (reachable) and unrooted (refcount=0) objects.         */
/* After collection, only rooted objects should remain.                 */
/* ------------------------------------------------------------------ */

TEST(rooted_survive)
{
    PyDosObj far *alive[GCS_ROOTED_N];
    PyDosObj far * far *roots[GCS_ROOTED_N];
    unsigned int before;
    unsigned int freed;
    int i;

    before = pydos_gc_tracked_count();

    /* Create rooted objects */
    for (i = 0; i < GCS_ROOTED_N; i++) {
        alive[i] = make_gc_list();
        ASSERT_NOT_NULL(alive[i]);
        roots[i] = &alive[i];
        pydos_gc_add_root(roots[i]);
    }

    /* Create unrooted objects with refcount=0 */
    for (i = 0; i < GCS_DEAD_N; i++) {
        PyDosObj far *obj;
        obj = make_gc_list();
        ASSERT_NOT_NULL(obj);
        obj->refcount = 0;
    }

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= (unsigned int)GCS_DEAD_N);

    /* All rooted objects must survive and be marked */
    for (i = 0; i < GCS_ROOTED_N; i++) {
        ASSERT_NOT_NULL(alive[i]);
        ASSERT_TRUE(alive[i]->flags & OBJ_FLAG_MARKED);
    }

    /* Only the rooted objects should remain tracked */
    ASSERT_EQ(pydos_gc_tracked_count(), before + GCS_ROOTED_N);

    /* Cleanup */
    for (i = 0; i < GCS_ROOTED_N; i++) {
        pydos_gc_remove_root(roots[i]);
        pydos_gc_untrack(alive[i]);
        pydos_far_free(GC_HDR(alive[i]));
    }
}

/* ------------------------------------------------------------------ */
/* Test 4: Repeated collection cycles                                   */
/*                                                                      */
/* Run many rounds of allocate-and-collect to verify no crashes,        */
/* no memory corruption, and tracked count returns to baseline.         */
/* ------------------------------------------------------------------ */

TEST(repeated_collect)
{
    int round;
    int i;
    unsigned int before;
    unsigned int after;

    before = pydos_gc_tracked_count();

    for (round = 0; round < GCS_REPEAT_ROUNDS; round++) {
        for (i = 0; i < GCS_OBJS_PER_ROUND; i++) {
            PyDosObj far *obj;
            obj = make_gc_list();
            ASSERT_NOT_NULL(obj);
            obj->refcount = 0;
        }
        pydos_gc_collect();
    }

    after = pydos_gc_tracked_count();
    ASSERT_EQ(after, before);
}

/* ------------------------------------------------------------------ */
/* Test 5: Mixed container types (lists + dicts)                        */
/*                                                                      */
/* Verify the sweep handles different container types correctly.        */
/* ------------------------------------------------------------------ */

TEST(mixed_types)
{
    unsigned int before;
    unsigned int freed;
    int i;

    before = pydos_gc_tracked_count();

    for (i = 0; i < GCS_MIX_N; i++) {
        PyDosObj far *obj;
        if (i & 1) {
            obj = make_gc_dict();
        } else {
            obj = make_gc_list();
        }
        ASSERT_NOT_NULL(obj);
        obj->refcount = 0;
    }

    ASSERT_EQ(pydos_gc_tracked_count(), before + GCS_MIX_N);

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= (unsigned int)GCS_MIX_N);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 6: Auto-collect threshold trigger                               */
/*                                                                      */
/* pydos_gc_alloc calls pydos_gc_maybe_collect which triggers a         */
/* collection every GC_THRESHOLD (500) allocations.  Allocate enough    */
/* objects to force at least one automatic collection.                  */
/* ------------------------------------------------------------------ */

TEST(threshold_trigger)
{
    unsigned long before_collections;
    unsigned long after_collections;
    int i;

    before_collections = pydos_gc_collections();

    /*
     * Each pydos_gc_alloc increments the alloc counter and may
     * trigger a collection at GC_THRESHOLD.  Allocate past the
     * threshold to guarantee at least one auto-collection fires.
     */
    for (i = 0; i < (int)GC_THRESHOLD + 50; i++) {
        PyDosObj far *obj;
        obj = pydos_gc_alloc();
        ASSERT_NOT_NULL(obj);
        obj->type = PYDT_LIST;
        obj->refcount = 0;
        obj->v.list.items = (PyDosObj far * far *)0;
        obj->v.list.len = 0;
        obj->v.list.cap = 0;
        pydos_gc_track(obj);
    }

    after_collections = pydos_gc_collections();
    ASSERT_TRUE(after_collections > before_collections);

    /* Final collect to sweep remaining objects */
    pydos_gc_collect();
}

/* ------------------------------------------------------------------ */
/* Test 7: Self-referencing cycle (collected by trial deletion)          */
/*                                                                      */
/* A list that references itself has refcount=1 from the self-ref.      */
/* Trial deletion subtracts the internal self-reference, leaving        */
/* gc_refs=0 — the object is correctly identified as unreachable        */
/* and collected.                                                       */
/* ------------------------------------------------------------------ */

TEST(self_ref_cycle)
{
    PyDosObj far *list;
    unsigned int before;
    unsigned int freed;

    before = pydos_gc_tracked_count();

    /* Create a list that contains a reference to itself */
    list = make_gc_list();
    ASSERT_NOT_NULL(list);

    list->v.list.items = (PyDosObj far * far *)
        pydos_far_alloc(sizeof(PyDosObj far *));
    ASSERT_NOT_NULL(list->v.list.items);
    list->v.list.items[0] = list;   /* self-reference */
    list->v.list.len = 1;
    list->v.list.cap = 1;
    list->refcount = 2;  /* 1 external (our variable) + 1 self-ref */

    /* "Drop" the external reference, leaving only the self-ref */
    list->refcount = 1;

    /* Collect: trial deletion detects the cycle and frees it */
    freed = pydos_gc_collect();

    /* Cycle collected — object freed by GC */
    ASSERT_TRUE(freed >= 1);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 8: Mutual reference cycle (A -> B -> A)                         */
/*                                                                      */
/* Two lists reference each other.  Trial deletion subtracts both       */
/* internal references, leaving gc_refs=0 for both — correctly          */
/* identified as unreachable and collected.                              */
/* ------------------------------------------------------------------ */

TEST(mutual_ref_cycle)
{
    PyDosObj far *a;
    PyDosObj far *b;
    unsigned int before;
    unsigned int freed;

    before = pydos_gc_tracked_count();

    a = make_gc_list();
    ASSERT_NOT_NULL(a);
    b = make_gc_list();
    ASSERT_NOT_NULL(b);

    /* a contains b */
    a->v.list.items = (PyDosObj far * far *)
        pydos_far_alloc(sizeof(PyDosObj far *));
    ASSERT_NOT_NULL(a->v.list.items);
    a->v.list.items[0] = b;
    a->v.list.len = 1;
    a->v.list.cap = 1;

    /* b contains a */
    b->v.list.items = (PyDosObj far * far *)
        pydos_far_alloc(sizeof(PyDosObj far *));
    ASSERT_NOT_NULL(b->v.list.items);
    b->v.list.items[0] = a;
    b->v.list.len = 1;
    b->v.list.cap = 1;

    /* Each has refcount 2: 1 external + 1 from the other */
    a->refcount = 2;
    b->refcount = 2;

    /* "Drop" external references */
    a->refcount = 1;
    b->refcount = 1;

    /* Collect: trial deletion detects the cycle and frees both */
    freed = pydos_gc_collect();

    /* Both collected — cycle detected */
    ASSERT_TRUE(freed >= 2);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 9: Cycle with external reference survives                       */
/*                                                                      */
/* A→B→A cycle where A is also referenced by a root.  Trial deletion    */
/* subtracts internal refs but A retains gc_refs > 0 from the root,     */
/* so both objects survive.                                              */
/* ------------------------------------------------------------------ */

TEST(cycle_with_root_survives)
{
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far * far *root_ptr;
    unsigned int before;
    unsigned int freed;

    before = pydos_gc_tracked_count();

    a = make_gc_list();
    ASSERT_NOT_NULL(a);
    b = make_gc_list();
    ASSERT_NOT_NULL(b);

    /* a contains b */
    a->v.list.items = (PyDosObj far * far *)
        pydos_far_alloc(sizeof(PyDosObj far *));
    ASSERT_NOT_NULL(a->v.list.items);
    a->v.list.items[0] = b;
    a->v.list.len = 1;
    a->v.list.cap = 1;

    /* b contains a */
    b->v.list.items = (PyDosObj far * far *)
        pydos_far_alloc(sizeof(PyDosObj far *));
    ASSERT_NOT_NULL(b->v.list.items);
    b->v.list.items[0] = a;
    b->v.list.len = 1;
    b->v.list.cap = 1;

    /* a has 3 refs: 1 root + 1 from b + 1 external (local var) */
    a->refcount = 3;
    /* b has 2 refs: 1 from a + 1 external (local var) */
    b->refcount = 2;

    /* Register a as a GC root */
    root_ptr = &a;
    pydos_gc_add_root(root_ptr);

    /* "Drop" external local-var references */
    a->refcount = 2;  /* 1 root + 1 from b */
    b->refcount = 1;  /* 1 from a */

    freed = pydos_gc_collect();

    /* Both survive: a is rooted, b is reachable from a */
    ASSERT_EQ(pydos_gc_tracked_count(), before + 2);
    ASSERT_TRUE(a->flags & OBJ_FLAG_MARKED);
    ASSERT_TRUE(b->flags & OBJ_FLAG_MARKED);

    /* Cleanup */
    pydos_gc_remove_root(root_ptr);

    a->v.list.items[0] = (PyDosObj far *)0;
    a->v.list.len = 0;
    pydos_far_free(a->v.list.items);
    a->v.list.items = (PyDosObj far * far *)0;

    b->v.list.items[0] = (PyDosObj far *)0;
    b->v.list.len = 0;
    pydos_far_free(b->v.list.items);
    b->v.list.items = (PyDosObj far * far *)0;

    pydos_gc_untrack(a);
    pydos_gc_untrack(b);
    pydos_far_free(GC_HDR(a));
    pydos_far_free(GC_HDR(b));
}

/* ------------------------------------------------------------------ */
/* Test 10: Memory stability after heavy GC pressure                   */
/*                                                                      */
/* After many rounds of alloc + collect, current_allocs should           */
/* return close to the starting point (no persistent leaks).            */
/* ------------------------------------------------------------------ */

TEST(memory_stable)
{
    unsigned long allocs_before;
    unsigned long allocs_after;
    int round;
    int i;

    allocs_before = pydos_mem_current_allocs();

    for (round = 0; round < GCS_PRESSURE_ROUNDS; round++) {
        for (i = 0; i < GCS_PRESSURE_OBJS; i++) {
            PyDosObj far *obj;
            obj = make_gc_list();
            ASSERT_NOT_NULL(obj);
            obj->refcount = 0;
        }
        pydos_gc_collect();
    }

    allocs_after = pydos_mem_current_allocs();

    /*
     * After creating and collecting many objects, current allocations
     * should return close to the starting point.  Allow a small delta
     * for runtime bookkeeping that persists across collections.
     */
    ASSERT_TRUE(allocs_after <= allocs_before + 5);
}

/* ------------------------------------------------------------------ */
/* Test 11: Double-track prevention                                     */
/*                                                                      */
/* Calling pydos_gc_track twice on the same object must not corrupt     */
/* the tracking list or double-count it.                                */
/* ------------------------------------------------------------------ */

TEST(double_track)
{
    PyDosObj far *obj;
    unsigned int before;

    before = pydos_gc_tracked_count();

    obj = make_gc_list();
    ASSERT_NOT_NULL(obj);

    ASSERT_EQ(pydos_gc_tracked_count(), before + 1);

    /* Track again — should be a no-op */
    pydos_gc_track(obj);
    ASSERT_EQ(pydos_gc_tracked_count(), before + 1);

    /* Untrack once should fully remove it */
    pydos_gc_untrack(obj);
    ASSERT_EQ(pydos_gc_tracked_count(), before);

    pydos_far_free(GC_HDR(obj));
}

/* ------------------------------------------------------------------ */
/* Public runner                                                        */
/* ------------------------------------------------------------------ */

void run_gcs_tests(void)
{
    SUITE("pdos_gc_stress");

    RUN(bulk_track_untrack);
    RUN(sweep_unreachable);
    RUN(rooted_survive);
    RUN(repeated_collect);
    RUN(mixed_types);
    RUN(threshold_trigger);
    RUN(self_ref_cycle);
    RUN(mutual_ref_cycle);
    RUN(cycle_with_root_survives);
    RUN(memory_stable);
    RUN(double_track);
}
