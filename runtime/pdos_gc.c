/*
 * pydos_gc.c - Mark-and-sweep garbage collector for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * The GC tracks container objects (lists, dicts, tuples, instances,
 * generators, classes) in a doubly-linked list using a GCHeader
 * stored immediately before the PyDosObj in memory.
 *
 * Collection algorithm:
 *   1. Clear per-collection traversal state
 *   2. Seed and iteratively propagate explicit roots
 *   3. Use trial deletion to find externally referenced components
 *   4. Release unreachable cycles in three protected passes
 */

#include "pdos_gc.h"
#include "pdos_mem.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* Module state                                                        */
/* ------------------------------------------------------------------ */

/* Sentinel node for the doubly-linked tracking list.
   gc_next/gc_prev point to the first/last tracked header. */
static GCHeader gc_sentinel;

/* Root table: array of pointers-to-far-pointers */
static PyDosObj far * far *gc_roots[GC_MAX_ROOTS];
static unsigned int gc_root_count = 0;

/* Allocation counter for auto-collection threshold */
static unsigned int gc_alloc_counter = 0;
static unsigned int gc_threshold0 = GC_THRESHOLD;
static unsigned int gc_threshold1 = 10;
static unsigned int gc_threshold2 = 10;
static int gc_enabled = 1;

/* Statistics */
static unsigned int gc_tracked = 0;
static unsigned long gc_num_collections = 0UL;
static unsigned long gc_num_collected = 0UL;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* Return nonzero if the object type may own Python references and can
   therefore participate in a reference cycle. */
int PYDOS_API pydos_gc_is_tracked_type(unsigned int type)
{
    switch ((PyDosType)type) {
    case PYDT_LIST:
    case PYDT_DICT:
    case PYDT_TUPLE:
    case PYDT_SET:
    case PYDT_INSTANCE:
    case PYDT_FUNCTION:
    case PYDT_CELL:
    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
    case PYDT_EXCEPTION:
    case PYDT_TRACEBACK:
    case PYDT_CLASS:
    case PYDT_EXC_GROUP:
    case PYDT_FROZENSET:
    case PYDT_TYPE_PARAM:
    case PYDT_TYPE_ALIAS:
    case PYDT_GENERIC_ALIAS:
    case PYDT_CODE:
    case PYDT_SUPER:
    case PYDT_MEMORYVIEW:
        return 1;
    default:
        return 0;
    }
}

#define GC_VISIT_MARK       1
#define GC_VISIT_SUBTRACT   2

/* Marking is deliberately iterative.  OBJ_FLAG_GC_SCANNED records which
 * marked objects have already had their children visited.  This avoids one
 * C stack frame per Python nesting level on 8086 and does not allocate while
 * handling an out-of-memory collection. */
static void gc_visit_child(PyDosObj far *child, int action)
{
    GCHeader far *child_hdr;

    if (child == (PyDosObj far *)0) return;
    if (!(child->flags & OBJ_FLAG_GC_TRACKED)) return;

    if (action == GC_VISIT_MARK) {
        child->flags |= OBJ_FLAG_MARKED;
        return;
    }

    child_hdr = GC_HDR(child);
    if (child_hdr->gc_refs > 0) child_hdr->gc_refs--;
}

