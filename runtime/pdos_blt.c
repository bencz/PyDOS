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
#include "pdos_vtb.h"
#include "pdos_exc.h"
#include <string.h>

#include "pdos_mem.h"

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
    buf = (char far *)pydos_far_alloc(256UL);
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
        case PYDT_INSTANCE:
            /* Check for __len__ via vtable slot */
            if (obj->v.instance.vtable != (struct PyDosVTable far *)0) {
                struct PyDosVTable far *vt = obj->v.instance.vtable;
                if (vt->slots[VSLOT_LEN] != (void (far *)(void))0) {
                    typedef PyDosObj far * (PYDOS_API far *LenFn)(PyDosObj far *);
                    return ((LenFn)vt->slots[VSLOT_LEN])(obj);
                }
            }
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
    PyDosObj far *obj;
    long start, stop, step;

    if (argc < 1) {
        return pydos_obj_new_none();
    }

    obj = pydos_obj_alloc();
    if (obj == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }

    obj->type = PYDT_RANGE;
    obj->flags = 0;
    obj->refcount = 1;

    if (argc == 1) {
        start = 0L;
        stop = (argv[0] != (PyDosObj far *)0 && argv[0]->type == PYDT_INT)
               ? argv[0]->v.int_val : 0L;
        step = 1L;
    } else if (argc == 2) {
        start = (argv[0] != (PyDosObj far *)0 && argv[0]->type == PYDT_INT)
                ? argv[0]->v.int_val : 0L;
        stop = (argv[1] != (PyDosObj far *)0 && argv[1]->type == PYDT_INT)
               ? argv[1]->v.int_val : 0L;
        step = 1L;
    } else {
        start = (argv[0] != (PyDosObj far *)0 && argv[0]->type == PYDT_INT)
                ? argv[0]->v.int_val : 0L;
        stop = (argv[1] != (PyDosObj far *)0 && argv[1]->type == PYDT_INT)
               ? argv[1]->v.int_val : 0L;
        step = (argv[2] != (PyDosObj far *)0 && argv[2]->type == PYDT_INT)
               ? argv[2]->v.int_val : 1L;
        if (step == 0L) {
            step = 1L;
        }
    }

    obj->v.range.start = start;
    obj->v.range.stop = stop;
    obj->v.range.step = step;
    obj->v.range.current = start;

    return obj;
}

/*
 * type(obj)
 *
 * Returns type name as a string.
 */
PyDosObj far * PYDOS_API pydos_builtin_type(int argc, PyDosObj far * far *argv)
{
    const char far *name;

    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_str_from_cstr("NoneType");
    }

    name = pydos_obj_type_name(argv[0]);
    if (name != (const char far *)0) {
        return pydos_obj_new_str(name, _fstrlen(name));
    }
    return pydos_str_from_cstr("unknown");
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

    /* Second arg is an INT containing the expected PYDT_* type tag */
    if (argv[1]->type == PYDT_INT) {
        expected_tag = argv[1]->v.int_val;

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

    /* Both args should be INT type tags */
    if (argv[0]->type == PYDT_INT && argv[1]->type == PYDT_INT) {
        tag_a = argv[0]->v.int_val;
        tag_b = argv[1]->v.int_val;

        /* Same type */
        if (tag_a == tag_b) return pydos_obj_new_bool(1);

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
        obj->v.instance.vtable != (struct PyDosVTable far *)0 &&
        obj->v.instance.vtable->slots[VSLOT_ABS] != (void (far *)(void))0) {
        typedef PyDosObj far * (PYDOS_API far *AbsFn)(PyDosObj far *);
        AbsFn abs_fn = (AbsFn)obj->v.instance.vtable->slots[VSLOT_ABS];
        return abs_fn(obj);
    }

    return pydos_obj_new_int(0L);
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
    /* repr(x) — type-dispatch string representation */
    if (argc < 1 || argv[0] == (PyDosObj far *)0) return pydos_obj_new_str((const char far *)"None", 4);
    return pydos_obj_to_str(argv[0]);
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

PyDosObj far * PYDOS_API pydos_builtin_dict_conv(int argc, PyDosObj far * far *argv)
{
    (void)argc; (void)argv;
    return pydos_dict_new(8);
}

PyDosObj far * PYDOS_API pydos_builtin_iter(int argc, PyDosObj far * far *argv)
{
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }
    return pydos_obj_get_iter(argv[0]);
}

