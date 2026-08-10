/*
 * pydos_builtins.c - Built-in functions for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_blt.h"
#include "pdos_obj.h"
#include "pdos_io.h"
#include "pdos_str.h"
#include "pdos_int.h"
#include "pdos_lst.h"
#include "pdos_dic.h"
#include "pdos_fzs.h"
#include "pdos_rng.h"
#include "pdos_gen.h"
#include "pdos_vtb.h"
#include "pdos_exc.h"
#include <string.h>

#include "pdos_mem.h"

static PyDosObj far *builtin_type_objects[PYDT_MAX];

PyDosObj far * PYDOS_API pydos_builtin_type_object(unsigned int type_tag)
{
    PyDosObj far *cls;
    PyDosObj temp;
    const char far *name;

    if (type_tag >= PYDT_MAX) return (PyDosObj far *)0;
    cls = builtin_type_objects[type_tag];
    if (cls == (PyDosObj far *)0) {
        _fmemset(&temp, 0, sizeof(temp));
        temp.type = (unsigned char)type_tag;
        name = type_tag == PYDT_INSTANCE
               ? (const char far *)"object" : pydos_obj_type_name(&temp);
        cls = pydos_class_new(name, pydos_builtin_vtables[type_tag]);
        if (cls == (PyDosObj far *)0) return (PyDosObj far *)0;
        cls->v.cls.runtime_type_tag = (signed char)type_tag;
        builtin_type_objects[type_tag] = cls;
        if (type_tag != PYDT_INSTANCE) {
            PyDosObj far *base;
            base = pydos_builtin_type_object(
                type_tag == PYDT_BOOL ? PYDT_INT : PYDT_INSTANCE);
            if (base != (PyDosObj far *)0) {
                pydos_class_add_base(cls, base);
                PYDOS_DECREF(base);
            }
        }
    }
    PYDOS_INCREF(cls);
    return cls;
}

/*
 * print(*objects, sep=' ', end='\n')
 *
 * For Phase 1: print each argument separated by space, followed by newline.
 * Keyword arguments (sep, end) are not yet supported.
 */
PyDosObj far * PYDOS_API pydos_builtin_print(int argc, PyDosObj far * far *argv)
{
    int i;
    PyDosObj far *str_obj;

    for (i = 0; i < argc; i++) {
        if (i > 0) {
            pydos_dos_putchar(' ');
        }

        if (argv[i] == (PyDosObj far *)0) {
            pydos_dos_write("None", 4);
        } else {
            str_obj = pydos_obj_to_str(argv[i]);
            if (str_obj != (PyDosObj far *)0 && str_obj->type == PYDT_STR) {
                if (str_obj->v.str.len > 0) {
                    pydos_dos_write_far(str_obj->v.str.data, str_obj->v.str.len);
                }
                PYDOS_DECREF(str_obj);
            } else {
                pydos_dos_write("<object>", 8);
                if (str_obj != (PyDosObj far *)0) {
                    PYDOS_DECREF(str_obj);
                }
            }
        }
    }

    pydos_dos_putchar('\r');
    pydos_dos_putchar('\n');

    return pydos_obj_new_none();
}

/*
 * input([prompt])
 *
 * Print optional prompt, read a line from stdin, return as string.
 */
PyDosObj far * PYDOS_API pydos_builtin_input(int argc, PyDosObj far * far *argv)
{
    char far *buf;
    unsigned int len;
    PyDosObj far *result;
    PyDosObj far *prompt;

    /* Print prompt if provided */
    if (argc > 0 && argv[0] != (PyDosObj far *)0) {
        prompt = pydos_obj_to_str(argv[0]);
        if (prompt != (PyDosObj far *)0 && prompt->type == PYDT_STR) {
            if (prompt->v.str.len > 0) {
                pydos_dos_write_far(prompt->v.str.data, prompt->v.str.len);
            }
            PYDOS_DECREF(prompt);
        }
    }

    /* Allocate buffer for input line */
    buf = (char far *)pydos_mem_alloc(PYDOS_MEM_BUFFER, 256UL);
    if (buf == (char far *)0) {
        return pydos_obj_new_str((const char far *)"", 0);
    }

    len = pydos_dos_readline(buf, 255);

    result = pydos_obj_new_str(buf, len);
    pydos_far_free(buf);
    return result;
}

/*
 * len(obj)
 *
 * Return length of string, list, dict, or tuple.
 */
