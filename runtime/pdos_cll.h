/*
 * pdos_cll.h - Cell object for PyDOS runtime
 *
 * A cell is a mutable container holding a single PyDosObj reference.
 * Used to implement Python's closure mechanism: captured variables
 * are stored in cells, which are shared between the enclosing and
 * enclosed function scopes via the closure list.
 */

#ifndef PDOS_CLL_H
#define PDOS_CLL_H

#include "pdos_obj.h"

/*
 * pydos_cell_new - Create a new cell with value = NULL.
 * Returns a PyDosObj with type = PYDT_CELL, refcount = 1.
 */
PyDosObj far * PYDOS_API pydos_cell_new(void);

/*
 * pydos_cell_get - Read the value from a cell.
 * Returns the contained value with an INCREF (caller owns the reference).
 * Returns NULL if the cell is empty or invalid.
 */
PyDosObj far * PYDOS_API pydos_cell_get(PyDosObj far *cell);

/*
 * pydos_cell_set - Store a new value into a cell.
 * DECREFs the old value (if any), INCREFs the new value, then stores it.
 */
void PYDOS_API pydos_cell_set(PyDosObj far *cell, PyDosObj far *value);

/*
 * pydos_cell_init / pydos_cell_shutdown - Module lifecycle.
 */
void PYDOS_API pydos_cell_init(void);
void PYDOS_API pydos_cell_shutdown(void);

#endif /* PDOS_CLL_H */
