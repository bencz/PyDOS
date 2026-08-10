/*
 * pydos_obj.c - Universal object type implementation for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_obj.h"
#include "pdos_mem.h"
#include "pdos_gc.h"
#include "pdos_dic.h"
#include "pdos_str.h"
#include "pdos_int.h"
#include "pdos_lst.h"
#include "pdos_vtb.h"
#include "pdos_exc.h"
#include "pdos_gen.h"
#include "pdos_fzs.h"
#include "pdos_cpx.h"
#include "pdos_bya.h"
#include "pdos_byt.h"
#include "pdos_rng.h"
#include "pdos_blt.h"
#include "pdos_mon.h"
#include "pdos_cod.h"
#include "pdos_rt.h"
#ifndef PYDOS_32BIT
#include <malloc.h>
#endif
#include <dos.h>

/* Placeholder reused by type.__new__ while a statically compiled class is
 * passing through the general metaclass protocol.  DOS execution is
 * single-threaded, and nested class construction saves/restores the value. */
static PyDosObj far *pydos_pending_class = (PyDosObj far *)0;
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#ifdef PYDOS_DEBUG_CMP
#include "pdos_io.h"
/* Unbuffered debug print via INT 21h — bypasses C library stdio */
static void dbg_puts(const char *s)
{
    while (*s) {
        pydos_dos_putchar(*s);
        s++;
    }
}
static void dbg_putlong(long v)
{
    char buf[12];
    int i = 0;
    unsigned long uv;
    if (v < 0) {
        pydos_dos_putchar('-');
        uv = (unsigned long)(-(v + 1)) + 1UL;
    } else {
        uv = (unsigned long)v;
    }
    if (uv == 0) {
        pydos_dos_putchar('0');
        return;
    }
    while (uv > 0) {
        buf[i++] = (char)('0' + (int)(uv % 10));
        uv /= 10;
    }
    while (i > 0) {
        pydos_dos_putchar(buf[--i]);
    }
}
static void dbg_putint(int v)
{
    dbg_putlong((long)v);
}
#endif

/* ------------------------------------------------------------------ */
/* Small integer cache: values -1 .. 127  (129 entries)                */
/* ------------------------------------------------------------------ */
#define SMALL_INT_MIN   (-1)
#define SMALL_INT_MAX   127
#define SMALL_INT_COUNT 129  /* 127 - (-1) + 1 */

static PyDosObj small_ints[SMALL_INT_COUNT];
static int small_ints_ready = 0;

/* ------------------------------------------------------------------ */
/* Singletons: None, True, False                                       */
/* ------------------------------------------------------------------ */
static PyDosObj singleton_none;
static PyDosObj singleton_notimplemented;
static PyDosObj singleton_true;
static PyDosObj singleton_false;
static PyDosObj singleton_empty_tuple;

static void param_spec_retain(PyDosParamSpec far *spec)
{
    if (spec != (PyDosParamSpec far *)0 && spec->refcount != 0xFFFFU)
        spec->refcount++;
}

static void param_spec_release(PyDosParamSpec far *spec)
{
    if (spec == (PyDosParamSpec far *)0 || spec->refcount == 0xFFFFU)
        return;
    if (--spec->refcount == 0) pydos_far_free(spec);
}

static PyDosParamSpec far *param_spec_alloc(unsigned int names_len,
                                             unsigned long flags)
{
    unsigned long size = (unsigned long)sizeof(PyDosParamSpec) + names_len;
    PyDosParamSpec far *spec = (PyDosParamSpec far *)pydos_mem_alloc(
        PYDOS_MEM_METADATA, size);
    if (spec == (PyDosParamSpec far *)0) return (PyDosParamSpec far *)0;
    spec->refcount = 1;
    spec->names_len = names_len;
    spec->flags = flags;
    spec->names[names_len] = '\0';
    return spec;
}

static unsigned int function_param_flags(PyDosObj far *func,
                                         unsigned int index)
{
    if (func == (PyDosObj far *)0 ||
        func->v.func.param_spec == (PyDosParamSpec far *)0 || index >= 8)
        return 0;
    return (unsigned int)((func->v.func.param_spec->flags >>
                           (index * 4U)) & 0x0FUL);
}

static int function_param_name_equal(PyDosObj far *func,
                                     unsigned int index,
                                     PyDosObj far *name)
{
    PyDosParamSpec far *spec;
    unsigned int current = 0;
    unsigned int start = 0;
    unsigned int pos;
    unsigned int len;
    if (func == (PyDosObj far *)0 || name == (PyDosObj far *)0 ||
        (PyDosType)name->type != PYDT_STR)
        return 0;
    spec = func->v.func.param_spec;
    if (spec == (PyDosParamSpec far *)0) return 0;
    for (pos = 0; pos <= spec->names_len; pos++) {
        if (pos == spec->names_len || spec->names[pos] == ',') {
            if (current == index) {
                len = pos - start;
                return len == name->v.str.len &&
                    _fmemcmp(spec->names + start, name->v.str.data, len) == 0;
            }
            current++;
            start = pos + 1;
        }
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* Free list for quick re-use of PyDosObj allocations                  */
/* ------------------------------------------------------------------ */
#ifdef PYDOS_32BIT
#define FREE_LIST_MAX 256
#else
/* Conventional memory is shared with code, stack and far heap.  A large
 * permanent object cache is counterproductive for substantial 8086 apps. */
#define FREE_LIST_MAX 64
#endif
#define PYDOS_CLASS_MAX_BASES 8
#define PYDOS_CLASS_MAX_MRO   32

static PyDosObj far *free_list[FREE_LIST_MAX];
static unsigned int  free_count = 0;
static unsigned int  suppress_getattr_fallback = 0;

/* ------------------------------------------------------------------ */
/* Internal helpers                                                    */
/* ------------------------------------------------------------------ */

static PyDosObj far *class_attr_lookup(PyDosObj far *cls,
                                        const char far *attr_name);
static PyDosObj far *dict_attr_lookup(PyDosObj far *dict,
                                      const char far *attr_name);
static void raise_missing_attribute(PyDosObj far *obj,
                                    const char far *attr_name);
static PyDosObj far *pydos_obj_get_attr_default(
    PyDosObj far *obj, const char far *attr_name);
static void pydos_obj_set_attr_default(
    PyDosObj far *obj, const char far *attr_name, PyDosObj far *value);
static void pydos_obj_del_attr_default(
    PyDosObj far *obj, const char far *attr_name);
static PyDosObj far *call_vtable_method(void (far *mfunc)(void),
                                        unsigned int argc,
                                        PyDosObj far * far *argv);
static PyDosObj far *materialize_function_code(PyDosObj far *func);

static PyDosObj far *call_materialized_instance_method(
    PyDosObj far *instance, const char far *name,
    unsigned int argc, PyDosObj far * far *argv)
{
    PyDosObj far *function;
    PyDosObj far *bound;
    PyDosObj far *result;
    if (instance == (PyDosObj far *)0 ||
        (PyDosType)instance->type != PYDT_INSTANCE ||
        instance->v.instance.cls == (PyDosObj far *)0)
        return (PyDosObj far *)0;
    function = class_attr_lookup(instance->v.instance.cls, name);
    if (function == (PyDosObj far *)0) return (PyDosObj far *)0;
    if ((PyDosType)function->type != PYDT_FUNCTION) {
        PYDOS_DECREF(function);
        return (PyDosObj far *)0;
    }
    bound = pydos_func_bind(function, instance);
    PYDOS_DECREF(function);
    if (bound == (PyDosObj far *)0) return (PyDosObj far *)0;
    result = pydos_obj_call(bound, argc, argv);
    PYDOS_DECREF(bound);
    return result;
}
static PyDosMethodSlot far *class_own_method_slot(
                                          PyDosObj far *cls,
                                          const char far *method_name);
static int descriptor_has_slot(PyDosObj far *descriptor, int slot);
static PyDosObj far *descriptor_call_get(PyDosObj far *descriptor,
                                         PyDosObj far *instance,
                                         PyDosObj far *owner);
static int descriptor_call_set(PyDosObj far *descriptor,
                               PyDosObj far *instance,
                               PyDosObj far *value);
static int descriptor_call_delete(PyDosObj far *descriptor,
                                  PyDosObj far *instance);

static int slot_string_matches(PyDosObj far *owner,
                               PyDosObj far *slot,
                               const char far *attr_name)
{
    const char far *slot_name;
    unsigned int slot_len;

    if (owner == (PyDosObj far *)0 || slot == (PyDosObj far *)0 ||
        (PyDosType)owner->type != PYDT_CLASS ||
        (PyDosType)slot->type != PYDT_STR)
        return 0;
    slot_name = slot->v.str.data;
    slot_len = slot->v.str.len;
    if (_fstrcmp(slot_name, attr_name) == 0) return 1;

    /* CPython applies private-name mangling to strings in __slots__. */
    if (slot_len >= 3 && slot_name[0] == '_' && slot_name[1] == '_' &&
        !(slot_name[slot_len - 1] == '_' &&
          slot_name[slot_len - 2] == '_')) {
        char mangled[96];
        const char far *class_name = owner->v.cls.name;
        unsigned int pos = 0;
        unsigned int i = 0;
        while (class_name[i] == '_') i++;
        if (class_name[i] == '\0') return 0;
        mangled[pos++] = '_';
        while (class_name[i] != '\0' && pos < 60)
            mangled[pos++] = class_name[i++];
        for (i = 0; i < slot_len && pos < 94; i++)
            mangled[pos++] = slot_name[i];
        mangled[pos] = '\0';
        return _fstrcmp((const char far *)mangled, attr_name) == 0;
    }
    return 0;
}

static int slots_value_contains(PyDosObj far *owner,
                                PyDosObj far *slots,
                                const char far *attr_name)
{
    unsigned int i;
    if (slots == (PyDosObj far *)0) return 0;
    if ((PyDosType)slots->type == PYDT_STR)
        return slot_string_matches(owner, slots, attr_name);
    if ((PyDosType)slots->type != PYDT_TUPLE &&
        (PyDosType)slots->type != PYDT_LIST)
        return 0;
    for (i = 0; i < slots->v.tuple.len; i++) {
        if (slot_string_matches(owner, slots->v.tuple.items[i], attr_name))
            return 1;
    }
    return 0;
}

static int class_instance_has_dict(PyDosObj far *cls)
{
    PyDosObj far *slots;
    int result;
    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS)
        return 1;
    slots = dict_attr_lookup(cls->v.cls.class_attrs,
                             (const char far *)"__slots__");
    if (slots == (PyDosObj far *)0) return 1;
    result = slots_value_contains(cls, slots,
                                  (const char far *)"__dict__");
    PYDOS_DECREF(slots);
    return result;
}

static int class_slots_allow_attr(PyDosObj far *cls,
                                  const char far *attr_name)
{
    PyDosObj far *own_slots;
    unsigned char mi;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS)
        return 1;
    own_slots = dict_attr_lookup(cls->v.cls.class_attrs,
                                 (const char far *)"__slots__");
    if (own_slots == (PyDosObj far *)0) return 1;
    if (slots_value_contains(cls, own_slots,
                             (const char far *)"__dict__")) {
        PYDOS_DECREF(own_slots);
        return 1;
    }
    PYDOS_DECREF(own_slots);

    for (mi = 0; mi < cls->v.cls.mro_len; mi++) {
        PyDosObj far *owner = cls->v.cls.mro[mi];
        PyDosObj far *slots = dict_attr_lookup(
            owner->v.cls.class_attrs, (const char far *)"__slots__");
        int found;
        if (slots == (PyDosObj far *)0) continue;
        found = slots_value_contains(owner, slots, attr_name);
        PYDOS_DECREF(slots);
        if (found) return 1;
    }
    return 0;
}

static void raise_slots_attribute_error(PyDosObj far *obj,
                                        const char far *attr_name)
{
    char message[160];
    const char far *owner_name = obj->v.instance.cls->v.cls.name;
    unsigned int pos = 0;
    unsigned int i;
    message[pos++] = '\'';
    for (i = 0; owner_name[i] != '\0' && pos < 45; i++)
        message[pos++] = owner_name[i];
    _fmemcpy((char far *)(message + pos),
             (const char far *)"' object has no attribute '", 27);
    pos += 27;
    for (i = 0; attr_name[i] != '\0' && pos < 105; i++)
        message[pos++] = attr_name[i];
    message[pos++] = '\'';
    message[pos] = '\0';
    pydos_exc_raise(PYDOS_EXC_ATTRIBUTE_ERROR,
                    (const char far *)message);
}

/* DJB2 hash for far string data */
static unsigned int djb2_hash(const char far *data, unsigned int len)
{
    unsigned int h = 5381U;
    unsigned int i;
    for (i = 0; i < len; i++) {
        h = ((h << 5) + h) + (unsigned char)data[i];  /* h * 33 + c */
    }
    return h;
}

/* Long-to-string helper.  Writes into caller-supplied far buffer.
   Returns number of characters written (excluding NUL). */
static unsigned int ltoa_far(long val, char far *buf, unsigned int bufsz)
{
    char tmp[12];  /* enough for -2147483648\0 */
    unsigned int len;
    unsigned int i;

    ltoa(val, tmp, 10);
    len = (unsigned int)strlen(tmp);
    if (len >= bufsz) {
        len = bufsz - 1;
    }
    for (i = 0; i <= len; i++) {   /* copy including NUL */
        buf[i] = tmp[i];
    }
    return len;
}

/* Double-to-string helper. */
static unsigned int dtoa_far(double val, char far *buf, unsigned int bufsz)
{
    char tmp[48];
    char candidate[48];
    unsigned int len;
    unsigned int i;
    int precision;

    tmp[0] = '\0';
    for (precision = 1; precision <= 17; precision++) {
        double parsed;
        sprintf(candidate, "%.*g", precision, val);
        parsed = strtod(candidate, (char **)0);
        strcpy(tmp, candidate);
        if (parsed == val) break;
    }

    /* Some DOS printf implementations pad a decimal exponent to three
     * digits (e-016).  Python uses no redundant leading zero (e-16). */
    {
        char *exponent = strchr(tmp, 'e');
        if (exponent == (char *)0) exponent = strchr(tmp, 'E');
        if (exponent != (char *)0) {
            char *digits = exponent + 1;
            if (*digits == '+' || *digits == '-') digits++;
            while (digits[0] == '0' && digits[1] != '\0') {
                memmove(digits, digits + 1, strlen(digits));
            }
        }
    }
    len = (unsigned int)strlen(tmp);
    if (len >= bufsz) {
        len = bufsz - 1;
    }
    for (i = 0; i <= len; i++) {
        buf[i] = tmp[i];
    }
    return len;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_alloc_scalar - allocate an untracked scalar object         */
/* ------------------------------------------------------------------ */
static PyDosObj far *pydos_obj_alloc_scalar(void)
{
    PyDosObj far *obj;

    /* Try the free list first */
    if (free_count > 0) {
        free_count--;
        obj = free_list[free_count];
        _fmemset(obj, 0, sizeof(PyDosObj));
        obj->refcount = 1;
        return obj;
    }

    /* Allocate from far heap */
    obj = (PyDosObj far *)pydos_mem_alloc(
        PYDOS_MEM_OBJECT, (unsigned long)sizeof(PyDosObj));
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    _fmemset(obj, 0, sizeof(PyDosObj));
    obj->refcount = 1;
    return obj;
}

/* Allocate an object with its final runtime type.  Types that can own
 * PyDosObj references must use the prefixed GC allocation and are linked
 * before the constructor performs any nested allocation. */
PyDosObj far * PYDOS_API pydos_obj_alloc_type(unsigned int type)
{
    PyDosObj far *obj;

    if (type >= PYDT_MAX) return (PyDosObj far *)0;

    if (pydos_gc_is_tracked_type(type)) {
        return pydos_gc_alloc_type(type);
    }

    obj = pydos_obj_alloc_scalar();
    if (obj != (PyDosObj far *)0) obj->type = (unsigned char)type;
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_release_data — free only internal/child data, not obj     */
/* Called by pydos_obj_free and by the GC sweep (which frees the       */
/* GCHeader+PyDosObj block separately).                                */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_release_data(PyDosObj far *obj)
{
    unsigned int i;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    switch ((PyDosType)obj->type) {
    case PYDT_STR:
        if (obj->v.str.data != (char far *)0) {
            pydos_far_free(obj->v.str.data);
        }
        break;

    case PYDT_LIST:
        if (obj->v.list.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.list.len; i++) {
                PYDOS_DECREF(obj->v.list.items[i]);
            }
            pydos_far_free(obj->v.list.items);
        }
        break;

    case PYDT_DICT:
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    PYDOS_DECREF(obj->v.dict.entries[i].key);
                    PYDOS_DECREF(obj->v.dict.entries[i].value);
                }
            }
            pydos_far_free(obj->v.dict.entries);
        }
        break;

    case PYDT_TUPLE:
        if (obj->v.tuple.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.tuple.len; i++) {
                PYDOS_DECREF(obj->v.tuple.items[i]);
            }
            pydos_far_free(obj->v.tuple.items);
        }
        break;

    case PYDT_INSTANCE:
        if (obj->v.instance.attrs != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.instance.attrs);
        }
        if (obj->v.instance.cls != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.instance.cls);
        }
        if (obj->v.instance.native_storage != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.instance.native_storage);
        }
        break;

    case PYDT_FUNCTION:
        pydos_code_ref_release(obj->v.func.code_ref);
        if (obj->v.func.defaults != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.defaults);
        }
        if (obj->v.func.closure != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.closure);
        }
        if (obj->v.func.bound_self != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.bound_self);
        }
        if (obj->v.func.attrs != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.attrs);
        }
        param_spec_release(obj->v.func.param_spec);
        if (obj->v.func.code_obj != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.func.code_obj);
        }
        break;

    case PYDT_CELL:
        if (obj->v.cell.value != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.cell.value);
        }
        break;

    case PYDT_GENERATOR:
    case PYDT_COROUTINE:
        if (obj->v.gen.state != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.gen.state);
        }
        if (obj->v.gen.locals != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.gen.locals);
        }
        if (obj->v.gen.vm_stack != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.gen.vm_stack);
        }
        if (obj->v.gen.vm_closure != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.gen.vm_closure);
        }
        pydos_code_ref_release(obj->v.gen.code_ref);
        break;

    case PYDT_EXCEPTION:
        if (obj->v.exc.message != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.message);
        }
        if (obj->v.exc.value != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.value);
        }
        if (obj->v.exc.traceback != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.traceback);
        }
        if (obj->v.exc.cause != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.exc.cause);
        }
        break;

    case PYDT_TRACEBACK:
        if (obj->v.traceback.next != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.traceback.next);
        }
        break;

    case PYDT_TYPE_PARAM:
        PYDOS_DECREF(obj->v.type_param.name);
        PYDOS_DECREF(obj->v.type_param.bound);
        PYDOS_DECREF(obj->v.type_param.constraints);
        PYDOS_DECREF(obj->v.type_param.bound_thunk);
        break;

    case PYDT_TYPE_ALIAS:
        PYDOS_DECREF(obj->v.type_alias.name);
        PYDOS_DECREF(obj->v.type_alias.type_params);
        PYDOS_DECREF(obj->v.type_alias.value);
        break;

    case PYDT_GENERIC_ALIAS:
        PYDOS_DECREF(obj->v.generic_alias.origin);
        PYDOS_DECREF(obj->v.generic_alias.args);
        break;

    case PYDT_CODE:
        PYDOS_DECREF(obj->v.code.freevars);
        PYDOS_DECREF(obj->v.code.consts);
        pydos_code_ref_release(obj->v.code.code_ref);
        break;

    case PYDT_SUPER:
        PYDOS_DECREF(obj->v.super_obj.start_type);
        PYDOS_DECREF(obj->v.super_obj.bound_obj);
        break;

    case PYDT_MEMORYVIEW:
        PYDOS_DECREF(obj->v.memoryview.source);
        PYDOS_DECREF(obj->v.memoryview.exporter);
        break;

    case PYDT_CLASS:
        if (obj->v.cls.bases != (PyDosObj far * far *)0) {
            for (i = 0; i < (unsigned int)obj->v.cls.num_bases; i++) {
                PYDOS_DECREF(obj->v.cls.bases[i]);
            }
            pydos_far_free(obj->v.cls.bases);
        }
        if (obj->v.cls.mro != (PyDosObj far * far *)0) {
            pydos_far_free(obj->v.cls.mro);
        }
        if (obj->v.cls.class_attrs != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.cls.class_attrs);
        }
        if (obj->v.cls.metaclass != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.cls.metaclass);
        }
        break;

    case PYDT_FILE:
        if (obj->v.file.buffer != (char far *)0) {
            pydos_far_free(obj->v.file.buffer);
        }
        break;

    case PYDT_EXC_GROUP:
        if (obj->v.excgroup.message != (PyDosObj far *)0) {
            PYDOS_DECREF(obj->v.excgroup.message);
        }
        if (obj->v.excgroup.exceptions != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.excgroup.count; i++) {
                PYDOS_DECREF(obj->v.excgroup.exceptions[i]);
            }
            pydos_far_free(obj->v.excgroup.exceptions);
        }
        break;

    case PYDT_SET:
        /* Set uses same dict layout — free keys (values are None singletons) */
        if (obj->v.dict.entries != (PyDosDictEntry far *)0) {
            for (i = 0; i < obj->v.dict.size; i++) {
                if (obj->v.dict.entries[i].key != (PyDosObj far *)0) {
                    PYDOS_DECREF(obj->v.dict.entries[i].key);
                    PYDOS_DECREF(obj->v.dict.entries[i].value);
                }
            }
            pydos_far_free(obj->v.dict.entries);
        }
        break;

    case PYDT_FROZENSET:
        if (obj->v.frozenset.items != (PyDosObj far * far *)0) {
            for (i = 0; i < obj->v.frozenset.len; i++) {
                PYDOS_DECREF(obj->v.frozenset.items[i]);
            }
            pydos_far_free(obj->v.frozenset.items);
        }
        break;

    case PYDT_BYTES:
        if (obj->v.str.data != (char far *)0) {
            pydos_far_free(obj->v.str.data);
        }
        break;

    case PYDT_BYTEARRAY:
        if (obj->v.bytearray.data != (unsigned char far *)0) {
            pydos_far_free(obj->v.bytearray.data);
        }
        break;

    case PYDT_RANGE:
    case PYDT_NONE:
    case PYDT_NOTIMPLEMENTED:
    case PYDT_BOOL:
    case PYDT_INT:
    case PYDT_FLOAT:
    case PYDT_COMPLEX:
        /* No internal heap data to free */
        break;

    default:
        break;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_free — release an object and its internal data            */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_free(PyDosObj far *obj)
{
    int gc_tracked;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    /* Never free immortal objects */
    if (obj->flags & OBJ_FLAG_IMMORTAL) {
        return;
    }

    gc_tracked = (obj->flags & OBJ_FLAG_GC_TRACKED) != 0;
    if (gc_tracked) pydos_gc_untrack(obj);

    /* Free type-specific internal data.  A tracked object is already
     * unlinked so recursive child destruction cannot leave a stale header. */
    pydos_obj_release_data(obj);

    if (gc_tracked) {
        pydos_gc_free_object(obj);
        return;
    }

    /* Return to free list or release back to far heap */
    if (free_count < FREE_LIST_MAX) {
        free_list[free_count] = obj;
        free_count++;
    } else {
        pydos_far_free(obj);
    }
}

/* ------------------------------------------------------------------ */
/* Constructors                                                        */
/* ------------------------------------------------------------------ */

PyDosObj far * PYDOS_API pydos_obj_new_none(void)
{
    PYDOS_INCREF((PyDosObj far *)&singleton_none);
    return (PyDosObj far *)&singleton_none;
}

PyDosObj far * PYDOS_API pydos_obj_new_notimplemented(void)
{
    PYDOS_INCREF((PyDosObj far *)&singleton_notimplemented);
    return (PyDosObj far *)&singleton_notimplemented;
}

PyDosObj far * PYDOS_API pydos_obj_new_empty_tuple(void)
{
    PYDOS_INCREF((PyDosObj far *)&singleton_empty_tuple);
    return (PyDosObj far *)&singleton_empty_tuple;
}

PyDosObj far * PYDOS_API pydos_obj_new_bool(int val)
{
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[BOOL ");
    dbg_putint(val);
    dbg_puts("]\r\n");
#endif
    if (val) {
        PYDOS_INCREF((PyDosObj far *)&singleton_true);
        return (PyDosObj far *)&singleton_true;
    }
    PYDOS_INCREF((PyDosObj far *)&singleton_false);
    return (PyDosObj far *)&singleton_false;
}

PyDosObj far * PYDOS_API pydos_obj_new_int(long val)
{
    PyDosObj far *obj;
    int idx;

    /* Check small integer cache */
    if (small_ints_ready && val >= SMALL_INT_MIN && val <= SMALL_INT_MAX) {
        idx = (int)(val - SMALL_INT_MIN);
        obj = (PyDosObj far *)&small_ints[idx];
        PYDOS_INCREF(obj);
        return obj;
    }

    obj = pydos_obj_alloc_type(PYDT_INT);
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    obj->v.int_val = val;
    return obj;
}

PyDosObj far * PYDOS_API pydos_obj_new_float(double val)
{
    PyDosObj far *obj;

    obj = pydos_obj_alloc_type(PYDT_FLOAT);
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }
    obj->v.float_val = val;
    return obj;
}

static int slice_index_value(PyDosObj far *value, long *result,
                             unsigned char *present)
{
    if (value == (PyDosObj far *)0 || (PyDosType)value->type == PYDT_NONE) {
        *present = 0;
        *result = 0L;
        return 1;
    }
    if ((PyDosType)value->type == PYDT_INT) {
        *present = 1;
        *result = value->v.int_val;
        return 1;
    }
    if ((PyDosType)value->type == PYDT_BOOL) {
        *present = 1;
        *result = (long)value->v.bool_val;
        return 1;
    }
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"slice indices must be integers or None");
    return 0;
}

PyDosObj far * PYDOS_API pydos_obj_new_slice(PyDosObj far *start,
                                               PyDosObj far *stop,
                                               PyDosObj far *step)
{
    PyDosObj far *slice;
    slice = pydos_obj_alloc_type(PYDT_SLICE);
    if (slice == (PyDosObj far *)0) return (PyDosObj far *)0;
    if (!slice_index_value(start, &slice->v.slice.start,
                           &slice->v.slice.has_start) ||
        !slice_index_value(stop, &slice->v.slice.stop,
                           &slice->v.slice.has_stop) ||
        !slice_index_value(step, &slice->v.slice.step,
                           &slice->v.slice.has_step)) {
        PYDOS_DECREF(slice);
        return (PyDosObj far *)0;
    }
    if (slice->v.slice.has_step && slice->v.slice.step == 0L) {
        PYDOS_DECREF(slice);
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"slice step cannot be zero");
        return (PyDosObj far *)0;
    }
    return slice;
}

PyDosObj far * PYDOS_API pydos_memoryview_new(PyDosObj far *source,
                                               PyDosObj far *exporter)
{
    PyDosObj far *view;
    if (source == (PyDosObj far *)0 ||
        ((PyDosType)source->type != PYDT_BYTES &&
         (PyDosType)source->type != PYDT_BYTEARRAY &&
         (PyDosType)source->type != PYDT_MEMORYVIEW)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"memoryview requires a bytes-like object");
        return (PyDosObj far *)0;
    }
    view = pydos_obj_alloc_type(PYDT_MEMORYVIEW);
    if (view == (PyDosObj far *)0) return (PyDosObj far *)0;
    view->v.memoryview.source = source;
    view->v.memoryview.exporter = exporter;
    view->v.memoryview.released = 0;
    PYDOS_INCREF(source);
    PYDOS_INCREF(exporter);
    return view;
}

PyDosObj far * PYDOS_API pydos_memoryview_tobytes(PyDosObj far *view)
{
    PyDosObj far *source;
    PyDosObj far *result;
    if (view == (PyDosObj far *)0 ||
        (PyDosType)view->type != PYDT_MEMORYVIEW ||
        view->v.memoryview.released) {
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"operation on released memoryview");
        return (PyDosObj far *)0;
    }
    source = view->v.memoryview.source;
    if ((PyDosType)source->type == PYDT_MEMORYVIEW)
        return pydos_memoryview_tobytes(source);
    if ((PyDosType)source->type == PYDT_BYTES) {
        result = pydos_obj_new_str(source->v.str.data, source->v.str.len);
        if (result != (PyDosObj far *)0) result->type = PYDT_BYTES;
        return result;
    }
    if ((PyDosType)source->type == PYDT_BYTEARRAY) {
        result = pydos_obj_new_str(
            (const char far *)source->v.bytearray.data,
            source->v.bytearray.len);
        if (result != (PyDosObj far *)0) result->type = PYDT_BYTES;
        return result;
    }
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"invalid memoryview source");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_memoryview_release(PyDosObj far *view)
{
    PyDosObj far *source;
    PyDosObj far *exporter;
    if (view == (PyDosObj far *)0 ||
        (PyDosType)view->type != PYDT_MEMORYVIEW) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"release requires a memoryview");
        return (PyDosObj far *)0;
    }
    if (view->v.memoryview.released) return pydos_obj_new_none();
    source = view->v.memoryview.source;
    exporter = view->v.memoryview.exporter;
    if (exporter != (PyDosObj far *)0) {
        PyDosObj far *args[2];
        PyDosObj far *result;
        args[0] = exporter;
        args[1] = source;
        result = pydos_obj_call_method(
            (const char far *)"__release_buffer__", 2, args);
        if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
        PYDOS_DECREF(result);
    }
    if (source != (PyDosObj far *)0 &&
        (PyDosType)source->type == PYDT_MEMORYVIEW) {
        PyDosObj far *result = pydos_memoryview_release(source);
        if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
        PYDOS_DECREF(result);
    }
    view->v.memoryview.released = 1;
    view->v.memoryview.source = (PyDosObj far *)0;
    view->v.memoryview.exporter = (PyDosObj far *)0;
    PYDOS_DECREF(source);
    PYDOS_DECREF(exporter);
    return pydos_obj_new_none();
}

static void replace_owned_ref(PyDosObj far * far *slot, PyDosObj far *value)
{
    if (value != (PyDosObj far *)0) PYDOS_INCREF(value);
    if (*slot != (PyDosObj far *)0) PYDOS_DECREF(*slot);
    *slot = value;
}

PyDosObj far * PYDOS_API pydos_type_param_new(
    PyDosObj far *name, PyDosObj far *kind, PyDosObj far *bound,
    PyDosObj far *constraints, PyDosObj far *bound_thunk)
{
    PyDosObj far *parameter;
    if (name == (PyDosObj far *)0 || (PyDosType)name->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"type parameter name must be str");
        return (PyDosObj far *)0;
    }
    parameter = pydos_obj_alloc_type(PYDT_TYPE_PARAM);
    if (parameter == (PyDosObj far *)0) return (PyDosObj far *)0;
    parameter->v.type_param.name = (PyDosObj far *)0;
    parameter->v.type_param.bound = (PyDosObj far *)0;
    parameter->v.type_param.constraints = (PyDosObj far *)0;
    parameter->v.type_param.bound_thunk = (PyDosObj far *)0;
    parameter->v.type_param.kind = kind != (PyDosObj far *)0 &&
        (PyDosType)kind->type == PYDT_INT
        ? (unsigned char)kind->v.int_val : 0;
    replace_owned_ref(&parameter->v.type_param.name, name);
    replace_owned_ref(&parameter->v.type_param.bound, bound);
    replace_owned_ref(&parameter->v.type_param.constraints, constraints);
    replace_owned_ref(&parameter->v.type_param.bound_thunk, bound_thunk);
    return parameter;
}

