/*
 * pydos_vtable.h - VTable mechanism for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Provides indexed slot array for O(1) dunder dispatch,
 * plus 64-method general lookup table with MRO chain.
 */

#ifndef PDOS_VTB_H
#define PDOS_VTB_H

#include "pdos_obj.h"

#define PYDOS_VTABLE_MAX_METHODS    64
#define PYDOS_VTABLE_MAX_MRO        8

/* ------------------------------------------------------------------ */
/* VSlotIndex: indexed dunder slot IDs for O(1) dispatch              */
/* ------------------------------------------------------------------ */

typedef enum VSlotIndex {
    /* Lifecycle (3) */
    VSLOT_INIT = 0,
    VSLOT_NEW,
    VSLOT_DEL,

    /* Representation (4) */
    VSLOT_STR,
    VSLOT_REPR,
    VSLOT_FORMAT,
    VSLOT_BYTES,

    /* Comparison (8) */
    VSLOT_EQ,
    VSLOT_NE,
    VSLOT_LT,
    VSLOT_LE,
    VSLOT_GT,
    VSLOT_GE,
    VSLOT_HASH,
    VSLOT_BOOL,

    /* Arithmetic (10) */
    VSLOT_ADD,
    VSLOT_SUB,
    VSLOT_MUL,
    VSLOT_FLOORDIV,
    VSLOT_TRUEDIV,
    VSLOT_MOD,
    VSLOT_POW,
    VSLOT_NEG,
    VSLOT_POS,
    VSLOT_ABS,

    /* Matmul (1) */
    VSLOT_MATMUL,

    /* Reflected arithmetic (8) */
    VSLOT_RADD,
    VSLOT_RSUB,
    VSLOT_RMUL,
    VSLOT_RFLOORDIV,
    VSLOT_RTRUEDIV,
    VSLOT_RMOD,
    VSLOT_RPOW,
    VSLOT_RMATMUL,

    /* In-place arithmetic (13) */
    VSLOT_IADD,
    VSLOT_ISUB,
    VSLOT_IMUL,
    VSLOT_IFLOORDIV,
    VSLOT_ITRUEDIV,
    VSLOT_IMOD,
    VSLOT_IPOW,
    VSLOT_IAND,
    VSLOT_IOR,
    VSLOT_IXOR,
    VSLOT_ILSHIFT,
    VSLOT_IRSHIFT,
    VSLOT_IMATMUL,

    /* Bitwise (6) */
    VSLOT_AND,
    VSLOT_OR,
    VSLOT_XOR,
    VSLOT_INVERT,
    VSLOT_LSHIFT,
    VSLOT_RSHIFT,

    /* Container (8) */
    VSLOT_LEN,
    VSLOT_GETITEM,
    VSLOT_SETITEM,
    VSLOT_DELITEM,
    VSLOT_CONTAINS,
    VSLOT_ITER,
    VSLOT_NEXT,
    VSLOT_REVERSED,

    /* Callable (1) */
    VSLOT_CALL,

    /* Context manager (2) */
    VSLOT_ENTER,
    VSLOT_EXIT,

    /* Conversion (3) */
    VSLOT_INT,
    VSLOT_FLOAT,
    VSLOT_INDEX,

    /* Attribute (3) */
    VSLOT_GETATTR,
    VSLOT_SETATTR,
    VSLOT_DELATTR,

    /* Descriptor (3) */
    VSLOT_GET,
    VSLOT_SET,
    VSLOT_DELETE,

    /* Sentinel — must be last */
    VSLOT_COUNT
} VSlotIndex;

/* Method slot entry (general lookup table) */
typedef struct PyDosMethodSlot {
    unsigned int        name_hash;
    const char far     *name;
    void              (far *func)(void);
} PyDosMethodSlot;

/* VTable structure */
typedef struct PyDosVTable {
    /* General method lookup table (hash-based) */
    PyDosMethodSlot             methods[PYDOS_VTABLE_MAX_METHODS];
    unsigned char               method_count;

    /* Method Resolution Order chain */
    struct PyDosVTable far     *mro[PYDOS_VTABLE_MAX_MRO];
    unsigned char               mro_len;

    /* Indexed slot array for O(1) dunder dispatch.
     * All slots are void(far*)(void) and must be cast to the
     * appropriate function pointer type before calling. */
    void (far *slots[VSLOT_COUNT])(void);

    /* Class name for repr fallback: "<__main__.ClassName object>" */
    const char far *class_name;
} PyDosVTable;

/* Allocate and zero a new vtable */
PyDosVTable far * PYDOS_API pydos_vtable_create(void);

/* Add a method to the vtable. Computes name hash automatically.
 * If the method is a known dunder, also sets the corresponding slot. */
void PYDOS_API pydos_vtable_add_method(PyDosVTable far *vtable,
                             const char far *name,
                             void (far *func)(void));

/* Look up a method by name hash, searching MRO chain */
void (far * PYDOS_API pydos_vtable_lookup(PyDosVTable far *vtable,
                                unsigned int name_hash))(void);

/* Inherit parent into child. Copies parent's slots where child has NULL.
 * Adds parent (and parent's MRO) to child's MRO chain. */
void PYDOS_API pydos_vtable_inherit(PyDosVTable far *child,
                          PyDosVTable far *parent);

/* Set a special slot by name string (e.g. "__init__", "__str__").
 * Uses sorted table + binary search for O(log n) dispatch. */
void PYDOS_API pydos_vtable_set_special(PyDosVTable far *vtable,
                              const char far *slot_name,
                              void (far *func)(void));

/* Set the class name for repr fallback.
 * The string pointer is stored directly (must point to static/DGROUP data). */
void PYDOS_API pydos_vtable_set_name(PyDosVTable far *vtable,
                            const char far *name);

/* Built-in type vtables, indexed by PyDosType */
extern PyDosVTable far * PYDOS_API pydos_builtin_vtables[PYDT_MAX];

void PYDOS_API pydos_vtable_init(void);
void PYDOS_API pydos_vtable_shutdown(void);

#endif /* PDOS_VTB_H */
