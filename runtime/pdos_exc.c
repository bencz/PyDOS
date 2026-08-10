/*
 * pydos_exc.c - Exception handling for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Exceptions are propagated explicitly by generated control flow.  Runtime
 * primitives only set the pending exception; callers return a failure
 * sentinel and generated code branches to the active exception landing pad.
 */

#include "pdos_exc.h"
#include "pdos_io.h"
#include "pdos_obj.h"
#include <dos.h>
#include <stdlib.h>
#include <string.h>

#include "pdos_mem.h"

/* Current exception value (if any) */
static PyDosObj far *current_exc = (PyDosObj far *)0;

/* Raising MemoryError must not allocate memory.  Keep one immortal exception
 * and message in static storage for allocator-failure paths. */
static PyDosObj emergency_memory_exc;
static PyDosObj emergency_memory_message;
static char emergency_memory_text[] = "out of memory";
static int emergency_memory_ready = 0;

static PyDosObj far *emergency_memory_error(void)
{
    if (!emergency_memory_ready) {
        memset(&emergency_memory_message, 0,
               sizeof(emergency_memory_message));
        emergency_memory_message.type = PYDT_STR;
        emergency_memory_message.flags = OBJ_FLAG_IMMORTAL;
        emergency_memory_message.refcount = REFCOUNT_MAX;
        emergency_memory_message.v.str.data =
            (char far *)emergency_memory_text;
        emergency_memory_message.v.str.len =
            (unsigned int)(sizeof(emergency_memory_text) - 1U);

        memset(&emergency_memory_exc, 0, sizeof(emergency_memory_exc));
        emergency_memory_exc.type = PYDT_EXCEPTION;
        emergency_memory_exc.flags = OBJ_FLAG_IMMORTAL;
        emergency_memory_exc.refcount = REFCOUNT_MAX;
        emergency_memory_exc.v.exc.type_code = PYDOS_EXC_MEMORY_ERROR;
        emergency_memory_exc.v.exc.message =
            (PyDosObj far *)&emergency_memory_message;
        emergency_memory_ready = 1;
    }
    return (PyDosObj far *)&emergency_memory_exc;
}

/* Exception type name table — indexed by type code 0..37 */
static const char *exc_type_names[] = {
    "BaseException",        /*  0 PYDOS_EXC_BASE */
    "Exception",            /*  1 PYDOS_EXC_EXCEPTION */
    "TypeError",            /*  2 PYDOS_EXC_TYPE_ERROR */
    "ValueError",           /*  3 PYDOS_EXC_VALUE_ERROR */
    "KeyError",             /*  4 PYDOS_EXC_KEY_ERROR */
    "IndexError",           /*  5 PYDOS_EXC_INDEX_ERROR */
    "AttributeError",       /*  6 PYDOS_EXC_ATTRIBUTE_ERROR */
    "NameError",            /*  7 PYDOS_EXC_NAME_ERROR */
    "RuntimeError",         /*  8 PYDOS_EXC_RUNTIME_ERROR */
    "StopIteration",        /*  9 PYDOS_EXC_STOP_ITERATION */
    "ZeroDivisionError",    /* 10 PYDOS_EXC_ZERO_DIVISION */
    "OverflowError",        /* 11 PYDOS_EXC_OVERFLOW */
    "OSError",              /* 12 PYDOS_EXC_OS_ERROR */
    "NotImplementedError",  /* 13 PYDOS_EXC_NOT_IMPLEMENTED */
    "MemoryError",          /* 14 PYDOS_EXC_MEMORY_ERROR */
    "AssertionError",       /* 15 PYDOS_EXC_ASSERTION_ERROR */
    "GeneratorExit",        /* 16 PYDOS_EXC_GENERATOR_EXIT */
    "ExceptionGroup",       /* 17 PYDOS_EXC_EXCEPTION_GROUP */
    "LookupError",          /* 18 PYDOS_EXC_LOOKUP_ERROR */
    "ArithmeticError",      /* 19 PYDOS_EXC_ARITHMETIC_ERROR */
    "SystemExit",           /* 20 PYDOS_EXC_SYSTEM_EXIT */
    "KeyboardInterrupt",    /* 21 PYDOS_EXC_KEYBOARD_INTERRUPT */
    "ImportError",          /* 22 PYDOS_EXC_IMPORT_ERROR */
    "ModuleNotFoundError",  /* 23 PYDOS_EXC_MODULE_NOT_FOUND */
    "SyntaxError",          /* 24 PYDOS_EXC_SYNTAX_ERROR */
    "IndentationError",     /* 25 PYDOS_EXC_INDENTATION_ERROR */
    "UnboundLocalError",    /* 26 PYDOS_EXC_UNBOUND_LOCAL */
    "FloatingPointError",   /* 27 PYDOS_EXC_FLOATING_POINT */
    "FileNotFoundError",    /* 28 PYDOS_EXC_FILE_NOT_FOUND */
    "PermissionError",      /* 29 PYDOS_EXC_PERMISSION_ERROR */
    "UnicodeError",         /* 30 PYDOS_EXC_UNICODE_ERROR */
    "UnicodeDecodeError",   /* 31 PYDOS_EXC_UNICODE_DECODE */
    "UnicodeEncodeError",   /* 32 PYDOS_EXC_UNICODE_ENCODE */
    "BufferError",          /* 33 PYDOS_EXC_BUFFER_ERROR */
    "EOFError",             /* 34 PYDOS_EXC_EOF_ERROR */
    "RecursionError",       /* 35 PYDOS_EXC_RECURSION_ERROR */
    "StopAsyncIteration",   /* 36 PYDOS_EXC_STOP_ASYNC_ITER */
    "TimeoutError"          /* 37 PYDOS_EXC_TIMEOUT_ERROR */
};

