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

/* Return nonzero for object layouts that may own Python references. */
int             PYDOS_API pydos_gc_is_tracked_type(unsigned int type);

/* Unlink an object immediately before normal refcount destruction. */
void            PYDOS_API pydos_gc_untrack(PyDosObj far *obj);

/* Release the combined GCHeader + PyDosObj allocation after unlinking. */
void            PYDOS_API pydos_gc_free_object(PyDosObj far *obj);

/* Run a full mark-and-sweep collection; returns number freed */
unsigned int    PYDOS_API pydos_gc_collect(void);

/* Register/unregister root pointers (globals, stack bases, etc.) */
int             PYDOS_API pydos_gc_add_root(PyDosObj far * far *root);
void            PYDOS_API pydos_gc_remove_root(PyDosObj far * far *root);

/* Called by the tracked-object allocator to maybe trigger collection. */
void            PYDOS_API pydos_gc_maybe_collect(void);

/* Allocate and immediately track a typed GCHeader + PyDosObj block. */
PyDosObj far *  PYDOS_API pydos_gc_alloc_type(unsigned int type);

/* Statistics */
unsigned int    PYDOS_API pydos_gc_tracked_count(void);
unsigned long   PYDOS_API pydos_gc_collections(void);
unsigned long   PYDOS_API pydos_gc_collected(void);
unsigned int    PYDOS_API pydos_gc_allocation_count(void);
void            PYDOS_API pydos_gc_enable(void);
void            PYDOS_API pydos_gc_disable(void);
int             PYDOS_API pydos_gc_is_enabled(void);
void            PYDOS_API pydos_gc_set_thresholds(unsigned int threshold0,
                                                   unsigned int threshold1,
                                                   unsigned int threshold2);
void            PYDOS_API pydos_gc_get_thresholds(unsigned int *threshold0,
                                                   unsigned int *threshold1,
                                                   unsigned int *threshold2);

/* Generic-call bridges used by the Python-backed gc module. */
PyDosObj far *  PYDOS_API pydos_gc_collect_builtin(
                                  int argc, PyDosObj far * far *argv);
PyDosObj far *  PYDOS_API pydos_gc_is_tracked_builtin(
                                  int argc, PyDosObj far * far *argv);
PyDosObj far *  PYDOS_API pydos_gc_control_builtin(
                                  int argc, PyDosObj far * far *argv);

#endif /* PDOS_GC_H */