PyDosObj far * PYDOS_API pydos_type_alias_new(PyDosObj far *name,
                                               PyDosObj far *type_params)
{
    PyDosObj far *alias;
    alias = pydos_obj_alloc_type(PYDT_TYPE_ALIAS);
    if (alias == (PyDosObj far *)0) return (PyDosObj far *)0;
    alias->v.type_alias.name = (PyDosObj far *)0;
    alias->v.type_alias.type_params = (PyDosObj far *)0;
    alias->v.type_alias.value = (PyDosObj far *)0;
    replace_owned_ref(&alias->v.type_alias.name, name);
    replace_owned_ref(&alias->v.type_alias.type_params, type_params);
    return alias;
}

PyDosObj far * PYDOS_API pydos_type_alias_set_value(PyDosObj far *alias,
                                                     PyDosObj far *value)
{
    if (alias == (PyDosObj far *)0 ||
        (PyDosType)alias->type != PYDT_TYPE_ALIAS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"type alias value owner is invalid");
        return (PyDosObj far *)0;
    }
    replace_owned_ref(&alias->v.type_alias.value, value);
    PYDOS_INCREF(alias);
    return alias;
}

PyDosObj far * PYDOS_API pydos_generic_alias_new(PyDosObj far *origin,
                                                  PyDosObj far *args)
{
    PyDosObj far *alias;
    alias = pydos_obj_alloc_type(PYDT_GENERIC_ALIAS);
    if (alias == (PyDosObj far *)0) return (PyDosObj far *)0;
    alias->v.generic_alias.origin = (PyDosObj far *)0;
    alias->v.generic_alias.args = (PyDosObj far *)0;
    replace_owned_ref(&alias->v.generic_alias.origin, origin);
    replace_owned_ref(&alias->v.generic_alias.args, args);
    return alias;
}

