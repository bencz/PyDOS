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
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_dic.h"
#include "../runtime/pdos_cll.h"

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
#define GCS_DEEP_N             192

/* ------------------------------------------------------------------ */
/* Helpers                                                              */
/* ------------------------------------------------------------------ */

/*
 * Allocate a normal list through its public constructor.  Typed allocation
 * makes the constructor responsible for attaching the GC header.
 */
static PyDosObj far *make_gc_list(void)
{
    return pydos_list_new(0);
}

/*
 * Allocate a GC-tracked empty dict via the GC allocator.
 */
static PyDosObj far *make_gc_dict(void)
{
    return pydos_dict_new(4);
}

static void make_self_cycle(PyDosObj far *list)
{
    PyDosObj far *result;
    result = pydos_list_append(list, list);
    PYDOS_DECREF(result);
}

static void make_dict_self_cycle(PyDosObj far *dict, long key_value)
{
    PyDosObj far *key;
    key = pydos_obj_new_int(key_value);
    pydos_dict_set(dict, key, dict);
    PYDOS_DECREF(key);
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

    /* Normal DECREF must unlink and free the complete GC block. */
    for (i = 0; i < GCS_BULK_N; i++) {
        PYDOS_DECREF(objs[i]);
    }

    after = pydos_gc_tracked_count();
    ASSERT_EQ(after, before);
}

