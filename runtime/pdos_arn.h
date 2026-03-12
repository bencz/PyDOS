/*
 * pdos_arn.h - Arena scope allocator for PyDOS runtime
 *
 * Provides scope-based object tracking for function-local objects
 * identified by escape analysis. Objects tracked by an arena scope
 * have OBJ_FLAG_ARENA set, making DECREF a no-op. At scope exit,
 * all tracked objects are bulk-freed.
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

/* Track an object in the current scope (sets OBJ_FLAG_ARENA) */
void PYDOS_API pydos_arena_scope_track(PyDosObj far *obj);

/* Pop scope and bulk-free all objects tracked since scope_enter */
void PYDOS_API pydos_arena_scope_exit(void);

#ifdef __cplusplus
}
#endif

#endif /* PDOS_ARN_H */
