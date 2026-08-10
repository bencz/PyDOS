/*
 * pdos_exg.h - ExceptionGroup support for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Implements PEP 654 ExceptionGroup type for except* handling.
 */

#ifndef PDOS_EXG_H
#define PDOS_EXG_H

#include "pdos_obj.h"

/* Create an ExceptionGroup from message string + array of exception objects.
 * Takes ownership references: INCREFs message and each exception. */
PyDosObj far * PYDOS_API pydos_excgroup_new(
    PyDosObj far *message,
    PyDosObj far * far *exceptions,
    unsigned int count);

/* Builtin constructor: ExceptionGroup(msg, [exc1, exc2, ...])
 * argv[0] = message string, argv[1] = list of exceptions */
PyDosObj far * PYDOS_API pydos_exc_new_exceptiongroup(
    int argc, PyDosObj far * far *argv);

/* Match exceptions in a group against a type code.
 * Returns a new ExceptionGroup containing matched exceptions,
 * or NULL if none match.
 * Sets *remainder to a new ExceptionGroup of unmatched exceptions,
 * or NULL if all matched. */
PyDosObj far * PYDOS_API pydos_excgroup_match(
    int argc, PyDosObj far * far *argv);

#endif /* PDOS_EXG_H */