PyDosObj far * PYDOS_API pydos_builtin_len(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *obj;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    obj = argv[0];

    switch (obj->type) {
        case PYDT_STR:
            return pydos_obj_new_int((long)obj->v.str.len);
        case PYDT_BYTES:
            return pydos_obj_new_int((long)obj->v.str.len);
        case PYDT_LIST:
            return pydos_obj_new_int((long)obj->v.list.len);
        case PYDT_DICT:
            return pydos_obj_new_int((long)obj->v.dict.used);
        case PYDT_SET:
            return pydos_obj_new_int((long)obj->v.dict.used);
        case PYDT_TUPLE:
            return pydos_obj_new_int((long)obj->v.tuple.len);
        case PYDT_FROZENSET:
            return pydos_obj_new_int((long)obj->v.frozenset.len);
        case PYDT_BYTEARRAY:
            return pydos_obj_new_int((long)obj->v.bytearray.len);
        case PYDT_RANGE:
            return pydos_range_len(obj);
        case PYDT_INSTANCE:
            /* Check for __len__ via vtable slot */
            if (obj->v.instance.vtable != (struct PyDosVTable far *)0) {
                struct PyDosVTable far *vt = obj->v.instance.vtable;
                void (far *entry)(void) =
                    pydos_vtable_get_special(vt, VSLOT_LEN);
                if (entry != (void (far *)(void))0) {
                    typedef PyDosObj far * (PYDOS_API far *LenFn)(PyDosObj far *);
                    return ((LenFn)entry)(obj);
                }
            }
            if (obj->v.instance.native_storage != (PyDosObj far *)0 &&
                (PyDosType)obj->v.instance.native_storage->type == PYDT_DICT)
                return pydos_obj_new_int(
                    (long)obj->v.instance.native_storage->v.dict.used);
            break;
        default:
            break;
    }

    return pydos_obj_new_int(0L);
}

/*
 * range(stop) or range(start, stop[, step])
 *
 * Returns a range object.
 */
PyDosObj far * PYDOS_API pydos_builtin_range(int argc, PyDosObj far * far *argv)
{
    long start, stop, step;
    int i;

    if (argc < 1 || argc > 3) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"range expected 1 to 3 arguments");
        return (PyDosObj far *)0;
    }
    for (i = 0; i < argc; i++) {
        if (argv[i] == (PyDosObj far *)0 ||
            ((PyDosType)argv[i]->type != PYDT_INT &&
             (PyDosType)argv[i]->type != PYDT_BOOL)) {
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"range arguments must be integers");
            return (PyDosObj far *)0;
        }
    }

    if (argc == 1) {
        start = 0L;
        stop = (PyDosType)argv[0]->type == PYDT_INT
               ? argv[0]->v.int_val : (long)argv[0]->v.bool_val;
        step = 1L;
    } else if (argc == 2) {
        start = (PyDosType)argv[0]->type == PYDT_INT
                ? argv[0]->v.int_val : (long)argv[0]->v.bool_val;
        stop = (PyDosType)argv[1]->type == PYDT_INT
               ? argv[1]->v.int_val : (long)argv[1]->v.bool_val;
        step = 1L;
    } else {
        start = (PyDosType)argv[0]->type == PYDT_INT
                ? argv[0]->v.int_val : (long)argv[0]->v.bool_val;
        stop = (PyDosType)argv[1]->type == PYDT_INT
               ? argv[1]->v.int_val : (long)argv[1]->v.bool_val;
        step = (PyDosType)argv[2]->type == PYDT_INT
               ? argv[2]->v.int_val : (long)argv[2]->v.bool_val;
    }
    return pydos_range_new(start, stop, step);
}

/*
 * type(obj)
 *
 * Returns type name as a string.
 */
PyDosObj far * PYDOS_API pydos_builtin_type(int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_builtin_type_object(PYDT_NONE);
    }

    if ((PyDosType)argv[0]->type == PYDT_INSTANCE &&
        argv[0]->v.instance.cls != (PyDosObj far *)0) {
        PYDOS_INCREF(argv[0]->v.instance.cls);
        return argv[0]->v.instance.cls;
    }

    if ((PyDosType)argv[0]->type == PYDT_CLASS &&
        argv[0]->v.cls.metaclass != (PyDosObj far *)0) {
        PYDOS_INCREF(argv[0]->v.cls.metaclass);
        return argv[0]->v.cls.metaclass;
    }

    return pydos_builtin_type_object((unsigned int)argv[0]->type);
}

static int abc_virtual_subclass(PyDosObj far *candidate,
                                PyDosObj far *abc_class,
                                int depth)
{
    PyDosObj far *key;
    PyDosObj far *registry;
    unsigned int i;
    int matched;

    if (candidate == (PyDosObj far *)0 || abc_class == (PyDosObj far *)0 ||
        (PyDosType)candidate->type != PYDT_CLASS ||
        (PyDosType)abc_class->type != PYDT_CLASS || depth > 32 ||
        abc_class->v.cls.class_attrs == (PyDosObj far *)0)
        return 0;
    key = pydos_obj_new_str((const char far *)"__abc_registry__", 16);
    if (key == (PyDosObj far *)0) return 0;
    registry = pydos_dict_get(abc_class->v.cls.class_attrs, key);
    PYDOS_DECREF(key);
    if (registry == (PyDosObj far *)0) return 0;
    matched = 0;
    if ((PyDosType)registry->type == PYDT_LIST) {
        for (i = 0; i < registry->v.list.len && !matched; i++) {
            PyDosObj far *registered = registry->v.list.items[i];
            if (registered != (PyDosObj far *)0 &&
                (PyDosType)registered->type == PYDT_CLASS &&
                (candidate == registered ||
                 pydos_class_is_subclass(candidate, registered) ||
                 abc_virtual_subclass(candidate, registered, depth + 1)))
                matched = 1;
        }
    }
    PYDOS_DECREF(registry);
    return matched;
}

