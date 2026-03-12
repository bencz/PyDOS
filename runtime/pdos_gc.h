/*
 * pydos_gc.h - Mark-and-sweep garbage collector for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * The GC supplements reference counting by collecting reference cycles
 * among container objects (lists, dicts, tuples, instances, etc.).
 *
 * Memory layout for tracked objects:
 *   [ GCHeader ][ PyDosObj ]
 *   ^           ^
 *   hdr         obj (what the rest of the runtime sees)
 */

#ifndef PDOS_GC_H
#define PDOS_GC_H

#include "pdos_obj.h"

/* ------------------------------------------------------------------ */
/* GCHeader — stored immediately before each tracked PyDosObj          */
/* ------------------------------------------------------------------ */
typedef struct GCHeader {
    struct GCHeader far *gc_next;
    struct GCHeader far *gc_prev;
    unsigned int gc_refs;   /* temporary refcount for trial deletion */
} GCHeader;

/* Convert between GCHeader and PyDosObj pointers */
#define GC_HDR(obj)  ((GCHeader far *)((char far *)(obj) - sizeof(GCHeader)))
#define GC_OBJ(hdr)  ((PyDosObj far *)((char far *)(hdr) + sizeof(GCHeader)))

/* Maximum number of GC root pointers */
#define GC_MAX_ROOTS  64

/* Allocation threshold before automatic collection */
#define GC_THRESHOLD  500

/* ------------------------------------------------------------------ */
/* Public API                                                          */
/* ------------------------------------------------------------------ */

void            PYDOS_API pydos_gc_init(void);
void            PYDOS_API pydos_gc_shutdown(void);

/* Track/untrack container objects in the GC linked list */
void            PYDOS_API pydos_gc_track(PyDosObj far *obj);
void            PYDOS_API pydos_gc_untrack(PyDosObj far *obj);

/* Run a full mark-and-sweep collection; returns number freed */
unsigned int    PYDOS_API pydos_gc_collect(void);

/* Register/unregister root pointers (globals, stack bases, etc.) */
int             PYDOS_API pydos_gc_add_root(PyDosObj far * far *root);
void            PYDOS_API pydos_gc_remove_root(PyDosObj far * far *root);

/* Called from pydos_obj_alloc to maybe trigger collection */
void            PYDOS_API pydos_gc_maybe_collect(void);

/* Allocate a GC-tracked object (GCHeader + PyDosObj in one block) */
PyDosObj far *  PYDOS_API pydos_gc_alloc(void);

/* Statistics */
unsigned int    PYDOS_API pydos_gc_tracked_count(void);
unsigned long   PYDOS_API pydos_gc_collections(void);

#endif /* PDOS_GC_H */