/* Single authoritative description of every strong PyDosObj edge. */
static void gc_visit_children(PyDosObj far *obj, int action)
{
    unsigned int i;

    switch ((PyDosType)obj->type) {
    case PYDT_LIST:
        if (obj->v.list.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.list.len; i++) {
                gc_visit_child(obj->v.list.items[i], action);
            }
        }
        break;

    case PYDT_DICT:
    case PYDT_SET:
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    gc_visit_child(obj->v.dict.entries[i].key, action);
                    gc_visit_child(obj->v.dict.entries[i].value, action);
                }
            }
        }
        break;

    case PYDT_TUPLE:
        if (obj->v.tuple.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.tuple.len; i++) {
                gc_visit_child(obj->v.tuple.items[i], action);
            }
        }
        break;

    case PYDT_INSTANCE:
        gc_visit_child(obj->v.instance.attrs, action);
        gc_visit_child(obj->v.instance.cls, action);
        gc_visit_child(obj->v.instance.native_storage, action);
        break;

    case PYDT_FUNCTION:
        gc_visit_child(obj->v.func.defaults, action);
        gc_visit_child(obj->v.func.closure, action);
        gc_visit_child(obj->v.func.bound_self, action);
        gc_visit_child(obj->v.func.attrs, action);
        gc_visit_child(obj->v.func.code_obj, action);
        break;

    case PYDT_CELL:
        gc_visit_child(obj->v.cell.value, action);
        break;

    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
        gc_visit_child(obj->v.gen.state, action);
        gc_visit_child(obj->v.gen.locals, action);
        gc_visit_child(obj->v.gen.vm_stack, action);
        gc_visit_child(obj->v.gen.vm_closure, action);
        break;

    case PYDT_EXCEPTION:
        gc_visit_child(obj->v.exc.message, action);
        gc_visit_child(obj->v.exc.value, action);
        gc_visit_child(obj->v.exc.traceback, action);
        gc_visit_child(obj->v.exc.cause, action);
        break;

    case PYDT_TRACEBACK:
        gc_visit_child(obj->v.traceback.next, action);
        break;

    case PYDT_CLASS:
        if (obj->v.cls.bases != (PyDosObj far * far *)0) {
            for (i = 0; i < (unsigned int)obj->v.cls.num_bases; i++) {
                gc_visit_child(obj->v.cls.bases[i], action);
            }
        }
        gc_visit_child(obj->v.cls.class_attrs, action);
        gc_visit_child(obj->v.cls.metaclass, action);
        break;

    case PYDT_EXC_GROUP:
        gc_visit_child(obj->v.excgroup.message, action);
        if (obj->v.excgroup.exceptions != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.excgroup.count; i++) {
                gc_visit_child(obj->v.excgroup.exceptions[i], action);
            }
        }
        break;

    case PYDT_FROZENSET:
        if (obj->v.frozenset.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.frozenset.len; i++) {
                gc_visit_child(obj->v.frozenset.items[i], action);
            }
        }
        break;

    case PYDT_TYPE_PARAM:
        gc_visit_child(obj->v.type_param.name, action);
        gc_visit_child(obj->v.type_param.bound, action);
        gc_visit_child(obj->v.type_param.constraints, action);
        gc_visit_child(obj->v.type_param.bound_thunk, action);
        break;

    case PYDT_TYPE_ALIAS:
        gc_visit_child(obj->v.type_alias.name, action);
        gc_visit_child(obj->v.type_alias.type_params, action);
        gc_visit_child(obj->v.type_alias.value, action);
        break;

    case PYDT_GENERIC_ALIAS:
        gc_visit_child(obj->v.generic_alias.origin, action);
        gc_visit_child(obj->v.generic_alias.args, action);
        break;

    case PYDT_CODE:
        gc_visit_child(obj->v.code.freevars, action);
        gc_visit_child(obj->v.code.consts, action);
        break;

    case PYDT_SUPER:
        gc_visit_child(obj->v.super_obj.start_type, action);
        gc_visit_child(obj->v.super_obj.bound_obj, action);
        break;

    case PYDT_MEMORYVIEW:
        gc_visit_child(obj->v.memoryview.source, action);
        gc_visit_child(obj->v.memoryview.exporter, action);
        break;

    default:
        break;
    }
}

static void gc_mark_root(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return;
    if (obj->flags & OBJ_FLAG_GC_TRACKED) {
        obj->flags |= OBJ_FLAG_MARKED;
    } else if (pydos_gc_is_tracked_type((unsigned int)obj->type)) {
        /* Compatibility for a borrowed root created by legacy code.  Under
         * the runtime invariant every child-owning object is tracked, so one
         * visit is enough to seed the tracked graph without marking the
         * untracked object itself. */
        gc_visit_children(obj, GC_VISIT_MARK);
    }
}

static void gc_propagate_marks(void)
{
    GCHeader far *hdr;
    int changed;

    do {
        changed = 0;
        hdr = gc_sentinel.gc_next;
        while (hdr != &gc_sentinel) {
            PyDosObj far *obj;
            obj = GC_OBJ(hdr);
            if ((obj->flags & OBJ_FLAG_MARKED) &&
                !(obj->flags & OBJ_FLAG_GC_SCANNED)) {
                obj->flags |= OBJ_FLAG_GC_SCANNED;
                gc_visit_children(obj, GC_VISIT_MARK);
                changed = 1;
            }
            hdr = hdr->gc_next;
        }
    } while (changed);
}

/*
 * gc_unlink — remove a GCHeader from the doubly-linked list.
 */