PyDosObj far * PYDOS_API pydos_obj_new_str(const char far *data, unsigned int len)
{
    PyDosObj far *obj;
    char far *buf;

    obj = pydos_obj_alloc_type(PYDT_STR);
    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    /* Allocate buffer for string data + NUL terminator */
    buf = (char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)(len + 1));
    if (buf == (char far *)0) {
        pydos_obj_free(obj);
        return (PyDosObj far *)0;
    }

    if (data != (const char far *)0 && len > 0) {
        _fmemcpy(buf, data, len);
    }
    buf[len] = '\0';

    obj->v.str.data = buf;
    obj->v.str.len = len;
    obj->v.str.hash = djb2_hash(buf, len);
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_is_truthy                                                 */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_is_truthy(PyDosObj far *obj)
{
    int result;

    if (obj == (PyDosObj far *)0) {
        result = 0;
        goto done;
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:
        result = 0; break;
    case PYDT_BOOL:
        result = obj->v.bool_val != 0; break;
    case PYDT_INT:
        result = obj->v.int_val != 0L; break;
    case PYDT_FLOAT:
        result = obj->v.float_val != 0.0; break;
    case PYDT_COMPLEX:
        result = (obj->v.complex_val.real != 0.0 || obj->v.complex_val.imag != 0.0); break;
    case PYDT_STR:
        result = obj->v.str.len > 0; break;
    case PYDT_BYTES:
        result = obj->v.str.len > 0; break;
    case PYDT_LIST:
        result = obj->v.list.len > 0; break;
    case PYDT_DICT:
        result = obj->v.dict.used > 0; break;
    case PYDT_SET:
        result = obj->v.dict.used > 0; break;
    case PYDT_TUPLE:
        result = obj->v.tuple.len > 0; break;
    case PYDT_FROZENSET:
        result = obj->v.frozenset.len > 0; break;
    case PYDT_BYTEARRAY:
        result = obj->v.bytearray.len > 0; break;
    case PYDT_RANGE: {
        /* A range is truthy if it contains at least one element */
        long s = obj->v.range.start;
        long e = obj->v.range.stop;
        long st = obj->v.range.step;
        if (st > 0 && s < e) { result = 1; break; }
        if (st < 0 && s > e) { result = 1; break; }
        result = 0; break;
    }
    case PYDT_INSTANCE:
        /* Check for __bool__ via vtable slot */
        if (obj->v.instance.vtable != (struct PyDosVTable far *)0) {
            typedef PyDosObj far * (PYDOS_API far *BoolFn)(PyDosObj far *);
            BoolFn bool_fn = (BoolFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_BOOL);
            if (bool_fn != (BoolFn)0) {
                PyDosObj far *res = bool_fn(obj);
                if (res != (PyDosObj far *)0) {
                    if ((PyDosType)res->type == PYDT_BOOL) {
                        result = res->v.bool_val != 0;
                    } else {
                        result = pydos_obj_is_truthy(res);
                    }
                } else {
                    result = 0;
                }
                break;
            }
        }
        /* Check for __len__ fallback (Python: empty = falsy) */
        if (obj->v.instance.vtable != (struct PyDosVTable far *)0) {
            typedef PyDosObj far * (PYDOS_API far *LenFn)(PyDosObj far *);
            LenFn len_fn = (LenFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_LEN);
            if (len_fn != (LenFn)0) {
                PyDosObj far *len_res = len_fn(obj);
                if (len_res != (PyDosObj far *)0 &&
                    (PyDosType)len_res->type == PYDT_INT) {
                    result = len_res->v.int_val != 0L;
                } else {
                    result = 1;
                }
                break;
            }
        }
        result = 1; break;  /* no __bool__ or __len__: truthy by default */
    default:
        result = 1; break;  /* functions, classes, etc. are truthy */
    }

done:
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[TRUTHY ");
    dbg_putint(result);
    dbg_puts("]\r\n");
#endif
    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_equal — deep equality comparison                          */
/* ------------------------------------------------------------------ */
/*
 * Ask an instance operand for a verdict through __eq__.  Returns 1 when the
 * method answered, 0 when there is no __eq__ or it returned NotImplemented.
 */
static int instance_equal(PyDosObj far *a, PyDosObj far *b, int *result)
{
    PyDosObj far *args[1];
    PyDosObj far *answer;

    if (a == (PyDosObj far *)0 || (PyDosType)a->type != PYDT_INSTANCE)
        return 0;

    args[0] = b;
    answer = call_materialized_instance_method(
        a, (const char far *)"__eq__", 1, args);
    if (answer != (PyDosObj far *)0) {
        if ((PyDosType)answer->type == PYDT_NOTIMPLEMENTED) {
            PYDOS_DECREF(answer);
        } else {
            *result = pydos_obj_is_truthy(answer);
            PYDOS_DECREF(answer);
            return 1;
        }
    }

    if (a->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vt = a->v.instance.vtable;
        void (far *entry)(void) = pydos_vtable_get_special(vt, VSLOT_EQ);
        if (entry != (void (far *)(void))0) {
            typedef PyDosObj far * (PYDOS_API far *EqFn)(PyDosObj far *,
                                                          PyDosObj far *);
            answer = ((EqFn)entry)(a, b);
            if (answer != (PyDosObj far *)0) {
                if ((PyDosType)answer->type == PYDT_NOTIMPLEMENTED) {
                    PYDOS_DECREF(answer);
                } else {
                    *result = pydos_obj_is_truthy(answer);
                    PYDOS_DECREF(answer);
                    return 1;
                }
            }
        }
    }
    return 0;
}

int PYDOS_API pydos_obj_equal(PyDosObj far *a, PyDosObj far *b)
{
    unsigned int i;

    if (a == b) {
        return 1;
    }
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return 0;
    }

    /* None equality */
    if (a->type == PYDT_NONE && b->type == PYDT_NONE) {
        return 1;
    }

    /* Bool-int interop: Python treats True==1, False==0 */
    if ((a->type == PYDT_BOOL || a->type == PYDT_INT) &&
        (b->type == PYDT_BOOL || b->type == PYDT_INT)) {
        long va, vb;
        va = (a->type == PYDT_BOOL) ? (long)a->v.bool_val : a->v.int_val;
        vb = (b->type == PYDT_BOOL) ? (long)b->v.bool_val : b->v.int_val;
        return va == vb;
    }

    /* Int-float interop */
    if ((a->type == PYDT_INT || a->type == PYDT_FLOAT) &&
        (b->type == PYDT_INT || b->type == PYDT_FLOAT)) {
        double da, db;
        da = (a->type == PYDT_INT) ? (double)a->v.int_val : a->v.float_val;
        db = (b->type == PYDT_INT) ? (double)b->v.int_val : b->v.float_val;
        return da == db;
    }

    /* A user __eq__ decides for operands of any type, so it runs before the
     * type check.  The reflected operand gets its turn on NotImplemented. */
    if ((PyDosType)a->type == PYDT_INSTANCE ||
        (PyDosType)b->type == PYDT_INSTANCE) {
        int verdict = 0;
        if (instance_equal(a, b, &verdict)) return verdict;
        if (instance_equal(b, a, &verdict)) return verdict;
    }

    /* Different types beyond numeric are never equal */
    if (a->type != b->type) {
        return 0;
    }

    switch ((PyDosType)a->type) {
    case PYDT_STR:
    case PYDT_BYTES:
        if (a->v.str.len != b->v.str.len) {
            return 0;
        }
        if (a->v.str.hash != b->v.str.hash) {
            return 0;
        }
        return _fmemcmp(a->v.str.data, b->v.str.data, a->v.str.len) == 0;

    case PYDT_TUPLE:
        if (a->v.tuple.len != b->v.tuple.len) {
            return 0;
        }
        for (i = 0; i < a->v.tuple.len; i++) {
            if (!pydos_obj_equal(a->v.tuple.items[i], b->v.tuple.items[i])) {
                return 0;
            }
        }
        return 1;

    case PYDT_LIST:
        if (a->v.list.len != b->v.list.len) {
            return 0;
        }
        for (i = 0; i < a->v.list.len; i++) {
            if (!pydos_obj_equal(a->v.list.items[i], b->v.list.items[i])) {
                return 0;
            }
        }
        return 1;

    case PYDT_DICT: {
        PyDosObj far *keys;
        if (a->v.dict.used != b->v.dict.used) return 0;
        keys = pydos_dict_keys(a);
        if (keys == (PyDosObj far *)0) return 0;
        for (i = 0; i < keys->v.list.len; i++) {
            PyDosObj far *key = keys->v.list.items[i];
            PyDosObj far *left;
            PyDosObj far *right;
            int same;
            if (!pydos_dict_contains(b, key)) {
                PYDOS_DECREF(keys);
                return 0;
            }
            left = pydos_dict_get(a, key);
            right = pydos_dict_get(b, key);
            same = left != (PyDosObj far *)0 &&
                   right != (PyDosObj far *)0 &&
                   pydos_obj_equal(left, right);
            if (left != (PyDosObj far *)0) PYDOS_DECREF(left);
            if (right != (PyDosObj far *)0) PYDOS_DECREF(right);
            if (!same) {
                PYDOS_DECREF(keys);
                return 0;
            }
        }
        PYDOS_DECREF(keys);
        return 1;
    }

    case PYDT_SET: {
        PyDosObj far *keys;
        if (a->v.dict.used != b->v.dict.used) return 0;
        keys = pydos_dict_keys(a);
        if (keys == (PyDosObj far *)0) return 0;
        for (i = 0; i < keys->v.list.len; i++) {
            if (!pydos_dict_contains(b, keys->v.list.items[i])) {
                PYDOS_DECREF(keys);
                return 0;
            }
        }
        PYDOS_DECREF(keys);
        return 1;
    }

    case PYDT_FROZENSET:
        if (a->v.frozenset.len != b->v.frozenset.len) {
            return 0;
        }
        if (a->v.frozenset.hash != b->v.frozenset.hash) {
            return 0;
        }
        for (i = 0; i < a->v.frozenset.len; i++) {
            if (!pydos_obj_equal(a->v.frozenset.items[i],
                                 b->v.frozenset.items[i])) {
                return 0;
            }
        }
        return 1;

    case PYDT_RANGE:
        return pydos_range_equal(a, b);

    case PYDT_COMPLEX:
        if ((PyDosType)b->type != PYDT_COMPLEX) return 0;
        return a->v.complex_val.real == b->v.complex_val.real &&
               a->v.complex_val.imag == b->v.complex_val.imag;

    case PYDT_BYTEARRAY:
        if ((PyDosType)b->type != PYDT_BYTEARRAY) return 0;
        if (a->v.bytearray.len != b->v.bytearray.len) return 0;
        if (a->v.bytearray.len == 0) return 1;
        return _fmemcmp(a->v.bytearray.data, b->v.bytearray.data, a->v.bytearray.len) == 0;

    case PYDT_GENERIC_ALIAS:
        return pydos_obj_equal(a->v.generic_alias.origin,
                               b->v.generic_alias.origin) &&
               pydos_obj_equal(a->v.generic_alias.args,
                               b->v.generic_alias.args);

    case PYDT_INSTANCE:
        /* __eq__ already ran above; without a verdict identity decides. */
        return (a == b);

    default:
        /* Identity comparison for other types */
        return (a == b);
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_hash                                                      */
/* ------------------------------------------------------------------ */
unsigned int PYDOS_API pydos_obj_hash(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return 0;
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:
        return (unsigned int)0x6E65U;  /* "None" hash, 16-bit */
    case PYDT_BOOL:
        return (unsigned int)obj->v.bool_val;
    case PYDT_INT: {
        /* Spread 32-bit long into 16-bit hash */
        unsigned int lo = (unsigned int)(obj->v.int_val & 0xFFFF);
        unsigned int hi = (unsigned int)((obj->v.int_val >> 16) & 0xFFFF);
        return lo ^ (hi * 31);
    }
    case PYDT_FLOAT: {
        /* Hash double by treating its bytes as data */
        unsigned int h;
        h = djb2_hash((const char far *)&obj->v.float_val,
                       (unsigned int)sizeof(double));
        return h;
    }
    case PYDT_STR:
    case PYDT_BYTES:
        return obj->v.str.hash;
    case PYDT_TUPLE: {
        unsigned int h = 0x5678U;
        unsigned int i;
        for (i = 0; i < obj->v.tuple.len; i++) {
            unsigned int ih = pydos_obj_hash(obj->v.tuple.items[i]);
            h = (h ^ ih) * 1000003U;
            h ^= obj->v.tuple.len - i;
        }
        return h;
    }
    case PYDT_FROZENSET:
        return obj->v.frozenset.hash;
    case PYDT_COMPLEX: {
        unsigned int hr, hi;
        union { double d; unsigned int u[2]; } conv;
        conv.d = obj->v.complex_val.real;
        hr = conv.u[0] ^ conv.u[1];
        conv.d = obj->v.complex_val.imag;
        hi = conv.u[0] ^ conv.u[1];
        return hr ^ (hi * 31);
    }
    case PYDT_INSTANCE:
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            typedef PyDosObj far * (PYDOS_API far *HashFn)(PyDosObj far *);
            PyDosObj far *hobj;
            HashFn hfn = (HashFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_HASH);
            if (hfn == (HashFn)0)
                return (unsigned int)((unsigned long)obj & 0xFFFFUL);
            hobj = hfn(obj);
            if (hobj != (PyDosObj far *)0 &&
                (PyDosType)hobj->type == PYDT_INT) {
                unsigned int h = (unsigned int)(hobj->v.int_val & 0xFFFFUL);
                PYDOS_DECREF(hobj);
                return h;
            }
            if (hobj != (PyDosObj far *)0) {
                PYDOS_DECREF(hobj);
            }
        }
        /* fallthrough to address-based hash */
        return (unsigned int)((unsigned long)obj & 0xFFFFUL);
    default:
        /* Unhashable types - use address as fallback */
        return (unsigned int)((unsigned long)obj & 0xFFFFUL);
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_to_str — produce a string representation                  */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_to_str(PyDosObj far *obj)
{
    char far *buf;
    unsigned int len;

    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_str((const char far *)"None", 4);
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:
        return pydos_obj_new_str((const char far *)"None", 4);
    case PYDT_NOTIMPLEMENTED:
        return pydos_obj_new_str((const char far *)"NotImplemented", 14);

    case PYDT_BOOL:
        if (obj->v.bool_val) {
            return pydos_obj_new_str((const char far *)"True", 4);
        }
        return pydos_obj_new_str((const char far *)"False", 5);

    case PYDT_INT:
        buf = (char far *)pydos_mem_alloc(PYDOS_MEM_BUFFER, 16UL);
        if (buf == (char far *)0) {
            return (PyDosObj far *)0;
        }
        len = ltoa_far(obj->v.int_val, buf, 16);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(buf, len);
            pydos_far_free(buf);
            return result;
        }

    case PYDT_FLOAT:
        buf = (char far *)pydos_mem_alloc(PYDOS_MEM_BUFFER, 32UL);
        if (buf == (char far *)0) {
            return (PyDosObj far *)0;
        }
        len = dtoa_far(obj->v.float_val, buf, 32);
        /* Python always prints whole-number floats with .0 suffix */
        {
            int has_dot = 0;
            unsigned int fi;
            for (fi = 0; fi < len; fi++) {
                if (buf[fi] == '.' || buf[fi] == 'e' || buf[fi] == 'E' ||
                    buf[fi] == 'i' || buf[fi] == 'n') {
                    has_dot = 1;
                    break;
                }
            }
            if (!has_dot && len + 2 < 32) {
                buf[len++] = '.';
                buf[len++] = '0';
                buf[len] = '\0';
            }
        }
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(buf, len);
            pydos_far_free(buf);
            return result;
        }

    case PYDT_COMPLEX: {
        double r = obj->v.complex_val.real;
        double i = obj->v.complex_val.imag;
        char rbuf[32], ibuf[32];
        unsigned int rlen, ilen, tlen;
        char far *out;
        unsigned int pos;

        if (r == 0.0 && i >= 0.0) {
            /* Pure imaginary, positive: "3j" or "0j" */
            buf = (char far *)pydos_mem_alloc(PYDOS_MEM_BUFFER, 34UL);
            if (buf == (char far *)0) return (PyDosObj far *)0;
            len = dtoa_far(i, buf, 32);
            buf[len++] = 'j';
            buf[len] = '\0';
            {
                PyDosObj far *result = pydos_obj_new_str(buf, len);
                pydos_far_free(buf);
                return result;
            }
        }
        if (r == 0.0 && i < 0.0) {
            /* Pure imaginary, negative: "-3j" */
            buf = (char far *)pydos_mem_alloc(PYDOS_MEM_BUFFER, 34UL);
            if (buf == (char far *)0) return (PyDosObj far *)0;
            len = dtoa_far(i, buf, 32);
            buf[len++] = 'j';
            buf[len] = '\0';
            {
                PyDosObj far *result = pydos_obj_new_str(buf, len);
                pydos_far_free(buf);
                return result;
            }
        }
        /* General case: "(r+ij)" or "(r-ij)" */
        rlen = (unsigned int)sprintf(rbuf, "%g", r);
        if (i >= 0.0) {
            ilen = (unsigned int)sprintf(ibuf, "%g", i);
            /* "(r+ij)" */
            tlen = 1 + rlen + 1 + ilen + 1 + 1 + 1; /* ( r + i j ) NUL */
            out = (char far *)pydos_mem_alloc(
                PYDOS_MEM_BUFFER, (unsigned long)tlen);
            if (out == (char far *)0) return (PyDosObj far *)0;
            pos = 0;
            out[pos++] = '(';
            _fmemcpy(out + pos, (const char far *)rbuf, rlen); pos += rlen;
            out[pos++] = '+';
            _fmemcpy(out + pos, (const char far *)ibuf, ilen); pos += ilen;
            out[pos++] = 'j';
            out[pos++] = ')';
            out[pos] = '\0';
        } else {
            double ai = -i;
            ilen = (unsigned int)sprintf(ibuf, "%g", ai);
            /* "(r-ij)" */
            tlen = 1 + rlen + 1 + ilen + 1 + 1 + 1;
            out = (char far *)pydos_mem_alloc(
                PYDOS_MEM_BUFFER, (unsigned long)tlen);
            if (out == (char far *)0) return (PyDosObj far *)0;
            pos = 0;
            out[pos++] = '(';
            _fmemcpy(out + pos, (const char far *)rbuf, rlen); pos += rlen;
            out[pos++] = '-';
            _fmemcpy(out + pos, (const char far *)ibuf, ilen); pos += ilen;
            out[pos++] = 'j';
            out[pos++] = ')';
            out[pos] = '\0';
        }
        {
            PyDosObj far *result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_STR:
        /* Return a new reference to the same string content */
        return pydos_obj_new_str(obj->v.str.data, obj->v.str.len);

    case PYDT_LIST: {
        /* Build "[item1, item2, ...]" representation.
         * Two-pass: first convert items + compute exact size,
         * then format into correctly sized buffer. */
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int i;
        unsigned int n = obj->v.list.len;
        PyDosObj far * far *strs;

        if (n == 0) {
            return pydos_obj_new_str((const char far *)"[]", 2);
        }

        /* Pass 1: convert all items to strings and compute exact size */
        strs = (PyDosObj far * far *)pydos_mem_alloc(PYDOS_MEM_METADATA,
            (unsigned long)n * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            return pydos_obj_new_str((const char far *)"[...]", 5);
        }

        alloc_sz = 3; /* '[' + ']' + NUL */
        for (i = 0; i < n; i++) {
            strs[i] = pydos_obj_repr(obj->v.list.items[i]);
            if (strs[i] != (PyDosObj far *)0) {
                alloc_sz += strs[i]->v.str.len;
            }
            if (i > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (i = 0; i < n; i++) {
                if (strs[i]) PYDOS_DECREF(strs[i]);
            }
            pydos_far_free(strs);
            return (PyDosObj far *)0;
        }

        /* Pass 2: format into buffer */
        out[0] = '[';
        pos = 1;
        for (i = 0; i < n; i++) {
            if (i > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (strs[i] != (PyDosObj far *)0) {
                unsigned int slen = strs[i]->v.str.len;
                _fmemcpy(out + pos, strs[i]->v.str.data, slen);
                pos += slen;
                PYDOS_DECREF(strs[i]);
            }
        }
        out[pos++] = ']';
        out[pos] = '\0';

        pydos_far_free(strs);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_TUPLE: {
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int i;
        unsigned int n = obj->v.tuple.len;
        PyDosObj far * far *strs;

        if (n == 0) {
            return pydos_obj_new_str((const char far *)"()", 2);
        }

        /* Pass 1: convert all items to strings and compute exact size */
        strs = (PyDosObj far * far *)pydos_mem_alloc(PYDOS_MEM_METADATA,
            (unsigned long)n * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            return pydos_obj_new_str((const char far *)"(...)", 4);
        }

        alloc_sz = 3; /* '(' + ')' + NUL */
        if (n == 1) alloc_sz++; /* trailing comma */
        for (i = 0; i < n; i++) {
            strs[i] = pydos_obj_repr(obj->v.tuple.items[i]);
            if (strs[i] != (PyDosObj far *)0) {
                alloc_sz += strs[i]->v.str.len;
            }
            if (i > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (i = 0; i < n; i++) {
                if (strs[i]) PYDOS_DECREF(strs[i]);
            }
            pydos_far_free(strs);
            return (PyDosObj far *)0;
        }

        /* Pass 2: format into buffer */
        out[0] = '(';
        pos = 1;
        for (i = 0; i < n; i++) {
            if (i > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (strs[i] != (PyDosObj far *)0) {
                unsigned int slen = strs[i]->v.str.len;
                _fmemcpy(out + pos, strs[i]->v.str.data, slen);
                pos += slen;
                PYDOS_DECREF(strs[i]);
            }
        }
        if (n == 1) out[pos++] = ',';
        out[pos++] = ')';
        out[pos] = '\0';

        pydos_far_free(strs);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_DICT: {
        /* Preserve insertion order, matching the iterator helpers and
         * Python 3.7+.  The old placeholder made every dictionary look
         * recursive, which hid the actual result of dataclasses.asdict(). */
        PyDosObj far *items;
        PyDosObj far * far *strs;
        char far *out;
        unsigned int n;
        unsigned int i;
        unsigned int pos;
        unsigned int alloc_sz;

        if (obj->v.dict.used == 0)
            return pydos_obj_new_str((const char far *)"{}", 2);

        items = pydos_dict_items(obj);
        if (items == (PyDosObj far *)0) return (PyDosObj far *)0;
        n = items->v.list.len;
        strs = (PyDosObj far * far *)pydos_mem_alloc(PYDOS_MEM_METADATA,
            (unsigned long)(n * 2U) * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            PYDOS_DECREF(items);
            return (PyDosObj far *)0;
        }

        alloc_sz = 3; /* braces and NUL */
        for (i = 0; i < n; i++) {
            PyDosObj far *pair = items->v.list.items[i];
            PyDosObj far *key = pair->v.tuple.items[0];
            PyDosObj far *value = pair->v.tuple.items[1];
            strs[i * 2U] = key == obj
                ? pydos_obj_new_str((const char far *)"{...}", 5)
                : pydos_obj_repr(key);
            strs[i * 2U + 1U] = value == obj
                ? pydos_obj_new_str((const char far *)"{...}", 5)
                : pydos_obj_repr(value);
            if (strs[i * 2U] != (PyDosObj far *)0)
                alloc_sz += strs[i * 2U]->v.str.len;
            if (strs[i * 2U + 1U] != (PyDosObj far *)0)
                alloc_sz += strs[i * 2U + 1U]->v.str.len;
            alloc_sz += 2; /* ": " */
            if (i > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (i = 0; i < n * 2U; i++) {
                if (strs[i] != (PyDosObj far *)0) PYDOS_DECREF(strs[i]);
            }
            pydos_far_free(strs);
            PYDOS_DECREF(items);
            return (PyDosObj far *)0;
        }

        out[0] = '{';
        pos = 1;
        for (i = 0; i < n; i++) {
            PyDosObj far *key_str = strs[i * 2U];
            PyDosObj far *value_str = strs[i * 2U + 1U];

            if (i > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (key_str != (PyDosObj far *)0) {
                _fmemcpy(out + pos, key_str->v.str.data,
                         key_str->v.str.len);
                pos += key_str->v.str.len;
                PYDOS_DECREF(key_str);
            }
            out[pos++] = ':';
            out[pos++] = ' ';
            if (value_str != (PyDosObj far *)0) {
                _fmemcpy(out + pos, value_str->v.str.data,
                         value_str->v.str.len);
                pos += value_str->v.str.len;
                PYDOS_DECREF(value_str);
            }
        }
        out[pos++] = '}';
        out[pos] = '\0';
        pydos_far_free(strs);
        PYDOS_DECREF(items);
        {
            PyDosObj far *result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_SET: {
        /* Build "{item1, item2, ...}" representation */
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int si;
        unsigned int first;

        alloc_sz = 3;
        for (si = 0; si < obj->v.dict.size; si++) {
            if (obj->v.dict.entries[si].key != (PyDosObj far *)0) {
                alloc_sz += 20;
            }
        }
        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) {
            return (PyDosObj far *)0;
        }
        out[0] = '{';
        pos = 1;
        first = 1;
        for (si = 0; si < obj->v.dict.size && pos < alloc_sz - 5; si++) {
            if (obj->v.dict.entries[si].key != (PyDosObj far *)0) {
                PyDosObj far *s;
                unsigned int slen;
                if (!first) {
                    out[pos++] = ',';
                    out[pos++] = ' ';
                }
                first = 0;
                s = pydos_obj_to_str(obj->v.dict.entries[si].key);
                if (s != (PyDosObj far *)0) {
                    slen = s->v.str.len;
                    if (pos + slen >= alloc_sz - 2) {
                        slen = alloc_sz - 2 - pos;
                    }
                    _fmemcpy(out + pos, s->v.str.data, slen);
                    pos += slen;
                    PYDOS_DECREF(s);
                }
            }
        }
        out[pos++] = '}';
        out[pos] = '\0';
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_FROZENSET: {
        /* Build "frozenset({elem1, elem2})" or "frozenset()" representation.
         * Two-pass: first convert items + compute exact size,
         * then format into correctly sized buffer. */
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;
        unsigned int fi;
        unsigned int n = obj->v.frozenset.len;
        PyDosObj far * far *strs;

        if (n == 0) {
            return pydos_obj_new_str((const char far *)"frozenset()", 11);
        }

        /* Pass 1: convert all items to strings and compute exact size */
        strs = (PyDosObj far * far *)pydos_mem_alloc(PYDOS_MEM_METADATA,
            (unsigned long)n * sizeof(PyDosObj far *));
        if (strs == (PyDosObj far * far *)0) {
            return pydos_obj_new_str((const char far *)"frozenset({...})", 16);
        }

        /* "frozenset({" = 11, "})" = 2, NUL = 1 => 14 base */
        alloc_sz = 14;
        for (fi = 0; fi < n; fi++) {
            strs[fi] = pydos_obj_to_str(obj->v.frozenset.items[fi]);
            if (strs[fi] != (PyDosObj far *)0) {
                int quote_it = (obj->v.frozenset.items[fi] != (PyDosObj far *)0 &&
                                obj->v.frozenset.items[fi]->type == PYDT_STR);
                alloc_sz += strs[fi]->v.str.len + (quote_it ? 2 : 0);
            }
            if (fi > 0) alloc_sz += 2; /* ", " */
        }

        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) {
            for (fi = 0; fi < n; fi++) {
                if (strs[fi]) PYDOS_DECREF(strs[fi]);
            }
            pydos_far_free(strs);
            return (PyDosObj far *)0;
        }

        /* Pass 2: format into buffer */
        _fmemcpy(out, (const char far *)"frozenset({", 11);
        pos = 11;
        for (fi = 0; fi < n; fi++) {
            if (fi > 0) {
                out[pos++] = ',';
                out[pos++] = ' ';
            }
            if (strs[fi] != (PyDosObj far *)0) {
                int quote_it = (obj->v.frozenset.items[fi] != (PyDosObj far *)0 &&
                                obj->v.frozenset.items[fi]->type == PYDT_STR);
                unsigned int slen = strs[fi]->v.str.len;
                if (quote_it) out[pos++] = '\'';
                _fmemcpy(out + pos, strs[fi]->v.str.data, slen);
                pos += slen;
                if (quote_it) out[pos++] = '\'';
                PYDOS_DECREF(strs[fi]);
            }
        }
        out[pos++] = '}';
        out[pos++] = ')';
        out[pos] = '\0';

        pydos_far_free(strs);
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_FUNCTION:
        if (obj->v.func.name != (const char far *)0) {
            /* Build "<function name>" */
            char far *out;
            unsigned int nlen;
            unsigned int pos;
            const char far *p;

            p = obj->v.func.name;
            nlen = 0;
            while (p[nlen] != '\0') nlen++;

            out = (char far *)pydos_mem_alloc(
                PYDOS_MEM_BUFFER, (unsigned long)(nlen + 12));
            if (out == (char far *)0) {
                return (PyDosObj far *)0;
            }
            _fmemcpy(out, (const char far *)"<function ", 10);
            _fmemcpy(out + 10, obj->v.func.name, nlen);
            pos = 10 + nlen;
            out[pos++] = '>';
            out[pos] = '\0';
            {
                PyDosObj far *result;
                result = pydos_obj_new_str(out, pos);
                pydos_far_free(out);
                return result;
            }
        }
        return pydos_obj_new_str((const char far *)"<function>", 10);

    case PYDT_TYPE_PARAM:
        if (obj->v.type_param.name != (PyDosObj far *)0) {
            PYDOS_INCREF(obj->v.type_param.name);
            return obj->v.type_param.name;
        }
        return pydos_obj_new_str((const char far *)"T", 1);

    case PYDT_TYPE_ALIAS:
        if (obj->v.type_alias.name != (PyDosObj far *)0) {
            PYDOS_INCREF(obj->v.type_alias.name);
            return obj->v.type_alias.name;
        }
        return pydos_obj_new_str((const char far *)"TypeAlias", 9);

    case PYDT_GENERIC_ALIAS: {
        PyDosObj far *origin_repr = pydos_obj_repr(
            obj->v.generic_alias.origin);
        PyDosObj far *args_repr = pydos_obj_repr(obj->v.generic_alias.args);
        PyDosObj far *result;
        unsigned int olen;
        unsigned int alen;
        char far *buffer;
        if (origin_repr == (PyDosObj far *)0 ||
            args_repr == (PyDosObj far *)0) {
            PYDOS_DECREF(origin_repr);
            PYDOS_DECREF(args_repr);
            return (PyDosObj far *)0;
        }
        olen = origin_repr->v.str.len;
        alen = args_repr->v.str.len;
        buffer = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)olen + alen + 3UL);
        if (buffer == (char far *)0) {
            PYDOS_DECREF(origin_repr);
            PYDOS_DECREF(args_repr);
            return (PyDosObj far *)0;
        }
        _fmemcpy(buffer, origin_repr->v.str.data, olen);
        buffer[olen] = '[';
        _fmemcpy(buffer + olen + 1, args_repr->v.str.data, alen);
        buffer[olen + alen + 1] = ']';
        buffer[olen + alen + 2] = '\0';
        result = pydos_obj_new_str(buffer, olen + alen + 2);
        pydos_far_free(buffer);
        PYDOS_DECREF(origin_repr);
        PYDOS_DECREF(args_repr);
        return result;
    }

    case PYDT_CLASS:
        if (obj->v.cls.name != (const char far *)0) {
            char far *out;
            unsigned int nlen;
            unsigned int pos;
            const char far *p;

            p = obj->v.cls.name;
            nlen = 0;
            while (p[nlen] != '\0') nlen++;

            out = (char far *)pydos_mem_alloc(
                PYDOS_MEM_BUFFER, (unsigned long)(nlen + 20));
            if (out == (char far *)0) {
                return (PyDosObj far *)0;
            }
            if (obj->v.cls.runtime_type_tag >= 0) {
                _fmemcpy(out, (const char far *)"<class '", 8);
                _fmemcpy(out + 8, obj->v.cls.name, nlen);
                pos = 8 + nlen;
            } else {
                _fmemcpy(out, (const char far *)"<class '__main__.", 17);
                _fmemcpy(out + 17, obj->v.cls.name, nlen);
                pos = 17 + nlen;
            }
            out[pos++] = '\'';
            out[pos++] = '>';
            out[pos] = '\0';
            {
                PyDosObj far *result;
                result = pydos_obj_new_str(out, pos);
                pydos_far_free(out);
                return result;
            }
        }
        return pydos_obj_new_str((const char far *)"<class>", 7);

    case PYDT_RANGE: {
        char far *out;
        unsigned int pos;

        out = (char far *)pydos_mem_alloc(PYDOS_MEM_BUFFER, 48UL);
        if (out == (char far *)0) {
            return (PyDosObj far *)0;
        }
        _fmemcpy(out, (const char far *)"range(", 6);
        pos = 6;
        pos += ltoa_far(obj->v.range.start, out + pos, 48 - pos);
        out[pos++] = ',';
        out[pos++] = ' ';
        pos += ltoa_far(obj->v.range.stop, out + pos, 48 - pos);
        if (obj->v.range.step != 1L) {
            out[pos++] = ',';
            out[pos++] = ' ';
            pos += ltoa_far(obj->v.range.step, out + pos, 48 - pos);
        }
        out[pos++] = ')';
        out[pos] = '\0';
        {
            PyDosObj far *result;
            result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_INSTANCE:
        {
            PyDosObj far *dynamic_result;
            dynamic_result = call_materialized_instance_method(
                obj, (const char far *)"__str__", 0,
                (PyDosObj far * far *)0);
            if (dynamic_result == (PyDosObj far *)0)
                dynamic_result = call_materialized_instance_method(
                    obj, (const char far *)"__repr__", 0,
                    (PyDosObj far * far *)0);
            if (dynamic_result != (PyDosObj far *)0 &&
                (PyDosType)dynamic_result->type == PYDT_STR)
                return dynamic_result;
            if (dynamic_result != (PyDosObj far *)0)
                PYDOS_DECREF(dynamic_result);
        }
        /* Check for __str__ in vtable */
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            PyDosVTable far *vt = obj->v.instance.vtable;
            void (far *entry)(void) =
                pydos_vtable_get_special(vt, VSLOT_STR);
            if (entry != (void (far *)(void))0) {
                typedef PyDosObj far * (PYDOS_API far *StrFn)(PyDosObj far *);
                return ((StrFn)entry)(obj);
            }
            /* Fallback to __repr__ if no __str__ */
            entry = pydos_vtable_get_special(vt, VSLOT_REPR);
            if (entry != (void (far *)(void))0) {
                typedef PyDosObj far * (PYDOS_API far *ReprFn)(PyDosObj far *);
                return ((ReprFn)entry)(obj);
            }
        }
        /* Match object.__repr__: include the concrete runtime address. */
        if (obj->v.instance.vtable != (PyDosVTable far *)0 &&
            obj->v.instance.vtable->class_name != (const char far *)0) {
            const char far *cn;
            char buf[128];
            char address[24];
            unsigned long linear_address;
            int pos;
            /* prefix: "<__main__." */
            buf[0] = '<';
            buf[1] = '_'; buf[2] = '_'; buf[3] = 'm'; buf[4] = 'a';
            buf[5] = 'i'; buf[6] = 'n'; buf[7] = '_'; buf[8] = '_';
            buf[9] = '.';
            pos = 10;
            cn = obj->v.instance.vtable->class_name;
            while (*cn && pos < 90) {
                buf[pos++] = *cn++;
            }
            /* 8086 far pointers are rendered as their physical linear
             * address; protected-mode pointers are already linear. */
#ifdef PYDOS_32BIT
            linear_address = (unsigned long)obj;
#else
            linear_address = ((unsigned long)FP_SEG(obj) << 4)
                             + (unsigned long)FP_OFF(obj);
#endif
            sprintf(address, "%lx", linear_address);
            buf[pos++] = ' ';
            buf[pos++] = 'o'; buf[pos++] = 'b'; buf[pos++] = 'j';
            buf[pos++] = 'e'; buf[pos++] = 'c'; buf[pos++] = 't';
            buf[pos++] = ' '; buf[pos++] = 'a'; buf[pos++] = 't';
            buf[pos++] = ' '; buf[pos++] = '0'; buf[pos++] = 'x';
            {
                int ai = 0;
                while (address[ai] != '\0' && pos < 126)
                    buf[pos++] = address[ai++];
            }
            buf[pos++] = '>';
            buf[pos] = '\0';
            return pydos_obj_new_str((const char far *)buf, pos);
        }
        return pydos_obj_new_str((const char far *)"<instance>", 10);

    case PYDT_GENERATOR:
        return pydos_obj_new_str((const char far *)"<generator>", 11);

    case PYDT_COROUTINE:
        return pydos_obj_new_str((const char far *)"<coroutine object>", 18);

    case PYDT_EXCEPTION:
        if (obj->v.exc.message != (PyDosObj far *)0 &&
            obj->v.exc.message->type == PYDT_STR) {
            return pydos_obj_new_str(obj->v.exc.message->v.str.data,
                                     obj->v.exc.message->v.str.len);
        }
        return pydos_obj_new_str((const char far *)"Exception", 9);

    case PYDT_TRACEBACK:
        return pydos_obj_new_str((const char far *)"<traceback object>", 18);

    case PYDT_FILE:
        return pydos_obj_new_str((const char far *)"<file>", 6);

    case PYDT_CELL:
        return pydos_obj_new_str((const char far *)"<cell>", 6);

    case PYDT_EXC_GROUP: {
        /* Format: ExceptionGroup('msg', [exc1, exc2, ...]) */
        char buf[128];
        unsigned int pos;
        unsigned int i;
        unsigned int msg_len;

        pos = 0;
        _fmemcpy((char far *)(buf + pos), (const char far *)"ExceptionGroup('", 16);
        pos += 16;

        if (obj->v.excgroup.message != (PyDosObj far *)0 &&
            obj->v.excgroup.message->type == PYDT_STR) {
            msg_len = obj->v.excgroup.message->v.str.len;
            if (pos + msg_len < 100) {
                _fmemcpy((char far *)(buf + pos),
                         obj->v.excgroup.message->v.str.data, msg_len);
                pos += msg_len;
            }
        }

        buf[pos++] = '\'';
        buf[pos++] = ',';
        buf[pos++] = ' ';
        buf[pos++] = '[';

        for (i = 0; i < obj->v.excgroup.count && pos < 120; i++) {
            PyDosObj far *child;
            if (i > 0 && pos < 118) {
                buf[pos++] = ',';
                buf[pos++] = ' ';
            }
            child = obj->v.excgroup.exceptions[i];
            if (child != (PyDosObj far *)0 &&
                (PyDosType)child->type == PYDT_EXCEPTION) {
                const char far *tn;
                unsigned int tn_len;
                int tc = child->v.exc.type_code;
                tn = pydos_exc_type_name(tc);
                tn_len = (unsigned int)_fstrlen(tn);
                if (pos + tn_len + 2 < 125) {
                    _fmemcpy((char far *)(buf + pos), tn, tn_len);
                    pos += tn_len;
                    buf[pos++] = '(';
                    buf[pos++] = ')';
                }
            }
        }

        buf[pos++] = ']';
        buf[pos++] = ')';
        buf[pos] = '\0';

        return pydos_obj_new_str((const char far *)buf, pos);
    }

    case PYDT_BYTES: {
        unsigned int i, n;
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;

        n = obj->v.str.len;
        alloc_sz = 4 + n * 4;
        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) return (PyDosObj far *)0;
        out[0] = 'b';
        out[1] = '\'';
        pos = 2;
        for (i = 0; i < n; i++) {
            unsigned char ch = (unsigned char)obj->v.str.data[i];
            if (ch >= 32 && ch < 127 && ch != '\'' && ch != '\\') {
                out[pos++] = (char)ch;
            } else if (ch == '\'' || ch == '\\') {
                out[pos++] = '\\';
                out[pos++] = (char)ch;
            } else {
                out[pos++] = '\\';
                out[pos++] = 'x';
                out[pos++] = "0123456789abcdef"[ch >> 4];
                out[pos++] = "0123456789abcdef"[ch & 0x0f];
            }
        }
        out[pos++] = '\'';
        out[pos] = '\0';
        {
            PyDosObj far *result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    case PYDT_BYTEARRAY: {
        unsigned int i, n;
        char far *out;
        unsigned int pos;
        unsigned int alloc_sz;

        n = obj->v.bytearray.len;
        /* "bytearray(b'" = 12 chars, "')" = 2 chars, NUL = 1 */
        alloc_sz = 15 + n * 4; /* worst case: each byte as \xNN */
        out = (char far *)pydos_mem_alloc(
            PYDOS_MEM_BUFFER, (unsigned long)alloc_sz);
        if (out == (char far *)0) return (PyDosObj far *)0;

        _fmemcpy(out, (const char far *)"bytearray(b'", 12);
        pos = 12;
        for (i = 0; i < n; i++) {
            unsigned char ch = obj->v.bytearray.data[i];
            if (ch >= 32 && ch < 127 && ch != '\'' && ch != '\\') {
                out[pos++] = (char)ch;
            } else {
                out[pos++] = '\\';
                out[pos++] = 'x';
                out[pos++] = "0123456789abcdef"[ch >> 4];
                out[pos++] = "0123456789abcdef"[ch & 0x0f];
            }
        }
        out[pos++] = '\'';
        out[pos++] = ')';
        out[pos] = '\0';
        {
            PyDosObj far *result = pydos_obj_new_str(out, pos);
            pydos_far_free(out);
            return result;
        }
    }

    default:
        return pydos_obj_new_str((const char far *)"<object>", 8);
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_repr - canonical recursive representation                 */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_repr(PyDosObj far *obj)
{
    unsigned int len;
    unsigned int i;
    unsigned int pos;
    char quote;
    int has_single;
    int has_double;
    char far *buf;
    PyDosObj far *result;

    if (obj == (PyDosObj far *)0)
        return pydos_obj_new_str((const char far *)"None", 4);
    if ((PyDosType)obj->type != PYDT_STR)
        return pydos_obj_to_str(obj);

    len = obj->v.str.len;
    quote = '\'';
    has_single = 0;
    has_double = 0;
    for (i = 0; i < len; i++) {
        if (obj->v.str.data[i] == '\'') has_single = 1;
        else if (obj->v.str.data[i] == '"') has_double = 1;
    }
    if (has_single && !has_double) quote = '"';

    buf = (char far *)pydos_mem_alloc(
        PYDOS_MEM_BUFFER, (unsigned long)len * 2UL + 3UL);
    if (buf == (char far *)0) return pydos_obj_to_str(obj);
    pos = 0;
    buf[pos++] = quote;
    for (i = 0; i < len; i++) {
        char c = obj->v.str.data[i];
        if (c == quote || c == '\\') {
            buf[pos++] = '\\';
            buf[pos++] = c;
        } else if (c == '\n') {
            buf[pos++] = '\\'; buf[pos++] = 'n';
        } else if (c == '\r') {
            buf[pos++] = '\\'; buf[pos++] = 'r';
        } else if (c == '\t') {
            buf[pos++] = '\\'; buf[pos++] = 't';
        } else {
            buf[pos++] = c;
        }
    }
    buf[pos++] = quote;
    buf[pos] = '\0';
    result = pydos_obj_new_str(buf, pos);
    pydos_far_free(buf);
    return result;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_type_name — return a human-readable type name string      */
/* ------------------------------------------------------------------ */
const char far * PYDOS_API pydos_obj_type_name(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return (const char far *)"NoneType";
    }

    switch ((PyDosType)obj->type) {
    case PYDT_NONE:       return (const char far *)"NoneType";
    case PYDT_NOTIMPLEMENTED:
        return (const char far *)"NotImplementedType";
    case PYDT_BOOL:       return (const char far *)"bool";
    case PYDT_INT:        return (const char far *)"int";
    case PYDT_FLOAT:      return (const char far *)"float";
    case PYDT_COMPLEX:    return (const char far *)"complex";
    case PYDT_STR:        return (const char far *)"str";
    case PYDT_LIST:       return (const char far *)"list";
    case PYDT_DICT:       return (const char far *)"dict";
    case PYDT_TUPLE:      return (const char far *)"tuple";
    case PYDT_SET:        return (const char far *)"set";
    case PYDT_BYTES:      return (const char far *)"bytes";
    case PYDT_INSTANCE:   return (const char far *)"instance";
    case PYDT_FUNCTION:   return (const char far *)"function";
    case PYDT_GENERATOR:  return (const char far *)"generator";
    case PYDT_EXCEPTION:  return (const char far *)"Exception";
    case PYDT_CLASS:      return (const char far *)"type";
    case PYDT_RANGE:      return (const char far *)"range";
    case PYDT_FILE:       return (const char far *)"file";
    case PYDT_CELL:       return (const char far *)"cell";
    case PYDT_COROUTINE:  return (const char far *)"coroutine";
    case PYDT_EXC_GROUP:  return (const char far *)"ExceptionGroup";
    case PYDT_FROZENSET:  return (const char far *)"frozenset";
    case PYDT_BYTEARRAY:  return (const char far *)"bytearray";
    case PYDT_TRACEBACK:  return (const char far *)"traceback";
    case PYDT_SLICE:      return (const char far *)"slice";
    case PYDT_TYPE_PARAM: return (const char far *)"TypeVar";
    case PYDT_TYPE_ALIAS: return (const char far *)"TypeAliasType";
    case PYDT_GENERIC_ALIAS: return (const char far *)"GenericAlias";
    case PYDT_CODE:       return (const char far *)"code";
    case PYDT_SUPER:      return (const char far *)"super";
    case PYDT_MEMORYVIEW: return (const char far *)"memoryview";
    default:              return (const char far *)"<unknown>";
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_init / pydos_obj_shutdown                                 */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_obj_init(void)
{
    int i;

    /* Initialize None singleton */
    _fmemset(&singleton_none, 0, sizeof(PyDosObj));
    singleton_none.type = PYDT_NONE;
    singleton_none.flags = OBJ_FLAG_IMMORTAL;
    singleton_none.refcount = 1;

    _fmemset(&singleton_notimplemented, 0, sizeof(PyDosObj));
    singleton_notimplemented.type = PYDT_NOTIMPLEMENTED;
    singleton_notimplemented.flags = OBJ_FLAG_IMMORTAL;
    singleton_notimplemented.refcount = 1;

    /* Initialize True singleton */
    _fmemset(&singleton_true, 0, sizeof(PyDosObj));
    singleton_true.type = PYDT_BOOL;
    singleton_true.flags = OBJ_FLAG_IMMORTAL;
    singleton_true.refcount = 1;
    singleton_true.v.bool_val = 1;

    /* Initialize False singleton */
    _fmemset(&singleton_false, 0, sizeof(PyDosObj));
    singleton_false.type = PYDT_BOOL;
    singleton_false.flags = OBJ_FLAG_IMMORTAL;
    singleton_false.refcount = 1;
    singleton_false.v.bool_val = 0;

    _fmemset(&singleton_empty_tuple, 0, sizeof(PyDosObj));
    singleton_empty_tuple.type = PYDT_TUPLE;
    singleton_empty_tuple.flags = OBJ_FLAG_IMMORTAL;
    singleton_empty_tuple.refcount = REFCOUNT_MAX;
    singleton_empty_tuple.v.tuple.items = (PyDosObj far * far *)0;
    singleton_empty_tuple.v.tuple.len = 0;

    /* Initialize small integer cache */
    for (i = 0; i < SMALL_INT_COUNT; i++) {
        _fmemset(&small_ints[i], 0, sizeof(PyDosObj));
        small_ints[i].type = PYDT_INT;
        small_ints[i].flags = OBJ_FLAG_IMMORTAL;
        small_ints[i].refcount = 1;
        small_ints[i].v.int_val = (long)(i + SMALL_INT_MIN);
    }
    small_ints_ready = 1;

    /* Clear free list */
    free_count = 0;
}

void PYDOS_API pydos_obj_shutdown(void)
{
    unsigned int i;

    /* Release free list entries */
    for (i = 0; i < free_count; i++) {
        pydos_far_free(free_list[i]);
        free_list[i] = (PyDosObj far *)0;
    }
    free_count = 0;
    small_ints_ready = 0;
}

void PYDOS_API pydos_incref(PyDosObj far *obj)
{
    PYDOS_INCREF(obj);
}

void PYDOS_API pydos_decref(PyDosObj far *obj)
{
    PYDOS_DECREF(obj);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_set_attr — set an instance attribute by name              */
/* ------------------------------------------------------------------ */
static void pydos_obj_set_attr_default(PyDosObj far *obj,
                                       const char far *attr_name,
                                       PyDosObj far *value)
{
    unsigned int len;
    const char far *p;
    PyDosObj far *key;
    PyDosObj far *attrs;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    if ((PyDosType)obj->type != PYDT_INSTANCE &&
        (PyDosType)obj->type != PYDT_CLASS &&
        (PyDosType)obj->type != PYDT_FUNCTION) {
        return;
    }

    /* Data descriptors own instance assignment and take precedence over
     * the instance dictionary.  The descriptor implementation itself stays
     * in Python; C only supplies the fundamental lookup/call protocol. */
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.cls != (PyDosObj far *)0) {
        PyDosObj far *descriptor;
        descriptor = class_attr_lookup(obj->v.instance.cls, attr_name);
        if (descriptor != (PyDosObj far *)0) {
            if (descriptor_has_slot(descriptor, VSLOT_SET)) {
                descriptor_call_set(descriptor, obj, value);
                PYDOS_DECREF(descriptor);
                return;
            }
            PYDOS_DECREF(descriptor);
        }
        if (!class_slots_allow_attr(obj->v.instance.cls, attr_name)) {
            raise_slots_attribute_error(obj, attr_name);
            return;
        }
    }

    /* Lazily create the attrs dict */
    attrs = (PyDosType)obj->type == PYDT_INSTANCE
            ? obj->v.instance.attrs
            : ((PyDosType)obj->type == PYDT_CLASS
               ? obj->v.cls.class_attrs : obj->v.func.attrs);
    if (attrs == (PyDosObj far *)0) {
        attrs = pydos_dict_new(8);
        if (attrs == (PyDosObj far *)0) return;
        if ((PyDosType)obj->type == PYDT_INSTANCE)
            obj->v.instance.attrs = attrs;
        else if ((PyDosType)obj->type == PYDT_CLASS)
            obj->v.cls.class_attrs = attrs;
        else
            obj->v.func.attrs = attrs;
    }

    /* Compute length of attr_name (far pointer) */
    p = attr_name;
    len = 0;
    while (p[len] != '\0') len++;

    /* Wrap raw C string in a PyDosObj string for dict key */
    key = pydos_obj_new_str(attr_name, len);
    if (key == (PyDosObj far *)0) {
        return;
    }

    pydos_dict_set(attrs, key, value);
    PYDOS_DECREF(key);
}

void PYDOS_API pydos_obj_set_attr(PyDosObj far *obj,
                                   const char far *attr_name,
                                   PyDosObj far *value)
{
    void (far *setattr_entry)(void) = (void (far *)(void))0;
    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0)
        setattr_entry = pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_SETATTR);
    if (setattr_entry != (void (far *)(void))0) {
        PyDosObj far *name_obj = pydos_obj_new_str(
            attr_name, (unsigned int)_fstrlen(attr_name));
        PyDosObj far *args[3];
        PyDosObj far *result;
        if (name_obj == (PyDosObj far *)0) return;
        args[0] = obj;
        args[1] = name_obj;
        args[2] = value;
        result = call_vtable_method(setattr_entry, 3, args);
        PYDOS_DECREF(name_obj);
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
        return;
    }
    pydos_obj_set_attr_default(obj, attr_name, value);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_del_attr — delete an attribute from an instance object     */
/* ------------------------------------------------------------------ */
static void pydos_obj_del_attr_default(PyDosObj far *obj,
                                       const char far *attr_name)
{
    unsigned int len;
    const char far *p;
    PyDosObj far *key;
    PyDosObj far *attrs;

    if (obj == (PyDosObj far *)0) {
        return;
    }

    if ((PyDosType)obj->type != PYDT_INSTANCE &&
        (PyDosType)obj->type != PYDT_CLASS &&
        (PyDosType)obj->type != PYDT_FUNCTION) {
        return;
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.cls != (PyDosObj far *)0) {
        PyDosObj far *descriptor;
        descriptor = class_attr_lookup(obj->v.instance.cls, attr_name);
        if (descriptor != (PyDosObj far *)0) {
            if (descriptor_has_slot(descriptor, VSLOT_DELETE)) {
                descriptor_call_delete(descriptor, obj);
                PYDOS_DECREF(descriptor);
                return;
            }
            PYDOS_DECREF(descriptor);
        }
    }

    /* No attrs dict => nothing to delete */
    attrs = (PyDosType)obj->type == PYDT_INSTANCE
            ? obj->v.instance.attrs
            : ((PyDosType)obj->type == PYDT_CLASS
               ? obj->v.cls.class_attrs : obj->v.func.attrs);
    if (attrs == (PyDosObj far *)0) {
        return;
    }

    /* Compute length of attr_name (far pointer) */
    p = attr_name;
    len = 0;
    while (p[len] != '\0') len++;

    /* Wrap raw C string in a PyDosObj string for dict key */
    key = pydos_obj_new_str(attr_name, len);
    if (key == (PyDosObj far *)0) {
        return;
    }

    pydos_dict_delete(attrs, key);
    PYDOS_DECREF(key);
}

void PYDOS_API pydos_obj_del_attr(PyDosObj far *obj,
                                   const char far *attr_name)
{
    void (far *delattr_entry)(void) = (void (far *)(void))0;
    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0)
        delattr_entry = pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_DELATTR);
    if (delattr_entry != (void (far *)(void))0) {
        PyDosObj far *name_obj = pydos_obj_new_str(
            attr_name, (unsigned int)_fstrlen(attr_name));
        PyDosObj far *args[2];
        PyDosObj far *result;
        if (name_obj == (PyDosObj far *)0) return;
        args[0] = obj;
        args[1] = name_obj;
        result = call_vtable_method(delattr_entry, 2, args);
        PYDOS_DECREF(name_obj);
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
        return;
    }
    pydos_obj_del_attr_default(obj, attr_name);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_set_vtable — assign a vtable to an instance object        */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_set_vtable(PyDosObj far *obj,
                                     struct PyDosVTable far *vt)
{
    if (obj == (PyDosObj far *)0) return;

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        obj->v.instance.vtable = vt;
    }
}

void PYDOS_API pydos_obj_set_class(PyDosObj far *obj, PyDosObj far *cls)
{
    if (obj == (PyDosObj far *)0 ||
        (PyDosType)obj->type != PYDT_INSTANCE) return;
    if (obj->v.instance.cls == cls) return;
    if (cls != (PyDosObj far *)0) PYDOS_INCREF(cls);
    if (obj->v.instance.cls != (PyDosObj far *)0)
        PYDOS_DECREF(obj->v.instance.cls);
    obj->v.instance.cls = cls;
}

PyDosObj far * PYDOS_API pydos_instance_new(PyDosObj far *cls)
{
    PyDosObj far *instance;
    unsigned char mro_index;
    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object.__new__ requires a type");
        return (PyDosObj far *)0;
    }
    instance = pydos_obj_alloc_type(PYDT_INSTANCE);
    if (instance == (PyDosObj far *)0) return (PyDosObj far *)0;
    pydos_obj_set_vtable(instance, cls->v.cls.vtable);
    pydos_obj_set_class(instance, cls);
    /* User subclasses of dict still need a real dict payload.  Built-in
     * base classes are represented by class objects carrying their runtime
     * type tag, so this remains valid through generic aliases and C3 MRO. */
    for (mro_index = 1; mro_index < cls->v.cls.mro_len; mro_index++) {
        PyDosObj far *base = cls->v.cls.mro[mro_index];
        if (base != (PyDosObj far *)0 &&
            (PyDosType)base->type == PYDT_CLASS &&
            base->v.cls.runtime_type_tag == PYDT_DICT) {
            instance->v.instance.native_storage = pydos_dict_new(8);
            if (instance->v.instance.native_storage == (PyDosObj far *)0) {
                PYDOS_DECREF(instance);
                return (PyDosObj far *)0;
            }
            break;
        }
    }
    return instance;
}

PyDosObj far * PYDOS_API pydos_class_new(
    const char far *name, struct PyDosVTable far *vtable)
{
    PyDosObj far *cls;

    cls = pydos_obj_alloc_type(PYDT_CLASS);
    if (cls == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate class object");
        return (PyDosObj far *)0;
    }
    cls->v.cls.name = name;
    cls->v.cls.vtable = vtable;
    cls->v.cls.bases = (PyDosObj far * far *)0;
    cls->v.cls.mro = (PyDosObj far * far *)pydos_mem_alloc(
        PYDOS_MEM_METADATA,
        (unsigned long)sizeof(PyDosObj far *));
    cls->v.cls.num_bases = 0;
    cls->v.cls.mro_len = 0;
    cls->v.cls.runtime_type_tag = -1;
    cls->v.cls.class_attrs = pydos_dict_new(0);
    cls->v.cls.metaclass = (PyDosObj far *)0;
    if (cls->v.cls.mro == (PyDosObj far * far *)0 ||
        cls->v.cls.class_attrs == (PyDosObj far *)0) {
        pydos_obj_free(cls);
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate class metadata");
        return (PyDosObj far *)0;
    }
    cls->v.cls.mro[0] = cls;
    cls->v.cls.mro_len = 1;
    return cls;
}

/* Build [cls] + merge(mro(base1), ..., mro(baseN), [base1, ..., baseN]).
 * Arrays are bounded deliberately: generated classes already support at
 * most eight direct bases, and a 32-entry MRO avoids unbounded 8086 stack
 * and conventional-memory use. */
static int class_rebuild_c3_mro(PyDosObj far *cls)
{
    PyDosObj far * far *sequences[PYDOS_CLASS_MAX_BASES + 1];
    unsigned char lengths[PYDOS_CLASS_MAX_BASES + 1];
    unsigned char positions[PYDOS_CLASS_MAX_BASES + 1];
    PyDosObj far *result[PYDOS_CLASS_MAX_MRO];
    PyDosObj far *candidate;
    PyDosObj far * far *stored;
    unsigned char sequence_count;
    unsigned char result_count;
    unsigned char i;
    unsigned char j;
    int found;

    sequence_count = (unsigned char)(cls->v.cls.num_bases + 1U);
    result_count = 1;
    result[0] = cls;
    for (i = 0; i < cls->v.cls.num_bases; i++) {
        sequences[i] = cls->v.cls.bases[i]->v.cls.mro;
        lengths[i] = cls->v.cls.bases[i]->v.cls.mro_len;
        positions[i] = 0;
    }
    sequences[cls->v.cls.num_bases] = cls->v.cls.bases;
    lengths[cls->v.cls.num_bases] = cls->v.cls.num_bases;
    positions[cls->v.cls.num_bases] = 0;

    for (;;) {
        int any = 0;
        for (i = 0; i < sequence_count; i++) {
            if (positions[i] < lengths[i]) any = 1;
        }
        if (!any) break;

        found = 0;
        candidate = (PyDosObj far *)0;
        for (i = 0; i < sequence_count && !found; i++) {
            int in_tail = 0;
            if (positions[i] >= lengths[i]) continue;
            candidate = sequences[i][positions[i]];
            for (j = 0; j < sequence_count && !in_tail; j++) {
                unsigned char k;
                for (k = (unsigned char)(positions[j] + 1U);
                     k < lengths[j]; k++) {
                    if (sequences[j][k] == candidate) {
                        in_tail = 1;
                        break;
                    }
                }
            }
            if (!in_tail) found = 1;
        }
        if (!found) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"inconsistent method resolution order");
            return 0;
        }
        if (result_count >= PYDOS_CLASS_MAX_MRO) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"class MRO exceeds DOS limit");
            return 0;
        }
        result[result_count++] = candidate;
        for (i = 0; i < sequence_count; i++) {
            if (positions[i] < lengths[i] &&
                sequences[i][positions[i]] == candidate)
                positions[i]++;
        }
    }

    stored = (PyDosObj far * far *)pydos_mem_alloc(PYDOS_MEM_METADATA,
        (unsigned long)result_count * sizeof(PyDosObj far *));
    if (stored == (PyDosObj far * far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate class MRO");
        return 0;
    }
    for (i = 0; i < result_count; i++) stored[i] = result[i];
    if (cls->v.cls.mro != (PyDosObj far * far *)0)
        pydos_far_free(cls->v.cls.mro);
    cls->v.cls.mro = stored;
    cls->v.cls.mro_len = result_count;

    /* Rebuild the compact vtable MRO in exactly the C3 order.  Special
     * methods are resolved from method metadata, so no inherited slot array
     * needs to be copied or invalidated here. */
    if (cls->v.cls.vtable != (PyDosVTable far *)0) {
        PyDosVTable far *vtable = cls->v.cls.vtable;
        vtable->mro_len = 0;
        for (i = 1; i < result_count; i++) {
            PyDosVTable far *ancestor_vtable = result[i]->v.cls.vtable;
            if (ancestor_vtable != (PyDosVTable far *)0 &&
                !pydos_vtable_add_mro(vtable, ancestor_vtable))
                return 0;
        }
    }
    return 1;
}

void PYDOS_API pydos_class_add_base(PyDosObj far *cls, PyDosObj far *base)
{
    PyDosObj far * far *bases;
    unsigned int count;

    if (cls == (PyDosObj far *)0 || base == (PyDosObj far *)0 ||
        (PyDosType)cls->type != PYDT_CLASS ||
        (PyDosType)base->type != PYDT_CLASS) return;
    if (cls->v.cls.num_bases >= PYDOS_CLASS_MAX_BASES) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"too many direct base classes");
        return;
    }
    if (cls == base || pydos_class_is_subclass(base, cls)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"inheritance cycle");
        return;
    }
    for (count = 0; count < cls->v.cls.num_bases; count++) {
        if (cls->v.cls.bases[count] == base) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"duplicate base class");
            return;
        }
    }
    count = (unsigned int)cls->v.cls.num_bases;
    if (cls->v.cls.bases == (PyDosObj far * far *)0) {
        bases = (PyDosObj far * far *)pydos_mem_alloc(
            PYDOS_MEM_METADATA,
            (unsigned long)(count + 1U) * sizeof(PyDosObj far *));
    } else {
        bases = (PyDosObj far * far *)pydos_mem_realloc(
            cls->v.cls.bases,
            (unsigned long)(count + 1U) * sizeof(PyDosObj far *));
    }
    if (bases == (PyDosObj far * far *)0) return;
    cls->v.cls.bases = bases;
    bases[count] = base;
    PYDOS_INCREF(base);
    cls->v.cls.num_bases++;
    if (!class_rebuild_c3_mro(cls)) {
        cls->v.cls.num_bases--;
        PYDOS_DECREF(base);
    }
}

void PYDOS_API pydos_class_add_object_base(PyDosObj far *cls)
{
    PyDosObj far *object_class;
    object_class = pydos_builtin_type_object(PYDT_INSTANCE);
    if (object_class == (PyDosObj far *)0) return;
    pydos_class_add_base(cls, object_class);
    PYDOS_DECREF(object_class);
}

int PYDOS_API pydos_class_is_subclass(PyDosObj far *cls,
                                       PyDosObj far *base)
{
    unsigned char i;

    if (cls == (PyDosObj far *)0 || base == (PyDosObj far *)0 ||
        (PyDosType)cls->type != PYDT_CLASS ||
        (PyDosType)base->type != PYDT_CLASS) return 0;
    for (i = 0; i < cls->v.cls.mro_len; i++)
        if (cls->v.cls.mro[i] == base) return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_isinstance_vtable — check if obj has a matching vtable    */
/* Checks direct vtable match, then walks MRO for inheritance.         */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_isinstance_vtable(PyDosObj far *obj,
                                           struct PyDosVTable far *target_vt)
{
    PyDosVTable far *vt;
    unsigned char i;

    if (obj == (PyDosObj far *)0) return 0;
    if ((PyDosType)obj->type != PYDT_INSTANCE) return 0;

    vt = obj->v.instance.vtable;
    if (vt == (PyDosVTable far *)0) return 0;

    /* Direct match */
    if (vt == target_vt) return 1;

    /* Check MRO chain (parent vtables) */
    for (i = 0; i < vt->mro_len; i++) {
        if (vt->mro[i] == target_vt) return 1;
    }

    return 0;
}

static PyDosObj far *binary_instance_dispatch(PyDosObj far *left,
                                               PyDosObj far *right,
                                               int left_slot,
                                               int right_slot)
{
    typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *,
                                                   PyDosObj far *);
    PyDosObj far *result;
    BinOp operation;
    if ((PyDosType)left->type == PYDT_INSTANCE &&
        left->v.instance.vtable != (PyDosVTable far *)0) {
        operation = (BinOp)pydos_vtable_get_special(
            left->v.instance.vtable, (unsigned int)left_slot);
    } else {
        operation = (BinOp)0;
    }
    if (operation != (BinOp)0) {
        result = operation(left, right);
        if (result == (PyDosObj far *)0 && pydos_exc_pending())
            return (PyDosObj far *)0;
        if (result != (PyDosObj far *)0 &&
            (PyDosType)result->type != PYDT_NOTIMPLEMENTED)
            return result;
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
    }
    if ((PyDosType)right->type == PYDT_INSTANCE &&
        right->v.instance.vtable != (PyDosVTable far *)0) {
        operation = (BinOp)pydos_vtable_get_special(
            right->v.instance.vtable, (unsigned int)right_slot);
    } else {
        operation = (BinOp)0;
    }
    if (operation != (BinOp)0) {
        result = operation(right, left);
        if (result == (PyDosObj far *)0 && pydos_exc_pending())
            return (PyDosObj far *)0;
        if (result != (PyDosObj far *)0 &&
            (PyDosType)result->type != PYDT_NOTIMPLEMENTED)
            return result;
        if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
    }
    return (PyDosObj far *)0;
}

static PyDosObj far *unsupported_binary(const char far *message)
{
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR, message);
    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_add — polymorphic + operator                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_add(PyDosObj far *a, PyDosObj far *b)
{
    unsigned char ta, tb;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    ta = a->type;
    tb = b->type;

    /* Both int or bool: integer addition */
    if ((ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        return pydos_int_add(a, b);
    }

    /* Float arithmetic (including int+float promotion) */
    if ((ta == PYDT_FLOAT || ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_FLOAT || tb == PYDT_INT || tb == PYDT_BOOL) &&
        (ta == PYDT_FLOAT || tb == PYDT_FLOAT)) {
        double da = (ta == PYDT_FLOAT) ? a->v.float_val :
                    (ta == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
        double db = (tb == PYDT_FLOAT) ? b->v.float_val :
                    (tb == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
        return pydos_obj_new_float(da + db);
    }

    /* Complex arithmetic */
    if (ta == PYDT_COMPLEX || tb == PYDT_COMPLEX) {
        return pydos_complex_add(a, b);
    }

    /* Either is a string: concatenate (coerce non-str via to_str) */
    if (ta == PYDT_STR || tb == PYDT_STR) {
        PyDosObj far *sa;
        PyDosObj far *sb;
        PyDosObj far *result;

        sa = (ta == PYDT_STR) ? a : pydos_obj_to_str(a);
        sb = (tb == PYDT_STR) ? b : pydos_obj_to_str(b);
        result = pydos_str_concat(sa, sb);
        if (ta != PYDT_STR) { PYDOS_DECREF(sa); }
        if (tb != PYDT_STR) { PYDOS_DECREF(sb); }
        return result;
    }

    if (ta == PYDT_BYTES && tb == PYDT_BYTES) {
        return pydos_bytes_concat(a, b);
    }

    if (ta == PYDT_BYTEARRAY && tb == PYDT_BYTEARRAY) {
        return pydos_bytearray_concat(a, b);
    }

    if (ta == PYDT_TUPLE && tb == PYDT_TUPLE) {
        return pydos_tuple_concat(a, b);
    }

    if (ta == PYDT_LIST && tb == PYDT_LIST) {
        return pydos_list_concat(a, b);
    }

    {
        PyDosObj far *result = binary_instance_dispatch(
            a, b, VSLOT_ADD, VSLOT_RADD);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for +");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_sub — polymorphic - operator                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_sub(PyDosObj far *a, PyDosObj far *b)
{
    unsigned char ta, tb;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    ta = a->type;
    tb = b->type;

    if ((ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        return pydos_int_sub(a, b);
    }

    /* Float arithmetic (including int-float promotion) */
    if ((ta == PYDT_FLOAT || ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_FLOAT || tb == PYDT_INT || tb == PYDT_BOOL) &&
        (ta == PYDT_FLOAT || tb == PYDT_FLOAT)) {
        double da = (ta == PYDT_FLOAT) ? a->v.float_val :
                    (ta == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
        double db = (tb == PYDT_FLOAT) ? b->v.float_val :
                    (tb == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
        return pydos_obj_new_float(da - db);
    }

    /* Complex arithmetic */
    if (ta == PYDT_COMPLEX || tb == PYDT_COMPLEX) {
        return pydos_complex_sub(a, b);
    }

    {
        PyDosObj far *result = binary_instance_dispatch(
            a, b, VSLOT_SUB, VSLOT_RSUB);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for -");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_mul — polymorphic * operator                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_mul(PyDosObj far *a, PyDosObj far *b)
{
    unsigned char ta, tb;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    ta = a->type;
    tb = b->type;

    if ((ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        return pydos_int_mul(a, b);
    }

    /* Float arithmetic (including int*float promotion) */
    if ((ta == PYDT_FLOAT || ta == PYDT_INT || ta == PYDT_BOOL) &&
        (tb == PYDT_FLOAT || tb == PYDT_INT || tb == PYDT_BOOL) &&
        (ta == PYDT_FLOAT || tb == PYDT_FLOAT)) {
        double da = (ta == PYDT_FLOAT) ? a->v.float_val :
                    (ta == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
        double db = (tb == PYDT_FLOAT) ? b->v.float_val :
                    (tb == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
        return pydos_obj_new_float(da * db);
    }

    /* Complex arithmetic */
    if (ta == PYDT_COMPLEX || tb == PYDT_COMPLEX) {
        return pydos_complex_mul(a, b);
    }

    if (ta == PYDT_BYTES && (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_bytes_repeat(a, count);
    }
    if (tb == PYDT_BYTES && (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_bytes_repeat(b, count);
    }
    if (ta == PYDT_BYTEARRAY && (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_bytearray_repeat(a, count);
    }
    if (tb == PYDT_BYTEARRAY && (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_bytearray_repeat(b, count);
    }
    if (ta == PYDT_STR && (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_str_repeat(a, count);
    }
    if (tb == PYDT_STR && (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_str_repeat(b, count);
    }
    if ((ta == PYDT_LIST || ta == PYDT_TUPLE) &&
        (tb == PYDT_INT || tb == PYDT_BOOL)) {
        long count = tb == PYDT_INT ? b->v.int_val : (long)b->v.bool_val;
        return pydos_seq_repeat(a, count);
    }
    if ((tb == PYDT_LIST || tb == PYDT_TUPLE) &&
        (ta == PYDT_INT || ta == PYDT_BOOL)) {
        long count = ta == PYDT_INT ? a->v.int_val : (long)a->v.bool_val;
        return pydos_seq_repeat(b, count);
    }

    {
        PyDosObj far *result = binary_instance_dispatch(
            a, b, VSLOT_MUL, VSLOT_RMUL);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for *");
}

PyDosObj far * PYDOS_API pydos_obj_floordiv(PyDosObj far *a,
                                             PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for //");
    result = binary_instance_dispatch(a, b, VSLOT_FLOORDIV, VSLOT_RFLOORDIV);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL ||
         (PyDosType)a->type == PYDT_FLOAT) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL ||
         (PyDosType)b->type == PYDT_FLOAT))
        return pydos_int_div(a, b);
    return unsupported_binary((const char far *)"unsupported operands for //");
}

PyDosObj far * PYDOS_API pydos_obj_truediv(PyDosObj far *a,
                                            PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for /");
    result = binary_instance_dispatch(a, b, VSLOT_TRUEDIV, VSLOT_RTRUEDIV);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if ((PyDosType)a->type == PYDT_COMPLEX ||
        (PyDosType)b->type == PYDT_COMPLEX)
        return pydos_complex_div(a, b);
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL ||
         (PyDosType)a->type == PYDT_FLOAT) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL ||
         (PyDosType)b->type == PYDT_FLOAT))
        return pydos_int_truediv(a, b);
    return unsupported_binary((const char far *)"unsupported operands for /");
}

PyDosObj far * PYDOS_API pydos_obj_mod(PyDosObj far *a, PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for %");
    result = binary_instance_dispatch(a, b, VSLOT_MOD, VSLOT_RMOD);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL ||
         (PyDosType)a->type == PYDT_FLOAT) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL ||
         (PyDosType)b->type == PYDT_FLOAT))
        return pydos_int_mod(a, b);
    return unsupported_binary((const char far *)"unsupported operands for %");
}

PyDosObj far * PYDOS_API pydos_obj_pow(PyDosObj far *a, PyDosObj far *b)
{
    PyDosObj far *result;
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0)
        return unsupported_binary((const char far *)"unsupported operands for **");
    result = binary_instance_dispatch(a, b, VSLOT_POW, VSLOT_RPOW);
    if (result != (PyDosObj far *)0) return result;
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    if (((PyDosType)a->type == PYDT_INT || (PyDosType)a->type == PYDT_BOOL) &&
        ((PyDosType)b->type == PYDT_INT || (PyDosType)b->type == PYDT_BOOL))
        return pydos_int_pow(a, b);
    return unsupported_binary((const char far *)"unsupported operands for **");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_matmul — @ operator dispatch                              */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_matmul(PyDosObj far *a, PyDosObj far *b)
{
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    {
        PyDosObj far *result = binary_instance_dispatch(
            a, b, VSLOT_MATMUL, VSLOT_RMATMUL);
        if (result != (PyDosObj far *)0) return result;
        if (pydos_exc_pending()) return (PyDosObj far *)0;
    }
    return unsupported_binary((const char far *)"unsupported operands for @");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_inplace — in-place operator dispatch                      */
/* op: 0=add,1=sub,2=mul,3=floordiv,4=truediv,5=mod,6=pow,            */
/*     7=and,8=or,9=xor,10=lshift,11=rshift,12=matmul                 */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_inplace(PyDosObj far *a, PyDosObj far *b,
                                            int op)
{
    typedef PyDosObj far * (PYDOS_API far *BinOp)(PyDosObj far *, PyDosObj far *);
    static const int iplace_slots[] = {
        VSLOT_IADD, VSLOT_ISUB, VSLOT_IMUL, VSLOT_IFLOORDIV,
        VSLOT_ITRUEDIV, VSLOT_IMOD, VSLOT_IPOW,
        VSLOT_IAND, VSLOT_IOR, VSLOT_IXOR,
        VSLOT_ILSHIFT, VSLOT_IRSHIFT, VSLOT_IMATMUL
    };
    int slot_idx;

    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    if (op < 0 || op > 12) {
        return pydos_obj_new_int(0L);
    }

    /* Try a.__iadd__(b) etc via vtable */
    slot_idx = iplace_slots[op];
    if ((PyDosType)a->type == PYDT_INSTANCE &&
        a->v.instance.vtable != (PyDosVTable far *)0) {
        BinOp operation = (BinOp)pydos_vtable_get_special(
            a->v.instance.vtable, (unsigned int)slot_idx);
        if (operation != (BinOp)0) return operation(a, b);
    }

    /* Fallback to regular binary op */
    switch (op) {
    case 0:  return pydos_obj_add(a, b);
    case 1:  return pydos_obj_sub(a, b);
    case 2:  return pydos_obj_mul(a, b);
    case 3:  return pydos_int_div(a, b);
    case 4:  return pydos_int_truediv(a, b);
    case 5:  return pydos_int_mod(a, b);
    case 6:  return pydos_int_pow(a, b);
    case 7:  return pydos_int_bitand(a, b);
    case 8:  return pydos_int_bitor(a, b);
    case 9:  return pydos_int_bitxor(a, b);
    case 10: return pydos_int_shl(a, b);
    case 11: return pydos_int_shr(a, b);
    case 12: return pydos_obj_matmul(a, b);
    default: break;
    }

    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_get_attr — get an instance attribute by name              */
/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
/* pydos_obj_getitem — polymorphic subscript operator                   */
/* ------------------------------------------------------------------ */
static int subscript_index(PyDosObj far *key, long *index)
{
    if (key != (PyDosObj far *)0 && (PyDosType)key->type == PYDT_INT) {
        *index = key->v.int_val;
        return 0;
    }
    if (key != (PyDosObj far *)0 && (PyDosType)key->type == PYDT_BOOL) {
        *index = (long)key->v.bool_val;
        return 0;
    }
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"sequence index must be an integer");
    return -1;
}

PyDosObj far * PYDOS_API pydos_obj_getitem(PyDosObj far *obj,
                                            PyDosObj far *key)
{
    if (obj == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object is not subscriptable");
        return (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_CLASS ||
        (PyDosType)obj->type == PYDT_TYPE_ALIAS ||
        (PyDosType)obj->type == PYDT_GENERIC_ALIAS)
        return pydos_generic_alias_new(obj, key);

    if (key != (PyDosObj far *)0 && (PyDosType)key->type == PYDT_SLICE) {
        long start = key->v.slice.has_start
                     ? key->v.slice.start
                     : 0x7FFFFFFFL;
        long stop = key->v.slice.has_stop
                    ? key->v.slice.stop
                    : 0x7FFFFFFFL;
        long step = key->v.slice.has_step ? key->v.slice.step : 1L;
        if ((PyDosType)obj->type == PYDT_LIST ||
            (PyDosType)obj->type == PYDT_TUPLE ||
            (PyDosType)obj->type == PYDT_STR ||
            (PyDosType)obj->type == PYDT_BYTES ||
            (PyDosType)obj->type == PYDT_BYTEARRAY ||
            (PyDosType)obj->type == PYDT_RANGE)
            return pydos_obj_slice(obj, start, stop, step);
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        long idx;
        if (subscript_index(key, &idx) != 0) return (PyDosObj far *)0;
        return pydos_list_get_op(obj, idx);
    }

    if ((PyDosType)obj->type == PYDT_STR) {
        long idx;
        if (subscript_index(key, &idx) != 0) return (PyDosObj far *)0;
        return pydos_str_index_op(obj, idx);
    }

    if ((PyDosType)obj->type == PYDT_BYTES) {
        long idx;
        int value;
        if (subscript_index(key, &idx) != 0) return (PyDosObj far *)0;
        value = pydos_bytes_getitem(obj, idx);
        if (value < 0) {
            pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                            (const char far *)"bytes index out of range");
            return (PyDosObj far *)0;
        }
        return pydos_obj_new_int((long)value);
    }

    if ((PyDosType)obj->type == PYDT_DICT) {
        return pydos_dict_get_op(obj, key);
    }

    if ((PyDosType)obj->type == PYDT_TUPLE) {
        long idx;
        PyDosObj far *result;
        if (subscript_index(key, &idx) != 0) return (PyDosObj far *)0;
        result = pydos_list_get(obj, idx);
        if (result != (PyDosObj far *)0) return result;
        pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                        (const char far *)"tuple index out of range");
        return (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        long index;
        int value;
        if (subscript_index(key, &index) != 0) return (PyDosObj far *)0;
        value = pydos_bytearray_getitem(obj, (int)index);
        if (value < 0) {
            pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                            (const char far *)"bytearray index out of range");
            return (PyDosObj far *)0;
        }
        return pydos_obj_new_int((long)value);
    }

    if ((PyDosType)obj->type == PYDT_RANGE) {
        long idx;
        if (key == (PyDosObj far *)0 ||
            ((PyDosType)key->type != PYDT_INT &&
             (PyDosType)key->type != PYDT_BOOL)) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"range indices must be integers or slices");
            return (PyDosObj far *)0;
        }
        idx = (PyDosType)key->type == PYDT_INT
              ? key->v.int_val : (long)key->v.bool_val;
        return pydos_range_getitem(obj, idx);
    }

    if ((PyDosType)obj->type == PYDT_MEMORYVIEW) {
        if (obj->v.memoryview.released ||
            obj->v.memoryview.source == (PyDosObj far *)0) {
            pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                            (const char far *)"operation on released memoryview");
            return (PyDosObj far *)0;
        }
        return pydos_obj_getitem(obj->v.memoryview.source, key);
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        /* A Python override wins over inherited native-container behavior. */
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            typedef PyDosObj far * (PYDOS_API far *GetItemFn)(
                PyDosObj far *, PyDosObj far *);
            GetItemFn getitem = (GetItemFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_GETITEM);
            if (getitem != (GetItemFn)0) return getitem(obj, key);
        }
        if (obj->v.instance.native_storage != (PyDosObj far *)0 &&
            (PyDosType)obj->v.instance.native_storage->type == PYDT_DICT) {
            PyDosObj far *result = pydos_dict_get(
                obj->v.instance.native_storage, key);
            if (result != (PyDosObj far *)0) return result;
            if (pydos_obj_has_attr(obj, (const char far *)"__missing__")) {
                PyDosObj far *args[2];
                args[0] = obj;
                args[1] = key;
                return pydos_obj_call_method(
                    (const char far *)"__missing__", 2U, args);
            }
            pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                            (const char far *)"dictionary key not found");
            return (PyDosObj far *)0;
        }
    }

    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"object is not subscriptable");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_match_class_arg(PyDosObj far *obj,
                                               PyDosObj far *index)
{
    PyDosObj far *match_args;
    PyDosObj far *name;
    PyDosObj far *result;
    long position;
    if (obj == (PyDosObj far *)0 || index == (PyDosObj far *)0 ||
        ((PyDosType)index->type != PYDT_INT &&
         (PyDosType)index->type != PYDT_BOOL)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid class pattern");
        return (PyDosObj far *)0;
    }
    position = (PyDosType)index->type == PYDT_INT
               ? index->v.int_val : (long)index->v.bool_val;

    /* CPython gives these built-in classes one positional self pattern,
     * e.g. case str(name).  They intentionally do not expose
     * __match_args__. */
    if ((PyDosType)obj->type == PYDT_BOOL ||
        (PyDosType)obj->type == PYDT_BYTEARRAY ||
        (PyDosType)obj->type == PYDT_BYTES ||
        (PyDosType)obj->type == PYDT_DICT ||
        (PyDosType)obj->type == PYDT_FLOAT ||
        (PyDosType)obj->type == PYDT_FROZENSET ||
        (PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_LIST ||
        (PyDosType)obj->type == PYDT_SET ||
        (PyDosType)obj->type == PYDT_STR ||
        (PyDosType)obj->type == PYDT_TUPLE) {
        if (position != 0) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"too many positional class patterns");
            return (PyDosObj far *)0;
        }
        PYDOS_INCREF(obj);
        return obj;
    }
    if ((PyDosType)obj->type != PYDT_INSTANCE ||
        obj->v.instance.cls == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid class pattern");
        return (PyDosObj far *)0;
    }
    match_args = pydos_obj_get_attr(obj->v.instance.cls,
                                    (const char far *)"__match_args__");
    if (match_args == (PyDosObj far *)0) return (PyDosObj far *)0;
    if ((PyDosType)match_args->type != PYDT_TUPLE) {
        PYDOS_DECREF(match_args);
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__match_args__ must be a tuple");
        return (PyDosObj far *)0;
    }
    if (position < 0 ||
        (unsigned long)position >= (unsigned long)match_args->v.tuple.len) {
        PYDOS_DECREF(match_args);
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"too many positional class patterns");
        return (PyDosObj far *)0;
    }
    name = match_args->v.tuple.items[(unsigned int)position];
    if (name == (PyDosObj far *)0 || (PyDosType)name->type != PYDT_STR) {
        PYDOS_DECREF(match_args);
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__match_args__ items must be strings");
        return (PyDosObj far *)0;
    }
    result = pydos_obj_get_attr(obj, name->v.str.data);
    PYDOS_DECREF(match_args);
    return result;
}

PyDosObj far * PYDOS_API pydos_match_sequence(PyDosObj far *obj)
{
    int matches;
    matches = obj != (PyDosObj far *)0 &&
              ((PyDosType)obj->type == PYDT_LIST ||
               (PyDosType)obj->type == PYDT_TUPLE ||
               (PyDosType)obj->type == PYDT_RANGE);
    if (!matches && obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0) {
        matches = pydos_vtable_get_special(
                      obj->v.instance.vtable, VSLOT_LEN) !=
                      (void (far *)(void))0 &&
                  pydos_vtable_get_special(
                      obj->v.instance.vtable, VSLOT_GETITEM) !=
                      (void (far *)(void))0;
    }
    return pydos_obj_new_bool(matches);
}

PyDosObj far * PYDOS_API pydos_match_mapping(PyDosObj far *obj)
{
    return pydos_obj_new_bool(
        obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_DICT);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_slice — polymorphic primitive slice operation             */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_slice(PyDosObj far *obj,
                                          long start, long stop, long step)
{
    if (obj == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object is not sliceable");
        return (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_STR) {
        return pydos_str_slice(obj, start, stop, step);
    }

    if ((PyDosType)obj->type == PYDT_BYTES) {
        return pydos_bytes_slice(obj, start, stop, step);
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        return pydos_list_slice(obj, start, stop, step);
    }

    if ((PyDosType)obj->type == PYDT_TUPLE) {
        return pydos_tuple_slice(obj, start, stop, step);
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        return pydos_bytearray_slice(obj, start, stop, step);
    }

    if ((PyDosType)obj->type == PYDT_RANGE) {
        return pydos_range_slice(obj, start, stop, step);
    }

    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"object is not sliceable");
    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_setitem — polymorphic subscript assignment                 */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_setitem(PyDosObj far *obj,
                                  PyDosObj far *key,
                                  PyDosObj far *value)
{
    if (obj == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object does not support item assignment");
        return;
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        long idx;
        if (subscript_index(key, &idx) != 0) return;
        pydos_list_set_op(obj, idx, value);
        return;
    }

    if ((PyDosType)obj->type == PYDT_DICT) {
        pydos_dict_set(obj, key, value);
        return;
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        long idx;
        long byte;
        long normalized;
        if (subscript_index(key, &idx) != 0) return;
        if (value == (PyDosObj far *)0 ||
            ((PyDosType)value->type != PYDT_INT &&
             (PyDosType)value->type != PYDT_BOOL)) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"an integer is required");
            return;
        }
        byte = (PyDosType)value->type == PYDT_INT
               ? value->v.int_val : (long)value->v.bool_val;
        if (byte < 0L || byte > 255L) {
            pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                            (const char far *)"byte must be in range(0, 256)");
            return;
        }
        normalized = idx < 0L ? idx + (long)obj->v.bytearray.len : idx;
        if (normalized < 0L ||
            (unsigned long)normalized >= (unsigned long)obj->v.bytearray.len) {
            pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                            (const char far *)"bytearray index out of range");
            return;
        }
        pydos_bytearray_setitem(obj, (int)normalized, (unsigned char)byte);
        return;
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            typedef void (PYDOS_API far *SetItemFn)(
                PyDosObj far *, PyDosObj far *, PyDosObj far *);
            SetItemFn setitem = (SetItemFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_SETITEM);
            if (setitem != (SetItemFn)0) {
                setitem(obj, key, value);
                return;
            }
        }
        if (obj->v.instance.native_storage != (PyDosObj far *)0 &&
            (PyDosType)obj->v.instance.native_storage->type == PYDT_DICT) {
            pydos_dict_set(obj->v.instance.native_storage, key, value);
            return;
        }
    }

    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"object does not support item assignment");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_delitem — polymorphic subscript deletion                  */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_obj_delitem(PyDosObj far *obj, PyDosObj far *key)
{
    if (obj == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object does not support item deletion");
        return;
    }
    if ((PyDosType)obj->type == PYDT_DICT) {
        if (!pydos_dict_delete(obj, key))
            pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                            (const char far *)"dictionary key not found");
        return;
    }
    if ((PyDosType)obj->type == PYDT_LIST) {
        long idx;
        PyDosObj far *removed;
        if (subscript_index(key, &idx) != 0) return;
        removed = pydos_list_pop(obj, idx);
        if (removed == (PyDosObj far *)0) {
            pydos_exc_raise(PYDOS_EXC_INDEX_ERROR,
                            (const char far *)"list assignment index out of range");
            return;
        }
        PYDOS_DECREF(removed);
        return;
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            typedef void (PYDOS_API far *DelItemFn)(
                PyDosObj far *, PyDosObj far *);
            DelItemFn delitem = (DelItemFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_DELITEM);
            if (delitem != (DelItemFn)0) {
                delitem(obj, key);
                return;
            }
        }
        if (obj->v.instance.native_storage != (PyDosObj far *)0 &&
            (PyDosType)obj->v.instance.native_storage->type == PYDT_DICT) {
            if (!pydos_dict_delete(obj->v.instance.native_storage, key))
                pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                                (const char far *)"dictionary key not found");
            return;
        }
    }
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"object does not support item deletion");
}

/* ------------------------------------------------------------------ */
/* pydos_obj_get_iter — get an iterator for an iterable object         */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_get_iter(PyDosObj far *obj)
{
    PyDosObj far *iter;

    if (obj == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[ITER_NEW type=");
    dbg_putint((int)obj->type);
    dbg_puts("]\r\n");
#endif

    /* Both compiled generators and the compact runtime iterators use the
     * PYDT_GENERATOR layout.  Every iterator must satisfy iter(it) is it;
     * resume == NULL merely distinguishes an internal sequence iterator
     * from a suspended Python generator. */
    if ((PyDosType)obj->type == PYDT_GENERATOR) {
        PYDOS_INCREF(obj);
        return obj;
    }

    /* Coroutines are NOT iterable — TypeError per Python spec */
    if ((PyDosType)obj->type == PYDT_COROUTINE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"coroutine object is not iterable");
        return (PyDosObj far *)0;
    }

    if ((PyDosType)obj->type == PYDT_RANGE) {
        return pydos_range_new(obj->v.range.start, obj->v.range.stop,
                               obj->v.range.step);
    }

    if ((PyDosType)obj->type == PYDT_LIST) {
        /* Use a GENERATOR object as a list iterator:
         * state = the list, pc = current index */
        iter = pydos_obj_alloc_type(PYDT_GENERATOR);
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[ITER_OK]\r\n");
#endif
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_DICT || (PyDosType)obj->type == PYDT_SET) {
        /* For dict/set iteration, get list of keys, then iterate over that.
         * Python semantics: `for k in d` iterates over keys.
         * pydos_dict_keys() doesn't check the type tag, works on sets too. */
        PyDosObj far *keys = pydos_dict_keys(obj);
        if (keys == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter = pydos_obj_alloc_type(PYDT_GENERATOR);
        if (iter == (PyDosObj far *)0) {
            PYDOS_DECREF(keys);
            return (PyDosObj far *)0;
        }
        iter->v.gen.state  = keys;   /* takes ownership of keys list */
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_STR ||
        (PyDosType)obj->type == PYDT_BYTES) {
        /* String/bytes iteration uses the source object plus an index. */
        iter = pydos_obj_alloc_type(PYDT_GENERATOR);
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_TUPLE) {
        /* Tuple iteration: use GENERATOR with state=tuple, pc=index */
        iter = pydos_obj_alloc_type(PYDT_GENERATOR);
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_FROZENSET) {
        /* Frozenset iteration: use GENERATOR with state=frozenset, pc=index */
        iter = pydos_obj_alloc_type(PYDT_GENERATOR);
        if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
        iter->v.gen.state  = obj;
        PYDOS_INCREF(obj);
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_BYTEARRAY) {
        /* Bytearray iteration: build a list of int objects, then iterate */
        unsigned int bi;
        PyDosObj far *lst = pydos_list_new(obj->v.bytearray.len > 0 ? obj->v.bytearray.len : 4);
        if (lst == (PyDosObj far *)0) return (PyDosObj far *)0;
        for (bi = 0; bi < obj->v.bytearray.len; bi++) {
            PyDosObj far *byte_obj = pydos_obj_new_int((long)obj->v.bytearray.data[bi]);
            pydos_list_append(lst, byte_obj);
            PYDOS_DECREF(byte_obj);
        }
        iter = pydos_obj_alloc_type(PYDT_GENERATOR);
        if (iter == (PyDosObj far *)0) {
            PYDOS_DECREF(lst);
            return (PyDosObj far *)0;
        }
        iter->v.gen.state  = lst;  /* takes ownership */
        iter->v.gen.pc     = 0;
        iter->v.gen.resume = (void (far *)(void))0;
        iter->v.gen.locals = (PyDosObj far *)0;
        return iter;
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        /* Instance with __iter__: call it and return result */
        if (obj->v.instance.vtable != (PyDosVTable far *)0) {
            typedef PyDosObj far * (PYDOS_API far *IterFn)(PyDosObj far *);
            PyDosObj far *result;
            PyDosObj far *wrapped;
            IterFn iter_fn = (IterFn)pydos_vtable_get_special(
                obj->v.instance.vtable, VSLOT_ITER);
            if (iter_fn != (IterFn)0) {
                result = iter_fn(obj);
                if (result == obj) {
                    /* __iter__ returned self; this instance is the iterator. */
                    return result;
                }
                if (result != (PyDosObj far *)0 &&
                    (PyDosType)result->type == PYDT_INSTANCE) {
                    /* __iter__ returned another instance; it is the iterator. */
                    return result;
                }
                if (result != (PyDosObj far *)0 &&
                    (PyDosType)result->type != PYDT_GENERATOR) {
                    /* __iter__ returned a builtin iterable, wrap it. */
                    wrapped = pydos_obj_get_iter(result);
                    PYDOS_DECREF(result);
                    return wrapped;
                }
                return result;
            }
        }
        if (obj->v.instance.native_storage != (PyDosObj far *)0)
            return pydos_obj_get_iter(obj->v.instance.native_storage);
    }

    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_iter_next — return next element or NULL (StopIteration)    */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_iter_next(PyDosObj far *iter)
{
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[ITNX]\r\n");
#endif
    if (iter == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[ITNX t=");
    dbg_putint((int)iter->type);
    dbg_puts("]\r\n");
#endif

    if ((PyDosType)iter->type == PYDT_RANGE) {
        return pydos_range_next(iter);
    }

    /* True generator (has a resume function) — call it */
#ifdef PYDOS_DEBUG_CMP
    if ((PyDosType)iter->type == PYDT_GENERATOR) {
        int has_resume = (iter->v.gen.resume != (void (far *)(void))0);
        dbg_puts("[ITNX r=");
        dbg_putint(has_resume);
        dbg_puts("]\r\n");
    }
#endif
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.resume != (void (far *)(void))0) {
        PyDosObj far *result = pydos_gen_next(iter);
        if (result == (PyDosObj far *)0 && pydos_exc_pending()) {
            PyDosObj far *exc = pydos_exc_current();
            if (exc != (PyDosObj far *)0 &&
                (PyDosType)exc->type == PYDT_EXCEPTION &&
                pydos_exc_matches(exc, PYDOS_EXC_STOP_ITERATION))
                pydos_exc_clear();
        }
        return result;
    }

    /* List iterator (stored as GENERATOR with state=list, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_LIST) {
        PyDosObj far *list = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[NEXT pc=");
        dbg_putint((int)idx);
        dbg_puts(" len=");
        dbg_putint((int)list->v.list.len);
        dbg_puts("]\r\n");
#endif
        if (idx < list->v.list.len) {
            PyDosObj far *item = list->v.list.items[idx];
            iter->v.gen.pc = (int)(idx + 1);
            PYDOS_INCREF(item);
            return item;
        }
        return (PyDosObj far *)0;
    }

    /* String iterator (stored as GENERATOR with state=str, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_STR) {
        PyDosObj far *str = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < str->v.str.len) {
            char c = str->v.str.data[idx];
            iter->v.gen.pc = (int)(idx + 1);
            return pydos_obj_new_str((const char far *)&c, 1);
        }
        return (PyDosObj far *)0;
    }

    /* Bytes iteration yields integers. */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_BYTES) {
        PyDosObj far *value = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < value->v.str.len) {
            iter->v.gen.pc = (int)(idx + 1);
            return pydos_obj_new_int(
                (long)(unsigned char)value->v.str.data[idx]);
        }
        return (PyDosObj far *)0;
    }

    /* Tuple iterator (stored as GENERATOR with state=tuple, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_TUPLE) {
        PyDosObj far *tup = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < tup->v.tuple.len) {
            PyDosObj far *item = tup->v.tuple.items[idx];
            iter->v.gen.pc = (int)(idx + 1);
            PYDOS_INCREF(item);
            return item;
        }
        return (PyDosObj far *)0;
    }

    /* Frozenset iterator (stored as GENERATOR with state=frozenset, pc=index) */
    if ((PyDosType)iter->type == PYDT_GENERATOR &&
        iter->v.gen.state != (PyDosObj far *)0 &&
        (PyDosType)iter->v.gen.state->type == PYDT_FROZENSET) {
        PyDosObj far *fs = iter->v.gen.state;
        unsigned int idx = (unsigned int)iter->v.gen.pc;
        if (idx < fs->v.frozenset.len) {
            PyDosObj far *item = fs->v.frozenset.items[idx];
            iter->v.gen.pc = (int)(idx + 1);
            PYDOS_INCREF(item);
            return item;
        }
        return (PyDosObj far *)0;
    }

    /* Instance iterator — call __next__ via vtable.  StopIteration is the
     * normal exhaustion sentinel here; any other exception remains pending
     * for the generated caller to propagate explicitly. */
    if ((PyDosType)iter->type == PYDT_INSTANCE &&
        iter->v.instance.vtable != (PyDosVTable far *)0) {
        typedef PyDosObj far * (PYDOS_API far *NextFn)(PyDosObj far *);
        NextFn next_fn;
        PyDosObj far *result;
        PyDosObj far *exc;

        next_fn = (NextFn)pydos_vtable_get_special(
            iter->v.instance.vtable, VSLOT_NEXT);
        if (next_fn == (NextFn)0) return (PyDosObj far *)0;
        result = next_fn(iter);
        if (result != (PyDosObj far *)0) {
            return result;
        }
        exc = pydos_exc_current();
        if (exc != (PyDosObj far *)0 &&
            (PyDosType)exc->type == PYDT_EXCEPTION &&
            pydos_exc_matches(exc, PYDOS_EXC_STOP_ITERATION)) {
            pydos_exc_clear();
        }
        return (PyDosObj far *)0;
    }

    return (PyDosObj far *)0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_call_method — generic method dispatch                     */
/* ------------------------------------------------------------------ */
static unsigned int method_name_hash(const char far *method_name)
{
    unsigned int hash;

    hash = 5381U;
    while (*method_name != '\0') {
        hash = ((hash << 5) + hash) + (unsigned char)*method_name;
        method_name++;
    }
    return hash;
}

static PyDosObj far *class_attr_lookup(PyDosObj far *cls,
                                       const char far *attr_name);

static PyDosObj far *call_vtable_method(void (far *mfunc)(void),
                                        unsigned int argc,
                                        PyDosObj far * far *argv)
{
    typedef PyDosObj far * (PYDOS_API far *MFn8)(
        PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *,
        PyDosObj far *, PyDosObj far *, PyDosObj far *, PyDosObj far *);
    PyDosObj far *args[8];
    PyDosObj far *none_val;
    unsigned int i;

    none_val = pydos_obj_new_none();
    for (i = 0; i < 8; i++) {
        args[i] = (i < argc) ? argv[i] : none_val;
    }
    return ((MFn8)mfunc)(args[0], args[1], args[2], args[3],
                         args[4], args[5], args[6], args[7]);
}

static PyDosObj far *call_builtin_type(unsigned int type_tag,
                                       int argc,
                                       PyDosObj far * far *argv)
{
    switch ((PyDosType)type_tag) {
    case PYDT_BOOL:      return pydos_builtin_bool_conv(argc, argv);
    case PYDT_INT:       return pydos_builtin_int_conv(argc, argv);
    case PYDT_FLOAT:     return pydos_builtin_float_conv(argc, argv);
    case PYDT_STR:       return pydos_builtin_str_conv(argc, argv);
    case PYDT_LIST:      return pydos_builtin_list_conv(argc, argv);
    case PYDT_DICT:      return pydos_builtin_dict_conv(argc, argv);
    case PYDT_TUPLE:     return pydos_builtin_tuple_conv(argc, argv);
    case PYDT_SET:       return pydos_builtin_set_conv(argc, argv);
    case PYDT_BYTES:     return pydos_builtin_bytes_conv(argc, argv);
    case PYDT_INSTANCE:  return pydos_builtin_object_conv(argc, argv);
    case PYDT_CLASS:     return pydos_builtin_type(argc, argv);
    case PYDT_RANGE:     return pydos_builtin_range(argc, argv);
    case PYDT_FROZENSET: return pydos_builtin_frozenset_conv(argc, argv);
    case PYDT_COMPLEX:   return pydos_builtin_complex_conv(argc, argv);
    case PYDT_BYTEARRAY: return pydos_builtin_bytearray_conv(argc, argv);
    default:
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"type is not constructible");
        return (PyDosObj far *)0;
    }
}

static void raise_invalid_function_arg_count(PyDosObj far *callable,
                                             unsigned int total)
{
    char message[80];
    unsigned int name_len;
    const char far *name;

    name = callable->v.func.name != (const char far *)0
           ? callable->v.func.name
           : (const char far *)"function";
    name_len = (unsigned int)_fstrlen(name);
    if (name_len > 30) name_len = 30;
    _fmemcpy((char far *)message, name, name_len);
    sprintf(message + name_len, "() expected %u arguments, got %u",
            (unsigned int)callable->v.func.arg_count, total);
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR, (const char far *)message);
}

static unsigned int function_default_count(PyDosObj far *callable)
{
    PyDosObj far *defaults;
    defaults = callable->v.func.defaults;
    if (defaults == (PyDosObj far *)0) return 0;
    if ((PyDosType)defaults->type == PYDT_TUPLE)
        return defaults->v.tuple.len;
    if ((PyDosType)defaults->type == PYDT_LIST)
        return defaults->v.list.len;
    return 0;
}

static PyDosObj far *function_default_at(PyDosObj far *callable,
                                         unsigned int index)
{
    PyDosObj far *defaults;
    defaults = callable->v.func.defaults;
    if (defaults == (PyDosObj far *)0) return (PyDosObj far *)0;
    if ((PyDosType)defaults->type == PYDT_TUPLE &&
        index < defaults->v.tuple.len)
        return defaults->v.tuple.items[index];
    if ((PyDosType)defaults->type == PYDT_LIST &&
        index < defaults->v.list.len)
        return defaults->v.list.items[index];
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_obj_call(PyDosObj far *callable,
                                        unsigned int argc,
                                        PyDosObj far * far *argv)
{
    PyDosObj far *call_args[9];
    unsigned int total;
    unsigned int provided_total;
    unsigned int i;
    PyDosObj far *previous_closure;
    PyDosObj far *call_result;
    PyDosObj far * far *bound_args;
    PyDosObj far * far *dynamic_args;
    unsigned int target_count;

    if (callable == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object is not callable");
        return (PyDosObj far *)0;
    }
    if ((PyDosType)callable->type == PYDT_FUNCTION) {
        if (pydos_code_ref_kind(callable->v.func.code_ref) ==
            PYDOS_CODE_BUILTIN)
            return pydos_code_ref_call(callable->v.func.code_ref,
                                       argc, argv);
        total = argc;
        if (callable->v.func.bound_self != (PyDosObj far *)0) total++;
        provided_total = total;
        if (callable->v.func.signature_known) {
            unsigned int expected;
            unsigned int default_count;
            unsigned int required;
            expected = (unsigned int)callable->v.func.arg_count;
            default_count = function_default_count(callable);
            required = expected >= default_count
                       ? expected - default_count : expected;
            if (provided_total < required || provided_total > expected) {
                raise_invalid_function_arg_count(callable, provided_total);
                return (PyDosObj far *)0;
            }
        }
        target_count = callable->v.func.signature_known
            ? (unsigned int)callable->v.func.arg_count : total;
        dynamic_args = (PyDosObj far * far *)0;
        bound_args = call_args;
        if (target_count > 8U &&
            pydos_code_ref_kind(callable->v.func.code_ref) !=
                PYDOS_CODE_PBC) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"too many call arguments");
            return (PyDosObj far *)0;
        }
        if (target_count > 8U) {
            dynamic_args = (PyDosObj far * far *)pydos_mem_alloc(
                PYDOS_MEM_METADATA,
                (unsigned long)target_count * sizeof(PyDosObj far *));
            if (dynamic_args == (PyDosObj far * far *)0) {
                pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                                (const char far *)"cannot bind call arguments");
                return (PyDosObj far *)0;
            }
            bound_args = dynamic_args;
        }
        total = 0;
        if (callable->v.func.bound_self != (PyDosObj far *)0)
            bound_args[total++] = callable->v.func.bound_self;
        for (i = 0; i < argc; i++) bound_args[total++] = argv[i];
        if (callable->v.func.signature_known &&
            total < (unsigned int)callable->v.func.arg_count) {
            unsigned int expected;
            unsigned int default_count;
            unsigned int first_default;
            expected = (unsigned int)callable->v.func.arg_count;
            default_count = function_default_count(callable);
            first_default = total - (expected - default_count);
            while (total < expected) {
                bound_args[total++] = function_default_at(
                    callable, first_default++);
            }
        }
        previous_closure = pydos_active_closure;
        pydos_active_closure = callable->v.func.closure;
        call_result = pydos_code_ref_call(callable->v.func.code_ref,
                                          total, bound_args);
        pydos_active_closure = previous_closure;
        if (dynamic_args != (PyDosObj far * far *)0)
            pydos_far_free(dynamic_args);
        return call_result;
    }
    if ((PyDosType)callable->type == PYDT_CLASS) {
        PyDosObj far *abstracts = class_attr_lookup(
            callable, (const char far *)"__abstractmethods__");
        if (abstracts != (PyDosObj far *)0) {
            int has_abstracts = pydos_obj_is_truthy(abstracts);
            PYDOS_DECREF(abstracts);
            if (has_abstracts) {
                pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"cannot instantiate abstract class");
                return (PyDosObj far *)0;
            }
        }
    }
    if ((PyDosType)callable->type == PYDT_CLASS ||
        (PyDosType)callable->type == PYDT_INSTANCE) {
        if (argc >= 8) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"too many call arguments");
            return (PyDosObj far *)0;
        }
        call_args[0] = callable;
        for (i = 0; i < argc; i++) call_args[i + 1] = argv[i];
        return pydos_obj_call_method((const char far *)"__call__",
                                     argc + 1, call_args);
    }
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"object is not callable");
    return (PyDosObj far *)0;
}