/* ------------------------------------------------------------------ */
/* Test 2: Sweep unreachable objects                                    */
/*                                                                      */
/* Create unreachable self-cycles through the public list API and       */
/* verify that trial deletion and sweep free them all.                   */
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
        make_self_cycle(obj);
        PYDOS_DECREF(obj);
    }

    ASSERT_EQ(pydos_gc_tracked_count(), before + GCS_SWEEP_N);

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= (unsigned int)GCS_SWEEP_N);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 3: Rooted objects survive, unrooted objects die                  */
/*                                                                      */
/* Mix rooted reachable objects with ownerless reference cycles.        */
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

    /* Create cycles through the public list API and drop their owners. */
    for (i = 0; i < GCS_DEAD_N; i++) {
        PyDosObj far *obj;
        obj = make_gc_list();
        ASSERT_NOT_NULL(obj);
        make_self_cycle(obj);
        PYDOS_DECREF(obj);
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
        PYDOS_DECREF(alive[i]);
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
            make_self_cycle(obj);
            PYDOS_DECREF(obj);
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
            ASSERT_NOT_NULL(obj);
            make_dict_self_cycle(obj, (long)i);
        } else {
            obj = make_gc_list();
            ASSERT_NOT_NULL(obj);
            make_self_cycle(obj);
        }
        PYDOS_DECREF(obj);
    }

    ASSERT_EQ(pydos_gc_tracked_count(), before + GCS_MIX_N);

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= (unsigned int)GCS_MIX_N);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 6: Auto-collect threshold trigger                               */
/*                                                                      */
/* Typed tracked allocation calls pydos_gc_maybe_collect, which triggers */
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
     * Each real tracked constructor increments the allocation counter.
     * Leave only a self-reference so automatic collection has useful work.
     */
    for (i = 0; i < (int)GC_THRESHOLD + 50; i++) {
        PyDosObj far *obj;
        obj = pydos_list_new(0);
        ASSERT_NOT_NULL(obj);
        make_self_cycle(obj);
        PYDOS_DECREF(obj);
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

    make_self_cycle(list);

    /* Drop the external reference, leaving only the self-reference. */
    PYDOS_DECREF(list);

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

    {
        PyDosObj far *result;
        result = pydos_list_append(a, b);
        PYDOS_DECREF(result);
        result = pydos_list_append(b, a);
        PYDOS_DECREF(result);
    }

    /* Drop both external references. */
    PYDOS_DECREF(a);
    PYDOS_DECREF(b);

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

    before = pydos_gc_tracked_count();

    a = make_gc_list();
    ASSERT_NOT_NULL(a);
    b = make_gc_list();
    ASSERT_NOT_NULL(b);

    {
        PyDosObj far *result;
        result = pydos_list_append(a, b);
        PYDOS_DECREF(result);
        result = pydos_list_append(b, a);
        PYDOS_DECREF(result);
    }

    /* Register a as a GC root */
    root_ptr = &a;
    pydos_gc_add_root(root_ptr);

    /* The root slot owns a's original reference.  Drop b's local owner. */
    PYDOS_DECREF(b);

    pydos_gc_collect();

    /* Both survive: a is rooted, b is reachable from a */
    ASSERT_EQ(pydos_gc_tracked_count(), before + 2);
    ASSERT_TRUE(a->flags & OBJ_FLAG_MARKED);
    ASSERT_TRUE(b->flags & OBJ_FLAG_MARKED);

    /* Cleanup */
    pydos_gc_remove_root(root_ptr);

    pydos_list_clear(a);
    PYDOS_DECREF(a);
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
            make_self_cycle(obj);
            PYDOS_DECREF(obj);
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
/* Test 11: Constructor tracking and normal destruction                 */
/* ------------------------------------------------------------------ */

TEST(constructor_tracks_once)
{
    PyDosObj far *obj;
    unsigned int before;

    before = pydos_gc_tracked_count();

    obj = make_gc_list();
    ASSERT_NOT_NULL(obj);

    ASSERT_EQ(pydos_gc_tracked_count(), before + 1);

    PYDOS_DECREF(obj);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 12: Cell/list closure-style cycle                               */
/* ------------------------------------------------------------------ */

TEST(cell_list_cycle)
{
    PyDosObj far *list;
    PyDosObj far *cell;
    PyDosObj far *result;
    unsigned int before;
    unsigned int freed;

    before = pydos_gc_tracked_count();
    list = pydos_list_new(1);
    cell = pydos_cell_new();
    ASSERT_NOT_NULL(list);
    ASSERT_NOT_NULL(cell);

    result = pydos_list_append(list, cell);
    PYDOS_DECREF(result);
    pydos_cell_set(cell, list);

    PYDOS_DECREF(list);
    PYDOS_DECREF(cell);

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= 2);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 13: A root gains a child after an earlier collection            */
/* ------------------------------------------------------------------ */

TEST(root_mutation_between_collections)
{
    PyDosObj far *root;
    PyDosObj far *child;
    PyDosObj far *key;
    PyDosObj far * far *root_ptr;
    unsigned int before;
    unsigned int freed;

    before = pydos_gc_tracked_count();
    root = pydos_dict_new(4);
    ASSERT_NOT_NULL(root);
    root_ptr = &root;
    ASSERT_EQ(pydos_gc_add_root(root_ptr), 0);

    child = pydos_list_new(1);
    ASSERT_NOT_NULL(child);
    make_self_cycle(child);
    key = pydos_obj_new_int(1L);
    pydos_dict_set(root, key, child);
    PYDOS_DECREF(key);
    PYDOS_DECREF(child);

    pydos_gc_collect();
    ASSERT_EQ(pydos_gc_tracked_count(), before + 2);

    child = pydos_list_new(1);
    ASSERT_NOT_NULL(child);
    make_self_cycle(child);
    key = pydos_obj_new_int(2L);
    pydos_dict_set(root, key, child);
    PYDOS_DECREF(key);
    PYDOS_DECREF(child);

    pydos_gc_collect();
    ASSERT_EQ(pydos_gc_tracked_count(), before + 3);

    pydos_gc_remove_root(root_ptr);
    PYDOS_DECREF(root);
    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= 2);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
}

/* ------------------------------------------------------------------ */
/* Test 14: Deep graph marking and sweep do not consume the C stack     */
/* ------------------------------------------------------------------ */

TEST(deep_cycle_uses_iterative_marking)
{
    PyDosObj far *root;
    PyDosObj far *current;
    PyDosObj far *child;
    PyDosObj far *result;
    PyDosObj far * far *root_ptr;
    unsigned int before;
    unsigned int freed;
    int i;

    before = pydos_gc_tracked_count();
    root = pydos_list_new(1);
    ASSERT_NOT_NULL(root);
    current = root;

    for (i = 1; i < GCS_DEEP_N; i++) {
        child = pydos_list_new(1);
        ASSERT_NOT_NULL(child);
        result = pydos_list_append(current, child);
        PYDOS_DECREF(result);
        PYDOS_DECREF(child);
        current = child;
    }

    result = pydos_list_append(current, root);
    PYDOS_DECREF(result);

    root_ptr = &root;
    ASSERT_EQ(pydos_gc_add_root(root_ptr), 0);
    pydos_gc_collect();
    ASSERT_EQ(pydos_gc_tracked_count(), before + GCS_DEEP_N);
    ASSERT_TRUE(current->flags & OBJ_FLAG_MARKED);

    pydos_gc_remove_root(root_ptr);
    PYDOS_DECREF(root);

    freed = pydos_gc_collect();
    ASSERT_TRUE(freed >= GCS_DEEP_N);
    ASSERT_EQ(pydos_gc_tracked_count(), before);
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
    RUN(constructor_tracks_once);
    RUN(cell_list_cycle);
    RUN(root_mutation_between_collections);
    RUN(deep_cycle_uses_iterative_marking);
}