#define NUM_EXC_TYPES 38

/*
 * Exception parent table — indexed by type code.
 * exc_parent[i] is the parent type code of exception i.
 * BaseException is the root: exc_parent[0] = 0 (self, sentinel).
 * GeneratorExit, SystemExit, KeyboardInterrupt inherit from BaseException.
 * All others inherit from Exception unless specified otherwise.
 */
static const int exc_parent[38] = {
    /* [ 0] BaseException      */ 0,
    /* [ 1] Exception          */ 0,
    /* [ 2] TypeError          */ 1,
    /* [ 3] ValueError         */ 1,
    /* [ 4] KeyError           */ 18, /* LookupError */
    /* [ 5] IndexError         */ 18, /* LookupError */
    /* [ 6] AttributeError     */ 1,
    /* [ 7] NameError          */ 1,
    /* [ 8] RuntimeError       */ 1,
    /* [ 9] StopIteration      */ 1,
    /* [10] ZeroDivisionError  */ 19, /* ArithmeticError */
    /* [11] OverflowError      */ 19, /* ArithmeticError */
    /* [12] OSError            */ 1,
    /* [13] NotImplementedError*/ 8,  /* RuntimeError */
    /* [14] MemoryError        */ 1,
    /* [15] AssertionError     */ 1,
    /* [16] GeneratorExit      */ 0,  /* BaseException (NOT Exception) */
    /* [17] ExceptionGroup     */ 1,
    /* [18] LookupError        */ 1,
    /* [19] ArithmeticError    */ 1,
    /* [20] SystemExit         */ 0,  /* BaseException (NOT Exception) */
    /* [21] KeyboardInterrupt  */ 0,  /* BaseException (NOT Exception) */
    /* [22] ImportError        */ 1,
    /* [23] ModuleNotFoundError*/ 22, /* ImportError */
    /* [24] SyntaxError        */ 1,
    /* [25] IndentationError   */ 24, /* SyntaxError */
    /* [26] UnboundLocalError  */ 7,  /* NameError */
    /* [27] FloatingPointError */ 19, /* ArithmeticError */
    /* [28] FileNotFoundError  */ 12, /* OSError */
    /* [29] PermissionError    */ 12, /* OSError */
    /* [30] UnicodeError       */ 3,  /* ValueError */
    /* [31] UnicodeDecodeError */ 30, /* UnicodeError */
    /* [32] UnicodeEncodeError */ 30, /* UnicodeError */
    /* [33] BufferError        */ 1,
    /* [34] EOFError           */ 1,
    /* [35] RecursionError     */ 8,  /* RuntimeError */
    /* [36] StopAsyncIteration */ 1,
    /* [37] TimeoutError       */ 12  /* OSError */
};