static PyDosObj far *call_sequence_item(PyDosObj far *sequence,
                                         unsigned int index)
{
    if (sequence == (PyDosObj far *)0) return (PyDosObj far *)0;
    if ((PyDosType)sequence->type == PYDT_LIST) {
        if (index >= sequence->v.list.len) return (PyDosObj far *)0;
        return sequence->v.list.items[index];
    }
    if ((PyDosType)sequence->type == PYDT_TUPLE) {
        if (index >= sequence->v.tuple.len) return (PyDosObj far *)0;
        return sequence->v.tuple.items[index];
    }
    return (PyDosObj far *)0;
}

static unsigned int call_sequence_len(PyDosObj far *sequence)
{
    if (sequence == (PyDosObj far *)0) return 0;
    if ((PyDosType)sequence->type == PYDT_LIST)
        return sequence->v.list.len;
    if ((PyDosType)sequence->type == PYDT_TUPLE)
        return sequence->v.tuple.len;
    return 0;
}

PyDosObj far * PYDOS_API pydos_obj_call_ex(PyDosObj far *callable,
                                            PyDosObj far *positional,
                                            PyDosObj far *keywords)
{
    PyDosObj far *bound[8];
    unsigned char assigned[8];
    unsigned int expected;
    unsigned int positional_count;
    unsigned int cursor;
    unsigned int i;
    int star_index;
    int dstar_index;
    PyDosObj far *star_values;
    PyDosObj far *dstar_values;
    PyDosObj far *result;
    PyDosObj far *previous_closure;

    if (callable == (PyDosObj far *)0 ||
        (PyDosType)callable->type != PYDT_FUNCTION ||
        pydos_code_ref_kind(callable->v.func.code_ref) ==
            PYDOS_CODE_BUILTIN ||
        callable->v.func.param_spec == (PyDosParamSpec far *)0) {
        if (keywords != (PyDosObj far *)0 &&
            (PyDosType)keywords->type == PYDT_DICT &&
            keywords->v.dict.used != 0) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"callable has no keyword signature");
            return (PyDosObj far *)0;
        }
        return pydos_obj_call(callable, call_sequence_len(positional),
                              positional && positional->type == PYDT_TUPLE
                              ? positional->v.tuple.items
                              : positional->v.list.items);
    }

    expected = callable->v.func.arg_count;
    if (expected > 8) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"too many call arguments");
        return (PyDosObj far *)0;
    }
    memset(assigned, 0, sizeof(assigned));
    memset(bound, 0, sizeof(bound));
    star_index = -1;
    dstar_index = -1;
    star_values = pydos_list_new(2);
    dstar_values = pydos_dict_new(4);

    for (i = 0; i < expected; i++) {
        unsigned int flags = function_param_flags(callable, i);
        if (flags & 1L) star_index = (int)i;
        if (flags & 2L) dstar_index = (int)i;
    }
    cursor = 0;
    if (callable->v.func.bound_self != (PyDosObj far *)0 && expected > 0) {
        bound[0] = callable->v.func.bound_self;
        assigned[0] = 1;
        cursor = 1;
    }

    positional_count = call_sequence_len(positional);
    for (i = 0; i < positional_count; i++) {
        PyDosObj far *value = call_sequence_item(positional, i);
        int slot = -1;
        while (cursor < expected) {
            unsigned int flags = function_param_flags(callable, cursor);
            if (!(flags & (1L | 2L | 8L)) && !assigned[cursor]) {
                slot = (int)cursor++;
                break;
            }
            cursor++;
        }
        if (slot >= 0) {
            bound[slot] = value;
            assigned[slot] = 1;
        } else if (star_index >= 0) {
            PyDosObj far *ignored = pydos_list_append(star_values, value);
            PYDOS_DECREF(ignored);
        } else {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"too many positional arguments");
            goto call_ex_fail;
        }
    }

    if (keywords != (PyDosObj far *)0 &&
        (PyDosType)keywords->type == PYDT_DICT) {
        unsigned int entry;
        for (entry = 0; entry < keywords->v.dict.size; entry++) {
            PyDosDictEntry far *item = &keywords->v.dict.entries[entry];
            int slot = -1;
            if (item->key == (PyDosObj far *)0) continue;
            if ((PyDosType)item->key->type != PYDT_STR) {
                pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                                (const char far *)"keyword names must be strings");
                goto call_ex_fail;
            }
            for (i = 0; i < expected; i++) {
                unsigned int flags = function_param_flags(callable, i);
                if (!(flags & (1U | 2U | 4U)) &&
                    function_param_name_equal(callable, i, item->key)) {
                    slot = (int)i;
                    break;
                }
            }
            if (slot >= 0) {
                if (assigned[slot]) {
                    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                                    (const char far *)"multiple values for argument");
                    goto call_ex_fail;
                }
                bound[slot] = item->value;
                assigned[slot] = 1;
            } else if (dstar_index >= 0) {
                pydos_dict_set(dstar_values, item->key, item->value);
            } else {
                pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                                (const char far *)"unexpected keyword argument");
                goto call_ex_fail;
            }
        }
    }

    if (star_index >= 0) {
        pydos_list_to_tuple(star_values);
        bound[star_index] = star_values;
        assigned[star_index] = 1;
    }
    if (dstar_index >= 0) {
        bound[dstar_index] = dstar_values;
        assigned[dstar_index] = 1;
    }
    {
        unsigned int defaults = function_default_count(callable);
        unsigned int first_default = expected >= defaults
                                     ? expected - defaults : 0;
        for (i = 0; i < expected; i++) {
            if (!assigned[i] && i >= first_default) {
                bound[i] = function_default_at(callable, i - first_default);
                assigned[i] = 1;
            }
            if (!assigned[i]) {
                pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                                (const char far *)"missing required argument");
                goto call_ex_fail;
            }
        }
    }

    previous_closure = pydos_active_closure;
    pydos_active_closure = callable->v.func.closure;
    result = pydos_code_ref_call(callable->v.func.code_ref,
                                 expected, bound);
    pydos_active_closure = previous_closure;
    PYDOS_DECREF(star_values);
    PYDOS_DECREF(dstar_values);
    return result;

