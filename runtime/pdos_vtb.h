/*
 * pydos_vtable.h - VTable mechanism for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Provides indexed slot array for O(1) dunder dispatch,
 * plus compact, dynamically sized method and MRO tables.
 */

#ifndef PDOS_VTB_H
#define PDOS_VTB_H

#include "pdos_obj.h"

#define PYDOS_VTABLE_MAX_METHODS    64
#define PYDOS_VTABLE_MAX_MRO        32

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

    /* Attribute (4) */
    VSLOT_GETATTRIBUTE,
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
    struct PyDosCodeRef far *code_ref;
    PyDosObj far        *defaults;
    unsigned char       arg_count;
    unsigned char       flags;
} PyDosMethodSlot;

/* A method needs only one byte for both pieces of dispatch metadata.  The
 * low seven bits encode special-slot index + 1 (zero means ordinary method),
 * while the high bit records a statically known call signature. */
#define PYDOS_METHOD_SPECIAL_MASK       0x7FU
#define PYDOS_METHOD_SIGNATURE_KNOWN    0x80U
#define PYDOS_METHOD_HAS_SIGNATURE(slot) \
    (((slot)->flags & PYDOS_METHOD_SIGNATURE_KNOWN) != 0)

/* VTable structure */
typedef struct PyDosVTable {
    /* General method lookup table (hash-based).  The compiler reserves the
     * exact number of declared methods; runtime-created vtables grow on
     * demand.  This avoids paying for 64 entries in every DOS class. */
    PyDosMethodSlot far        *methods;
    unsigned char               method_count;
    unsigned char               method_capacity;

    /* Method Resolution Order chain, also allocated on demand. */
    struct PyDosVTable far * far *mro;
    unsigned char               mro_len;
    unsigned char               mro_capacity;

    /* Class name for repr fallback: "<__main__.ClassName object>" */
    const char far *class_name;
} PyDosVTable;

/* Allocate and zero a new vtable */
PyDosVTable far * PYDOS_API pydos_vtable_create(void);

/* Allocate a vtable and reserve exactly enough general-method entries for
 * a compiled class.  The hard DOS safety limit remains 64 methods. */
PyDosVTable far * PYDOS_API pydos_vtable_create_sized(
                                      unsigned int method_capacity);

/* Release a vtable and both compact auxiliary arrays. */
void PYDOS_API pydos_vtable_destroy(PyDosVTable far *vtable);

/* Append one vtable to the cached MRO, growing its compact array. */
int PYDOS_API pydos_vtable_add_mro(PyDosVTable far *vtable,
                                    PyDosVTable far *ancestor);

/* Add a method to the vtable. Computes name hash automatically.
 * If the method is a known dunder, also sets the corresponding slot. */
void PYDOS_API pydos_vtable_add_method(PyDosVTable far *vtable,
                             const char far *name,
                                       void (far *func)(void));
void PYDOS_API pydos_vtable_add_code_ref(
                                       PyDosVTable far *vtable,
                                       const char far *name,
                                       struct PyDosCodeRef far *code_ref);
void PYDOS_API pydos_vtable_add_code_ref_sig(
                                       PyDosVTable far *vtable,
                                       const char far *name,
                                       struct PyDosCodeRef far *code_ref,
                                       unsigned int arg_count);

/* Register a generated Python method with its fixed low-level ABI arity.
 * Defaults and variadic arguments are bound before this call boundary. */
void PYDOS_API pydos_vtable_add_method_sig(PyDosVTable far *vtable,
                             const char far *name,
                             void (far *func)(void),
                             unsigned int arg_count);

/* Look up a method by name hash, searching MRO chain */
void (far * PYDOS_API pydos_vtable_lookup(PyDosVTable far *vtable,
                                          unsigned int name_hash))(void);
struct PyDosCodeRef far * PYDOS_API pydos_vtable_lookup_code_ref(
                                          PyDosVTable far *vtable,
                                          unsigned int name_hash);

PyDosMethodSlot far * PYDOS_API pydos_vtable_lookup_slot(
                                PyDosVTable far *vtable,
                                unsigned int name_hash);

/* Look up one Python special method through the cached C3 MRO.  Special
 * indices live in the corresponding method entries instead of a fixed
 * VSLOT_COUNT-sized array in every class. */
void (far * PYDOS_API pydos_vtable_get_special(
                                PyDosVTable far *vtable,
                                unsigned int slot))(void);
int PYDOS_API pydos_vtable_owns_special(PyDosVTable far *vtable,
                                        unsigned int slot);

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

/* Register a generated Python-backed method on a primitive type. */
void PYDOS_API pydos_builtin_vtable_add_method(
    unsigned int type_tag,
    const char far *name,
    void (far *func)(void));

void PYDOS_API pydos_vtable_init(void);
void PYDOS_API pydos_vtable_shutdown(void);

#endif /* PDOS_VTB_H */