/* Return -1 when the hook defers, otherwise its boolean decision.  DOS
 * execution is single-threaded, so a tiny recursion guard is sufficient for
 * hooks that call issubclass() themselves. */
static int class_subclasshook_decision(PyDosObj far *candidate,
                                       PyDosObj far *base)
{
    static int hook_depth = 0;
    PyDosObj far *hook;
    PyDosObj far *result;
    PyDosObj far *args[1];
    int decision;

    if (hook_depth > 0 || candidate == (PyDosObj far *)0 ||
        base == (PyDosObj far *)0 ||
        (PyDosType)candidate->type != PYDT_CLASS ||
        (PyDosType)base->type != PYDT_CLASS)
        return -1;
    if (!pydos_obj_has_attr(base,
                            (const char far *)"__subclasshook__"))
        return -1;
    hook = pydos_obj_get_attr(base,
                              (const char far *)"__subclasshook__");
    if (hook == (PyDosObj far *)0) return -1;
    if ((PyDosType)hook->type == PYDT_NONE) {
        PYDOS_DECREF(hook);
        return -1;
    }
    args[0] = candidate;
    hook_depth++;
    result = pydos_obj_call(hook, 1, args);
    hook_depth--;
    PYDOS_DECREF(hook);
    if (result == (PyDosObj far *)0) return -1;
    if ((PyDosType)result->type == PYDT_NONE) {
        PYDOS_DECREF(result);
        return -1;
    }
    decision = pydos_obj_is_truthy(result) ? 1 : 0;
    PYDOS_DECREF(result);
    return decision;
}

/*
 * isinstance(obj, classinfo)
 *
 * The compiler passes the second arg as an INT object containing the
 * PYDT_* type tag (e.g., PYDT_INT=2 for `int`, PYDT_STR=4 for `str`).
 * For PYDT_INSTANCE checks, any instance matches regardless of class.
 */
PyDosObj far * PYDOS_API pydos_builtin_isinstance(int argc, PyDosObj far * far *argv)
{
    long expected_tag;

    if (argc < 2) {
        return pydos_obj_new_bool(0);
    }

    if (argv[0] == (PyDosObj far *)0 || argv[1] == (PyDosObj far *)0) {
        return pydos_obj_new_bool(0);
    }

    if ((PyDosType)argv[1]->type == PYDT_TUPLE) {
        unsigned int i;
        for (i = 0; i < argv[1]->v.tuple.len; i++) {
            PyDosObj far *nested_args[2];
            PyDosObj far *matched;
            int truth;
            nested_args[0] = argv[0];
            nested_args[1] = argv[1]->v.tuple.items[i];
            matched = pydos_builtin_isinstance(2, nested_args);
            truth = matched != (PyDosObj far *)0 &&
                    pydos_obj_is_truthy(matched);
            if (matched != (PyDosObj far *)0) PYDOS_DECREF(matched);
            if (truth) return pydos_obj_new_bool(1);
        }
        return pydos_obj_new_bool(0);
    }

    if ((PyDosType)argv[1]->type == PYDT_CLASS) {
        int hook_decision;
        if (argv[1]->v.cls.runtime_type_tag >= 0) {
            long expected_tag = (long)argv[1]->v.cls.runtime_type_tag;
            if (expected_tag == PYDT_INSTANCE)
                return pydos_obj_new_bool(1);
            if (expected_tag == PYDT_INT &&
                ((PyDosType)argv[0]->type == PYDT_INT ||
                 (PyDosType)argv[0]->type == PYDT_BOOL))
                return pydos_obj_new_bool(1);
            return pydos_obj_new_bool((long)argv[0]->type == expected_tag);
        }
        hook_decision = (PyDosType)argv[0]->type == PYDT_INSTANCE &&
                        argv[0]->v.instance.cls != (PyDosObj far *)0
                        ? class_subclasshook_decision(
                              argv[0]->v.instance.cls, argv[1]) : -1;
        if (hook_decision >= 0)
            return pydos_obj_new_bool(hook_decision);
        return pydos_obj_new_bool(
            (PyDosType)argv[0]->type == PYDT_INSTANCE &&
            argv[0]->v.instance.cls != (PyDosObj far *)0 &&
            (pydos_class_is_subclass(argv[0]->v.instance.cls, argv[1]) ||
             abc_virtual_subclass(argv[0]->v.instance.cls, argv[1], 0)));
    }

    /* Second arg is an INT containing the expected PYDT_* type tag */
    if (argv[1]->type == PYDT_INT) {
        expected_tag = argv[1]->v.int_val;

        if (expected_tag == PYDT_INSTANCE)
            return pydos_obj_new_bool(1);

        /* Special: bool is a subtype of int in Python */
        if (expected_tag == PYDT_INT &&
            ((PyDosType)argv[0]->type == PYDT_INT ||
             (PyDosType)argv[0]->type == PYDT_BOOL)) {
            return pydos_obj_new_bool(1);
        }

        return pydos_obj_new_bool(
            (long)argv[0]->type == expected_tag ? 1 : 0);
    }

    /* Fallback: compare type tags directly */
    return pydos_obj_new_bool(argv[0]->type == argv[1]->type ? 1 : 0);
}

