/*
 * pydos_exc.h - Exception handling for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_EXC_H
#define PDOS_EXC_H

#include <setjmp.h>
#include "pdos_obj.h"

/* Exception type codes */
typedef enum {
    PYDOS_EXC_BASE              = 0,
    PYDOS_EXC_EXCEPTION         = 1,
    PYDOS_EXC_TYPE_ERROR        = 2,
    PYDOS_EXC_VALUE_ERROR       = 3,
    PYDOS_EXC_KEY_ERROR         = 4,
    PYDOS_EXC_INDEX_ERROR       = 5,
    PYDOS_EXC_ATTRIBUTE_ERROR   = 6,
    PYDOS_EXC_NAME_ERROR        = 7,
    PYDOS_EXC_RUNTIME_ERROR     = 8,
    PYDOS_EXC_STOP_ITERATION    = 9,
    PYDOS_EXC_ZERO_DIVISION     = 10,
    PYDOS_EXC_OVERFLOW          = 11,
    PYDOS_EXC_OS_ERROR          = 12,
    PYDOS_EXC_NOT_IMPLEMENTED   = 13,
    PYDOS_EXC_MEMORY_ERROR      = 14,
    PYDOS_EXC_ASSERTION_ERROR   = 15,
    PYDOS_EXC_GENERATOR_EXIT    = 16,
    PYDOS_EXC_EXCEPTION_GROUP   = 17,
    /* Phase 6A: new exception types with parent-chain hierarchy */
    PYDOS_EXC_LOOKUP_ERROR      = 18,
    PYDOS_EXC_ARITHMETIC_ERROR  = 19,
    PYDOS_EXC_SYSTEM_EXIT       = 20,
    PYDOS_EXC_KEYBOARD_INTERRUPT = 21,
    PYDOS_EXC_IMPORT_ERROR      = 22,
    PYDOS_EXC_MODULE_NOT_FOUND  = 23,
    PYDOS_EXC_SYNTAX_ERROR      = 24,
    PYDOS_EXC_INDENTATION_ERROR = 25,
    PYDOS_EXC_UNBOUND_LOCAL     = 26,
    PYDOS_EXC_FLOATING_POINT    = 27,
    PYDOS_EXC_FILE_NOT_FOUND    = 28,
    PYDOS_EXC_PERMISSION_ERROR  = 29,
    PYDOS_EXC_UNICODE_ERROR     = 30,
    PYDOS_EXC_UNICODE_DECODE    = 31,
    PYDOS_EXC_UNICODE_ENCODE    = 32,
    PYDOS_EXC_BUFFER_ERROR      = 33,
    PYDOS_EXC_EOF_ERROR         = 34,
    PYDOS_EXC_RECURSION_ERROR   = 35,
    PYDOS_EXC_STOP_ASYNC_ITER   = 36,
    PYDOS_EXC_TIMEOUT_ERROR     = 37,
    PYDOS_EXC_USER_BASE         = 128
} PyDosExcType;

/* Exception frame - stack-allocated, linked list */
typedef struct ExcFrame {
    jmp_buf                 env;
    struct ExcFrame        *prev;
    int                     handled;
    PyDosObj far           *exc_value;
    void                  (*cleanup)(void);
} ExcFrame;

/* Push an exception frame onto the exception stack */
void PYDOS_API pydos_exc_push(ExcFrame *frame);

/* Allocate a pool frame, push it, and setjmp.
 * Returns 0 on normal entry, non-zero on exception (longjmp). */
int PYDOS_API pydos_try_enter(void);

/* Allocate a pool frame, push it, return far pointer to jmp_buf.
 * The caller must call setjmp on the returned jmp_buf directly
 * (calling setjmp from a wrapper that returns is undefined behavior). */
void far * PYDOS_API pydos_exc_alloc_frame(void);

/* Pop the top exception frame */
void PYDOS_API pydos_exc_pop(void);

/* Raise an exception by type code and message string */
void PYDOS_API pydos_exc_raise(int type_code, const char far *message);

/* Raise an existing exception object */
void PYDOS_API pydos_exc_raise_obj(PyDosObj far *exc);

/* Return current exception or NULL */
PyDosObj far * PYDOS_API pydos_exc_current(void);

/* Clear current exception */
void PYDOS_API pydos_exc_clear(void);

/* Unhandled exception: print message and exit */
void PYDOS_API pydos_exc_panic(const char *message);

/* Exception constructor builtins: create exception objects.
 * Follow builtin convention: (int argc, PyDosObj far * far *argv) */
PyDosObj far * PYDOS_API pydos_exc_new_baseexception(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_exception(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_valueerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_typeerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_runtimeerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_indexerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_keyerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_stopiteration(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_assertionerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_attributeerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_nameerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_zerodivisionerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_overflowerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_oserror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_notimplementederror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_memoryerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_generatorexit(int argc, PyDosObj far * far *argv);

/* Phase 6A: new exception constructors */
PyDosObj far * PYDOS_API pydos_exc_new_lookuperror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_arithmeticerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_systemexit(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_keyboardinterrupt(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_importerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_modulenotfounderror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_syntaxerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_indentationerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_unboundlocalerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_floatingpointerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_filenotfounderror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_permissionerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_unicodeerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_unicodedecodeerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_unicodeencodeerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_buffererror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_eoferror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_recursionerror(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_stopasynciteration(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_exc_new_timeouterror(int argc, PyDosObj far * far *argv);

/* Check if an exception matches a given type code (with inheritance).
 * Returns 1 if the exception's type_code matches or is a subtype. */
int PYDOS_API pydos_exc_matches(PyDosObj far *exc, int type_code);

/* Get the type name string for a given exception type code */
const char far * PYDOS_API pydos_exc_type_name(int type_code);

void PYDOS_API pydos_exc_init(void);
void PYDOS_API pydos_exc_shutdown(void);

#endif /* PDOS_EXC_H */
