/*
 * pdos_cll.c - Cell object for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Cells are mutable single-value containers used to implement closures.
 * When a variable is captured by an inner function (via nonlocal), the
 * outer function allocates a cell for that variable. Both the outer and
 * inner functions read/write through the cell, ensuring they share state.
 */

#include "pdos_cll.h"
#include "pdos_obj.h"

PyDosObj far * PYDOS_API pydos_cell_new(void)
{
    PyDosObj far *cell;

    cell = pydos_obj_alloc();
    if (cell == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    cell->type = PYDT_CELL;
    cell->flags = 0;
    cell->refcount = 1;
    cell->v.cell.value = (PyDosObj far *)0;

    return cell;
}

PyDosObj far * PYDOS_API pydos_cell_get(PyDosObj far *cell)
{
    PyDosObj far *val;

    if (cell == (PyDosObj far *)0 || cell->type != PYDT_CELL) {
        return (PyDosObj far *)0;
    }

    val = cell->v.cell.value;
    if (val != (PyDosObj far *)0) {
        PYDOS_INCREF(val);
    }
    return val;
}

void PYDOS_API pydos_cell_set(PyDosObj far *cell, PyDosObj far *value)
{
    PyDosObj far *old;

    if (cell == (PyDosObj far *)0 || cell->type != PYDT_CELL) {
        return;
    }

    old = cell->v.cell.value;
    PYDOS_INCREF(value);
    cell->v.cell.value = value;
    PYDOS_DECREF(old);
}

void PYDOS_API pydos_cell_init(void)
{
    /* No global state to initialize */
}

void PYDOS_API pydos_cell_shutdown(void)
{
    /* No global state to clean up */
}