/*
 * issubclass(cls, classinfo)
 *
 * The compiler resolves issubclass() at compile time in most cases,
 * so this is a minimal fallback. Both args are INT type tags.
 */
PyDosObj far * PYDOS_API pydos_builtin_issubclass(int argc, PyDosObj far * far *argv)
{
    long tag_a, tag_b;

    if (argc < 2) {
        return pydos_obj_new_bool(0);
    }

    if (argv[0] == (PyDosObj far *)0 || argv[1] == (PyDosObj far *)0) {
        return pydos_obj_new_bool(0);
    }

    if ((PyDosType)argv[1]->type == PYDT_TUPLE) {
        unsigned int i;
        for (i = 0; i < argv[1]->v.tuple.len; i++) {
            PyDosObj far *nested_args[2];
            PyDosObj far *matched;
            int truth;
            nested_args[0] = argv[0];
            nested_args[1] = argv[1]->v.tuple.items[i];
            matched = pydos_builtin_issubclass(2, nested_args);
            truth = matched != (PyDosObj far *)0 &&
                    pydos_obj_is_truthy(matched);
            if (matched != (PyDosObj far *)0) PYDOS_DECREF(matched);
            if (truth) return pydos_obj_new_bool(1);
        }
        return pydos_obj_new_bool(0);
    }

    if ((PyDosType)argv[0]->type == PYDT_CLASS &&
        (PyDosType)argv[1]->type == PYDT_CLASS) {
        int hook_decision;
        if (argv[0]->v.cls.runtime_type_tag >= 0 &&
            argv[1]->v.cls.runtime_type_tag >= 0) {
            int tag_a = (int)argv[0]->v.cls.runtime_type_tag;
            int tag_b = (int)argv[1]->v.cls.runtime_type_tag;
            return pydos_obj_new_bool(tag_b == PYDT_INSTANCE ||
                                      tag_a == tag_b ||
                                      (tag_a == PYDT_BOOL &&
                                       tag_b == PYDT_INT));
        }
        hook_decision = class_subclasshook_decision(argv[0], argv[1]);
        if (hook_decision >= 0)
            return pydos_obj_new_bool(hook_decision);
        return pydos_obj_new_bool(
            pydos_class_is_subclass(argv[0], argv[1]) ||
            abc_virtual_subclass(argv[0], argv[1], 0));
    }

    /* Both args should be INT type tags */
    if (argv[0]->type == PYDT_INT && argv[1]->type == PYDT_INT) {
        tag_a = argv[0]->v.int_val;
        tag_b = argv[1]->v.int_val;

        /* Same type */
        if (tag_a == tag_b) return pydos_obj_new_bool(1);

        /* Every Python class is a subclass of object. */
        if (tag_b == PYDT_INSTANCE) return pydos_obj_new_bool(1);

        /* bool is subclass of int */
        if (tag_a == PYDT_BOOL && tag_b == PYDT_INT)
            return pydos_obj_new_bool(1);

        return pydos_obj_new_bool(0);
    }

    return pydos_obj_new_bool(0);
}

/*
 * int(x)
 *
 * Convert to integer.
 */
PyDosObj far * PYDOS_API pydos_builtin_int_conv(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *obj;
    PyDosObj far *result;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    obj = argv[0];

    switch (obj->type) {
        case PYDT_INT:
            return pydos_obj_new_int(obj->v.int_val);
        case PYDT_BOOL:
            return pydos_obj_new_int((long)obj->v.bool_val);
        case PYDT_FLOAT:
            return pydos_obj_new_int((long)obj->v.float_val);
        case PYDT_STR:
            result = pydos_int_from_str(obj);
            if (result != (PyDosObj far *)0) {
                return result;
            }
            return pydos_obj_new_int(0L);
        default:
            break;
    }

    return pydos_obj_new_int(0L);
}

/*
 * str(x)
 *
 * Convert to string.
 */
PyDosObj far * PYDOS_API pydos_builtin_str_conv(int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_str_from_cstr("");
    }

    return pydos_obj_to_str(argv[0]);
}

