/*
 * pdos_exg.c - ExceptionGroup implementation for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Implements PEP 654 ExceptionGroup for except* handling.
 */

#include "pdos_exg.h"
#include "pdos_obj.h"
#include "pdos_mem.h"
#include "pdos_exc.h"
#include "pdos_lst.h"
#include <string.h>

/* ------------------------------------------------------------------ */
/* pydos_excgroup_new - create an ExceptionGroup                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_excgroup_new(
    PyDosObj far *message,
    PyDosObj far * far *exceptions,
    unsigned int count)
{
    PyDosObj far *obj;
    PyDosObj far * far *arr;
    unsigned int i;

    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;

    arr = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned long)count * (unsigned long)sizeof(PyDosObj far *));
    if (arr == (PyDosObj far * far *)0 && count > 0) {
        pydos_obj_free(obj);
        return (PyDosObj far *)0;
    }

    obj->type = PYDT_EXC_GROUP;
    obj->flags = 0;
    obj->refcount = 1;

    /* Copy and INCREF each exception */
    for (i = 0; i < count; i++) {
        arr[i] = exceptions[i];
        PYDOS_INCREF(exceptions[i]);
    }

    obj->v.excgroup.exceptions = arr;
    obj->v.excgroup.count = count;
    obj->v.excgroup.message = message;
    PYDOS_INCREF(message);

    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_exc_new_exceptiongroup - builtin constructor                  */
/* ExceptionGroup(msg, [exc1, exc2, ...])                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_exc_new_exceptiongroup(
    int argc, PyDosObj far * far *argv)
{
    PyDosObj far *msg;
    PyDosObj far *exc_list;
    unsigned int count;
    unsigned int i;
    PyDosObj far * far *arr;
    PyDosObj far *result;

    if (argc < 2) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
            (const char far *)"ExceptionGroup requires 2 arguments");
        return (PyDosObj far *)0;
    }

    msg = argv[0];
    exc_list = argv[1];

    if ((PyDosType)exc_list->type != PYDT_LIST) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
            (const char far *)"ExceptionGroup second arg must be a list");
        return (PyDosObj far *)0;
    }

    count = exc_list->v.list.len;

    if (count == 0) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
            (const char far *)"ExceptionGroup cannot be empty");
        return (PyDosObj far *)0;
    }

    result = pydos_excgroup_new(msg, exc_list->v.list.items, count);
    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_excgroup_match - split group by exception type                */
/* argv[0] = exception (ExceptionGroup or regular exception)           */
/* argv[1] = type_code (int)                                           */
/* Returns: list [matched_group_or_none, remainder_group_or_none]      */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_excgroup_match(
    int argc, PyDosObj far * far *argv)
{
    PyDosObj far *exc;
    int type_code;
    unsigned int i;
    unsigned int count;
    PyDosObj far *result_list;
    PyDosObj far *matched_obj;
    PyDosObj far *remainder_obj;

    /* Temporary arrays for matched/unmatched exceptions */
    PyDosObj far * far *matched_arr;
    PyDosObj far * far *unmatched_arr;
    unsigned int matched_count;
    unsigned int unmatched_count;

    if (argc < 2) return (PyDosObj far *)0;

    exc = argv[0];
    type_code = (int)argv[1]->v.int_val;

    /* If it's not an ExceptionGroup, check if the single exception matches */
    if ((PyDosType)exc->type != PYDT_EXC_GROUP) {
        result_list = pydos_list_new(2);
        if (pydos_exc_matches(exc, type_code)) {
            /* Single exception matches: matched=exc, remainder=None */
            PYDOS_INCREF(exc);
            pydos_list_append(result_list, exc);
            PYDOS_DECREF(exc);
            {
                PyDosObj far *none_obj = pydos_obj_new_none();
                pydos_list_append(result_list, none_obj);
                PYDOS_DECREF(none_obj);
            }
        } else {
            /* No match: matched=None, remainder=exc */
            {
                PyDosObj far *none_obj = pydos_obj_new_none();
                pydos_list_append(result_list, none_obj);
                PYDOS_DECREF(none_obj);
            }
            PYDOS_INCREF(exc);
            pydos_list_append(result_list, exc);
            PYDOS_DECREF(exc);
        }
        return result_list;
    }

    /* It's an ExceptionGroup — split into matched and unmatched */
    count = exc->v.excgroup.count;

    matched_arr = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned long)count * (unsigned long)sizeof(PyDosObj far *));
    unmatched_arr = (PyDosObj far * far *)pydos_far_alloc(
        (unsigned long)count * (unsigned long)sizeof(PyDosObj far *));

    if (matched_arr == (PyDosObj far * far *)0 ||
        unmatched_arr == (PyDosObj far * far *)0) {
        if (matched_arr) pydos_far_free(matched_arr);
        if (unmatched_arr) pydos_far_free(unmatched_arr);
        return (PyDosObj far *)0;
    }

    matched_count = 0;
    unmatched_count = 0;

    for (i = 0; i < count; i++) {
        PyDosObj far *child = exc->v.excgroup.exceptions[i];
        if (pydos_exc_matches(child, type_code)) {
            matched_arr[matched_count++] = child;
        } else {
            unmatched_arr[unmatched_count++] = child;
        }
    }

    /* Build result list [matched_group_or_none, remainder_group_or_none] */
    result_list = pydos_list_new(2);

    if (matched_count > 0) {
        matched_obj = pydos_excgroup_new(exc->v.excgroup.message,
                                          matched_arr, matched_count);
        pydos_list_append(result_list, matched_obj);
        PYDOS_DECREF(matched_obj);
    } else {
        matched_obj = pydos_obj_new_none();
        pydos_list_append(result_list, matched_obj);
        PYDOS_DECREF(matched_obj);
    }

    if (unmatched_count > 0) {
        remainder_obj = pydos_excgroup_new(exc->v.excgroup.message,
                                            unmatched_arr, unmatched_count);
        pydos_list_append(result_list, remainder_obj);
        PYDOS_DECREF(remainder_obj);
    } else {
        remainder_obj = pydos_obj_new_none();
        pydos_list_append(result_list, remainder_obj);
        PYDOS_DECREF(remainder_obj);
    }

    pydos_far_free(matched_arr);
    pydos_far_free(unmatched_arr);

    return result_list;
}