static void gc_unlink(GCHeader far *hdr)
{
    hdr->gc_prev->gc_next = hdr->gc_next;
    hdr->gc_next->gc_prev = hdr->gc_prev;
    hdr->gc_next = (GCHeader far *)0;
    hdr->gc_prev = (GCHeader far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_gc_init                                                       */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gc_init(void)
{
    /* Initialize sentinel as empty circular list */
    gc_sentinel.gc_next = &gc_sentinel;
    gc_sentinel.gc_prev = &gc_sentinel;

    gc_root_count = 0;
    gc_alloc_counter = 0;
    gc_threshold0 = GC_THRESHOLD;
    gc_threshold1 = 10;
    gc_threshold2 = 10;
    gc_enabled = 1;
    gc_tracked = 0;
    gc_num_collections = 0UL;
    gc_num_collected = 0UL;
}

/* ------------------------------------------------------------------ */
/* pydos_gc_shutdown - free all remaining tracked objects              */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gc_shutdown(void)
{
    GCHeader far *hdr;
    GCHeader far *next;

    /* Protect every remaining tracked object before releasing children.
     * Shutdown is unconditional: owners should already have released their
     * references, but a damaged or partially initialized runtime must not
     * leave linked GC blocks behind. */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        GC_OBJ(hdr)->refcount = REFCOUNT_MAX;
        hdr = hdr->gc_next;
    }

    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        pydos_obj_release_data(GC_OBJ(hdr));
        hdr = hdr->gc_next;
    }

    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        next = hdr->gc_next;
        gc_unlink(hdr);
        pydos_far_free(hdr);
        hdr = next;
    }

    gc_sentinel.gc_next = &gc_sentinel;
    gc_sentinel.gc_prev = &gc_sentinel;
    gc_tracked = 0;
    gc_root_count = 0;
}

/* ------------------------------------------------------------------ */
/* gc_link - add a freshly allocated object to the tracking list       */
/* ------------------------------------------------------------------ */
static void gc_link(PyDosObj far *obj)
{
    GCHeader far *hdr;

    hdr = GC_HDR(obj);

    /* Insert at the head of the tracking list (after sentinel) */
    hdr->gc_next = gc_sentinel.gc_next;
    hdr->gc_prev = &gc_sentinel;
    gc_sentinel.gc_next->gc_prev = hdr;
    gc_sentinel.gc_next = hdr;

    obj->flags |= OBJ_FLAG_GC_TRACKED;
    gc_tracked++;
}

/* ------------------------------------------------------------------ */
/* pydos_gc_untrack — remove from tracking list                        */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gc_untrack(PyDosObj far *obj)
{
    GCHeader far *hdr;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    if (!(obj->flags & OBJ_FLAG_GC_TRACKED)) {
        return;
    }

    hdr = GC_HDR(obj);
    gc_unlink(hdr);

    obj->flags &= (unsigned char)~OBJ_FLAG_GC_TRACKED;
    if (gc_tracked > 0) {
        gc_tracked--;
    }
}

void PYDOS_API pydos_gc_free_object(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return;
    pydos_far_free(GC_HDR(obj));
}