/*
 * bool(x)
 *
 * Convert to boolean.
 */
PyDosObj far * PYDOS_API pydos_builtin_bool_conv(int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_bool(0);
    }

    return pydos_obj_new_bool(pydos_obj_is_truthy(argv[0]));
}

/*
 * abs(x)
 *
 * Return absolute value.
 */
PyDosObj far * PYDOS_API pydos_builtin_abs(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *obj;
    long val;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    obj = argv[0];

    if (obj->type == PYDT_INT) {
        val = obj->v.int_val;
        return pydos_obj_new_int(val < 0L ? -val : val);
    }
    if (obj->type == PYDT_FLOAT) {
        return pydos_obj_new_float(obj->v.float_val < 0.0 ?
                                   -obj->v.float_val : obj->v.float_val);
    }
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (struct PyDosVTable far *)0) {
        typedef PyDosObj far * (PYDOS_API far *AbsFn)(PyDosObj far *);
        AbsFn abs_fn = (AbsFn)pydos_vtable_get_special(
            obj->v.instance.vtable, VSLOT_ABS);
        if (abs_fn != (AbsFn)0) return abs_fn(obj);
    }

    return pydos_obj_new_int(0L);
}

static double round_half_even_value(double value)
{
    long whole = (long)value;
    double fraction = value - (double)whole;
    if (fraction > 0.5 ||
        (fraction == 0.5 && (whole % 2L) != 0L))
        whole++;
    else if (fraction < -0.5 ||
             (fraction == -0.5 && (whole % 2L) != 0L))
        whole--;
    return (double)whole;
}

/* Numeric primitive used by the Python round() builtin.  Decimal scaling is
 * intentionally kept here: doing it in Python would still require an exact
 * primitive for the final half-even integral rounding operation. */
PyDosObj far * PYDOS_API pydos_builtin_round(int argc,
                                              PyDosObj far * far *argv)
{
    PyDosObj far *number;
    long ndigits = 0L;
    double value;
    double scale = 1.0;
    double rounded;
    long i;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"round() missing number");
        return (PyDosObj far *)0;
    }
    number = argv[0];
    if ((PyDosType)number->type == PYDT_INT)
        value = (double)number->v.int_val;
    else if ((PyDosType)number->type == PYDT_BOOL)
        value = number->v.bool_val ? 1.0 : 0.0;
    else if ((PyDosType)number->type == PYDT_FLOAT)
        value = number->v.float_val;
    else {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"round() requires a number");
        return (PyDosObj far *)0;
    }

    if (argc < 2 || argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[1]->type == PYDT_NONE) {
        rounded = round_half_even_value(value);
        return pydos_obj_new_int((long)rounded);
    }
    if ((PyDosType)argv[1]->type != PYDT_INT &&
        (PyDosType)argv[1]->type != PYDT_BOOL) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"ndigits must be an integer");
        return (PyDosObj far *)0;
    }
    ndigits = (PyDosType)argv[1]->type == PYDT_INT
              ? argv[1]->v.int_val : (long)argv[1]->v.bool_val;
    for (i = 0; i < (ndigits < 0L ? -ndigits : ndigits) && i < 18L; i++)
        scale *= 10.0;
    if (ndigits >= 0L)
        rounded = round_half_even_value(value * scale) / scale;
    else
        rounded = round_half_even_value(value / scale) * scale;

    if ((PyDosType)number->type == PYDT_INT ||
        (PyDosType)number->type == PYDT_BOOL)
        return pydos_obj_new_int((long)rounded);
    return pydos_obj_new_float(rounded);
}

/*
 * ord(c)
 *
 * Return Unicode code point for a one-character string.
 */
PyDosObj far * PYDOS_API pydos_builtin_ord(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *obj;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_int(0L);
    }

    obj = argv[0];

    if (obj->type == PYDT_STR && obj->v.str.len == 1) {
        return pydos_obj_new_int((long)(unsigned char)obj->v.str.data[0]);
    }

    return pydos_obj_new_int(0L);
}

/*
 * chr(i)
 *
 * Return string of one character with given code point.
 */
PyDosObj far * PYDOS_API pydos_builtin_chr(int argc, PyDosObj far * far *argv)
{
    long val;
    char c;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_str_from_cstr("");
    }

    if (argv[0]->type != PYDT_INT) {
        return pydos_str_from_cstr("");
    }

    val = argv[0]->v.int_val;
    if (val < 0 || val > 255) {
        return pydos_str_from_cstr("");
    }

    c = (char)(unsigned char)val;
    return pydos_obj_new_str((const char far *)&c, 1);
}

/*
 * hex(x)
 *
 * Return hex string representation of integer.
 */