/*
 * Create an exception object from type code and message.
 */
static PyDosObj far *make_exc_obj(int type_code, const char far *message)
{
    PyDosObj far *exc;
    PyDosObj far *msg_obj;
    unsigned int mlen;

    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    if (exc == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    exc->refcount = 1;
    exc->v.exc.type_code = type_code;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.value = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    if (message != (const char far *)0) {
        mlen = _fstrlen(message);
        msg_obj = pydos_obj_new_str(message, mlen);
    } else {
        msg_obj = pydos_obj_new_str((const char far *)"", 0);
    }
    if (msg_obj == (PyDosObj far *)0) {
        PYDOS_DECREF(exc);
        return (PyDosObj far *)0;
    }
    exc->v.exc.message = msg_obj;

    return exc;
}

void PYDOS_API pydos_exc_raise(int type_code, const char far *message)
{
    PyDosObj far *exc;

    exc = make_exc_obj(type_code, message);
    if (exc == (PyDosObj far *)0)
        exc = emergency_memory_error();

    /* Store as current exception */
    if (current_exc != (PyDosObj far *)0) {
        PYDOS_DECREF(current_exc);
    }
    current_exc = exc;
}

void PYDOS_API pydos_exc_raise_stop_iteration(PyDosObj far *value)
{
    PyDosObj far *exc;

    exc = make_exc_obj(PYDOS_EXC_STOP_ITERATION,
                       (const char far *)"");
    if (exc == (PyDosObj far *)0) {
        exc = emergency_memory_error();
    }
    if (exc != (PyDosObj far *)&emergency_memory_exc &&
        value != (PyDosObj far *)0) {
        PYDOS_INCREF(value);
        exc->v.exc.value = value;
    }
    if (current_exc != (PyDosObj far *)0)
        PYDOS_DECREF(current_exc);
    current_exc = exc;
}

void PYDOS_API pydos_exc_raise_obj(PyDosObj far *exc)
{
    /* Store as current exception.
     * INCREF new before DECREF old to avoid use-after-free when
     * re-raising the same exception object (current_exc == exc). */
    if (exc == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"No active exception to reraise");
        return;
    }
    PYDOS_INCREF(exc);
    if (current_exc != (PyDosObj far *)0) {
        PYDOS_DECREF(current_exc);
    }
    current_exc = exc;
}

PyDosObj far * PYDOS_API pydos_exc_fetch(void)
{
    if (current_exc != (PyDosObj far *)0) PYDOS_INCREF(current_exc);
    return current_exc;
}

void PYDOS_API pydos_exc_add_traceback(const char far *function_name,
                                        unsigned int line)
{
    PyDosObj far *traceback;

    if (current_exc == (PyDosObj far *)0 ||
        (PyDosType)current_exc->type != PYDT_EXCEPTION)
        return;

    traceback = current_exc->v.exc.traceback;
    if (traceback != (PyDosObj far *)0 &&
        (PyDosType)traceback->type == PYDT_TRACEBACK &&
        traceback->v.traceback.line == line &&
        traceback->v.traceback.function_name != (const char far *)0 &&
        function_name != (const char far *)0 &&
        _fstrcmp(traceback->v.traceback.function_name, function_name) == 0) {
        return;
    }

    traceback = pydos_obj_alloc_type(PYDT_TRACEBACK);
    if (traceback == (PyDosObj far *)0) return;
    traceback->v.traceback.next = current_exc->v.exc.traceback;
    if (traceback->v.traceback.next != (PyDosObj far *)0)
        PYDOS_INCREF(traceback->v.traceback.next);
    traceback->v.traceback.function_name = function_name;
    traceback->v.traceback.line = line;

    if (current_exc->v.exc.traceback != (PyDosObj far *)0)
        PYDOS_DECREF(current_exc->v.exc.traceback);
    current_exc->v.exc.traceback = traceback;
}

int PYDOS_API pydos_exc_check_traceback(const char far *function_name)
{
    if (!pydos_exc_pending()) return 0;
    pydos_exc_add_traceback(function_name, 0U);
    return 1;
}

void PYDOS_API pydos_exc_require_traceback(const char far *function_name,
                                            unsigned int line)
{
    if (!pydos_exc_pending())
        pydos_exc_raise(PYDOS_EXC_RUNTIME_ERROR,
                        (const char far *)"null result without exception");
    pydos_exc_add_traceback(function_name, line);
}

