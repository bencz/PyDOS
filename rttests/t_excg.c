/*
 * t_excg.c - Unit tests for ExceptionGroup (PYDT_EXC_GROUP)
 *
 * Tests the runtime support for PEP 654 exception groups.
 */

#include "testfw.h"
#include "../runtime/pdos_exg.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_exc.h"
#include "../runtime/pdos_mem.h"
#include "../runtime/pdos_lst.h"

/* Helper: create an exception object with given type code */
static PyDosObj far *make_exc(int type_code)
{
    PyDosObj far *obj;
    PyDosObj far *msg;

    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    obj->type = PYDT_EXCEPTION;
    obj->flags = 0;
    obj->refcount = 1;
    obj->v.exc.type_code = type_code;

    msg = pydos_obj_new_str((const char far *)"test", 4);
    obj->v.exc.message = msg;
    obj->v.exc.traceback = (PyDosObj far *)0;
    obj->v.exc.cause = (PyDosObj far *)0;

    return obj;
}

/* ------------------------------------------------------------------ */
/* excgroup_new: create group with 3 exceptions, verify count/message  */
/* ------------------------------------------------------------------ */
TEST(excgroup_new)
{
    PyDosObj far *msg;
    PyDosObj far *excs[3];
    PyDosObj far *group;

    msg = pydos_obj_new_str((const char far *)"errors", 6);
    excs[0] = make_exc(PYDOS_EXC_VALUE_ERROR);
    excs[1] = make_exc(PYDOS_EXC_TYPE_ERROR);
    excs[2] = make_exc(PYDOS_EXC_KEY_ERROR);

    group = pydos_excgroup_new(msg, excs, 3);
    ASSERT_NOT_NULL(group);
    ASSERT_EQ((long)group->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)group->v.excgroup.count, 3L);
    ASSERT_NOT_NULL(group->v.excgroup.message);
    ASSERT_EQ((long)group->v.excgroup.message->type, (long)PYDT_STR);

    PYDOS_DECREF(group);
    PYDOS_DECREF(excs[0]);
    PYDOS_DECREF(excs[1]);
    PYDOS_DECREF(excs[2]);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_match_all: all exceptions match, remainder NULL            */