PyDosObj far * PYDOS_API pydos_builtin_hex(int argc, PyDosObj far * far *argv)
{
    long val;
    unsigned long uval;
    char buf[12]; /* "0x" + 8 hex digits + null */
    int pos, neg, i, j;
    char tmp;
    static const char hexchars[] = "0123456789abcdef";

    if (argc < 1 || argv[0] == (PyDosObj far *)0 ||
        argv[0]->type != PYDT_INT) {
        return pydos_str_from_cstr("0x0");
    }

    val = argv[0]->v.int_val;
    neg = 0;

    if (val < 0) {
        neg = 1;
        uval = (unsigned long)(-(val + 1)) + 1UL;
    } else {
        uval = (unsigned long)val;
    }

    /* Build hex digits in reverse */
    pos = 0;
    if (uval == 0) {
        buf[pos] = '0';
        pos++;
    } else {
        while (uval > 0) {
            buf[pos] = hexchars[(int)(uval & 0x0FUL)];
            pos++;
            uval >>= 4;
        }
    }

    /* Add "x0" prefix (reversed) */
    buf[pos] = 'x';
    pos++;
    buf[pos] = '0';
    pos++;

    if (neg) {
        buf[pos] = '-';
        pos++;
    }

    /* Reverse the entire string */
    for (i = 0, j = pos - 1; i < j; i++, j--) {
        tmp = buf[i];
        buf[i] = buf[j];
        buf[j] = tmp;
    }

    buf[pos] = '\0';
    return pydos_obj_new_str((const char far *)buf, (unsigned int)pos);
}


PyDosObj far * PYDOS_API pydos_builtin_float_conv(int argc, PyDosObj far * far *argv)
{
    double val = 0.0;
    if (argc < 1 || argv[0] == (PyDosObj far *)0) return pydos_obj_new_float(0.0);
    if (argv[0]->type == PYDT_INT) {
        val = (double)argv[0]->v.int_val;
    } else if (argv[0]->type == PYDT_FLOAT) {
        val = argv[0]->v.float_val;
    } else if (argv[0]->type == PYDT_BOOL) {
        val = argv[0]->v.int_val ? 1.0 : 0.0;
    }
    return pydos_obj_new_float(val);
}

PyDosObj far * PYDOS_API pydos_builtin_repr(int argc, PyDosObj far * far *argv)
{
    if (argc < 1) return pydos_obj_new_str((const char far *)"None", 4);
    return pydos_obj_repr(argv[0]);
}

PyDosObj far * PYDOS_API pydos_builtin_hash(int argc, PyDosObj far * far *argv)
{
    unsigned int h;
    if (argc < 1 || argv[0] == (PyDosObj far *)0) return pydos_obj_new_int(0L);
    h = pydos_obj_hash(argv[0]);
    return pydos_obj_new_int((long)h);
}

PyDosObj far * PYDOS_API pydos_builtin_id(int argc, PyDosObj far * far *argv)
{
    /* id(x) — return pointer value as int */
    unsigned long addr;
    if (argc < 1 || argv[0] == (PyDosObj far *)0) return pydos_obj_new_int(0L);
#ifdef PYDOS_32BIT
    addr = (unsigned long)argv[0];
#else
    /* Far pointer: segment * 16 + offset */
    {
        unsigned int seg, off;
        seg = (unsigned int)((unsigned long)argv[0] >> 16);
        off = (unsigned int)((unsigned long)argv[0] & 0xFFFFUL);
        addr = ((unsigned long)seg << 4) + (unsigned long)off;
    }
#endif
    return pydos_obj_new_int((long)addr);
}

/* ------------------------------------------------------------------ */
/* Phase 4: Additional builtin functions                               */
/* ------------------------------------------------------------------ */

PyDosObj far * PYDOS_API pydos_builtin_list_conv(int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_list_new(0);
    }
    return pydos_list_from_iter(argv[0]);
}

PyDosObj far * PYDOS_API pydos_builtin_tuple_conv(int argc,
                                                   PyDosObj far * far *argv)
{
    PyDosObj far *result;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        result = pydos_list_new(0);
        if (result != (PyDosObj far *)0) result->type = PYDT_TUPLE;
        return result;
    }

    /* Python preserves identity when tuple() receives an exact tuple. */
    if ((PyDosType)argv[0]->type == PYDT_TUPLE) {
        PYDOS_INCREF(argv[0]);
        return argv[0];
    }

    result = pydos_list_from_iter(argv[0]);
    if (result == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"tuple() argument must be iterable");
        return (PyDosObj far *)0;
    }
    result->type = PYDT_TUPLE;
    return result;
}

PyDosObj far * PYDOS_API pydos_builtin_object_conv(
    int argc, PyDosObj far * far *argv)
{
    PyDosObj far *result;
    PyDosObj far *object_class;
    (void)argv;

    if (argc != 0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"object() takes no arguments");
        return (PyDosObj far *)0;
    }
    result = pydos_obj_alloc_type(PYDT_INSTANCE);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    pydos_obj_set_vtable(result, pydos_builtin_vtables[PYDT_INSTANCE]);
    object_class = pydos_builtin_type_object(PYDT_INSTANCE);
    if (object_class == (PyDosObj far *)0) {
        PYDOS_DECREF(result);
        return (PyDosObj far *)0;
    }
    pydos_obj_set_class(result, object_class);
    PYDOS_DECREF(object_class);
    return result;
}