PyDosObj far * PYDOS_API pydos_builtin_open(int argc, PyDosObj far * far *argv)
{
    /* open(filename, mode) — simplified: mode "r"=0, "w"=1, "rw"=2 */
    const char far *path;
    unsigned char dos_mode;
    int handle;
    PyDosObj far *result;

    if (argc < 1 || argv[0] == (PyDosObj far *)0 ||
        argv[0]->type != PYDT_STR) {
        return pydos_obj_new_none();
    }
    path = argv[0]->v.str.data;
    dos_mode = 0; /* default: read */
    if (argc > 1 && argv[1] != (PyDosObj far *)0 &&
        argv[1]->type == PYDT_STR && argv[1]->v.str.len > 0) {
        if (argv[1]->v.str.data[0] == 'w') dos_mode = 1;
    }
    handle = pydos_dos_file_open(path, dos_mode);
    if (handle < 0) {
        return pydos_obj_new_none();
    }
    result = pydos_obj_new_int((long)handle);
    return result;
}

PyDosObj far * PYDOS_API pydos_builtin_hasattr(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *obj;
    const char far *name;
    unsigned int nhash;

    if (argc < 2 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 || argv[1]->type != PYDT_STR) {
        return pydos_obj_new_bool(0);
    }
    obj = argv[0];
    if ((PyDosType)obj->type != PYDT_INSTANCE ||
        obj->v.instance.vtable == (struct PyDosVTable far *)0) {
        return pydos_obj_new_bool(0);
    }
    name = argv[1]->v.str.data;
    nhash = 5381U;
    {
        const char far *p = name;
        while (*p != '\0') {
            nhash = ((nhash << 5) + nhash) + (unsigned char)*p;
            p++;
        }
    }
    if (pydos_vtable_lookup(obj->v.instance.vtable, nhash) !=
        (void (far *)(void))0) {
        return pydos_obj_new_bool(1);
    }
    return pydos_obj_new_bool(0);
}

PyDosObj far * PYDOS_API pydos_builtin_getattr(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *obj;
    const char far *name;
    unsigned int nhash;
    void (far *mfunc)(void);

    if (argc < 2 || argv[0] == (PyDosObj far *)0 ||
        argv[1] == (PyDosObj far *)0 || argv[1]->type != PYDT_STR) {
        if (argc > 2) return argv[2]; /* default */
        return pydos_obj_new_none();
    }
    obj = argv[0];
    if ((PyDosType)obj->type != PYDT_INSTANCE ||
        obj->v.instance.vtable == (struct PyDosVTable far *)0) {
        if (argc > 2) return argv[2]; /* default */
        return pydos_obj_new_none();
    }
    name = argv[1]->v.str.data;
    nhash = 5381U;
    {
        const char far *p = name;
        while (*p != '\0') {
            nhash = ((nhash << 5) + nhash) + (unsigned char)*p;
            p++;
        }
    }
    mfunc = pydos_vtable_lookup(obj->v.instance.vtable, nhash);
    if (mfunc != (void (far *)(void))0) {
        /* Return as function object wrapping the method */
        PyDosObj far *fn = pydos_obj_alloc();
        if (fn != (PyDosObj far *)0) {
            fn->type = PYDT_FUNCTION;
            fn->refcount = 1;
            fn->flags = 0;
            fn->v.func.code = mfunc;
            fn->v.func.name = (const char far *)0;
            fn->v.func.defaults = (PyDosObj far *)0;
            fn->v.func.closure = (PyDosObj far *)0;
            return fn;
        }
    }
    if (argc > 2) return argv[2]; /* default value */
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_builtin_setattr(int argc, PyDosObj far * far *argv)
{
    /* setattr(obj, name, value) — simplified: no-op for now */
    (void)argc; (void)argv;
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_builtin_super(int argc, PyDosObj far * far *argv)
{
    /* super() in PyDOS resolves at compile time; runtime stub returns self */
    if (argc > 0 && argv[0] != (PyDosObj far *)0) {
        PYDOS_INCREF(argv[0]);
        return argv[0];
    }
    return pydos_obj_new_none();
}

PyDosObj far * PYDOS_API pydos_builtin_next(int argc, PyDosObj far * far *argv)
{
    PyDosObj far *item;
    if (argc < 1 || argv[0] == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION,
                         (const char far *)"");
        return pydos_obj_new_none();
    }
    item = pydos_obj_iter_next(argv[0]);
    if (item == (PyDosObj far *)0) {
        if (argc > 1) {
            /* Return default value */
            PYDOS_INCREF(argv[1]);
            return argv[1];
        }
        pydos_exc_raise(PYDOS_EXC_STOP_ITERATION,
                         (const char far *)"");
        return pydos_obj_new_none();
    }
    return item;
}

void PYDOS_API pydos_builtins_init(void)
{
    /* No global state to initialize for Phase 1 */
}

void PYDOS_API pydos_builtins_shutdown(void)
{
    /* No global state to clean up */
}
