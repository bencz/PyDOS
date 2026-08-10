/*
 * t_cll.c - Unit tests for pdos_cll module (cell objects for closures)
 *
 * Tests cell creation, get/set, refcount behavior, overwrite, and NULL.
 * C89 compatible.
 */

#include "testfw.h"
#include "../runtime/pdos_cll.h"
#include "../runtime/pdos_obj.h"

/* ------------------------------------------------------------------ */
/* cll_new: create a cell, verify type and NULL value                   */
/* ------------------------------------------------------------------ */

TEST(cll_new)
{
    PyDosObj far *cell;

    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);
    ASSERT_EQ((int)cell->type, (int)PYDT_CELL);
    ASSERT_EQ((int)cell->refcount, 1);
    ASSERT_NULL(cell->v.cell.value);
    PYDOS_DECREF(cell);
}

/* ------------------------------------------------------------------ */
/* cll_set_get: set a value, verify get returns same object            */
/* ------------------------------------------------------------------ */

TEST(cll_set_get)
{
    PyDosObj far *cell;
    PyDosObj far *val;
    PyDosObj far *got;

    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);

    val = pydos_obj_new_int(42);
    ASSERT_NOT_NULL(val);

    pydos_cell_set(cell, val);
    got = pydos_cell_get(cell);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ((int)got->type, (int)PYDT_INT);
    ASSERT_EQ(got->v.int_val, 42L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(val);
    PYDOS_DECREF(cell);
}

/* ------------------------------------------------------------------ */
/* cll_overwrite: set new value, verify old is DECREFed                */
/* ------------------------------------------------------------------ */

TEST(cll_overwrite)
{
    PyDosObj far *cell;
    PyDosObj far *v1;
    PyDosObj far *v2;
    PyDosObj far *got;

    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);

    v1 = pydos_obj_new_int(1000);
    ASSERT_NOT_NULL(v1);
    pydos_cell_set(cell, v1);

    v2 = pydos_obj_new_int(2000);
    ASSERT_NOT_NULL(v2);
    pydos_cell_set(cell, v2);

    got = pydos_cell_get(cell);
    ASSERT_NOT_NULL(got);
    ASSERT_EQ(got->v.int_val, 2000L);

    PYDOS_DECREF(got);
    PYDOS_DECREF(v2);
    PYDOS_DECREF(v1);
    PYDOS_DECREF(cell);
}

/* ------------------------------------------------------------------ */
/* cll_null_get: get from empty cell returns NULL                       */
/* ------------------------------------------------------------------ */

TEST(cll_null_get)
{
    PyDosObj far *cell;
    PyDosObj far *got;

    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);

    got = pydos_cell_get(cell);
    ASSERT_NULL(got);

    PYDOS_DECREF(cell);
}

/* ------------------------------------------------------------------ */
/* cll_refcount: verify INCREF/DECREF of contained value               */
/* ------------------------------------------------------------------ */

TEST(cll_refcount)
{
    PyDosObj far *cell;
    PyDosObj far *val;
    unsigned int rc_before;

    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);

    val = pydos_obj_new_int(5000);
    ASSERT_NOT_NULL(val);
    rc_before = val->refcount;

    /* set INCREFs the value */
    pydos_cell_set(cell, val);
    ASSERT_EQ((long)val->refcount, (long)(rc_before + 1));

    /* get INCREFs the returned reference */
    {
        PyDosObj far *got = pydos_cell_get(cell);
        ASSERT_NOT_NULL(got);
        ASSERT_EQ((long)val->refcount, (long)(rc_before + 2));
        PYDOS_DECREF(got);
    }

    PYDOS_DECREF(val);
    PYDOS_DECREF(cell);
}

/* ------------------------------------------------------------------ */
/* cll_free: freeing cell DECREFs contained value                      */
/* ------------------------------------------------------------------ */

TEST(cll_free)
{
    PyDosObj far *cell;
    PyDosObj far *val;

    cell = pydos_cell_new();
    ASSERT_NOT_NULL(cell);

    val = pydos_obj_new_int(9999);
    ASSERT_NOT_NULL(val);
    pydos_cell_set(cell, val);

    /* val refcount: 1 (our ref) + 1 (cell's ref) = 2 */
    ASSERT_EQ((long)val->refcount, 2L);

    /* Free the cell — should DECREF val back to 1 */
    PYDOS_DECREF(cell);
    ASSERT_EQ((long)val->refcount, 1L);

    PYDOS_DECREF(val);
}

/* ------------------------------------------------------------------ */
/* cll_invalid_get: get from non-cell returns NULL                     */
/* ------------------------------------------------------------------ */

TEST(cll_invalid_get)
{
    PyDosObj far *val;
    PyDosObj far *got;

    val = pydos_obj_new_int(10);
    ASSERT_NOT_NULL(val);

    got = pydos_cell_get(val);
    ASSERT_NULL(got);

    PYDOS_DECREF(val);
}

/* ------------------------------------------------------------------ */
/* cll_invalid_set: set on non-cell is a no-op                         */
/* ------------------------------------------------------------------ */

TEST(cll_invalid_set)
{
    PyDosObj far *val;
    PyDosObj far *inner;

    val = pydos_obj_new_int(10);
    ASSERT_NOT_NULL(val);

    inner = pydos_obj_new_int(20);
    ASSERT_NOT_NULL(inner);

    /* Should be a no-op, not crash */
    pydos_cell_set(val, inner);

    PYDOS_DECREF(inner);
    PYDOS_DECREF(val);
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

void run_cll_tests(void)
{
    SUITE("pdos_cll");
    RUN(cll_new);
    RUN(cll_set_get);
    RUN(cll_overwrite);
    RUN(cll_null_get);
    RUN(cll_refcount);
    RUN(cll_free);
    RUN(cll_invalid_get);
    RUN(cll_invalid_set);
}