PyDosObj far * PYDOS_API pydos_builtin_memoryview(
    int argc, PyDosObj far * far *argv)
{
    PyDosObj far *source;
    PyDosObj far *view;
    if (argc != 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"memoryview() expects one argument");
        return (PyDosObj far *)0;
    }
    source = argv[0];
    if ((PyDosType)source->type == PYDT_BYTES ||
        (PyDosType)source->type == PYDT_BYTEARRAY ||
        (PyDosType)source->type == PYDT_MEMORYVIEW)
        return pydos_memoryview_new(source, (PyDosObj far *)0);

    if ((PyDosType)source->type == PYDT_INSTANCE &&
        pydos_obj_has_attr(source, (const char far *)"__buffer__")) {
        PyDosObj far *flags = pydos_obj_new_int(0L);
        PyDosObj far *args[2];
        args[0] = source;
        args[1] = flags;
        view = pydos_obj_call_method(
            (const char far *)"__buffer__", 2, args);
        PYDOS_DECREF(flags);
        if (view == (PyDosObj far *)0) return (PyDosObj far *)0;
        if ((PyDosType)view->type != PYDT_MEMORYVIEW) {
            PYDOS_DECREF(view);
            pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                            (const char far *)"__buffer__ must return memoryview");
            return (PyDosObj far *)0;
        }
        {
            PyDosObj far *wrapped = pydos_memoryview_new(view, source);
            PYDOS_DECREF(view);
            return wrapped;
        }
    }
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"object does not support the buffer protocol");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_builtin_dict_conv(int argc, PyDosObj far * far *argv)
{
    (void)argc; (void)argv;
    return pydos_dict_new(8);
}

PyDosObj far * PYDOS_API pydos_builtin_set_conv(int argc,
                                                 PyDosObj far * far *argv)
{
    PyDosObj far *result;
    PyDosObj far *items;
    unsigned int i;

    result = pydos_dict_new(8);
    if (result == (PyDosObj far *)0) return (PyDosObj far *)0;
    result->type = PYDT_SET;

    /* The compiler represents an omitted optional argument as None. */
    if (argc < 1 || argv[0] == (PyDosObj far *)0 ||
        (PyDosType)argv[0]->type == PYDT_NONE) {
        return result;
    }

    items = pydos_list_from_iter(argv[0]);
    if (items == (PyDosObj far *)0) return result;
    for (i = 0; i < items->v.list.len; i++) {
        pydos_set_add(result, items->v.list.items[i]);
    }
    PYDOS_DECREF(items);
    return result;
}

PyDosObj far * PYDOS_API pydos_builtin_set_empty(
                                      int argc, PyDosObj far * far *argv)
{
    (void)argc;
    (void)argv;
    return pydos_set_empty((PyDosObj far *)0);
}

PyDosObj far * PYDOS_API pydos_builtin_frozenset_from_list(
                                      int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_frozenset_new((PyDosObj far * far *)0, 0);
    }
    return pydos_frozenset_from_list((PyDosObj far *)0, argv[0]);
}

PyDosObj far * PYDOS_API pydos_builtin_iter(int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }
    return pydos_obj_get_iter(argv[0]);
}

PyDosObj far * PYDOS_API pydos_builtin_hasattr(int argc, PyDosObj far * far *argv)
{
    if (argc < 2 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 || argv[1]->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be string");
        return (PyDosObj far *)0;
    }
    return pydos_obj_new_bool(
        pydos_obj_has_attr(argv[0], argv[1]->v.str.data));
}

