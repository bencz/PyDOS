/*
 * pdos_arn.c - Arena scope allocator for PyDOS runtime
 *
 * Scope-based bulk deallocation for function-local objects.
 * Objects tracked in a scope have OBJ_FLAG_ARENA set (DECREF = no-op).
 * At scope exit, all tracked objects are released and freed at once.
 *
 * C89 compatible, Open Watcom wcc / wcc386.
 */

#include "pdos_arn.h"
#include "pdos_gc.h"
#include "pdos_mem.h"

/* ------------------------------------------------------------------ */
/* Static state                                                        */
/* ------------------------------------------------------------------ */

/* Scope marker stack: each entry records tracked_count at scope entry */
static int scope_markers[ARENA_MAX_DEPTH];
static int scope_depth = 0;

/* Tracked objects array */
static PyDosObj far *tracked[ARENA_MAX_TRACKED];
static int tracked_count = 0;

/* ------------------------------------------------------------------ */
/* Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_arena_init(void)
{
    scope_depth = 0;
    tracked_count = 0;
}

void PYDOS_API pydos_arena_shutdown(void)
{
    /* Clean up any remaining scopes */
    while (scope_depth > 0) {
        pydos_arena_scope_exit();
    }
}

/* ------------------------------------------------------------------ */
/* Scope operations                                                    */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_arena_scope_enter(void)
{
    if (scope_depth >= ARENA_MAX_DEPTH) return;
    scope_markers[scope_depth] = tracked_count;
    scope_depth++;
}

void PYDOS_API pydos_arena_scope_track(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return;
    /* Don't track immortal objects (None, True, False, small ints) */
    if (obj->flags & OBJ_FLAG_IMMORTAL) return;
    if (obj->flags & OBJ_FLAG_ARENA) return;    /* Already tracked — skip */
    if (tracked_count >= ARENA_MAX_TRACKED) return;
    obj->flags |= OBJ_FLAG_ARENA;
    tracked[tracked_count] = obj;
    tracked_count++;
}

void PYDOS_API pydos_arena_scope_exit(void)
{
    int marker;
    int i;

    if (scope_depth <= 0) return;
    scope_depth--;
    marker = scope_markers[scope_depth];

    /* Free objects in reverse order (children before parents) */
    for (i = tracked_count - 1; i >= marker; i--) {
        PyDosObj far *obj = tracked[i];
        if (obj != (PyDosObj far *)0) {
            /* Untrack from GC if this object was GC-tracked */
            if (obj->flags & OBJ_FLAG_GC_TRACKED) {
                pydos_gc_untrack(obj);
            }
            /* Clear arena flag so release_data's DECREFs on
             * children work normally (children may be non-arena) */
            obj->flags &= ~OBJ_FLAG_ARENA;
            /* Release internal data (string buffers, list items, etc.) */
            pydos_obj_release_data(obj);
            /* Free the object itself */
            pydos_far_free(obj);
        }
    }
    tracked_count = marker;
}