call_ex_fail:
    PYDOS_DECREF(star_values);
    PYDOS_DECREF(dstar_values);
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_call_pos_append(PyDosObj far *positional,
                                                PyDosObj far *value)
{
    return pydos_list_append(positional, value);
}

PyDosObj far * PYDOS_API pydos_call_pos_extend(PyDosObj far *positional,
                                                PyDosObj far *iterable)
{
    PyDosObj far *iter = pydos_obj_get_iter(iterable);
    PyDosObj far *item;
    if (iter == (PyDosObj far *)0) return (PyDosObj far *)0;
    while ((item = pydos_obj_iter_next(iter)) != (PyDosObj far *)0) {
        PyDosObj far *ignored = pydos_list_append(positional, item);
        PYDOS_DECREF(ignored);
        PYDOS_DECREF(item);
    }
    PYDOS_DECREF(iter);
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_call_kw_set(PyDosObj far *keywords,
                                           PyDosObj far *name,
                                           PyDosObj far *value)
{
    if (pydos_dict_contains(keywords, name)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"multiple values for keyword");
        return (PyDosObj far *)0;
    }
    pydos_dict_set(keywords, name, value);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_call_kw_update(PyDosObj far *keywords,
                                              PyDosObj far *mapping)
{
    unsigned int i;
    if (mapping == (PyDosObj far *)0 || mapping->type != PYDT_DICT) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"argument after ** must be a mapping");
        return (PyDosObj far *)0;
    }
    for (i = 0; i < mapping->v.dict.size; i++) {
        PyDosDictEntry far *entry = &mapping->v.dict.entries[i];
        if (entry->key == (PyDosObj far *)0) continue;
        if (pydos_dict_contains(keywords, entry->key)) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"multiple values for keyword");
            return (PyDosObj far *)0;
        }
        pydos_dict_set(keywords, entry->key, entry->value);
    }
    return pydos_obj_new_none();
}


static void module_set_attr(PyDosObj far *module, const char far *name,
                            PyDosObj far *value)
{
    PyDosObj far *key;
    if (module == (PyDosObj far *)0 || module->type != PYDT_INSTANCE ||
        module->v.instance.attrs == (PyDosObj far *)0) return;
    key = pydos_obj_new_str(name, (unsigned int)_fstrlen(name));
    if (key != (PyDosObj far *)0) {
        pydos_dict_set(module->v.instance.attrs, key, value);
        PYDOS_DECREF(key);
    }
}

PyDosObj far * PYDOS_API pydos_import_module(PyDosObj far *name)
{
    PyDosObj far *module;
    PyDosObj far *value;
    PyDosObj far *parts;
    PyDosObj far *part;

    if (name == (PyDosObj far *)0 || name->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_IMPORT_ERROR,
                        (const char far *)"invalid module name");
        return (PyDosObj far *)0;
    }
    if (!(name->v.str.len == 3 &&
          _fmemcmp(name->v.str.data, (const char far *)"sys", 3) == 0)) {
        pydos_exc_raise(PYDOS_EXC_MODULE_NOT_FOUND,
                        (const char far *)"module is not available");
        return (PyDosObj far *)0;
    }

    module = pydos_obj_alloc_type(PYDT_INSTANCE);
    if (module == (PyDosObj far *)0) return (PyDosObj far *)0;
    module->v.instance.attrs = pydos_dict_new(8);
    module->v.instance.vtable = (PyDosVTable far *)0;
    module->v.instance.cls = (PyDosObj far *)0;

    parts = pydos_list_new(5);
    part = pydos_obj_new_int(3); pydos_list_append(parts, part); PYDOS_DECREF(part);
    part = pydos_obj_new_int(12); pydos_list_append(parts, part); PYDOS_DECREF(part);
    part = pydos_obj_new_int(13); pydos_list_append(parts, part); PYDOS_DECREF(part);
    part = pydos_obj_new_str((const char far *)"final", 5);
    pydos_list_append(parts, part); PYDOS_DECREF(part);
    part = pydos_obj_new_int(0); pydos_list_append(parts, part); PYDOS_DECREF(part);
    pydos_list_to_tuple(parts);
    module_set_attr(module, (const char far *)"version_info", parts);
    PYDOS_DECREF(parts);

    value = pydos_obj_new_str(
        (const char far *)"3.12.13 (PyDOS, Python 3.12 compatible)", 39);
    module_set_attr(module, (const char far *)"version", value);
    PYDOS_DECREF(value);
    value = pydos_monitoring_new();
    if (value != (PyDosObj far *)0) {
        module_set_attr(module, (const char far *)"monitoring", value);
        PYDOS_DECREF(value);
    }
    return module;
}

PyDosObj far * PYDOS_API pydos_obj_call_method(
    const char far *method_name,
    unsigned int argc,
    PyDosObj far * far *argv)
{
    PyDosObj far *self;
    unsigned char stype;

    if (argc < 1 || argv == (PyDosObj far * far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"method call requires an object");
        return (PyDosObj far *)0;
    }

    self = argv[0];
    if (self == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"method call on null object");
        return (PyDosObj far *)0;
    }

    stype = self->type;

    if (stype == PYDT_SUPER) {
        PyDosObj far *resolved;
        PyDosObj far *result;
        resolved = pydos_super_get_attr(self, method_name);
        if (resolved == (PyDosObj far *)0) return (PyDosObj far *)0;
        result = pydos_obj_call(resolved,
            argc > 0 ? argc - 1 : 0,
            argc > 0 ? argv + 1 : argv);
        PYDOS_DECREF(resolved);
        return result;
    }

    /* Method-call syntax must be equivalent to attribute lookup followed by
     * calling the resolved value.  This single path preserves descriptors,
     * instance shadowing, class mutation and C3 ordering. */
    if (stype == PYDT_INSTANCE &&
        self->v.instance.cls != (PyDosObj far *)0) {
        PyDosObj far *resolved;
        PyDosObj far *result;
        resolved = pydos_obj_get_attr(self, method_name);
        if (resolved == (PyDosObj far *)0) return (PyDosObj far *)0;
        result = pydos_obj_call(resolved,
            argc > 0 ? argc - 1 : 0,
            argc > 0 ? argv + 1 : argv);
        PYDOS_DECREF(resolved);
        return result;
    }
    if (stype == PYDT_CLASS &&
        _fstrcmp(method_name, (const char far *)"__call__") != 0) {
        PyDosObj far *class_value;
        PyDosObj far *result;
        class_value = pydos_obj_get_attr(self, method_name);
        if (class_value == (PyDosObj far *)0) return (PyDosObj far *)0;
        result = pydos_obj_call(class_value,
            argc > 0 ? argc - 1 : 0,
            argc > 0 ? argv + 1 : argv);
        PYDOS_DECREF(class_value);
        return result;
    }

    /* Runtime class objects are callable.  Static constructor calls still
     * use the compiler fast path; this path handles aliases and values that
     * flow dynamically, e.g. factory = Box; factory(). */
    if (stype == PYDT_CLASS &&
        _fstrcmp(method_name, (const char far *)"__call__") == 0) {
        PyDosObj far *instance;
        PyDosObj far *init_result;
        PyDosObj far *call_args[8];
        PyDosObj far *new_probe;
        PyDosMethodSlot far *new_slot;
        unsigned int i;

        if (self->v.cls.runtime_type_tag >= 0) {
            return call_builtin_type(
                (unsigned int)self->v.cls.runtime_type_tag,
                (int)argc - 1, argv + 1);
        }
        if (argc > 8) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"too many constructor arguments");
            return (PyDosObj far *)0;
        }
        new_probe = class_attr_lookup(self, (const char far *)"__new__");
        new_slot = self->v.cls.vtable != (PyDosVTable far *)0
                   ? pydos_vtable_lookup_slot(
                         self->v.cls.vtable,
                         method_name_hash((const char far *)"__new__"))
                   : (PyDosMethodSlot far *)0;
        if (new_probe != (PyDosObj far *)0 ||
            new_slot != (PyDosMethodSlot far *)0) {
            PyDosObj far *new_callable;
            if (new_probe != (PyDosObj far *)0) PYDOS_DECREF(new_probe);
            new_callable = pydos_obj_get_attr(
                self, (const char far *)"__new__");
            if (new_callable == (PyDosObj far *)0)
                return (PyDosObj far *)0;
            call_args[0] = self;
            for (i = 1; i < argc; i++) call_args[i] = argv[i];
            instance = pydos_obj_call(new_callable, argc, call_args);
            PYDOS_DECREF(new_callable);
            if (instance == (PyDosObj far *)0)
                return (PyDosObj far *)0;
        } else {
            instance = pydos_instance_new(self);
            if (instance == (PyDosObj far *)0)
                return (PyDosObj far *)0;
        }

        /* Python only invokes __init__ when __new__ returned an instance of
         * the requested class (or one of its subclasses). */
        if ((PyDosType)instance->type != PYDT_INSTANCE ||
            instance->v.instance.cls == (PyDosObj far *)0 ||
            !pydos_class_is_subclass(instance->v.instance.cls, self))
            return instance;

        {
            PyDosObj far *materialized_init;
            materialized_init = class_attr_lookup(
                instance->v.instance.cls,
                (const char far *)"__init__");
            if (materialized_init != (PyDosObj far *)0 &&
                (PyDosType)materialized_init->type == PYDT_FUNCTION) {
                PyDosObj far *bound_init = pydos_func_bind(
                    materialized_init, instance);
                PYDOS_DECREF(materialized_init);
                init_result = bound_init != (PyDosObj far *)0
                    ? pydos_obj_call(bound_init, argc - 1, argv + 1)
                    : (PyDosObj far *)0;
                if (bound_init != (PyDosObj far *)0)
                    PYDOS_DECREF(bound_init);
                if (init_result == (PyDosObj far *)0) {
                    PYDOS_DECREF(instance);
                    return (PyDosObj far *)0;
                }
                if ((PyDosType)init_result->type != PYDT_NONE) {
                    PYDOS_DECREF(init_result);
                    PYDOS_DECREF(instance);
                    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__init__() must return None");
                    return (PyDosObj far *)0;
                }
                PYDOS_DECREF(init_result);
                return instance;
            }
            if (materialized_init != (PyDosObj far *)0)
                PYDOS_DECREF(materialized_init);
        }

        if (instance->v.instance.vtable != (PyDosVTable far *)0) {
            void (far *init_entry)(void) = pydos_vtable_get_special(
                instance->v.instance.vtable, VSLOT_INIT);
            if (init_entry != (void (far *)(void))0) {
                call_args[0] = instance;
                for (i = 1; i < argc; i++) call_args[i] = argv[i];
                init_result = call_vtable_method(init_entry, argc, call_args);
                if (init_result != (PyDosObj far *)0 &&
                    (PyDosType)init_result->type != PYDT_NONE) {
                    PYDOS_DECREF(init_result);
                    PYDOS_DECREF(instance);
                    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__init__() must return None");
                    return (PyDosObj far *)0;
                }
                if (init_result != (PyDosObj far *)0)
                    PYDOS_DECREF(init_result);
            }
        }
        return instance;
    }

    if (stype == PYDT_MEMORYVIEW) {
        if (_fstrcmp(method_name, (const char far *)"tobytes") == 0)
            return pydos_memoryview_tobytes(self);
        if (_fstrcmp(method_name, (const char far *)"release") == 0)
            return pydos_memoryview_release(self);
    }

    /* Generated Python implementations for primitive types are registered
     * here at module initialization.  This path is checked before legacy C
     * fallbacks so high-level stdlib behavior has a single Python source. */
    if (stype < PYDT_MAX &&
        pydos_builtin_vtables[stype] != (PyDosVTable far *)0) {
        void (far *builtin_func)(void);
        builtin_func = pydos_vtable_lookup(pydos_builtin_vtables[stype],
                                           method_name_hash(method_name));
        if (builtin_func != (void (far *)(void))0) {
            return call_vtable_method(builtin_func, argc, argv);
        }
    }

    /* ---- List methods ---- */
    if (stype == PYDT_LIST) {
        if (_fstrcmp(method_name, (const char far *)"append") == 0) {
            if (argc > 1) return pydos_list_append(self, argv[1]);
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"append expected 1 argument");
            return (PyDosObj far *)0;
        }
        if (_fstrcmp(method_name, (const char far *)"pop") == 0) {
            return pydos_list_pop_m(self,
                argc > 1 ? argv[1] : (PyDosObj far *)0);
        }
        if (_fstrcmp(method_name, (const char far *)"insert") == 0) {
            if (argc > 2) return pydos_list_insert_m(self, argv[1], argv[2]);
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"insert expected 2 arguments");
            return (PyDosObj far *)0;
        }
        if (_fstrcmp(method_name, (const char far *)"reverse") == 0) {
            return pydos_list_reverse_m(self);
        }
        if (_fstrcmp(method_name, (const char far *)"sort") == 0) {
            pydos_list_sort(self);
            /* reverse=True passed as positional arg after self */
            if (argc > 1 && argv[1] != (PyDosObj far *)0 &&
                pydos_obj_is_truthy(argv[1])) {
                pydos_list_reverse(self);
            }
            if (pydos_exc_pending()) return (PyDosObj far *)0;
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            return pydos_list_clear_m(self);
        }
        if (_fstrcmp(method_name, (const char far *)"remove") == 0) {
            if (argc > 1) {
                if (pydos_list_remove(self, argv[1]) != 0) {
                    pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"list.remove(x): x not in list");
                    return (PyDosObj far *)0;
                }
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"copy") == 0) {
            return pydos_list_copy(self);
        }
    }

    /* ---- Dict methods ---- */
    if (stype == PYDT_DICT) {
        if (_fstrcmp(method_name, (const char far *)"get") == 0) {
            PyDosObj far *result;
            if (argc < 2) return pydos_obj_new_none();
            result = pydos_dict_get(self, argv[1]);
            if (result != (PyDosObj far *)0) return result;
            if (argc > 2) {
                /* Method-call results use owned-reference semantics. */
                PYDOS_INCREF(argv[2]);
                return argv[2];
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            pydos_dict_clear(self);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"items") == 0) {
            return pydos_dict_items(self);
        }
        if (_fstrcmp(method_name, (const char far *)"keys") == 0) {
            return pydos_dict_keys(self);
        }
        if (_fstrcmp(method_name, (const char far *)"values") == 0) {
            return pydos_dict_values(self);
        }
    }

    /* ---- Set methods ---- */
    if (stype == PYDT_SET) {
        if (_fstrcmp(method_name, (const char far *)"add") == 0) {
            if (argc > 1) return pydos_set_add_m(self, argv[1]);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"remove") == 0) {
            if (argc > 1) return pydos_set_remove_m(self, argv[1]);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"discard") == 0) {
            if (argc > 1) return pydos_set_discard_m(self, argv[1]);
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            return pydos_set_clear_m(self);
        }
        if (_fstrcmp(method_name, (const char far *)"pop") == 0) {
            PyDosObj far *keys = pydos_dict_keys(self);
            if (keys != (PyDosObj far *)0 && keys->v.list.len > 0) {
                PyDosObj far *item = pydos_list_get(keys, 0L);
                pydos_dict_delete(self, item);
                PYDOS_DECREF(keys);
                return item;
            }
            if (keys != (PyDosObj far *)0) PYDOS_DECREF(keys);
            pydos_exc_raise(PYDOS_EXC_KEY_ERROR,
                             (const char far *)"pop from an empty set");
            return (PyDosObj far *)0;
        }
    }

    /* ---- Complex methods ---- */
    if ((PyDosType)self->type == PYDT_COMPLEX) {
        if (_fstrcmp(method_name, (const char far *)"conjugate") == 0) {
            return pydos_complex_conjugate(self);
        }
        return pydos_obj_new_none();
    }

    /* ---- Bytearray methods ---- */
    if ((PyDosType)self->type == PYDT_BYTEARRAY) {
        if (_fstrcmp(method_name, (const char far *)"append") == 0) {
            if (argc >= 2) {
                return pydos_bytearray_append_m(self, argv[1]);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"insert") == 0) {
            if (argc >= 3) {
                return pydos_bytearray_insert_m(self, argv[1], argv[2]);
            }
            return pydos_obj_new_none();
        }
        if (_fstrcmp(method_name, (const char far *)"pop") == 0) {
            PyDosObj far *index = argc >= 2 ? argv[1] : (PyDosObj far *)0;
            return pydos_bytearray_pop_m(self, index);
        }
        if (_fstrcmp(method_name, (const char far *)"clear") == 0) {
            return pydos_bytearray_clear_m(self);
        }
        return pydos_obj_new_none();
    }

    /* ---- Generator / Coroutine methods ---- */
    if (stype == PYDT_GENERATOR || stype == PYDT_COROUTINE) {
        if (_fstrcmp(method_name, (const char far *)"send") == 0) {
            PyDosObj far *val = (argc > 1) ? argv[1] : pydos_obj_new_none();
            return pydos_gen_send(self, val);
        }
        if (_fstrcmp(method_name, (const char far *)"throw") == 0) {
            /* g.throw(exc_instance) — extract type code and message */
            if (argc > 1 && argv[1] != (PyDosObj far *)0 &&
                (PyDosType)argv[1]->type == PYDT_EXCEPTION) {
                int tcode = argv[1]->v.exc.type_code;
                const char far *msg = (const char far *)"";
                if (argv[1]->v.exc.message != (PyDosObj far *)0 &&
                    (PyDosType)argv[1]->v.exc.message->type == PYDT_STR) {
                    msg = argv[1]->v.exc.message->v.str.data;
                }
                return pydos_gen_throw(self, tcode, msg);
            }
            /* g.throw() with no valid exception — raise TypeError */
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                (const char far *)"throw() argument must be an exception instance");
            return (PyDosObj far *)0;
        }
        if (_fstrcmp(method_name, (const char far *)"close") == 0) {
            pydos_gen_close(self);
            return pydos_obj_new_none();
        }
    }

    /* ---- Int methods ---- */
    if (stype == PYDT_INT) {
        if (_fstrcmp(method_name, (const char far *)"bit_length") == 0) {
            return pydos_int_bit_length(self);
        }
        if (_fstrcmp(method_name, (const char far *)"bit_count") == 0) {
            return pydos_int_bit_count(self);
        }
    }

    /* ---- Float methods ---- */
    if (stype == PYDT_FLOAT) {
        if (_fstrcmp(method_name, (const char far *)"is_integer") == 0) {
            return pydos_float_is_integer(self);
        }
    }

    /* ---- Instance methods (vtable dispatch) ---- */
    if (stype == PYDT_INSTANCE) {
        PyDosVTable far *vt = self->v.instance.vtable;
        if (vt != (PyDosVTable far *)0) {
            unsigned int mhash;
            PyDosMethodSlot far *method_slot;

            /* Compute hash of method name */
            mhash = method_name_hash(method_name);

            method_slot = pydos_vtable_lookup_slot(vt, mhash);
            if (method_slot != (PyDosMethodSlot far *)0) {
                return pydos_code_ref_call(
                    method_slot->code_ref, argc, argv);
            }
        }
    }

    return pydos_obj_new_none();
}

