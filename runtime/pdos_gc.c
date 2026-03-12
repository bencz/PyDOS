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
 *   1. Clear OBJ_FLAG_MARKED on all tracked objects
 *   2. Mark: from each root, recursively mark reachable objects
 *   3. Sweep: walk tracking list, free any unmarked objects
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

/* Statistics */
static unsigned int gc_tracked = 0;
static unsigned long gc_num_collections = 0UL;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

/* Return nonzero if the object type is a container that may
   participate in reference cycles. */
static int is_container_type(unsigned char type)
{
    switch ((PyDosType)type) {
    case PYDT_LIST:
    case PYDT_DICT:
    case PYDT_TUPLE:
    case PYDT_SET:
    case PYDT_INSTANCE:
    case PYDT_FUNCTION:
    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
    case PYDT_EXCEPTION:
    case PYDT_CLASS:
    case PYDT_EXC_GROUP:
    case PYDT_FROZENSET:
        return 1;
    default:
        return 0;
    }
}

/*
 * gc_mark_obj — recursively mark an object and its children as reachable.
 *
 * Uses iterative deepening for list/tuple to avoid deep recursion
 * on 8086 with limited stack space, but still recurses for dict
 * entries and other sub-objects.  The practical depth is bounded by
 * nesting depth of Python data structures.
 */
static void gc_mark_obj(PyDosObj far *obj)
{
    unsigned int i;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    /* Already marked — stop recursion */
    if (obj->flags & OBJ_FLAG_MARKED) {
        return;
    }

    obj->flags |= OBJ_FLAG_MARKED;

    /* Recurse into children based on type */
    switch ((PyDosType)obj->type) {
    case PYDT_LIST:
        if (obj->v.list.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.list.len; i++) {
                gc_mark_obj(obj->v.list.items[i]);
            }
        }
        break;

    case PYDT_DICT:
    case PYDT_SET:
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    gc_mark_obj(obj->v.dict.entries[i].key);
                    gc_mark_obj(obj->v.dict.entries[i].value);
                }
            }
        }
        break;

    case PYDT_TUPLE:
        if (obj->v.tuple.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.tuple.len; i++) {
                gc_mark_obj(obj->v.tuple.items[i]);
            }
        }
        break;

    case PYDT_INSTANCE:
        gc_mark_obj(obj->v.instance.attrs);
        gc_mark_obj(obj->v.instance.cls);
        break;

    case PYDT_FUNCTION:
        gc_mark_obj(obj->v.func.defaults);
        gc_mark_obj(obj->v.func.closure);
        break;

    case PYDT_CELL:
        gc_mark_obj(obj->v.cell.value);
        break;

    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
        gc_mark_obj(obj->v.gen.state);
        gc_mark_obj(obj->v.gen.locals);
        break;

    case PYDT_EXCEPTION:
        gc_mark_obj(obj->v.exc.message);
        gc_mark_obj(obj->v.exc.traceback);
        gc_mark_obj(obj->v.exc.cause);
        break;

    case PYDT_CLASS:
        if (obj->v.cls.bases != (PyDosObj far * far *)0) {
            for (i = 0; i < (unsigned int)obj->v.cls.num_bases; i++) {
                gc_mark_obj(obj->v.cls.bases[i]);
            }
        }
        gc_mark_obj(obj->v.cls.class_attrs);
        break;

    case PYDT_EXC_GROUP:
        gc_mark_obj(obj->v.excgroup.message);
        if (obj->v.excgroup.exceptions != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.excgroup.count; i++) {
                gc_mark_obj(obj->v.excgroup.exceptions[i]);
            }
        }
        break;

    case PYDT_FROZENSET:
        if (obj->v.frozenset.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.frozenset.len; i++) {
                gc_mark_obj(obj->v.frozenset.items[i]);
            }
        }
        break;

    default:
        /* Scalar types have no children to mark */
        break;
    }
}

/*
 * gc_visit_decref_child — if child is GC-tracked, decrement its gc_refs.
 * Used during trial deletion to subtract internal references.
 */
static void gc_visit_decref_child(PyDosObj far *child)
{
    GCHeader far *child_hdr;

    if (child == (PyDosObj far *)0) return;
    if (!(child->flags & OBJ_FLAG_GC_TRACKED)) return;

    child_hdr = GC_HDR(child);
    if (child_hdr->gc_refs > 0) {
        child_hdr->gc_refs--;
    }
}

/*
 * gc_visit_decref — walk children of obj, decrement gc_refs on each
 * tracked child.  Mirrors gc_mark_obj's traversal structure.
 */
