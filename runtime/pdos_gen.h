/*
 * pydos_gen.h - Generator helpers for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Phase 3: full generator protocol — send/throw/close + check_throw.
 */

#ifndef PDOS_GEN_H
#define PDOS_GEN_H

#include "pdos_obj.h"

/* Sent value global: set before calling resume(), read inside at yield resume.
 * Same single-threaded pattern as pydos_active_closure. */
extern PyDosObj far * pydos_gen_sent;
#ifdef __WATCOMC__
#pragma aux pydos_gen_sent "*_"
#endif

/* Create a generator object with the given resume function and number of locals */
PyDosObj far * PYDOS_API pydos_gen_new(void (far *resume)(void), int num_locals);

/* Resume generator and return next value. Raises StopIteration when done */
PyDosObj far * PYDOS_API pydos_gen_next(PyDosObj far *gen);

/* Send a value into the generator */
PyDosObj far * PYDOS_API pydos_gen_send(PyDosObj far *gen, PyDosObj far *value);

/* Throw an exception into the generator.
 * If the generator handles it and yields, returns the yielded value.
 * Otherwise propagates the exception. */
PyDosObj far * PYDOS_API pydos_gen_throw(PyDosObj far *gen, int exc_type,
                                          const char far *exc_msg);

/* Close the generator by throwing GeneratorExit.
 * If gen yields after GeneratorExit: raises RuntimeError.
 * Suppresses GeneratorExit and StopIteration. */
void           PYDOS_API pydos_gen_close(PyDosObj far *gen);

/* Check for pending throw (called at each yield resumption point).
 * Returns 0 if no throw pending, 1 if throw pending.
 * Does NOT raise — caller returns NULL from resume to propagate.
 * The exception is raised by pydos_gen_throw() on the current C stack. */
int            PYDOS_API pydos_gen_check_throw(PyDosObj far *gen);

void PYDOS_API pydos_gen_init(void);
void PYDOS_API pydos_gen_shutdown(void);

#endif /* PDOS_GEN_H */
