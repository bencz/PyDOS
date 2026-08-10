/*
 * pdos_mon.c - Low-overhead Python 3.12 sys.monitoring support
 */

#include "pdos_mon.h"
#include "pdos_gc.h"
#include "pdos_exc.h"
#include "pdos_vtb.h"
#include "pdos_cod.h"
#include <string.h>

#define PYDOS_MONITOR_TOOL_COUNT 6
#define PYDOS_MONITOR_LOCAL_COUNT 24
#define PYDOS_MONITOR_PY_START 1L
#define PYDOS_MONITOR_PY_RETURN 2L

typedef struct PyDosMonitorLocal {
    unsigned char tool_id;
    long events;
    PyDosObj far *code;
} PyDosMonitorLocal;

unsigned char pydos_monitoring_active = 0;
static PyDosObj far *monitor_tool_names[PYDOS_MONITOR_TOOL_COUNT];
static PyDosObj far *monitor_start_callbacks[PYDOS_MONITOR_TOOL_COUNT];
static PyDosObj far *monitor_return_callbacks[PYDOS_MONITOR_TOOL_COUNT];
static PyDosMonitorLocal monitor_locals[PYDOS_MONITOR_LOCAL_COUNT];

static void monitor_replace_owned_ref(PyDosObj far **slot,
                                      PyDosObj far *value)
{
    PyDosObj far *old = *slot;
    if (value != (PyDosObj far *)0) PYDOS_INCREF(value);
    *slot = value;
    if (old != (PyDosObj far *)0) PYDOS_DECREF(old);
}

static int monitor_tool_id(PyDosObj far *value)
{
    long id;
    if (value == (PyDosObj far *)0 ||
        ((PyDosType)value->type != PYDT_INT &&
         (PyDosType)value->type != PYDT_BOOL)) return -1;
    id = (PyDosType)value->type == PYDT_INT
         ? value->v.int_val : (long)value->v.bool_val;
    return id >= 0 && id < PYDOS_MONITOR_TOOL_COUNT ? (int)id : -1;
}

static void monitoring_recompute_active(void)
{
    int i;
    pydos_monitoring_active = 0;
    for (i = 0; i < PYDOS_MONITOR_LOCAL_COUNT; i++) {
        int tool = (int)monitor_locals[i].tool_id;
        if (monitor_locals[i].code == (PyDosObj far *)0 ||
            monitor_locals[i].events == 0L ||
            tool < 0 || tool >= PYDOS_MONITOR_TOOL_COUNT) continue;
        if (((monitor_locals[i].events & PYDOS_MONITOR_PY_START) &&
             monitor_start_callbacks[tool] != (PyDosObj far *)0) ||
            ((monitor_locals[i].events & PYDOS_MONITOR_PY_RETURN) &&
             monitor_return_callbacks[tool] != (PyDosObj far *)0)) {
            pydos_monitoring_active = 1;
            return;
        }
    }
}

void PYDOS_API pydos_monitoring_init(void)
{
    int i;
    _fmemset(monitor_tool_names, 0, sizeof(monitor_tool_names));
    _fmemset(monitor_start_callbacks, 0, sizeof(monitor_start_callbacks));
    _fmemset(monitor_return_callbacks, 0, sizeof(monitor_return_callbacks));
    _fmemset(monitor_locals, 0, sizeof(monitor_locals));
    for (i = 0; i < PYDOS_MONITOR_TOOL_COUNT; i++) {
        pydos_gc_add_root(&monitor_tool_names[i]);
        pydos_gc_add_root(&monitor_start_callbacks[i]);
        pydos_gc_add_root(&monitor_return_callbacks[i]);
    }
    for (i = 0; i < PYDOS_MONITOR_LOCAL_COUNT; i++)
        pydos_gc_add_root(&monitor_locals[i].code);
    pydos_monitoring_active = 0;
}

void PYDOS_API pydos_monitoring_shutdown(void)
{
    int i;
    pydos_monitoring_active = 0;
    for (i = 0; i < PYDOS_MONITOR_TOOL_COUNT; i++) {
        pydos_gc_remove_root(&monitor_tool_names[i]);
        pydos_gc_remove_root(&monitor_start_callbacks[i]);
        pydos_gc_remove_root(&monitor_return_callbacks[i]);
        PYDOS_DECREF(monitor_tool_names[i]);
        PYDOS_DECREF(monitor_start_callbacks[i]);
        PYDOS_DECREF(monitor_return_callbacks[i]);
        monitor_tool_names[i] = (PyDosObj far *)0;
        monitor_start_callbacks[i] = (PyDosObj far *)0;
        monitor_return_callbacks[i] = (PyDosObj far *)0;
    }
    for (i = 0; i < PYDOS_MONITOR_LOCAL_COUNT; i++) {
        pydos_gc_remove_root(&monitor_locals[i].code);
        PYDOS_DECREF(monitor_locals[i].code);
        monitor_locals[i].code = (PyDosObj far *)0;
        monitor_locals[i].events = 0L;
    }
}