/* ------------------------------------------------------------------ */
/* pydos_gc_collect — full mark-and-sweep cycle                        */
/* ------------------------------------------------------------------ */
unsigned int PYDOS_API pydos_gc_collect(void)
{
    GCHeader far *hdr;
    GCHeader far *next;
    unsigned int freed;
    unsigned int i;

    gc_num_collections++;

    /* Phase 1: Clear per-collection traversal state. */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        obj->flags &= (unsigned char)~(OBJ_FLAG_MARKED |
                                       OBJ_FLAG_GC_SCANNED);
        hdr = hdr->gc_next;
    }

    /* Phase 2a: Mark from explicit GC roots */
    for (i = 0; i < gc_root_count; i++) {
        if (gc_roots[i] != (PyDosObj far * far *)0) {
            PyDosObj far *root_obj;
            root_obj = *gc_roots[i];
            gc_mark_root(root_obj);
        }
    }
    gc_propagate_marks();

    /*
     * Phase 2b: Trial deletion — detect reference cycles.
     *
     * CPython-style algorithm:
     * 1. Copy refcount to gc_refs for all unmarked tracked objects
     * 2. Subtract internal references (among unmarked tracked objects)
     * 3. Objects with gc_refs > 0 have external references → mark them
     *
     * This correctly identifies cycles: in a pure cycle A→B→A with
     * no external refs, both gc_refs reach 0 after subtracting the
     * internal A→B and B→A references → both get swept.
     */

    /* Step 1: Copy refcount to gc_refs */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        if (!(obj->flags & OBJ_FLAG_MARKED)) {
            hdr->gc_refs = obj->refcount;
        } else {
            hdr->gc_refs = 0;
        }
        hdr = hdr->gc_next;
    }

    /* Step 2: Subtract internal references among unmarked objects */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        if (!(obj->flags & OBJ_FLAG_MARKED)) {
            gc_visit_children(obj, GC_VISIT_SUBTRACT);
        }
        hdr = hdr->gc_next;
    }

    /* Step 3: Objects with gc_refs > 0 have external references */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        if (!(obj->flags & OBJ_FLAG_MARKED) && hdr->gc_refs > 0) {
            obj->flags |= OBJ_FLAG_MARKED;
        }
        hdr = hdr->gc_next;
    }
    gc_propagate_marks();

    /*
     * Phase 3: Sweep — free unmarked tracked objects.
     *
     * Three-pass sweep to safely handle cycles:
     *   Pass 1: Set REFCOUNT_MAX on unmarked objects — prevents
     *           DECREF from triggering pydos_obj_free during
     *           release_data (cycle members stay alive until Pass 3).
     *   Pass 2: Release internal data (frees buffers, DECREFs
     *           children safely — cycle members have REFCOUNT_MAX).
     *   Pass 3: Unlink from tracking list, free GCHeader+PyDosObj.
     */

    /* Pass 1: Protect cycle members from DECREF cascade */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        if (!(obj->flags & OBJ_FLAG_MARKED)) {
            obj->refcount = REFCOUNT_MAX;
        }
        hdr = hdr->gc_next;
    }

    /* Pass 2: Release internal data */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        if (!(obj->flags & OBJ_FLAG_MARKED)) {
            pydos_obj_release_data(obj);
        }
        hdr = hdr->gc_next;
    }

    /* Pass 3: Unlink and free */
    freed = 0;
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        next = hdr->gc_next;
        {
            PyDosObj far *obj;
            obj = GC_OBJ(hdr);

            if (!(obj->flags & OBJ_FLAG_MARKED)) {
                gc_unlink(hdr);
                obj->flags &= (unsigned char)~OBJ_FLAG_GC_TRACKED;
                if (gc_tracked > 0) {
                    gc_tracked--;
                }

                pydos_far_free(hdr);
                freed++;
            }
        }
        hdr = next;
    }

    /* Reset allocation counter */
    gc_alloc_counter = 0;
    gc_num_collected += (unsigned long)freed;

    return freed;
}

/* ------------------------------------------------------------------ */
/* pydos_gc_add_root / pydos_gc_remove_root                            */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_gc_add_root(PyDosObj far * far *root)
{
    if (gc_root_count >= GC_MAX_ROOTS) {
        return -1;  /* root table full */
    }
    gc_roots[gc_root_count] = root;
    gc_root_count++;
    return 0;
}

void PYDOS_API pydos_gc_remove_root(PyDosObj far * far *root)
{
    unsigned int i;
    unsigned int j;

    for (i = 0; i < gc_root_count; i++) {
        if (gc_roots[i] == root) {
            /* Shift remaining entries down */
            for (j = i; j + 1 < gc_root_count; j++) {
                gc_roots[j] = gc_roots[j + 1];
            }
            gc_root_count--;
            return;
        }
    }
}

/* ------------------------------------------------------------------ */
/* pydos_gc_maybe_collect — check threshold, possibly collect          */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gc_maybe_collect(void)
{
    if (gc_alloc_counter < 0xFFFFU) gc_alloc_counter++;
    if (gc_enabled && gc_threshold0 > 0 &&
        gc_alloc_counter >= gc_threshold0) {
        pydos_gc_collect();
    }
}

/* ------------------------------------------------------------------ */
/* pydos_gc_alloc_type - allocate and track a typed GC object          */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_gc_alloc_type(unsigned int type)
{
    unsigned long total;
    char far *block;
    GCHeader far *hdr;
    PyDosObj far *obj;

    if (!pydos_gc_is_tracked_type(type)) {
        return (PyDosObj far *)0;
    }

    /* Maybe trigger collection before allocating. */
    pydos_gc_maybe_collect();

    total = (unsigned long)sizeof(GCHeader) + (unsigned long)sizeof(PyDosObj);
    block = (char far *)pydos_mem_alloc(PYDOS_MEM_OBJECT, total);
    if (block == (char far *)0) {
        /* Try a collection and retry once */
        pydos_gc_collect();
        block = (char far *)pydos_mem_alloc(PYDOS_MEM_OBJECT, total);
        if (block == (char far *)0) {
            return (PyDosObj far *)0;
        }
    }

    _fmemset(block, 0, (unsigned int)total);

    hdr = (GCHeader far *)block;
    hdr->gc_next = (GCHeader far *)0;
    hdr->gc_prev = (GCHeader far *)0;

    obj = GC_OBJ(hdr);
    obj->type = (unsigned char)type;
    obj->refcount = 1;
    gc_link(obj);

    return obj;
}