PyDosObj far * PYDOS_API pydos_exc_get_traceback(PyDosObj far *exc)
{
    PyDosObj far *traceback;

    if (exc == (PyDosObj far *)0 ||
        (PyDosType)exc->type != PYDT_EXCEPTION)
        return pydos_obj_new_none();
    traceback = exc->v.exc.traceback;
    if (traceback == (PyDosObj far *)0)
        return pydos_obj_new_none();
    PYDOS_INCREF(traceback);
    return traceback;
}

int PYDOS_API pydos_exc_pending(void)
{
    return current_exc != (PyDosObj far *)0;
}

void PYDOS_API pydos_exc_panic_current(void)
{
    PyDosObj far *exc = current_exc;
    if (exc != (PyDosObj far *)0 && (PyDosType)exc->type == PYDT_EXCEPTION) {
        char buf[80];
        const char *type_name;
        const char far *function_name;
        PyDosObj far *traceback;
        unsigned int tlen, total;
        int tc = exc->v.exc.type_code;

        if (tc >= 0 && tc < NUM_EXC_TYPES) {
            type_name = exc_type_names[tc];
        } else {
            type_name = "Exception";
        }
        tlen = (unsigned int)strlen(type_name);
        total = 0;

        if (tlen < 78) {
            memcpy(buf, type_name, tlen);
            total = tlen;
        }

        if (exc->v.exc.message != (PyDosObj far *)0 &&
            (PyDosType)exc->v.exc.message->type == PYDT_STR &&
            exc->v.exc.message->v.str.len > 0) {
            unsigned int mlen = exc->v.exc.message->v.str.len;
            if (total + 2 + mlen < 79) {
                buf[total++] = ':';
                buf[total++] = ' ';
                _fmemcpy((char far *)(buf + total),
                         exc->v.exc.message->v.str.data, mlen);
                total += mlen;
            }
        }

        /* Preserve the most useful traceback detail in the compact fatal
         * diagnostic.  Full traceback objects remain available to Python;
         * this only makes uncaught initialization failures debuggable on a
         * DOS console. */
        traceback = exc->v.exc.traceback;
        if (traceback != (PyDosObj far *)0 &&
            (PyDosType)traceback->type == PYDT_TRACEBACK) {
            while (traceback->v.traceback.next != (PyDosObj far *)0 &&
                   (PyDosType)traceback->v.traceback.next->type ==
                       PYDT_TRACEBACK) {
                traceback = traceback->v.traceback.next;
            }
            function_name = traceback->v.traceback.function_name;
            if (function_name != (const char far *)0) {
                unsigned int flen = _fstrlen(function_name);
                if (total + 4 + flen < 79) {
                    buf[total++] = ' ';
                    buf[total++] = 'a';
                    buf[total++] = 't';
                    buf[total++] = ' ';
                    _fmemcpy((char far *)(buf + total), function_name, flen);
                    total += flen;
                    if (traceback->v.traceback.line > 0U) {
                        char line_text[12];
                        unsigned int line_len;
                        line_text[0] = ':';
                        ltoa((long)traceback->v.traceback.line,
                             line_text + 1, 10);
                        line_len = (unsigned int)strlen(line_text);
                        if (total + line_len < 79) {
                            memcpy(buf + total, line_text, line_len);
                            total += line_len;
                        }
                    }
                }
            }
        }
        buf[total] = '\0';
        pydos_exc_panic(buf);
    } else if (exc != (PyDosObj far *)0 &&
               (PyDosType)exc->type == PYDT_EXC_GROUP) {
        pydos_exc_panic("ExceptionGroup");
    } else {
        pydos_exc_panic("Unhandled exception");
    }
}

PyDosObj far * PYDOS_API pydos_exc_current(void)
{
    return current_exc;
}

void PYDOS_API pydos_exc_clear(void)
{
    if (current_exc != (PyDosObj far *)0) {
        PYDOS_DECREF(current_exc);
        current_exc = (PyDosObj far *)0;
    }
}