static void gc_visit_decref(PyDosObj far *obj)
{
    unsigned int i;

    switch ((PyDosType)obj->type) {
    case PYDT_LIST:
        if (obj->v.list.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.list.len; i++) {
                gc_visit_decref_child(obj->v.list.items[i]);
            }
        }
        break;

    case PYDT_DICT:
    case PYDT_SET:
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    gc_visit_decref_child(obj->v.dict.entries[i].key);
                    gc_visit_decref_child(obj->v.dict.entries[i].value);
                }
            }
        }
        break;

    case PYDT_TUPLE:
        if (obj->v.tuple.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.tuple.len; i++) {
                gc_visit_decref_child(obj->v.tuple.items[i]);
            }
        }
        break;

    case PYDT_INSTANCE:
        gc_visit_decref_child(obj->v.instance.attrs);
        gc_visit_decref_child(obj->v.instance.cls);
        break;

    case PYDT_FUNCTION:
        gc_visit_decref_child(obj->v.func.defaults);
        gc_visit_decref_child(obj->v.func.closure);
        break;

    case PYDT_CELL:
        gc_visit_decref_child(obj->v.cell.value);
        break;

    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
        gc_visit_decref_child(obj->v.gen.state);
        gc_visit_decref_child(obj->v.gen.locals);
        break;

    case PYDT_EXCEPTION:
        gc_visit_decref_child(obj->v.exc.message);
        gc_visit_decref_child(obj->v.exc.traceback);
        gc_visit_decref_child(obj->v.exc.cause);
        break;

    case PYDT_CLASS:
        if (obj->v.cls.bases != (PyDosObj far * far *)0) {
            for (i = 0; i < (unsigned int)obj->v.cls.num_bases; i++) {
                gc_visit_decref_child(obj->v.cls.bases[i]);
            }
        }
        gc_visit_decref_child(obj->v.cls.class_attrs);
        break;

    case PYDT_EXC_GROUP:
        gc_visit_decref_child(obj->v.excgroup.message);
        if (obj->v.excgroup.exceptions != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.excgroup.count; i++) {
                gc_visit_decref_child(obj->v.excgroup.exceptions[i]);
            }
        }
        break;

    case PYDT_FROZENSET:
        if (obj->v.frozenset.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.frozenset.len; i++) {
                gc_visit_decref_child(obj->v.frozenset.items[i]);
            }
        }
        break;

    default:
        break;
    }
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
    gc_tracked = 0;
    gc_num_collections = 0UL;
}

/* ------------------------------------------------------------------ */
/* pydos_gc_shutdown — free all tracked objects                         */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gc_shutdown(void)
{
    GCHeader far *hdr;
    GCHeader far *next;

    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        next = hdr->gc_next;
        {
            PyDosObj far *obj;
            obj = GC_OBJ(hdr);
            /* Clear GC_TRACKED flag so pydos_obj_free won't try to untrack */
            obj->flags &= (unsigned char)~OBJ_FLAG_GC_TRACKED;
            pydos_obj_free(obj);
        }
        /* Free the combined GCHeader+PyDosObj block */
        pydos_far_free(hdr);
        hdr = next;
    }

    gc_sentinel.gc_next = &gc_sentinel;
    gc_sentinel.gc_prev = &gc_sentinel;
    gc_tracked = 0;
    gc_root_count = 0;
}

/* ------------------------------------------------------------------ */
/* pydos_gc_track — add a container to the tracking list               */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_gc_track(PyDosObj far *obj)
{
    GCHeader far *hdr;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    /* Don't double-track */
    if (obj->flags & OBJ_FLAG_GC_TRACKED) {
        return;
    }

    /* Only track container types */
    if (!is_container_type(obj->type)) {
        return;
    }

    /*
     * The object must have been allocated with pydos_gc_alloc()
     * so that a GCHeader exists immediately before it.
     */
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

    /* Phase 1: Clear MARKED flag on all tracked objects */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        obj->flags &= (unsigned char)~OBJ_FLAG_MARKED;
        hdr = hdr->gc_next;
    }

    /* Phase 2a: Mark from explicit GC roots */
    for (i = 0; i < gc_root_count; i++) {
        if (gc_roots[i] != (PyDosObj far * far *)0) {
            PyDosObj far *root_obj;
            root_obj = *gc_roots[i];
            gc_mark_obj(root_obj);
        }
    }

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
            gc_visit_decref(obj);
        }
        hdr = hdr->gc_next;
    }

    /* Step 3: Objects with gc_refs > 0 have external references */
    hdr = gc_sentinel.gc_next;
    while (hdr != &gc_sentinel) {
        PyDosObj far *obj;
        obj = GC_OBJ(hdr);
        if (!(obj->flags & OBJ_FLAG_MARKED) && hdr->gc_refs > 0) {
            gc_mark_obj(obj);
        }
        hdr = hdr->gc_next;
    }

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
    gc_alloc_counter++;
    if (gc_alloc_counter >= GC_THRESHOLD) {
        pydos_gc_collect();
    }
}

/* ------------------------------------------------------------------ */
/* pydos_gc_alloc — allocate a GC-managed object                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_gc_alloc(void)
{
    unsigned long total;
    char far *block;
    GCHeader far *hdr;
    PyDosObj far *obj;

    /* Maybe trigger collection before allocating */
    pydos_gc_maybe_collect();

    total = (unsigned long)sizeof(GCHeader) + (unsigned long)sizeof(PyDosObj);
    block = (char far *)pydos_far_alloc(total);
    if (block == (char far *)0) {
        /* Try a collection and retry once */
        pydos_gc_collect();
        block = (char far *)pydos_far_alloc(total);
        if (block == (char far *)0) {
            return (PyDosObj far *)0;
        }
    }

    _fmemset(block, 0, (unsigned int)total);

    hdr = (GCHeader far *)block;
    hdr->gc_next = (GCHeader far *)0;
    hdr->gc_prev = (GCHeader far *)0;

    obj = GC_OBJ(hdr);
    obj->refcount = 1;

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