/* ------------------------------------------------------------------ */
/* Statistics                                                           */
/* ------------------------------------------------------------------ */

unsigned int PYDOS_API pydos_gc_tracked_count(void)
{
    return gc_tracked;
}

unsigned long PYDOS_API pydos_gc_collections(void)
{
    return gc_num_collections;
}

unsigned long PYDOS_API pydos_gc_collected(void)
{
    return gc_num_collected;
}

unsigned int PYDOS_API pydos_gc_allocation_count(void)
{
    return gc_alloc_counter;
}

void PYDOS_API pydos_gc_enable(void)
{
    gc_enabled = 1;
}

void PYDOS_API pydos_gc_disable(void)
{
    gc_enabled = 0;
}

int PYDOS_API pydos_gc_is_enabled(void)
{
    return gc_enabled;
}

void PYDOS_API pydos_gc_set_thresholds(unsigned int threshold0,
                                        unsigned int threshold1,
                                        unsigned int threshold2)
{
    gc_threshold0 = threshold0;
    gc_threshold1 = threshold1;
    gc_threshold2 = threshold2;
}

void PYDOS_API pydos_gc_get_thresholds(unsigned int *threshold0,
                                        unsigned int *threshold1,
                                        unsigned int *threshold2)
{
    if (threshold0 != (unsigned int *)0) *threshold0 = gc_threshold0;
    if (threshold1 != (unsigned int *)0) *threshold1 = gc_threshold1;
    if (threshold2 != (unsigned int *)0) *threshold2 = gc_threshold2;
}

/* ------------------------------------------------------------------ */
/* Python-module bridges                                               */
/* ------------------------------------------------------------------ */

PyDosObj far * PYDOS_API pydos_gc_collect_builtin(
                                      int argc, PyDosObj far * far *argv)
{
    unsigned int freed;
    (void)argc;
    (void)argv;
    freed = pydos_gc_collect();
    return pydos_obj_new_int((long)freed);
}

PyDosObj far * PYDOS_API pydos_gc_is_tracked_builtin(
                                      int argc, PyDosObj far * far *argv)
{
    int tracked;
    tracked = argc >= 1 && argv != (PyDosObj far * far *)0 &&
              argv[0] != (PyDosObj far *)0 &&
              (argv[0]->flags & OBJ_FLAG_GC_TRACKED) != 0;
    return pydos_obj_new_bool(tracked);
}

PyDosObj far * PYDOS_API pydos_gc_control_builtin(
                                      int argc, PyDosObj far * far *argv)
{
    long operation;
    unsigned int threshold0;
    unsigned int threshold1;
    unsigned int threshold2;

    if (argc < 1 || argv == (PyDosObj far * far *)0 ||
        argv[0] == (PyDosObj far *)0 || argv[0]->type != PYDT_INT) {
        return pydos_obj_new_none();
    }

    operation = argv[0]->v.int_val;
    switch ((int)operation) {
    case 0:
        pydos_gc_enable();
        return pydos_obj_new_none();
    case 1:
        pydos_gc_disable();
        return pydos_obj_new_none();
    case 2:
        return pydos_obj_new_bool(pydos_gc_is_enabled());
    case 3:
        return pydos_obj_new_int((long)pydos_gc_allocation_count());
    case 4:
        return pydos_obj_new_int((long)pydos_gc_collections());
    case 5:
        return pydos_obj_new_int((long)pydos_gc_collected());
    case 6:
        return pydos_obj_new_int((long)pydos_gc_tracked_count());
    case 7:
    case 9:
    case 10:
        pydos_gc_get_thresholds(&threshold0, &threshold1, &threshold2);
        if (operation == 7) return pydos_obj_new_int((long)threshold0);
        if (operation == 9) return pydos_obj_new_int((long)threshold1);
        return pydos_obj_new_int((long)threshold2);
    case 8:
        if (argc >= 4 && argv[1] != (PyDosObj far *)0 &&
            argv[2] != (PyDosObj far *)0 && argv[3] != (PyDosObj far *)0 &&
            argv[1]->type == PYDT_INT && argv[2]->type == PYDT_INT &&
            argv[3]->type == PYDT_INT) {
            pydos_gc_set_thresholds((unsigned int)argv[1]->v.int_val,
                                    (unsigned int)argv[2]->v.int_val,
                                    (unsigned int)argv[3]->v.int_val);
        }
        return pydos_obj_new_none();
    default:
        return pydos_obj_new_none();
    }
}