/* ------------------------------------------------------------------ */
/* pydos_obj_get_attr — get an instance attribute by name              */
/* ------------------------------------------------------------------ */
static PyDosObj far *dict_attr_lookup(PyDosObj far *dict,
                                      const char far *attr_name)
{
    PyDosObj far *key;
    PyDosObj far *value;

    if (dict == (PyDosObj far *)0 || (PyDosType)dict->type != PYDT_DICT)
        return (PyDosObj far *)0;
    key = pydos_obj_new_str(attr_name,
                            (unsigned int)_fstrlen(attr_name));
    if (key == (PyDosObj far *)0) return (PyDosObj far *)0;
    value = pydos_dict_get(dict, key);
    PYDOS_DECREF(key);
    return value;
}

static PyDosObj far *class_attr_lookup(PyDosObj far *cls,
                                       const char far *attr_name)
{
    PyDosObj far *value;
    unsigned char i;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS)
        return (PyDosObj far *)0;
    for (i = 0; i < cls->v.cls.mro_len; i++) {
        PyDosObj far *mro_class = cls->v.cls.mro[i];
        value = dict_attr_lookup(mro_class->v.cls.class_attrs, attr_name);
        if (value != (PyDosObj far *)0) return value;
        /* A compiled method defined by this exact MRO entry shadows values
         * from later bases.  Returning no materialized value lets the caller
         * use the vtable fast path without allowing a base __dict__ entry to
         * jump ahead of the subclass definition. */
        if (class_own_method_slot(mro_class, attr_name) !=
            (PyDosMethodSlot far *)0)
            return (PyDosObj far *)0;
    }
    return (PyDosObj far *)0;
}

static PyDosMethodSlot far *class_own_method_slot(
                                          PyDosObj far *cls,
                                          const char far *method_name)
{
    PyDosVTable far *vtable;
    unsigned int hash;
    unsigned char i;
    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS ||
        method_name == (const char far *)0)
        return (PyDosMethodSlot far *)0;
    vtable = cls->v.cls.vtable;
    if (vtable == (PyDosVTable far *)0) return (PyDosMethodSlot far *)0;
    hash = method_name_hash(method_name);
    for (i = 0; i < vtable->method_count; i++) {
        if (vtable->methods[i].name_hash == hash)
            return &vtable->methods[i];
    }
    return (PyDosMethodSlot far *)0;
}

PyDosObj far * PYDOS_API pydos_obj_call_method_guarded(
    const char far *method_name,
    void (far *expected_func)(void),
    unsigned int argc,
    PyDosObj far * far *argv)
{
    PyDosObj far *self;
    PyDosObj far *key;
    unsigned char i;

    if (argc < 1 || argv == (PyDosObj far * far *)0 ||
        expected_func == (void (far *)(void))0)
        return pydos_obj_call_method(method_name, argc, argv);
    self = argv[0];
    if (self == (PyDosObj far *)0 ||
        (PyDosType)self->type != PYDT_INSTANCE ||
        self->v.instance.cls == (PyDosObj far *)0)
        return pydos_obj_call_method(method_name, argc, argv);

    /* A non-data compiled method may be shadowed by the instance. */
    key = pydos_obj_new_str(method_name,
                            (unsigned int)_fstrlen(method_name));
    if (key == (PyDosObj far *)0)
        return pydos_obj_call_method(method_name, argc, argv);
    if (self->v.instance.attrs != (PyDosObj far *)0 &&
        pydos_dict_contains(self->v.instance.attrs, key)) {
        PYDOS_DECREF(key);
        return pydos_obj_call_method(method_name, argc, argv);
    }
    PYDOS_DECREF(key);

    /* Walk the authoritative C3 sequence.  Any materialized entry needs the
     * full descriptor path; an unchanged compiled entry can call directly. */
    for (i = 0; i < self->v.instance.cls->v.cls.mro_len; i++) {
        PyDosObj far *mro_class = self->v.instance.cls->v.cls.mro[i];
        PyDosObj far *materialized = dict_attr_lookup(
            mro_class->v.cls.class_attrs, method_name);
        PyDosMethodSlot far *compiled_slot;
        void (far *compiled)(void);
        if (materialized != (PyDosObj far *)0) {
            PYDOS_DECREF(materialized);
            return pydos_obj_call_method(method_name, argc, argv);
        }
        compiled_slot = class_own_method_slot(mro_class, method_name);
        compiled = compiled_slot != (PyDosMethodSlot far *)0
                   ? pydos_code_ref_native_entry(
                         compiled_slot->code_ref)
                   : (void (far *)(void))0;
        if (compiled != (void (far *)(void))0) {
            if (compiled == expected_func) {
        if (PYDOS_METHOD_HAS_SIGNATURE(compiled_slot) &&
                    argc != (unsigned int)compiled_slot->arg_count) {
                    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                                    (const char far *)"invalid method argument count");
                    return (PyDosObj far *)0;
                }
                return call_vtable_method(expected_func, argc, argv);
            }
            return pydos_obj_call_method(method_name, argc, argv);
        }
    }
    return pydos_obj_call_method(method_name, argc, argv);
}

static int descriptor_has_slot(PyDosObj far *descriptor, int slot)
{
    if (descriptor == (PyDosObj far *)0 ||
        slot < 0 || slot >= VSLOT_COUNT)
        return 0;
    if ((PyDosType)descriptor->type != PYDT_INSTANCE ||
        descriptor->v.instance.vtable == (PyDosVTable far *)0)
        return 0;
    return pydos_vtable_get_special(descriptor->v.instance.vtable,
                                    (unsigned int)slot) !=
           (void (far *)(void))0;
}

static PyDosObj far *descriptor_call_get(PyDosObj far *descriptor,
                                         PyDosObj far *instance,
                                         PyDosObj far *owner)
{
    PyDosObj far *none_obj;
    PyDosObj far *args[3];
    PyDosObj far *result;

    none_obj = pydos_obj_new_none();
    args[0] = descriptor;
    args[1] = instance != (PyDosObj far *)0 ? instance : none_obj;
    args[2] = owner != (PyDosObj far *)0 ? owner : none_obj;
    result = pydos_obj_call_method((const char far *)"__get__", 3, args);
    PYDOS_DECREF(none_obj);
    return result;
}

static int descriptor_call_set(PyDosObj far *descriptor,
                               PyDosObj far *instance,
                               PyDosObj far *value)
{
    PyDosObj far *args[3];
    PyDosObj far *result;

    args[0] = descriptor;
    args[1] = instance;
    args[2] = value;
    result = pydos_obj_call_method((const char far *)"__set__", 3, args);
    if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
    return result != (PyDosObj far *)0;
}

static int descriptor_call_delete(PyDosObj far *descriptor,
                                  PyDosObj far *instance)
{
    PyDosObj far *args[2];
    PyDosObj far *result;

    args[0] = descriptor;
    args[1] = instance;
    result = pydos_obj_call_method((const char far *)"__delete__", 2, args);
    if (result != (PyDosObj far *)0) PYDOS_DECREF(result);
    return result != (PyDosObj far *)0;
}

static PyDosObj far * PYDOS_API object_new_callable(
    int argc, PyDosObj far * far *argv)
{
    if (argc != 1) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object.__new__() takes one argument");
        return (PyDosObj far *)0;
    }
    return pydos_instance_new(argv[0]);
}

static PyDosObj far * PYDOS_API object_init_method(PyDosObj far *self)
{
    (void)self;
    return pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API object_init_subclass_method(
    PyDosObj far *cls)
{
    (void)cls;
    return pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API object_getattribute_method(
    PyDosObj far *self, PyDosObj far *name)
{
    if (name == (PyDosObj far *)0 || (PyDosType)name->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be str");
        return (PyDosObj far *)0;
    }
    {
        PyDosObj far *result;
        suppress_getattr_fallback++;
        result = pydos_obj_get_attr_default(self, name->v.str.data);
        suppress_getattr_fallback--;
        return result;
    }
}

static PyDosObj far * PYDOS_API object_setattr_method(
    PyDosObj far *self, PyDosObj far *name, PyDosObj far *value)
{
    if (name == (PyDosObj far *)0 || (PyDosType)name->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be str");
        return (PyDosObj far *)0;
    }
    pydos_obj_set_attr_default(self, name->v.str.data, value);
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    return pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API object_delattr_method(
    PyDosObj far *self, PyDosObj far *name)
{
    if (name == (PyDosObj far *)0 || (PyDosType)name->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be str");
        return (PyDosObj far *)0;
    }
    pydos_obj_del_attr_default(self, name->v.str.data);
    if (pydos_exc_pending()) return (PyDosObj far *)0;
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_super_new(PyDosObj far *start_type,
                                          PyDosObj far *bound_obj)
{
    PyDosObj far *actual_type;
    PyDosObj far *result;
    unsigned char i;
    int found;

    if (start_type == (PyDosObj far *)0 ||
        (PyDosType)start_type->type != PYDT_CLASS ||
        bound_obj == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid super() arguments");
        return (PyDosObj far *)0;
    }
    actual_type = (PyDosType)bound_obj->type == PYDT_INSTANCE
                  ? bound_obj->v.instance.cls : bound_obj;
    if (actual_type == (PyDosObj far *)0 ||
        (PyDosType)actual_type->type != PYDT_CLASS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"super() object is not an instance or type");
        return (PyDosObj far *)0;
    }
    found = 0;
    for (i = 0; i < actual_type->v.cls.mro_len; i++) {
        if (actual_type->v.cls.mro[i] == start_type) {
            found = 1;
            break;
        }
    }
    if (!found) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"super() start type is outside the MRO");
        return (PyDosObj far *)0;
    }
    result = pydos_obj_alloc_type(PYDT_SUPER);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    result->v.super_obj.start_type = start_type;
    result->v.super_obj.bound_obj = bound_obj;
    PYDOS_INCREF(start_type);
    PYDOS_INCREF(bound_obj);
    return result;
}

PyDosObj far * PYDOS_API pydos_super_get_attr(PyDosObj far *super_obj,
                                               const char far *attr_name)
{
    PyDosObj far *start_type;
    PyDosObj far *bound_obj;
    PyDosObj far *actual_type;
    unsigned char i;
    int after_start;

    if (super_obj == (PyDosObj far *)0 ||
        (PyDosType)super_obj->type != PYDT_SUPER) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid super object");
        return (PyDosObj far *)0;
    }
    start_type = super_obj->v.super_obj.start_type;
    bound_obj = super_obj->v.super_obj.bound_obj;
    actual_type = (PyDosType)bound_obj->type == PYDT_INSTANCE
                  ? bound_obj->v.instance.cls : bound_obj;
    after_start = 0;

    for (i = 0; i < actual_type->v.cls.mro_len; i++) {
        PyDosObj far *owner = actual_type->v.cls.mro[i];
        PyDosObj far *value;
        PyDosMethodSlot far *slot;
        if (!after_start) {
            if (owner == start_type) after_start = 1;
            continue;
        }

        value = dict_attr_lookup(owner->v.cls.class_attrs, attr_name);
        if (value != (PyDosObj far *)0) {
            if (descriptor_has_slot(value, VSLOT_GET)) {
                PyDosObj far *described;
                described = descriptor_call_get(
                    value,
                    (PyDosType)bound_obj->type == PYDT_CLASS
                    ? (PyDosObj far *)0 : bound_obj,
                    actual_type);
                PYDOS_DECREF(value);
                return described;
            }
            if ((PyDosType)value->type == PYDT_FUNCTION &&
                _fstrcmp(attr_name, (const char far *)"__new__") != 0) {
                PyDosObj far *bound = pydos_func_bind(value, bound_obj);
                PYDOS_DECREF(value);
                return bound;
            }
            return value;
        }

        slot = class_own_method_slot(owner, attr_name);
        if (slot != (PyDosMethodSlot far *)0) {
            PyDosObj far *method;
            if (_fstrcmp(attr_name, (const char far *)"__new__") == 0)
                method = pydos_func_new_from_code_ref(
                    slot->code_ref, attr_name);
            else
                method = pydos_bound_method_new_from_code_ref(
                    slot->code_ref, bound_obj, attr_name);
            if (method != (PyDosObj far *)0 &&
                PYDOS_METHOD_HAS_SIGNATURE(slot))
                pydos_func_set_arg_count(method, slot->arg_count);
            if (method != (PyDosObj far *)0 &&
                slot->defaults != (PyDosObj far *)0)
                pydos_func_set_defaults(method, slot->defaults);
            return method;
        }

        if (owner->v.cls.runtime_type_tag == PYDT_INSTANCE) {
            if (_fstrcmp(attr_name, (const char far *)"__new__") == 0)
                return pydos_func_new_builtin(
                    (void (far *)(void))object_new_callable,
                    (const char far *)"__new__");
            if (_fstrcmp(attr_name, (const char far *)"__init__") == 0) {
                PyDosObj far *method = pydos_bound_method_new(
                    (void (far *)(void))object_init_method, bound_obj,
                    (const char far *)"__init__");
                if (method != (PyDosObj far *)0)
                    pydos_func_set_arg_count(method, 1);
                return method;
            }
            if (_fstrcmp(attr_name,
                         (const char far *)"__init_subclass__") == 0) {
                PyDosObj far *method = pydos_bound_method_new(
                    (void (far *)(void))object_init_subclass_method,
                    bound_obj, (const char far *)"__init_subclass__");
                if (method != (PyDosObj far *)0)
                    pydos_func_set_arg_count(method, 1);
                return method;
            }
        }
    }

    raise_missing_attribute(bound_obj, attr_name);
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_class_apply_inherited_hook(
    PyDosObj far *cls)
{
    unsigned char bi;
    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS)
        return pydos_obj_new_none();
    for (bi = 0; bi < cls->v.cls.num_bases; bi++) {
        PyDosObj far *hook = class_attr_lookup(
            cls->v.cls.bases[bi],
            (const char far *)"__pydos_metaclass_hook__");
        if (hook != (PyDosObj far *)0) {
            PyDosObj far *args[1];
            PyDosObj far *result;
            args[0] = cls;
            result = pydos_obj_call(hook, 1, args);
            PYDOS_DECREF(hook);
            /* Python-level class construction hooks conventionally return
             * the class they received.  Function returns currently borrow
             * argument objects, so releasing that alias here freed the new
             * class while its global still pointed at it. */
            if (result != (PyDosObj far *)0 && result != cls)
                PYDOS_DECREF(result);
            break;
        }
    }
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_class_call_init_subclass(
    PyDosObj far *cls, PyDosObj far *keywords)
{
    PyDosObj far *proxy;
    PyDosObj far *callable;
    PyDosObj far *positional;
    PyDosObj far *empty_keywords;
    PyDosObj far *result;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__init_subclass__ requires a class");
        return (PyDosObj far *)0;
    }
    proxy = pydos_super_new(cls, cls);
    if (proxy == (PyDosObj far *)0) return (PyDosObj far *)0;
    callable = pydos_super_get_attr(
        proxy, (const char far *)"__init_subclass__");
    PYDOS_DECREF(proxy);
    if (callable == (PyDosObj far *)0) return (PyDosObj far *)0;

    positional = pydos_list_new(0);
    if (positional == (PyDosObj far *)0) {
        PYDOS_DECREF(callable);
        return (PyDosObj far *)0;
    }
    positional->type = PYDT_TUPLE;
    empty_keywords = (PyDosObj far *)0;
    if (keywords == (PyDosObj far *)0) {
        empty_keywords = pydos_dict_new(1);
        keywords = empty_keywords;
    }
    result = pydos_obj_call_ex(callable, positional, keywords);
    PYDOS_DECREF(positional);
    PYDOS_DECREF(callable);
    if (empty_keywords != (PyDosObj far *)0)
        PYDOS_DECREF(empty_keywords);
    return result;
}

PyDosObj far * PYDOS_API pydos_class_apply_metaclass(
    PyDosObj far *cls, PyDosObj far *metaclass)
{
    PyDosMethodSlot far *slot;
    PyDosObj far *args[1];
    PyDosObj far *result;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS ||
        metaclass == (PyDosObj far *)0 ||
        (PyDosType)metaclass->type != PYDT_CLASS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"metaclass must be a class");
        return (PyDosObj far *)0;
    }
    if (cls->v.cls.metaclass != metaclass) {
        PYDOS_INCREF(metaclass);
        if (cls->v.cls.metaclass != (PyDosObj far *)0)
            PYDOS_DECREF(cls->v.cls.metaclass);
        cls->v.cls.metaclass = metaclass;
    }

    slot = metaclass->v.cls.vtable != (PyDosVTable far *)0
           ? pydos_vtable_lookup_slot(
                 metaclass->v.cls.vtable,
                 method_name_hash(
                     (const char far *)"__pydos_metaclass_init__"))
           : (PyDosMethodSlot far *)0;
    if (slot != (PyDosMethodSlot far *)0) {
        args[0] = cls;
        result = pydos_code_ref_call(slot->code_ref, 1, args);
        if (result != (PyDosObj far *)0 && result != cls)
            PYDOS_DECREF(result);
    }
    return pydos_obj_new_none();
}

static PyDosObj far *metaclass_protocol_call(
    PyDosObj far *owner, const char far *name,
    PyDosObj far * far *candidates, unsigned int candidate_count)
{
    PyDosObj far *callable;
    PyDosObj far *result;
    unsigned int start;
    unsigned int argc;

    if (!pydos_obj_has_attr(owner, name))
        return (PyDosObj far *)0;
    callable = pydos_obj_get_attr(owner, name);
    if (callable == (PyDosObj far *)0) return (PyDosObj far *)0;
    if ((PyDosType)callable->type == PYDT_NONE) {
        PYDOS_DECREF(callable);
        return (PyDosObj far *)0;
    }
    start = 0;
    argc = candidate_count;
    if ((PyDosType)callable->type == PYDT_FUNCTION &&
        callable->v.func.signature_known) {
        unsigned int bound = callable->v.func.bound_self !=
                             (PyDosObj far *)0 ? 1U : 0U;
        unsigned int wanted = callable->v.func.arg_count >= bound
                              ? callable->v.func.arg_count - bound : 0U;
        if (bound && candidate_count > 0) start = 1;
        if (wanted < candidate_count - start) argc = wanted;
        else argc = candidate_count - start;
    }
    result = pydos_obj_call(callable, argc, candidates + start);
    PYDOS_DECREF(callable);
    return result;
}

static void replay_class_namespace(PyDosObj far *cls,
                                   PyDosObj far *namespace_obj)
{
    PyDosObj far *keys;
    unsigned int i;
    if (cls == (PyDosObj far *)0 || namespace_obj == (PyDosObj far *)0 ||
        cls->v.cls.class_attrs == (PyDosObj far *)0 ||
        namespace_obj == cls->v.cls.class_attrs)
        return;
    keys = pydos_dict_keys(cls->v.cls.class_attrs);
    if (keys == (PyDosObj far *)0) return;
    for (i = 0; i < keys->v.list.len; i++) {
        PyDosObj far *key = keys->v.list.items[i];
        PyDosObj far *value = pydos_dict_get(cls->v.cls.class_attrs, key);
        if (value != (PyDosObj far *)0) {
            pydos_obj_setitem(namespace_obj, key, value);
            PYDOS_DECREF(value);
        }
    }
    PYDOS_DECREF(keys);
}

static void apply_namespace_to_class(PyDosObj far *cls,
                                     PyDosObj far *namespace_obj)
{
    PyDosObj far *keys;
    unsigned int i;
    if (cls == (PyDosObj far *)0 || namespace_obj == (PyDosObj far *)0 ||
        (PyDosType)namespace_obj->type != PYDT_DICT)
        return;
    keys = pydos_dict_keys(namespace_obj);
    if (keys == (PyDosObj far *)0) return;
    for (i = 0; i < keys->v.list.len; i++) {
        PyDosObj far *key = keys->v.list.items[i];
        PyDosObj far *value;
        if ((PyDosType)key->type != PYDT_STR) continue;
        value = pydos_dict_get(namespace_obj, key);
        if (value != (PyDosObj far *)0) {
            pydos_obj_set_attr(cls, key->v.str.data, value);
            PYDOS_DECREF(value);
        }
    }
    PYDOS_DECREF(keys);
}

/* Invoke the descriptor initialization protocol after the complete class
 * namespace has been installed.  The dictionary key object is passed
 * directly so its exact Python spelling is preserved. */
PyDosObj far * PYDOS_API pydos_class_set_names(PyDosObj far *cls)
{
    PyDosObj far *keys;
    unsigned int i;

    if (cls == (PyDosObj far *)0) {
        /* Preserve the original failure.  Class construction is a sequence
         * of runtime calls, and an allocation failure may legitimately pass
         * NULL into this finalization step. */
        if (pydos_exc_pending()) return (PyDosObj far *)0;
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__set_name__ owner is NULL");
        return (PyDosObj far *)0;
    }
    if ((PyDosType)cls->type != PYDT_CLASS) {
        if (pydos_exc_pending()) return (PyDosObj far *)0;
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"__set_name__ owner has invalid type");
        return (PyDosObj far *)0;
    }
    if (cls->v.cls.class_attrs == (PyDosObj far *)0)
        return pydos_obj_new_none();

    keys = pydos_dict_keys(cls->v.cls.class_attrs);
    if (keys == (PyDosObj far *)0) return (PyDosObj far *)0;
    for (i = 0; i < keys->v.list.len; i++) {
        PyDosObj far *key = keys->v.list.items[i];
        PyDosObj far *value;

        if (key == (PyDosObj far *)0 ||
            (PyDosType)key->type != PYDT_STR)
            continue;
        value = pydos_dict_get(cls->v.cls.class_attrs, key);
        if (value == (PyDosObj far *)0) {
            PYDOS_DECREF(keys);
            return (PyDosObj far *)0;
        }
        if (pydos_obj_has_attr(value,
                               (const char far *)"__set_name__")) {
            PyDosObj far *args[3];
            PyDosObj far *result;
            args[0] = value;
            args[1] = cls;
            args[2] = key;
            result = pydos_obj_call_method(
                (const char far *)"__set_name__", 3, args);
            if (result == (PyDosObj far *)0) {
                PYDOS_DECREF(value);
                PYDOS_DECREF(keys);
                return (PyDosObj far *)0;
            }
            PYDOS_DECREF(result);
        }
        PYDOS_DECREF(value);
    }
    PYDOS_DECREF(keys);
    return pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API pydos_type_new_placeholder(
    PyDosObj far *metaclass, PyDosObj far *name,
    PyDosObj far *bases, PyDosObj far *namespace_obj)
{
    (void)metaclass;
    (void)name;
    (void)bases;
    if (pydos_pending_class == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"type.__new__ outside class creation");
        return (PyDosObj far *)0;
    }
    apply_namespace_to_class(pydos_pending_class, namespace_obj);
    PYDOS_INCREF(pydos_pending_class);
    return pydos_pending_class;
}

PyDosObj far * PYDOS_API pydos_class_apply_metaclass_protocol(
    PyDosObj far *cls, PyDosObj far *metaclass, PyDosObj far *keywords)
{
    PyDosObj far *applied;
    PyDosObj far *name_obj;
    PyDosObj far *bases_obj;
    PyDosObj far *namespace_obj;
    PyDosObj far *prepared;
    PyDosObj far *created;
    PyDosObj far *initialized;
    PyDosObj far *previous_pending;
    PyDosObj far *prepare_args[4];
    PyDosObj far *new_args[5];
    PyDosObj far *init_args[5];
    unsigned char bi;

    applied = pydos_class_apply_metaclass(cls, metaclass);
    if (applied == (PyDosObj far *)0) return (PyDosObj far *)0;
    PYDOS_DECREF(applied);
    if (keywords == (PyDosObj far *)0) keywords = pydos_dict_new(4);
    else PYDOS_INCREF(keywords);
    name_obj = pydos_obj_new_str(cls->v.cls.name,
        (unsigned int)_fstrlen(cls->v.cls.name));
    bases_obj = pydos_list_new((unsigned int)cls->v.cls.num_bases);
    for (bi = 0; bi < cls->v.cls.num_bases; bi++)
        pydos_list_append(bases_obj, cls->v.cls.bases[bi]);
    bases_obj->type = PYDT_TUPLE;

    prepare_args[0] = metaclass;
    prepare_args[1] = name_obj;
    prepare_args[2] = bases_obj;
    prepare_args[3] = keywords;
    prepared = metaclass_protocol_call(
        metaclass, (const char far *)"__prepare__", prepare_args, 4);
    if (prepared == (PyDosObj far *)0 ||
        (PyDosType)prepared->type == PYDT_NONE) {
        if (prepared != (PyDosObj far *)0) PYDOS_DECREF(prepared);
        namespace_obj = cls->v.cls.class_attrs;
        PYDOS_INCREF(namespace_obj);
    } else {
        namespace_obj = prepared;
        replay_class_namespace(cls, namespace_obj);
    }

    new_args[0] = metaclass;
    new_args[1] = name_obj;
    new_args[2] = bases_obj;
    new_args[3] = namespace_obj;
    new_args[4] = keywords;
    previous_pending = pydos_pending_class;
    pydos_pending_class = cls;
    created = metaclass_protocol_call(
        metaclass, (const char far *)"__new__", new_args, 5);
    pydos_pending_class = previous_pending;
    if (created == (PyDosObj far *)0 ||
        (PyDosType)created->type == PYDT_NONE) {
        if (created != (PyDosObj far *)0) PYDOS_DECREF(created);
        created = cls;
        PYDOS_INCREF(created);
    } else if ((PyDosType)created->type != PYDT_CLASS) {
        PYDOS_DECREF(created);
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"metaclass __new__ must return a class");
        created = (PyDosObj far *)0;
    }

    if (created != (PyDosObj far *)0) {
        PyDosObj far *set_names_result = pydos_class_set_names(created);
        if (set_names_result == (PyDosObj far *)0) {
            PYDOS_DECREF(created);
            created = (PyDosObj far *)0;
        } else {
            PYDOS_DECREF(set_names_result);
        }
    }

    if (created != (PyDosObj far *)0) {
        init_args[0] = created;
        init_args[1] = name_obj;
        init_args[2] = bases_obj;
        init_args[3] = namespace_obj;
        init_args[4] = keywords;
        initialized = metaclass_protocol_call(
            metaclass, (const char far *)"__init__", init_args, 5);
        if (initialized != (PyDosObj far *)0) PYDOS_DECREF(initialized);
    }
    PYDOS_DECREF(namespace_obj);
    PYDOS_DECREF(bases_obj);
    PYDOS_DECREF(name_obj);
    PYDOS_DECREF(keywords);
    return created;
}