void PYDOS_API pydos_exc_panic(const char *message)
{
    /* Write "Unhandled exception: " + message to stderr (handle 2) */
    static const char prefix[] = "Unhandled exception: ";
    union REGS inregs, outregs;
    struct SREGS sregs;
    unsigned int len;

    /* Write prefix to stderr */
    segread(&sregs);
    inregs.h.ah = 0x40;
#ifdef PYDOS_32BIT
    inregs.x.ebx = 2; /* stderr */
    inregs.x.ecx = (unsigned int)(sizeof(prefix) - 1);
    inregs.x.edx = (unsigned int)prefix;
    int386x(0x21, &inregs, &outregs, &sregs);
#else
    inregs.x.bx = 2; /* stderr */
    inregs.x.cx = (unsigned int)(sizeof(prefix) - 1);
    inregs.x.dx = FP_OFF(prefix);
    int86x(0x21, &inregs, &outregs, &sregs);
#endif

    /* Write message to stderr */
    if (message != (const char *)0) {
        len = (unsigned int)strlen(message);
        segread(&sregs);
        inregs.h.ah = 0x40;
#ifdef PYDOS_32BIT
        inregs.x.ebx = 2;
        inregs.x.ecx = len;
        inregs.x.edx = (unsigned int)message;
        int386x(0x21, &inregs, &outregs, &sregs);
#else
        inregs.x.bx = 2;
        inregs.x.cx = len;
        inregs.x.dx = FP_OFF(message);
        int86x(0x21, &inregs, &outregs, &sregs);
#endif
    }

    /* Write newline to stderr */
    {
        static const char nl[] = "\r\n";
        segread(&sregs);
        inregs.h.ah = 0x40;
#ifdef PYDOS_32BIT
        inregs.x.ebx = 2;
        inregs.x.ecx = 2;
        inregs.x.edx = (unsigned int)nl;
        int386x(0x21, &inregs, &outregs, &sregs);
#else
        inregs.x.bx = 2;
        inregs.x.cx = 2;
        inregs.x.dx = FP_OFF(nl);
        int86x(0x21, &inregs, &outregs, &sregs);
#endif
    }

    exit(1);
}

/* --------------------------------------------------------------- */
/* Exception constructor builtins: ValueError(msg), TypeError(msg)  */
/* These follow the builtin convention: (int argc, PyDosObj far * far *argv) */
/* --------------------------------------------------------------- */