static PyDosObj far * PYDOS_API monitoring_get_tool(
    int argc, PyDosObj far * far *argv)
{
    int id = argc > 0 ? monitor_tool_id(argv[0]) : -1;
    if (id < 0) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"invalid monitoring tool id");
        return (PyDosObj far *)0;
    }
    if (monitor_tool_names[id] == (PyDosObj far *)0)
        return pydos_obj_new_none();
    PYDOS_INCREF(monitor_tool_names[id]);
    return monitor_tool_names[id];
}

static PyDosObj far * PYDOS_API monitoring_use_tool_id(
    int argc, PyDosObj far * far *argv)
{
    int id = argc > 0 ? monitor_tool_id(argv[0]) : -1;
    if (id < 0 || argc < 2 || argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[1]->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"invalid monitoring tool registration");
        return (PyDosObj far *)0;
    }
    if (monitor_tool_names[id] != (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"monitoring tool id is already in use");
        return (PyDosObj far *)0;
    }
    monitor_replace_owned_ref(&monitor_tool_names[id], argv[1]);
    return pydos_obj_new_none();
}

static void monitoring_clear_tool(int id)
{
    int i;
    monitor_replace_owned_ref(&monitor_tool_names[id], (PyDosObj far *)0);
    monitor_replace_owned_ref(&monitor_start_callbacks[id], (PyDosObj far *)0);
    monitor_replace_owned_ref(&monitor_return_callbacks[id], (PyDosObj far *)0);
    for (i = 0; i < PYDOS_MONITOR_LOCAL_COUNT; i++) {
        if (monitor_locals[i].code != (PyDosObj far *)0 &&
            (int)monitor_locals[i].tool_id == id) {
            monitor_replace_owned_ref(&monitor_locals[i].code,
                              (PyDosObj far *)0);
            monitor_locals[i].events = 0L;
        }
    }
    monitoring_recompute_active();
}

static PyDosObj far * PYDOS_API monitoring_free_tool_id(
    int argc, PyDosObj far * far *argv)
{
    int id = argc > 0 ? monitor_tool_id(argv[0]) : -1;
    if (id < 0) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"invalid monitoring tool id");
        return (PyDosObj far *)0;
    }
    monitoring_clear_tool(id);
    return pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API monitoring_register_callback(
    int argc, PyDosObj far * far *argv)
{
    int id = argc > 0 ? monitor_tool_id(argv[0]) : -1;
    long event;
    PyDosObj far **slot;
    PyDosObj far *previous;
    PyDosObj far *callback;
    if (id < 0 || argc < 3 || argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[1]->type != PYDT_INT) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"invalid monitoring callback");
        return (PyDosObj far *)0;
    }
    event = argv[1]->v.int_val;
    if (event == PYDOS_MONITOR_PY_START)
        slot = &monitor_start_callbacks[id];
    else if (event == PYDOS_MONITOR_PY_RETURN)
        slot = &monitor_return_callbacks[id];
    else {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"unsupported monitoring event");
        return (PyDosObj far *)0;
    }
    previous = *slot;
    if (previous != (PyDosObj far *)0) PYDOS_INCREF(previous);
    callback = (PyDosType)argv[2]->type == PYDT_NONE
               ? (PyDosObj far *)0 : argv[2];
    monitor_replace_owned_ref(slot, callback);
    monitoring_recompute_active();
    return previous != (PyDosObj far *)0
           ? previous : pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API monitoring_set_local_events(
    int argc, PyDosObj far * far *argv)
{
    int id = argc > 0 ? monitor_tool_id(argv[0]) : -1;
    long events;
    int i;
    int free_slot = -1;
    if (id < 0 || argc < 3 || argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[1]->type != PYDT_CODE ||
        argv[2] == (PyDosObj far *)0 ||
        (PyDosType)argv[2]->type != PYDT_INT) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"invalid local monitoring events");
        return (PyDosObj far *)0;
    }
    events = argv[2]->v.int_val;
    for (i = 0; i < PYDOS_MONITOR_LOCAL_COUNT; i++) {
        if (monitor_locals[i].code == (PyDosObj far *)0) {
            if (free_slot < 0) free_slot = i;
        } else if ((int)monitor_locals[i].tool_id == id &&
                   monitor_locals[i].code == argv[1]) {
            free_slot = i;
            break;
        }
    }
    if (free_slot < 0) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"too many local monitoring registrations");
        return (PyDosObj far *)0;
    }
    if (events == 0L) {
        monitor_replace_owned_ref(&monitor_locals[free_slot].code,
                          (PyDosObj far *)0);
        monitor_locals[free_slot].events = 0L;
    } else {
        monitor_replace_owned_ref(&monitor_locals[free_slot].code, argv[1]);
        monitor_locals[free_slot].tool_id = (unsigned char)id;
        monitor_locals[free_slot].events = events;
    }
    monitoring_recompute_active();
    return pydos_obj_new_none();
}