PyDosObj far * PYDOS_API pydos_class_apply_inherited_metaclass(
    PyDosObj far *cls)
{
    PyDosObj far *selected;
    PyDosObj far *set_names_result;
    PyDosObj far *init_subclass_result;
    PyDosObj far *metaclass_result;
    unsigned char bi;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS)
        return pydos_obj_new_none();
    selected = (PyDosObj far *)0;
    for (bi = 0; bi < cls->v.cls.num_bases; bi++) {
        PyDosObj far *candidate = cls->v.cls.bases[bi]->v.cls.metaclass;
        if (candidate == (PyDosObj far *)0) continue;
        if (selected == (PyDosObj far *)0 || selected == candidate) {
            selected = candidate;
        } else if (pydos_class_is_subclass(candidate, selected)) {
            selected = candidate;
        } else if (!pydos_class_is_subclass(selected, candidate)) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"metaclass conflict");
            return (PyDosObj far *)0;
        }
    }
    set_names_result = pydos_class_set_names(cls);
    if (set_names_result == (PyDosObj far *)0)
        return (PyDosObj far *)0;
    PYDOS_DECREF(set_names_result);
    if (selected != (PyDosObj far *)0) {
        metaclass_result = pydos_class_apply_metaclass(cls, selected);
        if (metaclass_result == (PyDosObj far *)0)
            return (PyDosObj far *)0;
        PYDOS_DECREF(metaclass_result);
    }
    init_subclass_result = pydos_class_call_init_subclass(
        cls, (PyDosObj far *)0);
    if (init_subclass_result == (PyDosObj far *)0)
        return (PyDosObj far *)0;
    PYDOS_DECREF(init_subclass_result);
    return pydos_class_apply_inherited_hook(cls);
}

PyDosObj far * PYDOS_API pydos_class_apply_inherited_metaclass_protocol(
    PyDosObj far *cls, PyDosObj far *keywords)
{
    PyDosObj far *selected;
    unsigned char bi;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS)
        return (PyDosObj far *)0;
    selected = (PyDosObj far *)0;
    for (bi = 0; bi < cls->v.cls.num_bases; bi++) {
        PyDosObj far *candidate = cls->v.cls.bases[bi]->v.cls.metaclass;
        if (candidate == (PyDosObj far *)0) continue;
        if (selected == (PyDosObj far *)0 || selected == candidate) {
            selected = candidate;
        } else if (pydos_class_is_subclass(candidate, selected)) {
            selected = candidate;
        } else if (!pydos_class_is_subclass(selected, candidate)) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"metaclass conflict");
            return (PyDosObj far *)0;
        }
    }
    if (selected != (PyDosObj far *)0)
        return pydos_class_apply_metaclass_protocol(cls, selected, keywords);
    {
        PyDosObj far *set_names_result;
        PyDosObj far *hook_result;
        PyDosObj far *init_subclass_result;
        set_names_result = pydos_class_set_names(cls);
        if (set_names_result == (PyDosObj far *)0)
            return (PyDosObj far *)0;
        PYDOS_DECREF(set_names_result);
        init_subclass_result = pydos_class_call_init_subclass(cls, keywords);
        if (init_subclass_result == (PyDosObj far *)0)
            return (PyDosObj far *)0;
        PYDOS_DECREF(init_subclass_result);
        hook_result = pydos_class_apply_inherited_hook(cls);
        if (hook_result != (PyDosObj far *)0) PYDOS_DECREF(hook_result);
    }
    PYDOS_INCREF(cls);
    return cls;
}

static void raise_missing_attribute(PyDosObj far *obj,
                                    const char far *attr_name)
{
    char message[96];
    const char far *owner_name;
    unsigned int pos;
    unsigned int i;

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.cls != (PyDosObj far *)0)
        owner_name = obj->v.instance.cls->v.cls.name;
    else
        owner_name = pydos_obj_type_name(obj);
    pos = 0;
    message[pos++] = '\'';
    for (i = 0; owner_name[i] != '\0' && pos < 35; i++)
        message[pos++] = owner_name[i];
    _fmemcpy((char far *)(message + pos),
             (const char far *)"' object has no attribute '", 27);
    pos += 27;
    for (i = 0; attr_name[i] != '\0' && pos < 93; i++)
        message[pos++] = attr_name[i];
    message[pos++] = '\'';
    message[pos] = '\0';
    pydos_exc_raise(PYDOS_EXC_ATTRIBUTE_ERROR,
                    (const char far *)message);
}

static PyDosObj far *pydos_obj_get_attr_default(
    PyDosObj far *obj, const char far *attr_name)
{
    unsigned int len;
    const char far *p;
    PyDosObj far *dict;
    PyDosObj far *resolved_class_value;

    resolved_class_value = (PyDosObj far *)0;

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_SUPER)
        return pydos_super_get_attr(obj, attr_name);

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_TYPE_PARAM) {
        PyDosObj far *value = (PyDosObj far *)0;
        if (_fstrcmp(attr_name, (const char far *)"__name__") == 0)
            value = obj->v.type_param.name;
        else if (_fstrcmp(attr_name,
                          (const char far *)"__constraints__") == 0)
            value = obj->v.type_param.constraints;
        else if (_fstrcmp(attr_name,
                          (const char far *)"__bound__") == 0) {
            if (obj->v.type_param.bound_thunk != (PyDosObj far *)0 &&
                (PyDosType)obj->v.type_param.bound_thunk->type != PYDT_NONE)
                return pydos_obj_call(obj->v.type_param.bound_thunk, 0,
                                      (PyDosObj far * far *)0);
            value = obj->v.type_param.bound;
        }
        if (value != (PyDosObj far *)0) {
            PYDOS_INCREF(value);
            return value;
        }
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_TYPE_ALIAS) {
        PyDosObj far *value = (PyDosObj far *)0;
        if (_fstrcmp(attr_name, (const char far *)"__name__") == 0)
            value = obj->v.type_alias.name;
        else if (_fstrcmp(attr_name,
                          (const char far *)"__type_params__") == 0)
            value = obj->v.type_alias.type_params;
        else if (_fstrcmp(attr_name, (const char far *)"__value__") == 0)
            value = obj->v.type_alias.value;
        if (value != (PyDosObj far *)0) {
            PYDOS_INCREF(value);
            return value;
        }
    }

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_CODE) {
        PyDosObj far *value = (PyDosObj far *)0;
        if (_fstrcmp(attr_name, (const char far *)"co_name") == 0) {
            const char far *name = obj->v.code.name != (const char far *)0
                                   ? obj->v.code.name
                                   : (const char far *)"";
            return pydos_obj_new_str(name, (unsigned int)_fstrlen(name));
        } else if (_fstrcmp(attr_name,
                            (const char far *)"co_freevars") == 0)
            value = obj->v.code.freevars;
        else if (_fstrcmp(attr_name, (const char far *)"co_consts") == 0)
            value = obj->v.code.consts;
        if (value != (PyDosObj far *)0) {
            PYDOS_INCREF(value);
            return value;
        }
    }

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_CELL &&
        _fstrcmp(attr_name, (const char far *)"cell_contents") == 0) {
        if (obj->v.cell.value != (PyDosObj far *)0) {
            PYDOS_INCREF(obj->v.cell.value);
            return obj->v.cell.value;
        }
        pydos_exc_raise(PYDOS_EXC_VALUE_ERROR,
                        (const char far *)"Cell is empty");
        return (PyDosObj far *)0;
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_CLASS &&
        obj->v.cls.runtime_type_tag == PYDT_INSTANCE) {
        void (far *method)(void) = (void (far *)(void))0;
        unsigned int arg_count = 0;
        if (_fstrcmp(attr_name,
                     (const char far *)"__getattribute__") == 0) {
            method = (void (far *)(void))object_getattribute_method;
            arg_count = 2;
        } else if (_fstrcmp(attr_name,
                            (const char far *)"__setattr__") == 0) {
            method = (void (far *)(void))object_setattr_method;
            arg_count = 3;
        } else if (_fstrcmp(attr_name,
                            (const char far *)"__delattr__") == 0) {
            method = (void (far *)(void))object_delattr_method;
            arg_count = 2;
        }
        if (method != (void (far *)(void))0) {
            PyDosObj far *function = pydos_func_new(method, attr_name);
            if (function != (PyDosObj far *)0)
                pydos_func_set_arg_count(function, arg_count);
            return function;
        }
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_CLASS &&
        obj->v.cls.runtime_type_tag == PYDT_CLASS &&
        _fstrcmp(attr_name, (const char far *)"__new__") == 0) {
        PyDosObj far *type_new;
        type_new = pydos_func_new(
            (void (far *)(void))pydos_type_new_placeholder,
            (const char far *)"__new__");
        if (type_new != (PyDosObj far *)0)
            pydos_func_set_arg_count(type_new, 4);
        return type_new;
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_CLASS &&
        _fstrcmp(attr_name, (const char far *)"__class__") == 0 &&
        obj->v.cls.metaclass != (PyDosObj far *)0) {
        PYDOS_INCREF(obj->v.cls.metaclass);
        return obj->v.cls.metaclass;
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        _fstrcmp(attr_name, (const char far *)"__class__") == 0 &&
        obj->v.instance.cls != (PyDosObj far *)0) {
        PYDOS_INCREF(obj->v.instance.cls);
        return obj->v.instance.cls;
    }

    if (obj != (PyDosObj far *)0 &&
        ((PyDosType)obj->type == PYDT_INSTANCE ||
         (PyDosType)obj->type == PYDT_CLASS ||
         (PyDosType)obj->type == PYDT_FUNCTION) &&
        _fstrcmp(attr_name, (const char far *)"__dict__") == 0 &&
        ((PyDosType)obj->type != PYDT_INSTANCE ||
         class_instance_has_dict(obj->v.instance.cls))) {
        PyDosObj far *attrs;
        if ((PyDosType)obj->type == PYDT_INSTANCE)
            attrs = obj->v.instance.attrs;
        else if ((PyDosType)obj->type == PYDT_CLASS)
            attrs = obj->v.cls.class_attrs;
        else
            attrs = obj->v.func.attrs;
        if (attrs == (PyDosObj far *)0) {
            attrs = pydos_dict_new(8);
            if (attrs == (PyDosObj far *)0) return (PyDosObj far *)0;
            if ((PyDosType)obj->type == PYDT_INSTANCE)
                obj->v.instance.attrs = attrs;
            else if ((PyDosType)obj->type == PYDT_CLASS)
                obj->v.cls.class_attrs = attrs;
            else
                obj->v.func.attrs = attrs;
        }
        PYDOS_INCREF(attrs);
        return attrs;
    }

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_CLASS) {
        PyDosObj far *class_value;

        if (_fstrcmp(attr_name, (const char far *)"__name__") == 0) {
            return pydos_obj_new_str(obj->v.cls.name,
                                     (unsigned int)_fstrlen(obj->v.cls.name));
        }
        if (_fstrcmp(attr_name, (const char far *)"__module__") == 0) {
            if (obj->v.cls.runtime_type_tag >= 0)
                return pydos_obj_new_str((const char far *)"builtins", 8);
            return pydos_obj_new_str((const char far *)"__main__", 8);
        }
        if (_fstrcmp(attr_name, (const char far *)"__bases__") == 0) {
            PyDosObj far *result;
            unsigned char bi;
            result = pydos_list_new((unsigned int)obj->v.cls.num_bases);
            if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
            for (bi = 0; bi < obj->v.cls.num_bases; bi++)
                pydos_list_append(result, obj->v.cls.bases[bi]);
            result->type = PYDT_TUPLE;
            return result;
        }
        if (_fstrcmp(attr_name, (const char far *)"__mro__") == 0) {
            PyDosObj far *result;
            unsigned char mi;
            result = pydos_list_new((unsigned int)obj->v.cls.mro_len);
            if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
            for (mi = 0; mi < obj->v.cls.mro_len; mi++)
                pydos_list_append(result, obj->v.cls.mro[mi]);
            result->type = PYDT_TUPLE;
            return result;
        }

        /* A materialized class-dictionary entry represents the current
         * Python value of the attribute (for example, after applying a
         * decorator) and therefore takes precedence over the compiled
         * method stored in the vtable. */
        class_value = dict_attr_lookup(obj->v.cls.class_attrs, attr_name);
        if (class_value != (PyDosObj far *)0) {
            if (descriptor_has_slot(class_value, VSLOT_GET)) {
                PyDosObj far *result;
                result = descriptor_call_get(class_value,
                                             (PyDosObj far *)0, obj);
                PYDOS_DECREF(class_value);
                return result;
            }
            return class_value;
        }
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_FUNCTION &&
        _fstrcmp(attr_name, (const char far *)"__annotations__") == 0) {
        PyDosObj far *annotations = dict_attr_lookup(
            obj->v.func.attrs, (const char far *)"__annotations__");
        if (annotations != (PyDosObj far *)0) return annotations;
        annotations = pydos_dict_new(1);
        if (annotations == (PyDosObj far *)0)
            return (PyDosObj far *)0;
        pydos_obj_set_attr_default(
            obj, (const char far *)"__annotations__", annotations);
        return annotations;
    }
    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_FUNCTION &&
        _fstrcmp(attr_name, (const char far *)"__name__") == 0) {
        const char far *name = obj->v.func.name
                               ? obj->v.func.name
                               : (const char far *)"";
        return pydos_obj_new_str(name, (unsigned int)_fstrlen(name));
    }
    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_FUNCTION &&
        _fstrcmp(attr_name, (const char far *)"__code__") == 0) {
        return materialize_function_code(obj);
    }
    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_FUNCTION &&
        _fstrcmp(attr_name, (const char far *)"__closure__") == 0) {
        PyDosObj far *closure = obj->v.func.closure;
        PyDosObj far *result;
        unsigned int i;
        if (closure == (PyDosObj far *)0 ||
            (PyDosType)closure->type == PYDT_NONE)
            return pydos_obj_new_none();
        if ((PyDosType)closure->type == PYDT_TUPLE) {
            PYDOS_INCREF(closure);
            return closure;
        }
        if ((PyDosType)closure->type != PYDT_LIST)
            return pydos_obj_new_none();
        result = pydos_list_new(closure->v.list.len);
        if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
        for (i = 0; i < closure->v.list.len; i++)
            pydos_list_append(result, closure->v.list.items[i]);
        return pydos_list_to_tuple(result);
    }

    /* Numeric primitive attributes. */
    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_INT) {
        if (_fstrcmp(attr_name, (const char far *)"real") == 0 ||
            _fstrcmp(attr_name, (const char far *)"numerator") == 0) {
            PYDOS_INCREF(obj);
            return obj;
        }
        if (_fstrcmp(attr_name, (const char far *)"imag") == 0) {
            return pydos_obj_new_int(0L);
        }
        if (_fstrcmp(attr_name, (const char far *)"denominator") == 0) {
            return pydos_obj_new_int(1L);
        }
    }

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_FLOAT) {
        if (_fstrcmp(attr_name, (const char far *)"real") == 0) {
            PYDOS_INCREF(obj);
            return obj;
        }
        if (_fstrcmp(attr_name, (const char far *)"imag") == 0) {
            return pydos_obj_new_float(0.0);
        }
    }

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_COMPLEX) {
        if (_fstrcmp(attr_name, (const char far *)"real") == 0) {
            return pydos_obj_new_float(obj->v.complex_val.real);
        }
        if (_fstrcmp(attr_name, (const char far *)"imag") == 0) {
            return pydos_obj_new_float(obj->v.complex_val.imag);
        }
    }

    if (obj != (PyDosObj far *)0 && (PyDosType)obj->type == PYDT_RANGE) {
        if (_fstrcmp(attr_name, (const char far *)"start") == 0) {
            return pydos_obj_new_int(obj->v.range.start);
        }
        if (_fstrcmp(attr_name, (const char far *)"stop") == 0) {
            return pydos_obj_new_int(obj->v.range.stop);
        }
        if (_fstrcmp(attr_name, (const char far *)"step") == 0) {
            return pydos_obj_new_int(obj->v.range.step);
        }
    }

    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_EXCEPTION &&
        obj->v.exc.type_code == PYDOS_EXC_STOP_ITERATION &&
        _fstrcmp(attr_name, (const char far *)"value") == 0) {
        if (obj->v.exc.value != (PyDosObj far *)0) {
            PYDOS_INCREF(obj->v.exc.value);
            return obj->v.exc.value;
        }
        return pydos_obj_new_none();
    }

    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }

    /* Compute length of attr_name up front (needed for __getattr__ too) */
    p = attr_name;
    len = 0;
    while (p[len] != '\0') len++;

    /* Resolve the class side once.  A data descriptor is consulted before
     * the instance dictionary; non-data descriptors and ordinary class
     * values are consulted afterwards. */
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.cls != (PyDosObj far *)0) {
        resolved_class_value = class_attr_lookup(obj->v.instance.cls,
                                                 attr_name);
        if (resolved_class_value != (PyDosObj far *)0 &&
            (descriptor_has_slot(resolved_class_value, VSLOT_SET) ||
             descriptor_has_slot(resolved_class_value, VSLOT_DELETE)) &&
            descriptor_has_slot(resolved_class_value, VSLOT_GET)) {
            PyDosObj far *result;
            result = descriptor_call_get(resolved_class_value, obj,
                                         obj->v.instance.cls);
            PYDOS_DECREF(resolved_class_value);
            return result;
        }
    }

    if ((PyDosType)obj->type == PYDT_INSTANCE)
        dict = obj->v.instance.attrs;
    else if ((PyDosType)obj->type == PYDT_FUNCTION)
        dict = obj->v.func.attrs;
    else
        dict = (PyDosObj far *)0;
    if (dict != (PyDosObj far *)0) {
        PyDosObj far *value = dict_attr_lookup(dict, attr_name);
        if (value != (PyDosObj far *)0) {
            if (resolved_class_value != (PyDosObj far *)0)
                PYDOS_DECREF(resolved_class_value);
            return value;
        }
    }

    /* Plain class attributes and non-data descriptors follow instance
     * attributes and are inherited. */
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.cls != (PyDosObj far *)0) {
        if (resolved_class_value != (PyDosObj far *)0) {
            if (descriptor_has_slot(resolved_class_value, VSLOT_GET)) {
                PyDosObj far *result;
                result = descriptor_call_get(resolved_class_value, obj,
                                             obj->v.instance.cls);
                PYDOS_DECREF(resolved_class_value);
                return result;
            }
            if ((PyDosType)resolved_class_value->type == PYDT_FUNCTION) {
                PyDosObj far *bound = pydos_func_bind(resolved_class_value,
                                                      obj);
                PYDOS_DECREF(resolved_class_value);
                return bound;
            }
            return resolved_class_value;
        }
    } else if ((PyDosType)obj->type == PYDT_CLASS) {
        PyDosMethodSlot far *own_slot;
        own_slot = obj->v.cls.vtable != (PyDosVTable far *)0
                   ? pydos_vtable_lookup_slot(obj->v.cls.vtable,
                         method_name_hash(attr_name))
                   : (PyDosMethodSlot far *)0;
        if (own_slot != (PyDosMethodSlot far *)0) {
            PyDosObj far *method_obj;
            /* The slot lookup also answers for inherited methods, so a
             * descriptor in the dictionary of any MRO entry has to win:
             * an inherited classmethod must still bind its class. */
            PyDosObj far *described = class_attr_lookup(obj, attr_name);
            if (described != (PyDosObj far *)0) {
                if (descriptor_has_slot(described, VSLOT_GET)) {
                    PyDosObj far *bound_value;
                    bound_value = descriptor_call_get(described,
                                                      (PyDosObj far *)0, obj);
                    PYDOS_DECREF(described);
                    return bound_value;
                }
                PYDOS_DECREF(described);
            }
            method_obj = pydos_func_new_from_code_ref(
                own_slot->code_ref, attr_name);
            if (method_obj != (PyDosObj far *)0 &&
                PYDOS_METHOD_HAS_SIGNATURE(own_slot))
                pydos_func_set_arg_count(method_obj, own_slot->arg_count);
            if (method_obj != (PyDosObj far *)0 &&
                own_slot->defaults != (PyDosObj far *)0)
                pydos_func_set_defaults(method_obj, own_slot->defaults);
            return method_obj;
        }
        {
            PyDosObj far *base_value;
            base_value = class_attr_lookup(obj, attr_name);
            if (base_value != (PyDosObj far *)0) {
                if (descriptor_has_slot(base_value, VSLOT_GET)) {
                    PyDosObj far *result;
                    result = descriptor_call_get(base_value,
                                                 (PyDosObj far *)0, obj);
                    PYDOS_DECREF(base_value);
                    return result;
                }
                return base_value;
            }
        }
        /* A class object is an instance of its metaclass.  Compiled
         * metaclass methods therefore bind the class as their first
         * argument, enabling ABC.register and other class protocols. */
        if (obj->v.cls.metaclass != (PyDosObj far *)0 &&
            obj->v.cls.metaclass->v.cls.vtable !=
                (PyDosVTable far *)0) {
            PyDosMethodSlot far *meta_slot;
            meta_slot = pydos_vtable_lookup_slot(
                obj->v.cls.metaclass->v.cls.vtable,
                method_name_hash(attr_name));
            if (meta_slot != (PyDosMethodSlot far *)0) {
                PyDosObj far *bound;
                bound = pydos_bound_method_new_from_code_ref(
                    meta_slot->code_ref, obj, attr_name);
                if (bound != (PyDosObj far *)0 &&
                    PYDOS_METHOD_HAS_SIGNATURE(meta_slot))
                    pydos_func_set_arg_count(bound,
                                             meta_slot->arg_count);
                if (bound != (PyDosObj far *)0 &&
                    meta_slot->defaults != (PyDosObj far *)0)
                    pydos_func_set_defaults(bound, meta_slot->defaults);
                return bound;
            }
        }
    }

    /* Attribute not found — check __getattr__ vtable slot */
    if (!suppress_getattr_fallback &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (struct PyDosVTable far *)0) {
        typedef PyDosObj far * (PYDOS_API far *GAFn)(PyDosObj far *, PyDosObj far *);
        GAFn ga_fn = (GAFn)pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_GETATTR);
        if (ga_fn != (GAFn)0) {
            PyDosObj far *name_obj = pydos_obj_new_str(attr_name, len);
            PyDosObj far *result = ga_fn(obj, name_obj);
            PYDOS_DECREF(name_obj);
            return result;
        }
    }

    /* Methods are attributes too.  Return the same lightweight function
     * wrapper used by getattr(); bound-method state remains a future object
     * model improvement. */
    {
        PyDosVTable far *vtable = (PyDosVTable far *)0;
        PyDosMethodSlot far *method_slot;
        if ((PyDosType)obj->type == PYDT_INSTANCE) {
            vtable = obj->v.instance.vtable;
        } else if ((PyDosType)obj->type == PYDT_CLASS) {
            vtable = obj->v.cls.vtable;
        } else if (obj->type < PYDT_MAX) {
            vtable = pydos_builtin_vtables[obj->type];
        }
        method_slot = vtable != (PyDosVTable far *)0
                      ? pydos_vtable_lookup_slot(
                            vtable, method_name_hash(attr_name))
                      : (PyDosMethodSlot far *)0;
        if (method_slot != (PyDosMethodSlot far *)0) {
            PyDosObj far *method_obj;
            if ((PyDosType)obj->type == PYDT_INSTANCE)
                method_obj = pydos_bound_method_new_from_code_ref(
                    method_slot->code_ref, obj, attr_name);
            else
                method_obj = pydos_func_new_from_code_ref(
                    method_slot->code_ref, (const char far *)0);
            if (method_obj != (PyDosObj far *)0 &&
                PYDOS_METHOD_HAS_SIGNATURE(method_slot))
                pydos_func_set_arg_count(method_obj,
                                         method_slot->arg_count);
            if (method_obj != (PyDosObj far *)0 &&
                method_slot->defaults != (PyDosObj far *)0)
                pydos_func_set_defaults(method_obj,
                                        method_slot->defaults);
            return method_obj;
        }
    }

    raise_missing_attribute(obj, attr_name);
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_obj_get_attr(PyDosObj far *obj,
                                             const char far *attr_name)
{
    void (far *getattribute_entry)(void) = (void (far *)(void))0;
    void (far *getattr_entry)(void) = (void (far *)(void))0;
    if (obj != (PyDosObj far *)0 &&
        (PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0) {
        getattribute_entry = pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_GETATTRIBUTE);
        getattr_entry = pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_GETATTR);
    }
    if (getattribute_entry != (void (far *)(void))0) {
        PyDosObj far *name_obj = pydos_obj_new_str(
            attr_name, (unsigned int)_fstrlen(attr_name));
        PyDosObj far *args[2];
        PyDosObj far *result;
        if (name_obj == (PyDosObj far *)0) return (PyDosObj far *)0;
        args[0] = obj;
        args[1] = name_obj;
        result = call_vtable_method(getattribute_entry, 2, args);
        if (result == (PyDosObj far *)0 &&
            getattr_entry != (void (far *)(void))0) {
            PyDosObj far *exception = pydos_exc_current();
            if (exception != (PyDosObj far *)0 &&
                pydos_exc_matches(exception, PYDOS_EXC_ATTRIBUTE_ERROR)) {
                pydos_exc_clear();
                result = call_vtable_method(getattr_entry, 2, args);
            }
        }
        PYDOS_DECREF(name_obj);
        return result;
    }
    return pydos_obj_get_attr_default(obj, attr_name);
}

int PYDOS_API pydos_obj_has_attr(PyDosObj far *obj,
                                  const char far *attr_name)
{
    PyDosObj far *dict;
    PyDosObj far *key;
    PyDosVTable far *vtable;

    if (obj == (PyDosObj far *)0 || attr_name == (const char far *)0)
        return 0;
    if ((PyDosType)obj->type == PYDT_SUPER) {
        PyDosObj far *value = pydos_super_get_attr(obj, attr_name);
        if (value == (PyDosObj far *)0) {
            pydos_exc_clear();
            return 0;
        }
        PYDOS_DECREF(value);
        return 1;
    }
    if ((PyDosType)obj->type == PYDT_TYPE_PARAM &&
        (_fstrcmp(attr_name, (const char far *)"__name__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__bound__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__constraints__") == 0))
        return 1;
    if ((PyDosType)obj->type == PYDT_TYPE_ALIAS &&
        (_fstrcmp(attr_name, (const char far *)"__name__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__type_params__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__value__") == 0))
        return 1;
    if ((PyDosType)obj->type == PYDT_CODE &&
        (_fstrcmp(attr_name, (const char far *)"co_name") == 0 ||
         _fstrcmp(attr_name, (const char far *)"co_freevars") == 0 ||
         _fstrcmp(attr_name, (const char far *)"co_consts") == 0)) return 1;
    if ((PyDosType)obj->type == PYDT_CELL &&
        _fstrcmp(attr_name, (const char far *)"cell_contents") == 0)
        return obj->v.cell.value != (PyDosObj far *)0;
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        _fstrcmp(attr_name, (const char far *)"__class__") == 0 &&
        obj->v.instance.cls != (PyDosObj far *)0) return 1;
    if ((PyDosType)obj->type == PYDT_CLASS &&
        (_fstrcmp(attr_name, (const char far *)"__class__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__name__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__module__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__bases__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__mro__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__dict__") == 0)) return 1;
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        _fstrcmp(attr_name, (const char far *)"__dict__") == 0 &&
        class_instance_has_dict(obj->v.instance.cls)) return 1;
    if ((PyDosType)obj->type == PYDT_FUNCTION &&
        (_fstrcmp(attr_name, (const char far *)"__name__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__annotations__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__dict__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__code__") == 0 ||
         _fstrcmp(attr_name, (const char far *)"__closure__") == 0)) return 1;
    if ((PyDosType)obj->type == PYDT_INT &&
        (_fstrcmp(attr_name, (const char far *)"real") == 0 ||
         _fstrcmp(attr_name, (const char far *)"imag") == 0 ||
         _fstrcmp(attr_name, (const char far *)"numerator") == 0 ||
         _fstrcmp(attr_name, (const char far *)"denominator") == 0)) return 1;
    if (((PyDosType)obj->type == PYDT_FLOAT ||
         (PyDosType)obj->type == PYDT_COMPLEX) &&
        (_fstrcmp(attr_name, (const char far *)"real") == 0 ||
         _fstrcmp(attr_name, (const char far *)"imag") == 0)) return 1;
    if ((PyDosType)obj->type == PYDT_RANGE &&
        (_fstrcmp(attr_name, (const char far *)"start") == 0 ||
         _fstrcmp(attr_name, (const char far *)"stop") == 0 ||
         _fstrcmp(attr_name, (const char far *)"step") == 0)) return 1;
    if ((PyDosType)obj->type == PYDT_EXCEPTION &&
        obj->v.exc.type_code == PYDOS_EXC_STOP_ITERATION &&
        _fstrcmp(attr_name, (const char far *)"value") == 0) return 1;

    if ((PyDosType)obj->type == PYDT_INSTANCE) {
        dict = obj->v.instance.attrs;
        if (dict != (PyDosObj far *)0 && (PyDosType)dict->type == PYDT_DICT) {
            key = pydos_obj_new_str(attr_name,
                                    (unsigned int)_fstrlen(attr_name));
            if (key != (PyDosObj far *)0) {
                int found = pydos_dict_contains(dict, key);
                PYDOS_DECREF(key);
                if (found) return 1;
            }
        }
        if (obj->v.instance.cls != (PyDosObj far *)0) {
            PyDosObj far *class_value;
            class_value = class_attr_lookup(obj->v.instance.cls, attr_name);
            if (class_value != (PyDosObj far *)0) {
                PYDOS_DECREF(class_value);
                return 1;
            }
        }
        vtable = obj->v.instance.vtable;
        if (vtable != (PyDosVTable far *)0 &&
            pydos_vtable_lookup(vtable, method_name_hash(attr_name)) !=
                (void (far *)(void))0) return 1;
        if (vtable != (PyDosVTable far *)0 &&
            pydos_vtable_get_special(vtable, VSLOT_GETATTR) !=
                (void (far *)(void))0) return 1;
    } else if ((PyDosType)obj->type == PYDT_CLASS) {
        dict = obj->v.cls.class_attrs;
        if (dict != (PyDosObj far *)0 && (PyDosType)dict->type == PYDT_DICT) {
            key = pydos_obj_new_str(attr_name,
                                    (unsigned int)_fstrlen(attr_name));
            if (key != (PyDosObj far *)0) {
                int found = pydos_dict_contains(dict, key);
                PYDOS_DECREF(key);
                if (found) return 1;
            }
        }
        {
            PyDosObj far *class_value;
            class_value = class_attr_lookup(obj, attr_name);
            if (class_value != (PyDosObj far *)0) {
                PYDOS_DECREF(class_value);
                return 1;
            }
        }
        vtable = obj->v.cls.vtable;
        if (obj->v.cls.metaclass != (PyDosObj far *)0 &&
            obj->v.cls.metaclass->v.cls.vtable !=
                (PyDosVTable far *)0 &&
            pydos_vtable_lookup(
                obj->v.cls.metaclass->v.cls.vtable,
                method_name_hash(attr_name)) !=
                    (void (far *)(void))0)
            return 1;
    } else if ((PyDosType)obj->type == PYDT_FUNCTION) {
        dict = obj->v.func.attrs;
        if (dict != (PyDosObj far *)0 && (PyDosType)dict->type == PYDT_DICT) {
            key = pydos_obj_new_str(attr_name,
                                    (unsigned int)_fstrlen(attr_name));
            if (key != (PyDosObj far *)0) {
                int found = pydos_dict_contains(dict, key);
                PYDOS_DECREF(key);
                if (found) return 1;
            }
        }
        vtable = (PyDosVTable far *)0;
    } else {
        vtable = obj->type < PYDT_MAX ? pydos_builtin_vtables[obj->type]
                                      : (PyDosVTable far *)0;
    }
    return vtable != (PyDosVTable far *)0 &&
           pydos_vtable_lookup(vtable, method_name_hash(attr_name)) !=
           (void (far *)(void))0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_contains — polymorphic 'in' operator                      */
