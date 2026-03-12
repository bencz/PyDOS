/*
 * pdos_asn.h - Coroutine / async runtime for PyDOS
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Phase 4: async def / await — cooperative scheduling.
 * Coroutines reuse the PyDosGen struct layout (same resume/state/pc/locals).
 * PYDT_COROUTINE type tag prevents iteration via for loops.
 */

#ifndef PDOS_ASN_H
#define PDOS_ASN_H

#include "pdos_obj.h"

/* Create a coroutine object with the given resume function and local count.
 * Same as pydos_gen_new but sets type to PYDT_COROUTINE. */
PyDosObj far * PYDOS_API pydos_cor_new(void (far *resume)(void), int num_locals);

/* Run a single coroutine to completion (trampoline event loop).
 * argv[0] = root coroutine object.
 * Returns the coroutine's return value (from gen->state). */
PyDosObj far * PYDOS_API pydos_async_run(int argc, PyDosObj far * far *argv);

/* Run multiple coroutines concurrently via round-robin scheduling.
 * argv[0] = list of coroutine objects.
 * Returns list of results (one per coroutine, in order). */
PyDosObj far * PYDOS_API pydos_async_gather(int argc, PyDosObj far * far *argv);

/* Initialize coroutine subsystem (called from pydos_rt_init) */
void PYDOS_API pydos_cor_init(void);

/* Shutdown coroutine subsystem (called from pydos_rt_shutdown) */
void PYDOS_API pydos_cor_shutdown(void);

#endif /* PDOS_ASN_H */