static void monitoring_dispatch(void (far *code)(void),
                                PyDosObj far *value, long event)
{
    int i;
    for (i = 0; i < PYDOS_MONITOR_LOCAL_COUNT; i++) {
        int tool;
        PyDosObj far *callback;
        PyDosObj far *offset;
        PyDosObj far *args[3];
        PyDosObj far *result;
        if (monitor_locals[i].code == (PyDosObj far *)0 ||
            !(monitor_locals[i].events & event) ||
            pydos_code_ref_native_entry(
                monitor_locals[i].code->v.code.code_ref) != code) continue;
        tool = (int)monitor_locals[i].tool_id;
        callback = event == PYDOS_MONITOR_PY_START
                   ? monitor_start_callbacks[tool]
                   : monitor_return_callbacks[tool];
        if (callback == (PyDosObj far *)0) continue;
        offset = pydos_obj_new_int(0L);
        args[0] = monitor_locals[i].code;
        args[1] = offset;
        args[2] = value;
        result = pydos_obj_call(callback,
            event == PYDOS_MONITOR_PY_START ? 2U : 3U, args);
        PYDOS_DECREF(offset);
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
        if (pydos_exc_pending()) return;
    }
}

void PYDOS_API pydos_monitoring_py_start(void (far *code)(void))
{
    if (pydos_monitoring_active)
        monitoring_dispatch(code, (PyDosObj far *)0,
                            PYDOS_MONITOR_PY_START);
}

void PYDOS_API pydos_monitoring_py_return(void (far *code)(void),
                                          PyDosObj far *value)
{
    if (pydos_monitoring_active)
        monitoring_dispatch(code, value, PYDOS_MONITOR_PY_RETURN);
}

static void monitoring_add_method(PyDosObj far *cls,
                                  const char far *name,
                                  void (far *code)(void))
{
    PyDosObj far *function = pydos_func_new_builtin(code, name);
    if (function == (PyDosObj far *)0) return;
    pydos_obj_set_attr(cls, name, function);
    PYDOS_DECREF(function);
}

PyDosObj far * PYDOS_API pydos_monitoring_new(void)
{
    PyDosObj far *cls = pydos_class_new(
        (const char far *)"monitoring", (PyDosVTable far *)0);
    PyDosObj far *monitoring;
    PyDosObj far *events;
    PyDosObj far *value;
    if (cls == (PyDosObj far *)0) return (PyDosObj far *)0;
    monitoring_add_method(cls, (const char far *)"get_tool",
                          (void (far *)(void))monitoring_get_tool);
    monitoring_add_method(cls, (const char far *)"use_tool_id",
                          (void (far *)(void))monitoring_use_tool_id);
    monitoring_add_method(cls, (const char far *)"free_tool_id",
                          (void (far *)(void))monitoring_free_tool_id);
    monitoring_add_method(cls, (const char far *)"register_callback",
                          (void (far *)(void))monitoring_register_callback);
    monitoring_add_method(cls, (const char far *)"set_local_events",
                          (void (far *)(void))monitoring_set_local_events);
    monitoring = pydos_instance_new(cls);
    PYDOS_DECREF(cls);
    if (monitoring == (PyDosObj far *)0) return (PyDosObj far *)0;
    events = pydos_obj_alloc_type(PYDT_INSTANCE);
    if (events == (PyDosObj far *)0) {
        PYDOS_DECREF(monitoring);
        return (PyDosObj far *)0;
    }
    value = pydos_obj_new_int(0L);
    pydos_obj_set_attr(events, (const char far *)"NO_EVENTS", value);
    PYDOS_DECREF(value);
    value = pydos_obj_new_int(PYDOS_MONITOR_PY_START);
    pydos_obj_set_attr(events, (const char far *)"PY_START", value);
    PYDOS_DECREF(value);
    value = pydos_obj_new_int(PYDOS_MONITOR_PY_RETURN);
    pydos_obj_set_attr(events, (const char far *)"PY_RETURN", value);
    PYDOS_DECREF(value);
    pydos_obj_set_attr(monitoring, (const char far *)"events", events);
    PYDOS_DECREF(events);
    return monitoring;
}