/* ------------------------------------------------------------------ */
int PYDOS_API pydos_obj_contains(PyDosObj far *container, PyDosObj far *item)
{
    if (container == (PyDosObj far *)0) {
        return 0;
    }
    switch ((PyDosType)container->type) {
    case PYDT_LIST:
        return pydos_list_contains(container, item);
    case PYDT_DICT:
        return pydos_dict_contains(container, item);
    case PYDT_SET:
        return pydos_dict_contains(container, item);
    case PYDT_STR:
        /* 'x' in 'hello' — substring search */
        if (item != (PyDosObj far *)0 && item->type == PYDT_STR) {
            return pydos_str_find(container, item) >= 0 ? 1 : 0;
        }
        return 0;
    case PYDT_TUPLE: {
        unsigned int i;
        for (i = 0; i < container->v.tuple.len; i++) {
            if (pydos_obj_equal(container->v.tuple.items[i], item)) {
                return 1;
            }
        }
        return 0;
    }
    case PYDT_FROZENSET:
        return pydos_frozenset_contains(container, item);
    case PYDT_BYTES: {
        if (item != (PyDosObj far *)0 &&
            ((PyDosType)item->type == PYDT_INT ||
             (PyDosType)item->type == PYDT_BOOL)) {
            long value = (PyDosType)item->type == PYDT_INT ?
                item->v.int_val : (long)item->v.bool_val;
            unsigned int i;
            if (value < 0 || value > 255) return 0;
            for (i = 0; i < container->v.str.len; i++) {
                if ((unsigned char)container->v.str.data[i] ==
                    (unsigned char)value) return 1;
            }
        }
        return 0;
    }
    case PYDT_BYTEARRAY: {
        if (item != (PyDosObj far *)0 &&
            ((PyDosType)item->type == PYDT_INT || (PyDosType)item->type == PYDT_BOOL)) {
            long val = ((PyDosType)item->type == PYDT_INT) ? item->v.int_val : (long)item->v.bool_val;
            unsigned int i;
            if (val < 0 || val > 255) return 0;
            for (i = 0; i < container->v.bytearray.len; i++) {
                if (container->v.bytearray.data[i] == (unsigned char)val) return 1;
            }
        }
        return 0;
    }
    case PYDT_RANGE:
        return pydos_range_contains(container, item);
    case PYDT_INSTANCE:
        /* Instance with __contains__: dispatch via vtable */
        if (container->v.instance.vtable != (PyDosVTable far *)0) {
            typedef PyDosObj far * (PYDOS_API far *ContainsFn)(PyDosObj far *, PyDosObj far *);
            ContainsFn contains_fn = (ContainsFn)pydos_vtable_get_special(
                container->v.instance.vtable, VSLOT_CONTAINS);
            PyDosObj far *res;
            if (contains_fn != (ContainsFn)0) {
                res = contains_fn(container, item);
                if (res != (PyDosObj far *)0 && pydos_obj_is_truthy(res)) {
                    return 1;
                }
                return 0;
            }
        }
        if (container->v.instance.native_storage != (PyDosObj far *)0 &&
            (PyDosType)container->v.instance.native_storage->type ==
                PYDT_DICT)
            return pydos_dict_contains(
                container->v.instance.native_storage, item);
        return 0;
    default:
        return 0;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_obj_compare — polymorphic ordering comparison                  */
/* Returns negative if a<b, 0 if a==b, positive if a>b                */
/*                                                                      */
/* Split into type-specific helpers so that Watcom does not allocate    */
/* stack space for ALL paths (doubles, vtable vars, hash loops) in a   */
/* single frame.  The monolithic version caused 8086 crashes under -od */
/* due to excessive frame size interacting with FP emulation.          */
/* ------------------------------------------------------------------ */

/* Fast-path: INT/BOOL comparison (no FP, no vtable, no strings) */
static int compare_int(long va, long vb)
{
    if (va < vb) return -1;
    if (va > vb) return 1;
    return 0;
}

/* Instance comparison via vtable __lt__/__gt__ (O(1) slot dispatch) */
static int compare_instance(PyDosObj far *a, PyDosObj far *b)
{
    PyDosVTable far *vt = a->v.instance.vtable;
    typedef PyDosObj far * (PYDOS_API far *CmpFn)(PyDosObj far *, PyDosObj far *);
    CmpFn comparison;
    PyDosObj far *args[1];
    PyDosObj far *dynamic_result;

    args[0] = b;
    dynamic_result = call_materialized_instance_method(
        a, (const char far *)"__lt__", 1, args);
    if (dynamic_result != (PyDosObj far *)0) {
        int truthy = pydos_obj_is_truthy(dynamic_result);
        PYDOS_DECREF(dynamic_result);
        if (truthy) return -1;
    }
    args[0] = a;
    dynamic_result = call_materialized_instance_method(
        b, (const char far *)"__lt__", 1, args);
    if (dynamic_result != (PyDosObj far *)0) {
        int truthy = pydos_obj_is_truthy(dynamic_result);
        PYDOS_DECREF(dynamic_result);
        if (truthy) return 1;
    }
    if (pydos_obj_equal(a, b)) return 0;

    /* __lt__ via slot */
    comparison = (CmpFn)pydos_vtable_get_special(vt, VSLOT_LT);
    if (comparison != (CmpFn)0) {
        PyDosObj far *res = comparison(a, b);
        if (res != (PyDosObj far *)0 && pydos_obj_is_truthy(res)) {
            PYDOS_DECREF(res);
            return -1;
        }
        if (res != (PyDosObj far *)0) PYDOS_DECREF(res);
    }
    /* __gt__ via slot */
    comparison = (CmpFn)pydos_vtable_get_special(vt, VSLOT_GT);
    if (comparison != (CmpFn)0) {
        PyDosObj far *res = comparison(a, b);
        if (res != (PyDosObj far *)0 && pydos_obj_is_truthy(res)) {
            PYDOS_DECREF(res);
            return 1;
        }
        if (res != (PyDosObj far *)0) PYDOS_DECREF(res);
    }
    /* Reflected __lt__ on the right operand: a is greater when b is less.
     * A class that defines only __lt__ still orders both ways. */
    if ((PyDosType)b->type == PYDT_INSTANCE &&
        b->v.instance.vtable != (PyDosVTable far *)0) {
        PyDosObj far *res;
        comparison = (CmpFn)pydos_vtable_get_special(
            b->v.instance.vtable, VSLOT_LT);
        if (comparison == (CmpFn)0) return 0;
        res = comparison(b, a);
        if (res != (PyDosObj far *)0) {
            int truthy = pydos_obj_is_truthy(res);
            PYDOS_DECREF(res);
            if (truthy) return 1;
        }
    }
    /* Try __eq__ for equality */
    if (pydos_obj_equal(a, b)) return 0;
    return 0;
}

/* Float comparison with int/bool promotion */
static int compare_float(PyDosObj far *a, PyDosObj far *b)
{
    double da = (a->type == PYDT_FLOAT) ? a->v.float_val :
                (a->type == PYDT_INT) ? (double)a->v.int_val : (double)a->v.bool_val;
    double db = (b->type == PYDT_FLOAT) ? b->v.float_val :
                (b->type == PYDT_INT) ? (double)b->v.int_val : (double)b->v.bool_val;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

/* Lexicographic comparison for the two ordered sequence types. */
static int compare_sequence(PyDosObj far *a, PyDosObj far *b)
{
    PyDosObj far * far *a_items;
    PyDosObj far * far *b_items;
    unsigned int a_len;
    unsigned int b_len;
    unsigned int common;
    unsigned int i;

    if ((PyDosType)a->type == PYDT_TUPLE) {
        a_items = a->v.tuple.items;
        b_items = b->v.tuple.items;
        a_len = a->v.tuple.len;
        b_len = b->v.tuple.len;
    } else {
        a_items = a->v.list.items;
        b_items = b->v.list.items;
        a_len = a->v.list.len;
        b_len = b->v.list.len;
    }
    common = a_len < b_len ? a_len : b_len;
    for (i = 0; i < common; i++) {
        int comparison;
        if (pydos_obj_equal(a_items[i], b_items[i])) continue;
        comparison = pydos_obj_compare(a_items[i], b_items[i]);
        if (comparison != 0) return comparison;
    }
    if (a_len < b_len) return -1;
    if (a_len > b_len) return 1;
    return 0;
}

/* Main dispatcher — thin, small frame */
int PYDOS_API pydos_obj_compare(PyDosObj far *a, PyDosObj far *b)
{
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[CMP ENTER a.type=");
    dbg_putint(a ? (int)a->type : -1);
    dbg_puts(" b.type=");
    dbg_putint(b ? (int)b->type : -1);
    dbg_puts("]\r\n");
#endif
    if (a == (PyDosObj far *)0 || b == (PyDosObj far *)0) {
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[CMP NULL => 0]\r\n");
#endif
        return 0;
    }

    /* Instance with vtable */
    if (a->type == PYDT_INSTANCE && a->v.instance.vtable != (PyDosVTable far *)0) {
        return compare_instance(a, b);
    }

    /* Int/Bool comparison */
    if ((a->type == PYDT_INT || a->type == PYDT_BOOL) &&
        (b->type == PYDT_INT || b->type == PYDT_BOOL)) {
        long va = (a->type == PYDT_BOOL) ? (long)a->v.bool_val : a->v.int_val;
        long vb = (b->type == PYDT_BOOL) ? (long)b->v.bool_val : b->v.int_val;
#ifdef PYDOS_DEBUG_CMP
        dbg_puts("[CMP va=");
        dbg_putlong(va);
        dbg_puts(" vb=");
        dbg_putlong(vb);
        if (va < vb) dbg_puts(" => -1]\r\n");
        else if (va > vb) dbg_puts(" => 1]\r\n");
        else dbg_puts(" => 0]\r\n");
#endif
        return compare_int(va, vb);
    }

    /* Float (including int-float promotion) */
    if ((a->type == PYDT_FLOAT || a->type == PYDT_INT || a->type == PYDT_BOOL) &&
        (b->type == PYDT_FLOAT || b->type == PYDT_INT || b->type == PYDT_BOOL) &&
        (a->type == PYDT_FLOAT || b->type == PYDT_FLOAT)) {
        return compare_float(a, b);
    }

    /* String comparison */
    if (a->type == PYDT_STR && b->type == PYDT_STR) {
        return pydos_str_compare(a, b);
    }

    if (a->type == PYDT_BYTES && b->type == PYDT_BYTES) {
        return pydos_bytes_compare(a, b);
    }

    if (((PyDosType)a->type == PYDT_TUPLE &&
         (PyDosType)b->type == PYDT_TUPLE) ||
        ((PyDosType)a->type == PYDT_LIST &&
         (PyDosType)b->type == PYDT_LIST)) {
        return compare_sequence(a, b);
    }

    /* Fallback: compare by type tag */
#ifdef PYDOS_DEBUG_CMP
    dbg_puts("[CMP FALLBACK a.type=");
    dbg_putint((int)a->type);
    dbg_puts(" b.type=");
    dbg_putint((int)b->type);
    dbg_puts("]\r\n");
#endif
    if (a->type != b->type) {
        return ((int)a->type < (int)b->type) ? -1 : 1;
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_neg — polymorphic unary negation                          */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_neg(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }
    if ((PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_BOOL) {
        return pydos_int_neg(obj);
    }
    if ((PyDosType)obj->type == PYDT_FLOAT) {
        return pydos_obj_new_float(-obj->v.float_val);
    }
    if ((PyDosType)obj->type == PYDT_COMPLEX) {
        return pydos_complex_neg(obj);
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0) {
        typedef PyDosObj far * (PYDOS_API far *UnaryFn)(PyDosObj far *);
        UnaryFn fn = (UnaryFn)pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_NEG);
        if (fn != (UnaryFn)0) return fn(obj);
    }
    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_obj_pos — polymorphic unary positive                          */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_pos(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }
    if ((PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_BOOL ||
        (PyDosType)obj->type == PYDT_FLOAT ||
        (PyDosType)obj->type == PYDT_COMPLEX) {
        PYDOS_INCREF(obj);
        return obj;
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0) {
        typedef PyDosObj far * (PYDOS_API far *UnaryFn)(PyDosObj far *);
        UnaryFn fn = (UnaryFn)pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_POS);
        if (fn != (UnaryFn)0) return fn(obj);
    }
    PYDOS_INCREF(obj);
    return obj;
}

/* ------------------------------------------------------------------ */
/* pydos_obj_invert — polymorphic bitwise invert                       */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_obj_invert(PyDosObj far *obj)
{
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }
    if ((PyDosType)obj->type == PYDT_INT ||
        (PyDosType)obj->type == PYDT_BOOL) {
        return pydos_int_bitnot(obj);
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0) {
        typedef PyDosObj far * (PYDOS_API far *UnaryFn)(PyDosObj far *);
        UnaryFn fn = (UnaryFn)pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_INVERT);
        if (fn != (UnaryFn)0) return fn(obj);
    }
    return pydos_obj_new_int(0L);
}

/* ------------------------------------------------------------------ */
/* pydos_func_new — create a first-class function object               */
/* ------------------------------------------------------------------ */
PyDosObj far * PYDOS_API pydos_func_new(void (far *code)(void),
                                         const char far *name)
{
    PyDosCodeRef far *reference;
    PyDosObj far *obj;
    reference = pydos_code_ref_new_native(code, PYDOS_CODE_NATIVE);
    if (reference == (PyDosCodeRef far *)0) return (PyDosObj far *)0;
    obj = pydos_func_new_from_code_ref(reference, name);
    pydos_code_ref_release(reference);
    return obj;
}

PyDosObj far * PYDOS_API pydos_func_new_from_code_ref(
    PyDosCodeRef far *code_ref, const char far *name)
{
    PyDosObj far *obj;
    if (code_ref == (PyDosCodeRef far *)0) return (PyDosObj far *)0;
    obj = pydos_obj_alloc_type(PYDT_FUNCTION);
    if (obj == (PyDosObj far *)0) return (PyDosObj far *)0;
    obj->refcount = 1;
    obj->v.func.code_ref = code_ref;
    pydos_code_ref_retain(code_ref);
    obj->v.func.name = name;
    obj->v.func.defaults = (PyDosObj far *)0;
    obj->v.func.closure = (PyDosObj far *)0;
    obj->v.func.bound_self = (PyDosObj far *)0;
    obj->v.func.attrs = (PyDosObj far *)0;
    obj->v.func.param_spec = (PyDosParamSpec far *)0;
    obj->v.func.code_obj = (PyDosObj far *)0;
    obj->v.func.arg_count = 0;
    obj->v.func.signature_known = 0;
    return obj;
}

PyDosObj far * PYDOS_API pydos_func_new_builtin(void (far *code)(void),
                                                 const char far *name)
{
    PyDosCodeRef far *reference;
    PyDosObj far *obj;
    reference = pydos_code_ref_new_native(code, PYDOS_CODE_BUILTIN);
    if (reference == (PyDosCodeRef far *)0) return (PyDosObj far *)0;
    obj = pydos_func_new_from_code_ref(reference, name);
    pydos_code_ref_release(reference);
    return obj;
}

PyDosObj far * PYDOS_API pydos_func_new_pbc(
    const struct PyDosVMModule far *module, unsigned short function_index,
    const char far *name)
{
    PyDosCodeRef far *reference;
    PyDosObj far *obj;
    reference = pydos_code_ref_new_pbc(module, (PBCU16)function_index);
    if (reference == (PyDosCodeRef far *)0) return (PyDosObj far *)0;
    obj = pydos_func_new_from_code_ref(reference, name);
    pydos_code_ref_release(reference);
    return obj;
}

void PYDOS_API pydos_func_set_arg_count(PyDosObj far *func,
                                         unsigned int arg_count)
{
    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION)
        return;
    if (arg_count > 255U) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"function has too many arguments");
        return;
    }
    func->v.func.arg_count = (unsigned char)arg_count;
    func->v.func.signature_known = 1;
}

static PyDosObj far *function_default_literal(const char far *text,
                                              unsigned int len)
{
    unsigned int begin;
    unsigned int end;
    long value;
    int negative;
    unsigned int i;

    begin = 0;
    end = len;
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t'))
        begin++;
    while (end > begin &&
           (text[end - 1] == ' ' || text[end - 1] == '\t'))
        end--;
    len = end - begin;
    text += begin;

    if (len == 4 && _fmemcmp(text, (const char far *)"None", 4) == 0)
        return pydos_obj_new_none();
    if (len == 4 && _fmemcmp(text, (const char far *)"True", 4) == 0)
        return pydos_obj_new_bool(1);
    if (len == 5 && _fmemcmp(text, (const char far *)"False", 5) == 0)
        return pydos_obj_new_bool(0);
    if (len >= 2 &&
        ((text[0] == '\'' && text[len - 1] == '\'') ||
         (text[0] == '"' && text[len - 1] == '"')))
        return pydos_obj_new_str(text + 1, len - 2);

    negative = 0;
    i = 0;
    if (i < len && text[i] == '-') {
        negative = 1;
        i++;
    }
    if (i >= len) return (PyDosObj far *)0;
    value = 0;
    for (; i < len; i++) {
        if (text[i] < '0' || text[i] > '9')
            return (PyDosObj far *)0;
        value = value * 10L + (long)(text[i] - '0');
    }
    return pydos_obj_new_int(negative ? -value : value);
}

void PYDOS_API pydos_func_set_signature(PyDosObj far *func,
                                         unsigned int arg_count,
                                         const char far *signature)
{
    PyDosObj far *defaults;
    PyDosObj far *default_value;
    PyDosObj far *tuple_defaults;
    unsigned int start;
    unsigned int equals;
    unsigned int i;
    int depth;
    char quote;

    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION ||
        signature == (const char far *)0)
        return;

    defaults = pydos_list_new(4);
    if (defaults == (PyDosObj far *)0) return;
    start = 0;
    equals = (unsigned int)-1;
    depth = 0;
    quote = '\0';
    for (i = 0;; i++) {
        char ch = signature[i];
        if (quote != '\0') {
            if (ch == quote && (i == 0 || signature[i - 1] != '\\'))
                quote = '\0';
        } else if (ch == '\'' || ch == '"') {
            quote = ch;
        } else if (ch == '[' || ch == '(' || ch == '{') {
            depth++;
        } else if (ch == ']' || ch == ')' || ch == '}') {
            if (depth > 0) depth--;
        } else if (ch == '=' && depth == 0) {
            equals = i;
        }

        if ((ch == ',' && depth == 0 && quote == '\0') || ch == '\0') {
            if (equals != (unsigned int)-1 && equals >= start) {
                default_value = function_default_literal(
                    signature + equals + 1, i - equals - 1);
                if (default_value == (PyDosObj far *)0) {
                    PYDOS_DECREF(defaults);
                    return;
                }
                pydos_list_append(defaults, default_value);
                PYDOS_DECREF(default_value);
            }
            if (ch == '\0') break;
            start = i + 1;
            equals = (unsigned int)-1;
        }
    }

    tuple_defaults = pydos_list_to_tuple(defaults);
    if (tuple_defaults == (PyDosObj far *)0) {
        PYDOS_DECREF(defaults);
        return;
    }
    pydos_func_set_defaults(func, tuple_defaults);
    PYDOS_DECREF(tuple_defaults);
    func->v.func.arg_count = (unsigned char)arg_count;
    func->v.func.signature_known = 1;
}

void PYDOS_API pydos_func_set_defaults(PyDosObj far *func,
                                        PyDosObj far *defaults)
{
    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION)
        return;
    if (defaults != (PyDosObj far *)0) PYDOS_INCREF(defaults);
    if (func->v.func.defaults != (PyDosObj far *)0)
        PYDOS_DECREF(func->v.func.defaults);
    func->v.func.defaults = defaults;
}

void PYDOS_API pydos_func_set_closure(PyDosObj far *func,
                                       PyDosObj far *closure)
{
    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION ||
        (closure != (PyDosObj far *)0 &&
         (PyDosType)closure->type != PYDT_TUPLE)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"function closure must be a tuple");
        return;
    }
    if (closure != (PyDosObj far *)0) PYDOS_INCREF(closure);
    if (func->v.func.closure != (PyDosObj far *)0)
        PYDOS_DECREF(func->v.func.closure);
    func->v.func.closure = closure;
}

PyDosObj far * PYDOS_API pydos_func_set_parameters(PyDosObj far *func,
                                                    PyDosObj far *names,
                                                    PyDosObj far *flags)
{
    PyDosParamSpec far *spec;
    unsigned int total = 0;
    unsigned int count;
    unsigned int i;
    unsigned int pos = 0;
    unsigned long packed = 0UL;
    if (func == (PyDosObj far *)0 || func->type != PYDT_FUNCTION ||
        names == (PyDosObj far *)0 || flags == (PyDosObj far *)0 ||
        names->type != PYDT_TUPLE || flags->type != PYDT_TUPLE ||
        names->v.tuple.len != flags->v.tuple.len) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid function parameter metadata");
        return (PyDosObj far *)0;
    }
    count = names->v.tuple.len;
    for (i = 0; i < count; i++) {
        PyDosObj far *name = names->v.tuple.items[i];
        PyDosObj far *flag = flags->v.tuple.items[i];
        if (name == (PyDosObj far *)0 || name->type != PYDT_STR ||
            flag == (PyDosObj far *)0 || flag->type != PYDT_INT) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"invalid function parameter metadata");
            return (PyDosObj far *)0;
        }
        total += name->v.str.len;
        if (i + 1 < count) total++;
        if (i < 8)
            packed |= ((unsigned long)flag->v.int_val & 0x0FUL) << (i * 4U);
    }
    spec = param_spec_alloc(total, packed);
    if (spec == (PyDosParamSpec far *)0) return (PyDosObj far *)0;
    for (i = 0; i < count; i++) {
        PyDosObj far *name = names->v.tuple.items[i];
        _fmemcpy(spec->names + pos, name->v.str.data, name->v.str.len);
        pos += name->v.str.len;
        if (i + 1 < count) spec->names[pos++] = ',';
    }
    param_spec_release(func->v.func.param_spec);
    func->v.func.param_spec = spec;
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_func_set_param_spec(PyDosObj far *func,
                                                   PyDosObj far *names,
                                                   PyDosObj far *flags)
{
    PyDosParamSpec far *spec;
    if (func == (PyDosObj far *)0 || func->type != PYDT_FUNCTION ||
        names == (PyDosObj far *)0 || names->type != PYDT_STR ||
        flags == (PyDosObj far *)0 || flags->type != PYDT_INT) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid compact parameter metadata");
        return (PyDosObj far *)0;
    }
    spec = param_spec_alloc(names->v.str.len,
                            (unsigned long)flags->v.int_val);
    if (spec == (PyDosParamSpec far *)0) return (PyDosObj far *)0;
    if (names->v.str.len != 0)
        _fmemcpy(spec->names, names->v.str.data, names->v.str.len);
    param_spec_release(func->v.func.param_spec);
    func->v.func.param_spec = spec;
    return pydos_obj_new_none();
}

static PyDosObj far *create_function_code(PyDosObj far *func,
                                          PyDosObj far *freevars,
                                          PyDosObj far *consts)
{
    PyDosObj far *code;
    code = pydos_obj_alloc_type(PYDT_CODE);
    if (code == (PyDosObj far *)0) return (PyDosObj far *)0;
    code->v.code.name = func->v.func.name;
    code->v.code.freevars = freevars;
    code->v.code.consts = consts;
    code->v.code.code_ref = func->v.func.code_ref;
    PYDOS_INCREF(freevars);
    PYDOS_INCREF(consts);
    pydos_code_ref_retain(code->v.code.code_ref);
    return code;
}

static PyDosObj far *materialize_function_code(PyDosObj far *func)
{
    PyDosObj far *pending;
    PyDosObj far *freevars;
    PyDosObj far *consts;
    PyDosObj far *code;

    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION)
        return (PyDosObj far *)0;
    pending = func->v.func.code_obj;
    if (pending != (PyDosObj far *)0 &&
        (PyDosType)pending->type == PYDT_CODE) {
        PYDOS_INCREF(pending);
        return pending;
    }
    freevars = pending != (PyDosObj far *)0
               ? pending : pydos_obj_new_empty_tuple();
    consts = pydos_obj_new_empty_tuple();
    code = create_function_code(func, freevars, consts);
    if (pending == (PyDosObj far *)0) PYDOS_DECREF(freevars);
    PYDOS_DECREF(consts);
    if (code == (PyDosObj far *)0) return (PyDosObj far *)0;
    if (pending != (PyDosObj far *)0) PYDOS_DECREF(pending);
    func->v.func.code_obj = code;
    PYDOS_INCREF(code);
    return code;
}

PyDosObj far * PYDOS_API pydos_func_set_code_metadata(
    PyDosObj far *func, PyDosObj far *name, PyDosObj far *freevars,
    PyDosObj far *consts)
{
    PyDosObj far *metadata;
    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION ||
        name == (PyDosObj far *)0 || (PyDosType)name->type != PYDT_STR ||
        freevars == (PyDosObj far *)0 ||
        (PyDosType)freevars->type != PYDT_TUPLE ||
        consts == (PyDosObj far *)0 ||
        (PyDosType)consts->type != PYDT_TUPLE) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid code object metadata");
        return (PyDosObj far *)0;
    }

    /* co_consts is empty for the current native lowering.  Keep only a
     * non-empty free-variable tuple until reflection asks for __code__.
     * This avoids one permanent GC object per compiled function. */
    metadata = (PyDosObj far *)0;
    if (consts->v.tuple.len > 0) {
        metadata = create_function_code(func, freevars, consts);
        if (metadata == (PyDosObj far *)0) return (PyDosObj far *)0;
    } else if (freevars->v.tuple.len > 0) {
        metadata = freevars;
        PYDOS_INCREF(metadata);
    }
    if (func->v.func.code_obj != (PyDosObj far *)0)
        PYDOS_DECREF(func->v.func.code_obj);
    func->v.func.code_obj = metadata;
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_class_set_method_defaults(
    PyDosObj far *cls, PyDosObj far *method_name, PyDosObj far *defaults)
{
    PyDosMethodSlot far *slot;
    PyDosObj far *registry_key;
    PyDosObj far *registry;

    if (cls == (PyDosObj far *)0 || (PyDosType)cls->type != PYDT_CLASS ||
        method_name == (PyDosObj far *)0 ||
        (PyDosType)method_name->type != PYDT_STR ||
        defaults == (PyDosObj far *)0 ||
        ((PyDosType)defaults->type != PYDT_TUPLE &&
         (PyDosType)defaults->type != PYDT_LIST)) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"invalid method defaults");
        return (PyDosObj far *)0;
    }

    slot = pydos_vtable_lookup_slot(
        cls->v.cls.vtable, method_name_hash(method_name->v.str.data));
    if (slot == (PyDosMethodSlot far *)0) return pydos_obj_new_none();

    /* Keep defaults owned by the class dictionary.  The slot only borrows
     * this pointer, avoiding another per-method owner in the 8086 runtime. */
    registry_key = pydos_obj_new_str(
        (const char far *)"__pydos_method_defaults__", 25);
    registry = pydos_dict_get(cls->v.cls.class_attrs, registry_key);
    if (registry == (PyDosObj far *)0) {
        registry = pydos_dict_new(4);
        if (registry != (PyDosObj far *)0)
            pydos_dict_set(cls->v.cls.class_attrs, registry_key, registry);
    }
    PYDOS_DECREF(registry_key);
    if (registry == (PyDosObj far *)0) return (PyDosObj far *)0;
    pydos_dict_set(registry, method_name, defaults);
    slot->defaults = defaults;
    PYDOS_DECREF(registry);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_bound_method_new(
    void (far *code)(void), PyDosObj far *self, const char far *name)
{
    PyDosCodeRef far *reference;
    PyDosObj far *method;
    reference = pydos_code_ref_new_native(code, PYDOS_CODE_NATIVE);
    if (reference == (PyDosCodeRef far *)0) return (PyDosObj far *)0;
    method = pydos_bound_method_new_from_code_ref(reference, self, name);
    pydos_code_ref_release(reference);
    return method;
}

PyDosObj far * PYDOS_API pydos_bound_method_new_from_code_ref(
    PyDosCodeRef far *code_ref, PyDosObj far *self, const char far *name)
{
    PyDosObj far *method;
    method = pydos_func_new_from_code_ref(code_ref, name);
    if (method == (PyDosObj far *)0) return (PyDosObj far *)0;
    method->v.func.bound_self = self;
    if (self != (PyDosObj far *)0) PYDOS_INCREF(self);
    return method;
}

PyDosObj far * PYDOS_API pydos_func_bind(PyDosObj far *func,
                                          PyDosObj far *self)
{
    PyDosObj far *method;
    if (func == (PyDosObj far *)0 ||
        (PyDosType)func->type != PYDT_FUNCTION)
        return (PyDosObj far *)0;
    method = pydos_func_new_from_code_ref(func->v.func.code_ref,
                                          func->v.func.name);
    if (method == (PyDosObj far *)0) return (PyDosObj far *)0;
    method->v.func.defaults = func->v.func.defaults;
    method->v.func.closure = func->v.func.closure;
    method->v.func.attrs = func->v.func.attrs;
    method->v.func.param_spec = func->v.func.param_spec;
    method->v.func.code_obj = func->v.func.code_obj;
    method->v.func.arg_count = func->v.func.arg_count;
    method->v.func.signature_known = func->v.func.signature_known;
    method->v.func.bound_self = self;
    if (method->v.func.defaults != (PyDosObj far *)0)
        PYDOS_INCREF(method->v.func.defaults);
    if (method->v.func.closure != (PyDosObj far *)0)
        PYDOS_INCREF(method->v.func.closure);
    if (method->v.func.attrs != (PyDosObj far *)0)
        PYDOS_INCREF(method->v.func.attrs);
    param_spec_retain(method->v.func.param_spec);
    if (method->v.func.code_obj != (PyDosObj far *)0)
        PYDOS_INCREF(method->v.func.code_obj);
    if (self != (PyDosObj far *)0) PYDOS_INCREF(self);
    return method;
}
