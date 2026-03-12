/*
 * pydos_exc.c - Exception handling for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Uses setjmp/longjmp for non-local jumps to exception handlers.
 * ExcFrame objects are stack-allocated and linked together.
 */

#include "pdos_exc.h"
#include "pdos_io.h"
#include "pdos_obj.h"
#include <dos.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

#include "pdos_mem.h"

/* Global exception stack: linked list of ExcFrame (stack-allocated, near ptrs) */
static ExcFrame *exc_stack = (ExcFrame *)0;

/* Static pool of ExcFrames for pydos_try_enter */
#define EXC_FRAME_POOL_SIZE 8
static ExcFrame exc_frame_pool[EXC_FRAME_POOL_SIZE];
static int exc_frame_next = 0;

/* Current exception value (if any) */
static PyDosObj far *current_exc = (PyDosObj far *)0;

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

void PYDOS_API pydos_exc_push(ExcFrame *frame)
{
    frame->prev = exc_stack;
    frame->handled = 0;
    frame->exc_value = (PyDosObj far *)0;
    frame->cleanup = (void (*)(void))0;
    exc_stack = frame;
}

void PYDOS_API pydos_exc_pop(void)
{
    ExcFrame *top;

    if (exc_stack != (ExcFrame *)0) {
        top = exc_stack;
        exc_stack = top->prev;

        /* Clean up exception value if frame had one */
        if (top->exc_value != (PyDosObj far *)0) {
            PYDOS_DECREF(top->exc_value);
            top->exc_value = (PyDosObj far *)0;
        }

        /* Run cleanup function if registered */
        if (top->cleanup != (void (*)(void))0) {
            top->cleanup();
        }

        /* Return pool frame if applicable */
        if (exc_frame_next > 0 &&
            top == &exc_frame_pool[exc_frame_next - 1]) {
            exc_frame_next--;
        }
    }
}

/*
 * Create an exception object from type code and message.
 */
static PyDosObj far *make_exc_obj(int type_code, const char far *message)
{
    PyDosObj far *exc;
    PyDosObj far *msg_obj;
    unsigned int mlen;

    exc = pydos_obj_alloc();
    if (exc == (PyDosObj far *)0) {
        return (PyDosObj far *)0;
    }

    exc->type = PYDT_EXCEPTION;
    exc->flags = 0;
    exc->refcount = 1;
    exc->v.exc.type_code = type_code;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;

    if (message != (const char far *)0) {
        mlen = _fstrlen(message);
        msg_obj = pydos_obj_new_str(message, mlen);
    } else {
        msg_obj = pydos_obj_new_str((const char far *)"", 0);
    }
    exc->v.exc.message = msg_obj;

    return exc;
}

int PYDOS_API pydos_try_enter(void)
{
    ExcFrame *frame;
    if (exc_frame_next >= EXC_FRAME_POOL_SIZE) {
        pydos_exc_panic("too many nested try blocks");
    }
    frame = &exc_frame_pool[exc_frame_next++];
    pydos_exc_push(frame);
    return setjmp(frame->env);
}

void far * PYDOS_API pydos_exc_alloc_frame(void)
{
    ExcFrame *frame;
    if (exc_frame_next >= EXC_FRAME_POOL_SIZE) {
        pydos_exc_panic("too many nested try blocks");
    }
    frame = &exc_frame_pool[exc_frame_next++];
    pydos_exc_push(frame);
    return (void far *)frame->env;
}

void PYDOS_API pydos_exc_raise(int type_code, const char far *message)
{
    PyDosObj far *exc;
    ExcFrame *frame;

    exc = make_exc_obj(type_code, message);

    /* Store as current exception */
    if (current_exc != (PyDosObj far *)0) {
        PYDOS_DECREF(current_exc);
    }
    current_exc = exc;

    /* If there's a frame on the stack, longjmp to it */
    frame = exc_stack;
    if (frame != (ExcFrame *)0) {
        frame->exc_value = exc;
        if (exc != (PyDosObj far *)0) {
            PYDOS_INCREF(exc);
        }
        longjmp(frame->env, 1);
    }

    /* No handler - panic */
    if (message != (const char far *)0) {
        /* Build a near-string panic message */
        char buf[80];
        const char *type_name;
        unsigned int tlen, mlen, total;

        if (type_code >= 0 && type_code < NUM_EXC_TYPES) {
            type_name = exc_type_names[type_code];
        } else {
            type_name = "Exception";
        }

        tlen = (unsigned int)strlen(type_name);
        mlen = _fstrlen(message);
        if (mlen > 60) mlen = 60;
        total = 0;

        if (tlen + 2 + mlen < 79) {
            memcpy(buf, type_name, tlen);
            total = tlen;
            buf[total] = ':';
            total++;
            buf[total] = ' ';
            total++;
            _fmemcpy((char far *)(buf + total), message, mlen);
            total += mlen;
            buf[total] = '\0';
        } else {
            memcpy(buf, type_name, tlen < 79 ? tlen : 79);
            buf[tlen < 79 ? tlen : 79] = '\0';
        }

        pydos_exc_panic(buf);
    } else {
        pydos_exc_panic("Unspecified exception");
    }
}

void PYDOS_API pydos_exc_raise_obj(PyDosObj far *exc)
{
    ExcFrame *frame;

    /* Store as current exception.
     * INCREF new before DECREF old to avoid use-after-free when
     * re-raising the same exception object (current_exc == exc). */
    if (exc != (PyDosObj far *)0) {
        PYDOS_INCREF(exc);
    }
    if (current_exc != (PyDosObj far *)0) {
        PYDOS_DECREF(current_exc);
    }
    current_exc = exc;

    /* If there's a frame on the stack, longjmp to it */
    frame = exc_stack;
    if (frame != (ExcFrame *)0) {
        frame->exc_value = exc;
        if (exc != (PyDosObj far *)0) {
            PYDOS_INCREF(exc);
        }
        longjmp(frame->env, 1);
    }

    /* No handler - format proper error message from exception object */
    if (exc != (PyDosObj far *)0 && (PyDosType)exc->type == PYDT_EXCEPTION) {
        char buf[80];
        const char *type_name;
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
        buf[total] = '\0';
        pydos_exc_panic(buf);
    } else if (exc != (PyDosObj far *)0 && (PyDosType)exc->type == PYDT_EXC_GROUP) {
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
    exc = pydos_obj_alloc();
    if (exc == (PyDosObj far *)0) {
        return pydos_obj_new_none();
    }
    exc->type = PYDT_EXCEPTION;
    exc->v.exc.type_code = type_code;
    exc->v.exc.message = (PyDosObj far *)0;
    exc->v.exc.traceback = (PyDosObj far *)0;
    exc->v.exc.cause = (PyDosObj far *)0;
    if (argc > 0 && argv[0] != (PyDosObj far *)0) {
        exc->v.exc.message = argv[0];
        PYDOS_INCREF(argv[0]);
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
    exc_stack = (ExcFrame *)0;
    current_exc = (PyDosObj far *)0;
    exc_frame_next = 0;
}

void PYDOS_API pydos_exc_shutdown(void)
{
    /* Pop all remaining exception frames */
    while (exc_stack != (ExcFrame *)0) {
        pydos_exc_pop();
    }

    /* Clear current exception */
    pydos_exc_clear();
}