/* ------------------------------------------------------------------ */
TEST(excgroup_match_all)
{
    PyDosObj far *msg;
    PyDosObj far *excs[2];
    PyDosObj far *group;
    PyDosObj far *argv[2];
    PyDosObj far *type_code_obj;
    PyDosObj far *result;
    PyDosObj far *matched;
    PyDosObj far *remainder;

    msg = pydos_obj_new_str((const char far *)"g", 1);
    excs[0] = make_exc(PYDOS_EXC_VALUE_ERROR);
    excs[1] = make_exc(PYDOS_EXC_VALUE_ERROR);
    group = pydos_excgroup_new(msg, excs, 2);

    type_code_obj = pydos_obj_new_int((long)PYDOS_EXC_VALUE_ERROR);
    argv[0] = group;
    argv[1] = type_code_obj;

    result = pydos_excgroup_match(2, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ((long)result->type, (long)PYDT_LIST);
    ASSERT_EQ((long)result->v.list.len, 2L);

    /* matched = result[0] — should be an ExceptionGroup */
    matched = pydos_list_get(result, 0L);
    ASSERT_NOT_NULL(matched);
    ASSERT_EQ((long)matched->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)matched->v.excgroup.count, 2L);
    PYDOS_DECREF(matched);

    /* remainder = result[1] — should be None */
    remainder = pydos_list_get(result, 1L);
    ASSERT_NOT_NULL(remainder);
    ASSERT_EQ((long)remainder->type, (long)PYDT_NONE);
    PYDOS_DECREF(remainder);

    PYDOS_DECREF(result);
    PYDOS_DECREF(type_code_obj);
    PYDOS_DECREF(group);
    PYDOS_DECREF(excs[0]);
    PYDOS_DECREF(excs[1]);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_match_some: partial match, remainder has unmatched         */
/* ------------------------------------------------------------------ */
TEST(excgroup_match_some)
{
    PyDosObj far *msg;
    PyDosObj far *excs[3];
    PyDosObj far *group;
    PyDosObj far *argv[2];
    PyDosObj far *type_code_obj;
    PyDosObj far *result;
    PyDosObj far *matched;
    PyDosObj far *remainder;

    msg = pydos_obj_new_str((const char far *)"mixed", 5);
    excs[0] = make_exc(PYDOS_EXC_VALUE_ERROR);
    excs[1] = make_exc(PYDOS_EXC_TYPE_ERROR);
    excs[2] = make_exc(PYDOS_EXC_VALUE_ERROR);
    group = pydos_excgroup_new(msg, excs, 3);

    type_code_obj = pydos_obj_new_int((long)PYDOS_EXC_VALUE_ERROR);
    argv[0] = group;
    argv[1] = type_code_obj;

    result = pydos_excgroup_match(2, argv);
    ASSERT_NOT_NULL(result);

    /* matched = result[0] — should have 2 ValueErrors */
    matched = pydos_list_get(result, 0L);
    ASSERT_NOT_NULL(matched);
    ASSERT_EQ((long)matched->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)matched->v.excgroup.count, 2L);
    PYDOS_DECREF(matched);

    /* remainder = result[1] — should have 1 TypeError */
    remainder = pydos_list_get(result, 1L);
    ASSERT_NOT_NULL(remainder);
    ASSERT_EQ((long)remainder->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)remainder->v.excgroup.count, 1L);
    PYDOS_DECREF(remainder);

    PYDOS_DECREF(result);
    PYDOS_DECREF(type_code_obj);
    PYDOS_DECREF(group);
    PYDOS_DECREF(excs[0]);
    PYDOS_DECREF(excs[1]);
    PYDOS_DECREF(excs[2]);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_match_none: no exceptions match, returns NULL matched      */
/* ------------------------------------------------------------------ */
TEST(excgroup_match_none)
{
    PyDosObj far *msg;
    PyDosObj far *excs[2];
    PyDosObj far *group;
    PyDosObj far *argv[2];
    PyDosObj far *type_code_obj;
    PyDosObj far *result;
    PyDosObj far *matched;
    PyDosObj far *remainder;

    msg = pydos_obj_new_str((const char far *)"g", 1);
    excs[0] = make_exc(PYDOS_EXC_TYPE_ERROR);
    excs[1] = make_exc(PYDOS_EXC_KEY_ERROR);
    group = pydos_excgroup_new(msg, excs, 2);

    type_code_obj = pydos_obj_new_int((long)PYDOS_EXC_VALUE_ERROR);
    argv[0] = group;
    argv[1] = type_code_obj;

    result = pydos_excgroup_match(2, argv);
    ASSERT_NOT_NULL(result);

    /* matched = result[0] — should be None */
    matched = pydos_list_get(result, 0L);
    ASSERT_NOT_NULL(matched);
    ASSERT_EQ((long)matched->type, (long)PYDT_NONE);
    PYDOS_DECREF(matched);

    /* remainder = result[1] — should be the full group */
    remainder = pydos_list_get(result, 1L);
    ASSERT_NOT_NULL(remainder);
    ASSERT_EQ((long)remainder->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)remainder->v.excgroup.count, 2L);
    PYDOS_DECREF(remainder);

    PYDOS_DECREF(result);
    PYDOS_DECREF(type_code_obj);
    PYDOS_DECREF(group);
    PYDOS_DECREF(excs[0]);
    PYDOS_DECREF(excs[1]);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_single: group with 1 exception                             */