PyDosObj far * PYDOS_API pydos_builtin_getattr(int argc, PyDosObj far * far *argv)
{
    if (argc < 2 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 || argv[1]->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be string");
        return (PyDosObj far *)0;
    }
    if (pydos_obj_has_attr(argv[0], argv[1]->v.str.data)) {
        return pydos_obj_get_attr(argv[0], argv[1]->v.str.data);
    }
    if (argc > 2) {
        PYDOS_INCREF(argv[2]);
        return argv[2];
    }
    pydos_exc_raise(PYDOS_EXC_ATTRIBUTE_ERROR,
                    (const char far *)"object has no such attribute");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_builtin_setattr(int argc, PyDosObj far * far *argv)
{
    if (argc < 3 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[1]->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be string");
        return (PyDosObj far *)0;
    }
    if ((PyDosType)argv[0]->type != PYDT_INSTANCE &&
        (PyDosType)argv[0]->type != PYDT_CLASS &&
        (PyDosType)argv[0]->type != PYDT_FUNCTION) {
        pydos_exc_raise(PYDOS_EXC_ATTRIBUTE_ERROR,
                        (const char far *)"object has no writable attributes");
        return (PyDosObj far *)0;
    }
    pydos_obj_set_attr(argv[0], argv[1]->v.str.data, argv[2]);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_builtin_delattr(int argc,
                                                PyDosObj far * far *argv)
{
    if (argc < 2 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[1]->type != PYDT_STR) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"attribute name must be string");
        return (PyDosObj far *)0;
    }
    if (((PyDosType)argv[0]->type != PYDT_INSTANCE &&
         (PyDosType)argv[0]->type != PYDT_CLASS) ||
        !pydos_obj_has_attr(argv[0], argv[1]->v.str.data)) {
        pydos_exc_raise(PYDOS_EXC_ATTRIBUTE_ERROR,
                        (const char far *)"object has no such attribute");
        return (PyDosObj far *)0;
    }
    pydos_obj_del_attr(argv[0], argv[1]->v.str.data);
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_builtin_callable(int argc,
                                                PyDosObj far * far *argv)
{
    PyDosObj far *obj;
    int result;
    if (argc < 1 || argv[0] == (PyDosObj far *)0)
        return pydos_obj_new_bool(0);
    obj = argv[0];
    result = (PyDosType)obj->type == PYDT_FUNCTION ||
             (PyDosType)obj->type == PYDT_CLASS;
    if ((PyDosType)obj->type == PYDT_INSTANCE &&
        obj->v.instance.vtable != (PyDosVTable far *)0 &&
        pydos_vtable_get_special(obj->v.instance.vtable, VSLOT_CALL) !=
        (void (far *)(void))0) result = 1;
    return pydos_obj_new_bool(result);
}

PyDosObj far * PYDOS_API pydos_builtin_bind_method(
                                                int argc,
                                                PyDosObj far * far *argv)
{
    PyDosObj far *bound;
    if (argc < 2 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 ||
        (PyDosType)argv[0]->type != PYDT_FUNCTION) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"classmethod requires a function");
        return (PyDosObj far *)0;
    }
    bound = pydos_func_bind(argv[0], argv[1]);
    if (bound == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"cannot bind class method");
    }
    return bound;
}

PyDosObj far * PYDOS_API pydos_builtin_vars(int argc,
                                            PyDosObj far * far *argv)
{
    PyDosObj far *dict;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"vars() requires an object");
        return (PyDosObj far *)0;
    }
    if ((PyDosType)argv[0]->type != PYDT_INSTANCE &&
        (PyDosType)argv[0]->type != PYDT_CLASS &&
        (PyDosType)argv[0]->type != PYDT_FUNCTION) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"vars() argument has no __dict__");
        return (PyDosObj far *)0;
    }

    dict = pydos_obj_get_attr(argv[0], (const char far *)"__dict__");
    if (dict == (PyDosObj far *)0 || (PyDosType)dict->type != PYDT_DICT) {
        if (dict != (PyDosObj far *)0) PYDOS_DECREF(dict);
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"vars() argument has no __dict__");
        return (PyDosObj far *)0;
    }
    return dict;
}

PyDosObj far * PYDOS_API pydos_builtin_super(int argc, PyDosObj far * far *argv)
{
    if (argc == 2)
        return pydos_super_new(argv[0], argv[1]);
    pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                    (const char far *)"super() expects zero or two arguments");
    return (PyDosObj far *)0;
}

PyDosObj far * PYDOS_API pydos_builtin_next(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *item;
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION,
                         (const char far *)"");
        return (PyDosObj far *)0;
    }
    if ((PyDosType)argv[0]->type == PYDT_GENERATOR &&
        argv[0]->v.gen.resume != (void (far *)(void))0)
        item = pydos_gen_next(argv[0]);
    else
        item = pydos_obj_iter_next(argv[0]);
    if (item == (PyDosObj far *)0) {
        if (argc > 1) {
            /* Return default value */
            if (pydos_exc_pending()) pydos_exc_clear();
            PYDOS_INCREF(argv[1]);
            return argv[1];
        }
        if (!pydos_exc_pending())
            pydos_exc_raise(PYDOS_EXC_STOP_ITERATION,
                            (const char far *)"");
        return (PyDosObj far *)0;
    }
    return item;
}

void PYDOS_API pydos_builtins_init(void)
{
    int i;
    for (i = 0; i < PYDT_MAX; i++)
        builtin_type_objects[i] = (PyDosObj far *)0;
}

void PYDOS_API pydos_builtins_shutdown(void)
{
    int i;
    for (i = 0; i < PYDT_MAX; i++) {
        if (builtin_type_objects[i] != (PyDosObj far *)0) {
            PYDOS_DECREF(builtin_type_objects[i]);
            builtin_type_objects[i] = (PyDosObj far *)0;
        }
    }
}
