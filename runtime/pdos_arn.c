/*
 * pdos_arn.c - Arena scope allocator for PyDOS runtime
 *
 * Scope-based bulk deallocation for function-local objects.
 * The arena is an ownership ledger: each entry represents exactly one owned
 * reference and scope exit releases those references in reverse order.
 * Objects retained elsewhere survive through ordinary reference counting.
 *
 * C89 compatible, Open Watcom wcc / wcc386.
 */

#include "pdos_arn.h"
#include "pdos_gc.h"
#include "pdos_mem.h"

#include <string.h>

/* ------------------------------------------------------------------ */
/* Static state                                                        */
/* ------------------------------------------------------------------ */

/* Scope marker stack: each entry records tracked_count at scope entry */
static int scope_markers[ARENA_MAX_DEPTH];
static int scope_depth = 0;
/* Number of active scopes beyond ARENA_MAX_DEPTH.  These scopes cannot
 * track objects, but they must still consume matching scope_exit calls.
 * Without this counter an overflowing enter followed by exit incorrectly
 * popped and freed the nearest valid parent scope. */
static int scope_overflow_depth = 0;

/* Tracked objects array */
static PyDosObj far *tracked[ARENA_MAX_TRACKED];
static int tracked_count = 0;

/* ------------------------------------------------------------------ */
/* Init / Shutdown                                                     */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_arena_init(void)
{
    scope_depth = 0;
    scope_overflow_depth = 0;
    tracked_count = 0;
}

void PYDOS_API pydos_arena_shutdown(void)
{
    /* Clean up any remaining scopes */
    while (scope_overflow_depth > 0 || scope_depth > 0) {
        pydos_arena_scope_exit();
    }
}

/* ------------------------------------------------------------------ */
/* Scope operations                                                    */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_arena_scope_enter(void)
{
    if (scope_depth >= ARENA_MAX_DEPTH) {
        scope_overflow_depth++;
        return;
    }
    scope_markers[scope_depth] = tracked_count;
    scope_depth++;
}

void PYDOS_API pydos_arena_scope_track(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return;
    /* Don't track immortal objects (None, True, False, small ints) */
    if (obj->flags & OBJ_FLAG_IMMORTAL) return;
    if (tracked_count >= ARENA_MAX_TRACKED) return;
    tracked[tracked_count] = obj;
    tracked_count++;
}

void PYDOS_API pydos_arena_scope_track_ref(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) return;
    if (obj->flags & OBJ_FLAG_IMMORTAL) return;
    if (tracked_count >= ARENA_MAX_TRACKED) return;
    tracked[tracked_count] = obj;
    tracked_count++;
}

static void arena_release_from(int marker)
{
    int i;

    for (i = tracked_count - 1; i >= marker; i--) {
        PyDosObj far *obj = tracked[i];
        if (obj != (PyDosObj far *)0) {
            PYDOS_DECREF(obj);
            tracked[i] = (PyDosObj far *)0;
        }
    }
    tracked_count = marker;
}

void PYDOS_API pydos_arena_scope_exit(void)
{
    int marker;

    if (scope_overflow_depth > 0) {
        scope_overflow_depth--;
        return;
    }
    if (scope_depth <= 0) return;
    scope_depth--;
    marker = scope_markers[scope_depth];

    /* Release owned references in reverse order.  Do not force-free here:
     * a container or attribute may have retained the object while it was in
     * this scope. */
    arena_release_from(marker);
}

void PYDOS_API pydos_arena_release_frame(
    void far *frame_base, const short far *offsets, unsigned int count)
{
    char far *base;

    if (frame_base == (void far *)0 || offsets == (const short far *)0)
        return;

    base = (char far *)frame_base;
    while (count > 0) {
        PyDosObj far * far *slot;
        PyDosObj far *obj;
        count--;
        slot = (PyDosObj far * far *)(base + offsets[count]);
        obj = *slot;
        if (obj != (PyDosObj far *)0) {
            PYDOS_DECREF(obj);
        }
    }
}