static PyDosObj far * exc_new_helper(int type_code, int argc,
                                      PyDosObj far * far *argv)
{
    PyDosObj far *exc;
    exc = pydos_obj_alloc_type(PYDT_EXCEPTION);
    if (exc == (PyDosObj far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate exception");
        return (PyDosObj far *)0;
    }
    exc->v.exc.type_code = type_code;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.value = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;
    if (argc > 0 && argv[0] != (PyDosObj far *)0) {
        exc->v.exc.message = argv[0];
        PYDOS_INCREF(argv[0]);
        if (type_code == PYDOS_EXC_STOP_ITERATION) {
            exc->v.exc.value = argv[0];
            PYDOS_INCREF(argv[0]);
        }
    }
    return exc;
}

PyDosObj far * PYDOS_API pydos_exc_new_baseexception(int argc,
                                                      PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_BASE, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_exception(int argc,
                                                  PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_EXCEPTION, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_valueerror(int argc,
                                                   PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_VALUE_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_typeerror(int argc,
                                                  PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_TYPE_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_runtimeerror(int argc,
                                                     PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_RUNTIME_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_indexerror(int argc,
                                                   PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_INDEX_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_keyerror(int argc,
                                                 PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_KEY_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_stopiteration(int argc,
                                                      PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_STOP_ITERATION, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_assertionerror(int argc,
                                                       PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_ASSERTION_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_attributeerror(int argc,
                                                       PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_ATTRIBUTE_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_nameerror(int argc,
                                                  PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_NAME_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_zerodivisionerror(int argc,
                                                          PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_ZERO_DIVISION, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_overflowerror(int argc,
                                                      PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_OVERFLOW, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_oserror(int argc,
                                                PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_OS_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_notimplementederror(int argc,
                                                            PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_NOT_IMPLEMENTED, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_memoryerror(int argc,
                                                    PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_MEMORY_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_generatorexit(int argc,
                                                      PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_GENERATOR_EXIT, argc, argv);
}

/* Phase 6A: 20 new exception constructors */

PyDosObj far * PYDOS_API pydos_exc_new_lookuperror(int argc,
                                                    PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_LOOKUP_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_arithmeticerror(int argc,
                                                        PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_ARITHMETIC_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_systemexit(int argc,
                                                    PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_SYSTEM_EXIT, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_keyboardinterrupt(int argc,
                                                          PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_KEYBOARD_INTERRUPT, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_importerror(int argc,
                                                    PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_IMPORT_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_modulenotfounderror(int argc,
                                                            PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_MODULE_NOT_FOUND, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_syntaxerror(int argc,
                                                    PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_SYNTAX_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_indentationerror(int argc,
                                                         PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_INDENTATION_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_unboundlocalerror(int argc,
                                                          PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_UNBOUND_LOCAL, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_floatingpointerror(int argc,
                                                           PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_FLOATING_POINT, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_filenotfounderror(int argc,
                                                          PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_FILE_NOT_FOUND, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_permissionerror(int argc,
                                                        PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_PERMISSION_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_unicodeerror(int argc,
                                                     PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_UNICODE_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_unicodedecodeerror(int argc,
                                                           PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_UNICODE_DECODE, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_unicodeencodeerror(int argc,
                                                           PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_UNICODE_ENCODE, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_buffererror(int argc,
                                                    PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_BUFFER_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_eoferror(int argc,
                                                 PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_EOF_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_recursionerror(int argc,
                                                       PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_RECURSION_ERROR, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_stopasynciteration(int argc,
                                                           PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_STOP_ASYNC_ITER, argc, argv);
}

PyDosObj far * PYDOS_API pydos_exc_new_timeouterror(int argc,
                                                     PyDosObj far * far *argv)
{
    return exc_new_helper(PYDOS_EXC_TIMEOUT_ERROR, argc, argv);
}

/* --------------------------------------------------------------- */
/* pydos_exc_matches — check if exception matches a type code       */
/* Uses exc_parent[] table for parent-chain walk.                   */
/* --------------------------------------------------------------- */
int PYDOS_API pydos_exc_matches(PyDosObj far *exc, int type_code)
{
    int exc_code;
    int walk;

    if (exc == (PyDosObj far *)0) return 0;

    /* ExceptionGroup (PYDT_EXC_GROUP type tag): walk from code 17 */
    if ((PyDosType)exc->type == PYDT_EXC_GROUP) {
        walk = PYDOS_EXC_EXCEPTION_GROUP;
        while (walk >= 0 && walk < NUM_EXC_TYPES) {
            if (walk == type_code) return 1;
            if (walk == exc_parent[walk]) break;
            walk = exc_parent[walk];
        }
        return 0;
    }

    if ((PyDosType)exc->type != PYDT_EXCEPTION) return 0;

    exc_code = exc->v.exc.type_code;

    /* User exceptions (>= USER_BASE): walk RuntimeError chain */
    if (exc_code >= PYDOS_EXC_USER_BASE) {
        walk = PYDOS_EXC_RUNTIME_ERROR;
        while (walk >= 0 && walk < NUM_EXC_TYPES) {
            if (walk == type_code) return 1;
            if (walk == exc_parent[walk]) break;
            walk = exc_parent[walk];
        }
        return 0;
    }

    /* Walk exc_code up parent chain, check if type_code appears */
    walk = exc_code;
    while (walk >= 0 && walk < NUM_EXC_TYPES) {
        if (walk == type_code) return 1;
        if (walk == exc_parent[walk]) break;
        walk = exc_parent[walk];
    }

    return 0;
}

/* --------------------------------------------------------------- */
/* pydos_exc_type_name — return name for a given type code          */
/* --------------------------------------------------------------- */
const char far * PYDOS_API pydos_exc_type_name(int type_code)
{
    if (type_code >= 0 && type_code < NUM_EXC_TYPES) {
        return (const char far *)exc_type_names[type_code];
    }
    return (const char far *)"Exception";
}

/* --------------------------------------------------------------- */

void PYDOS_API pydos_exc_init(void)
{
    (void)emergency_memory_error();
    current_exc = (PyDosObj far *)0;
}

void PYDOS_API pydos_exc_shutdown(void)
{
    /* Clear current exception */
    pydos_exc_clear();
}
