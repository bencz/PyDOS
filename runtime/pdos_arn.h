/*
 * pdos_arn.h - Arena scope allocator for PyDOS runtime
 *
 * Provides scope-based ownership tracking for function-local objects
 * identified by escape analysis. Each ticket represents one owned reference;
 * scope exit releases the tickets in reverse order. It deliberately does not
 * alter object flags, so nested scopes and containers obey normal refcounts.
 *
 * C89 compatible, Open Watcom wcc / wcc386.
 */

#ifndef PDOS_ARN_H
#define PDOS_ARN_H

#include "pdos_obj.h"

/* Maximum nesting depth of arena scopes (recursive/nested functions) */
#define ARENA_MAX_DEPTH   16

/* Maximum objects tracked across all active scopes */
#define ARENA_MAX_TRACKED 512

#ifdef __cplusplus
extern "C" {
#endif

void PYDOS_API pydos_arena_init(void);
void PYDOS_API pydos_arena_shutdown(void);

/* Push a new scope marker */
void PYDOS_API pydos_arena_scope_enter(void);

/* Track one newly-created owned reference in the current scope. */
void PYDOS_API pydos_arena_scope_track(PyDosObj far *obj);

/* Track one owned reference returned by a lookup or call. */
void PYDOS_API pydos_arena_scope_track_ref(PyDosObj far *obj);

/* Pop scope and release all owned references tracked since scope_enter */
void PYDOS_API pydos_arena_scope_exit(void);

/* Release owned references stored in compiler frame slots.  Offsets are
 * signed byte displacements from frame_base and are processed in reverse
 * creation order.  This compact form avoids one tracking call per temporary
 * in 8086 native code. */
void PYDOS_API pydos_arena_release_frame(
    void far *frame_base, const short far *offsets, unsigned int count);

#ifdef __cplusplus
}
#endif

#endif /* PDOS_ARN_H */