/* ------------------------------------------------------------------ */
TEST(excgroup_single)
{
    PyDosObj far *msg;
    PyDosObj far *exc;
    PyDosObj far *group;

    msg = pydos_obj_new_str((const char far *)"single", 6);
    exc = make_exc(PYDOS_EXC_VALUE_ERROR);

    group = pydos_excgroup_new(msg, &exc, 1);
    ASSERT_NOT_NULL(group);
    ASSERT_EQ((long)group->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)group->v.excgroup.count, 1L);

    PYDOS_DECREF(group);
    PYDOS_DECREF(exc);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_release: verify DECREF frees group properly                */
/* ------------------------------------------------------------------ */
TEST(excgroup_release)
{
    PyDosObj far *msg;
    PyDosObj far *exc;
    PyDosObj far *group;

    msg = pydos_obj_new_str((const char far *)"rel", 3);
    exc = make_exc(PYDOS_EXC_VALUE_ERROR);

    group = pydos_excgroup_new(msg, &exc, 1);
    ASSERT_NOT_NULL(group);
    ASSERT_EQ((long)group->refcount, 1L);

    /* exc was INCREFed by excgroup_new, so refcount should be 2 */
    ASSERT_EQ((long)exc->refcount, 2L);

    PYDOS_DECREF(group); /* should DECREF the exception inside */

    /* exc refcount should now be back to 1 */
    ASSERT_EQ((long)exc->refcount, 1L);

    PYDOS_DECREF(exc);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_to_str: verify string representation                       */
/* ------------------------------------------------------------------ */
TEST(excgroup_to_str)
{
    PyDosObj far *msg;
    PyDosObj far *excs[2];
    PyDosObj far *group;
    PyDosObj far *str_rep;

    msg = pydos_obj_new_str((const char far *)"errs", 4);
    excs[0] = make_exc(PYDOS_EXC_VALUE_ERROR);
    excs[1] = make_exc(PYDOS_EXC_TYPE_ERROR);
    group = pydos_excgroup_new(msg, excs, 2);

    str_rep = pydos_obj_to_str(group);
    ASSERT_NOT_NULL(str_rep);
    ASSERT_EQ((long)str_rep->type, (long)PYDT_STR);
    /* Should contain "ExceptionGroup" */
    ASSERT_TRUE(str_rep->v.str.len > 0);

    PYDOS_DECREF(str_rep);
    PYDOS_DECREF(group);
    PYDOS_DECREF(excs[0]);
    PYDOS_DECREF(excs[1]);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* excgroup_constructor: test builtin constructor                       */
/* ------------------------------------------------------------------ */
TEST(excgroup_constructor)
{
    PyDosObj far *msg;
    PyDosObj far *exc_list;
    PyDosObj far *exc1;
    PyDosObj far *exc2;
    PyDosObj far *argv[2];
    PyDosObj far *group;

    msg = pydos_obj_new_str((const char far *)"test", 4);
    exc_list = pydos_list_new(2);
    exc1 = make_exc(PYDOS_EXC_VALUE_ERROR);
    exc2 = make_exc(PYDOS_EXC_TYPE_ERROR);
    pydos_list_append(exc_list, exc1);
    pydos_list_append(exc_list, exc2);

    argv[0] = msg;
    argv[1] = exc_list;
    group = pydos_exc_new_exceptiongroup(2, argv);
    ASSERT_NOT_NULL(group);
    ASSERT_EQ((long)group->type, (long)PYDT_EXC_GROUP);
    ASSERT_EQ((long)group->v.excgroup.count, 2L);

    PYDOS_DECREF(group);
    PYDOS_DECREF(exc1);
    PYDOS_DECREF(exc2);
    PYDOS_DECREF(exc_list);
    PYDOS_DECREF(msg);
}

/* ------------------------------------------------------------------ */
/* Runner                                                              */
/* ------------------------------------------------------------------ */

void run_excg_tests(void)
{
    SUITE("pdos_exg");
    RUN(excgroup_new);
    RUN(excgroup_match_all);
    RUN(excgroup_match_some);
    RUN(excgroup_match_none);
    RUN(excgroup_single);
    RUN(excgroup_release);
    RUN(excgroup_to_str);
    RUN(excgroup_constructor);
}
