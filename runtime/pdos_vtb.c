/*
 * pydos_vtable.c - VTable mechanism for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * Provides method dispatch tables for user-defined classes.
 * 56-slot indexed array for O(1) dunder dispatch.
 * Sorted slot name table with binary search for set_special.
 */

#include "pdos_vtb.h"
#include "pdos_exc.h"
#include "pdos_obj.h"
#include "pdos_cod.h"
#include <string.h>

#include "pdos_mem.h"

/* Built-in type vtables, indexed by PyDosType */
PyDosVTable far * PYDOS_API pydos_builtin_vtables[PYDT_MAX];

void PYDOS_API pydos_builtin_vtable_add_method(
    unsigned int type_tag,
    const char far *name,
    void (far *func)(void))
{
    PyDosVTable far *vtable;
    unsigned int hash;
    unsigned char i;

    if (name == (const char far *)0 || func == (void (far *)(void))0 ||
        type_tag >= PYDT_MAX ||
        pydos_builtin_vtables[type_tag] == (PyDosVTable far *)0) {
        return;
    }
    vtable = pydos_builtin_vtables[type_tag];

    /* Multiple compiled modules may link the same stdlib method. Replace an
     * existing registration instead of consuming another bounded slot. */
    hash = 5381U;
    {
        const char far *p = name;
        while (*p != '\0') {
            hash = ((hash << 5) + hash) + (unsigned char)*p;
            p++;
        }
    }
    for (i = 0; i < vtable->method_count; i++) {
        if (vtable->methods[i].name_hash == hash) {
            const char far *a = vtable->methods[i].name;
            const char far *b = name;
            while (*a != '\0' && *a == *b) {
                a++;
                b++;
            }
            if (*a == *b) {
                PyDosCodeRef far *reference;
                reference = pydos_code_ref_new_native(
                    func, PYDOS_CODE_NATIVE);
                if (reference == (PyDosCodeRef far *)0) return;
                vtable->methods[i].name = name;
                pydos_code_ref_release(vtable->methods[i].code_ref);
                vtable->methods[i].code_ref = reference;
                pydos_vtable_set_special(vtable, name, func);
                return;
            }
        }
    }

    pydos_vtable_add_method(vtable, name, func);
}

/*
 * djb2_hash_cstr_far - DJB2 hash for a far null-terminated string.
 */
static unsigned int djb2_hash_cstr_far(const char far *s)
{
    unsigned int hash;

    hash = 5381;
    while (*s) {
        hash = ((hash << 5) + hash) + (unsigned char)*s;
        s++;
    }
    return hash;
}

/* ------------------------------------------------------------------ */
/* Sorted slot name table for binary search dispatch                  */
/* MUST be kept sorted by name string (strcmp order).                  */
/* ------------------------------------------------------------------ */

typedef struct SlotMapEntry {
    const char *name;
    VSlotIndex  index;
} SlotMapEntry;

/* Sorted alphabetically by dunder name */
static const SlotMapEntry slot_map[] = {
    { "__abs__",       VSLOT_ABS },
    { "__add__",       VSLOT_ADD },
    { "__and__",       VSLOT_AND },
    { "__bool__",      VSLOT_BOOL },
    { "__bytes__",     VSLOT_BYTES },
    { "__call__",      VSLOT_CALL },
    { "__contains__",  VSLOT_CONTAINS },
    { "__del__",       VSLOT_DEL },
    { "__delattr__",   VSLOT_DELATTR },
    { "__delete__",    VSLOT_DELETE },
    { "__delitem__",   VSLOT_DELITEM },
    { "__enter__",     VSLOT_ENTER },
    { "__eq__",        VSLOT_EQ },
    { "__exit__",      VSLOT_EXIT },
    { "__float__",     VSLOT_FLOAT },
    { "__floordiv__",  VSLOT_FLOORDIV },
    { "__format__",    VSLOT_FORMAT },
    { "__ge__",        VSLOT_GE },
    { "__get__",       VSLOT_GET },
    { "__getattr__",   VSLOT_GETATTR },
    { "__getattribute__", VSLOT_GETATTRIBUTE },
    { "__getitem__",   VSLOT_GETITEM },
    { "__gt__",        VSLOT_GT },
    { "__hash__",      VSLOT_HASH },
    { "__iadd__",      VSLOT_IADD },
    { "__iand__",      VSLOT_IAND },
    { "__ifloordiv__", VSLOT_IFLOORDIV },
    { "__ilshift__",   VSLOT_ILSHIFT },
    { "__imatmul__",   VSLOT_IMATMUL },
    { "__imod__",      VSLOT_IMOD },
    { "__imul__",      VSLOT_IMUL },
    { "__index__",     VSLOT_INDEX },
    { "__init__",      VSLOT_INIT },
    { "__int__",       VSLOT_INT },
    { "__invert__",    VSLOT_INVERT },
    { "__ior__",       VSLOT_IOR },
    { "__ipow__",      VSLOT_IPOW },
    { "__irshift__",   VSLOT_IRSHIFT },
    { "__isub__",      VSLOT_ISUB },
    { "__iter__",      VSLOT_ITER },
    { "__itruediv__",  VSLOT_ITRUEDIV },
    { "__ixor__",      VSLOT_IXOR },
    { "__le__",        VSLOT_LE },
    { "__len__",       VSLOT_LEN },
    { "__lshift__",    VSLOT_LSHIFT },
    { "__lt__",        VSLOT_LT },
    { "__matmul__",    VSLOT_MATMUL },
    { "__mod__",       VSLOT_MOD },
    { "__mul__",       VSLOT_MUL },
    { "__ne__",        VSLOT_NE },
    { "__neg__",       VSLOT_NEG },
    { "__new__",       VSLOT_NEW },
    { "__next__",      VSLOT_NEXT },
    { "__or__",        VSLOT_OR },
    { "__pos__",       VSLOT_POS },
    { "__pow__",       VSLOT_POW },
    { "__radd__",      VSLOT_RADD },
    { "__rand__",      VSLOT_RAND },
    { "__repr__",      VSLOT_REPR },
    { "__reversed__",  VSLOT_REVERSED },
    { "__rfloordiv__", VSLOT_RFLOORDIV },
    { "__rlshift__",   VSLOT_RLSHIFT },
    { "__rmatmul__",   VSLOT_RMATMUL },
    { "__rmod__",      VSLOT_RMOD },
    { "__rmul__",      VSLOT_RMUL },
    { "__ror__",       VSLOT_ROR },
    { "__rpow__",      VSLOT_RPOW },
    { "__rrshift__",   VSLOT_RRSHIFT },
    { "__rshift__",    VSLOT_RSHIFT },
    { "__rsub__",      VSLOT_RSUB },
    { "__rtruediv__",  VSLOT_RTRUEDIV },
    { "__rxor__",      VSLOT_RXOR },
    { "__set__",       VSLOT_SET },
    { "__setattr__",   VSLOT_SETATTR },
    { "__setitem__",   VSLOT_SETITEM },
    { "__str__",       VSLOT_STR },
    { "__sub__",       VSLOT_SUB },
    { "__truediv__",   VSLOT_TRUEDIV },
    { "__xor__",       VSLOT_XOR }
};

#define SLOT_MAP_SIZE  (sizeof(slot_map) / sizeof(slot_map[0]))

/*
 * find_slot_index - Binary search for a dunder name in the sorted table.
 * Returns the VSlotIndex, or -1 if not a known dunder.
 */
static int find_slot_index(const char far *name)
{
    int lo, hi, mid, cmp;
    const char *mp;
    const char far *np;

    lo = 0;
    hi = (int)SLOT_MAP_SIZE - 1;

    while (lo <= hi) {
        mid = (lo + hi) / 2;
        /* Compare far name against near table entry */
        mp = slot_map[mid].name;
        np = name;
        cmp = 0;
        while (*mp && *np) {
            if ((unsigned char)*mp < (unsigned char)*np) {
                cmp = -1;
                break;
            }
            if ((unsigned char)*mp > (unsigned char)*np) {
                cmp = 1;
                break;
            }
            mp++;
            np++;
        }
        if (cmp == 0) {
            if (*mp == '\0' && *np == '\0') {
                return (int)slot_map[mid].index;
            }
            if (*mp == '\0') {
                cmp = -1;
            } else {
                cmp = 1;
            }
        }
        if (cmp < 0) {
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return -1;
}

static int reserve_methods(PyDosVTable far *vtable,
                           unsigned int required)
{
    PyDosMethodSlot far *methods;
    unsigned int capacity;

    if (required <= vtable->method_capacity) return 1;
    if (required > PYDOS_VTABLE_MAX_METHODS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"class exceeds vtable method limit");
        return 0;
    }

    capacity = vtable->method_capacity;
    if (capacity == 0) capacity = 4;
    while (capacity < required && capacity < PYDOS_VTABLE_MAX_METHODS)
        capacity *= 2;
    if (capacity > PYDOS_VTABLE_MAX_METHODS)
        capacity = PYDOS_VTABLE_MAX_METHODS;

    if (vtable->methods == (PyDosMethodSlot far *)0) {
        methods = (PyDosMethodSlot far *)pydos_mem_alloc(
            PYDOS_MEM_METADATA,
            (unsigned long)capacity * sizeof(PyDosMethodSlot));
    } else {
        methods = (PyDosMethodSlot far *)pydos_mem_realloc(
            vtable->methods,
            (unsigned long)capacity * sizeof(PyDosMethodSlot));
    }
    if (methods == (PyDosMethodSlot far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate vtable methods");
        return 0;
    }
    vtable->methods = methods;
    vtable->method_capacity = (unsigned char)capacity;
    return 1;
}

static int reserve_mro(PyDosVTable far *vtable, unsigned int required)
{
    PyDosVTable far * far *mro;
    unsigned int capacity;

    if (required <= vtable->mro_capacity) return 1;
    if (required > PYDOS_VTABLE_MAX_MRO) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"class MRO exceeds DOS limit");
        return 0;
    }

    capacity = vtable->mro_capacity;
    if (capacity == 0) capacity = 4;
    while (capacity < required && capacity < PYDOS_VTABLE_MAX_MRO)
        capacity *= 2;
    if (capacity > PYDOS_VTABLE_MAX_MRO)
        capacity = PYDOS_VTABLE_MAX_MRO;

    if (vtable->mro == (PyDosVTable far * far *)0) {
        mro = (PyDosVTable far * far *)pydos_mem_alloc(
            PYDOS_MEM_METADATA,
            (unsigned long)capacity * sizeof(PyDosVTable far *));
    } else {
        mro = (PyDosVTable far * far *)pydos_mem_realloc(
            vtable->mro,
            (unsigned long)capacity * sizeof(PyDosVTable far *));
    }
    if (mro == (PyDosVTable far * far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate vtable MRO");
        return 0;
    }
    vtable->mro = mro;
    vtable->mro_capacity = (unsigned char)capacity;
    return 1;
}

PyDosVTable far * PYDOS_API pydos_vtable_create_sized(
                                      unsigned int method_capacity)
{
    PyDosVTable far *vt;

    if (method_capacity > PYDOS_VTABLE_MAX_METHODS) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"class exceeds vtable method limit");
        return (PyDosVTable far *)0;
    }

    vt = (PyDosVTable far *)pydos_mem_alloc(
        PYDOS_MEM_METADATA, (unsigned long)sizeof(PyDosVTable));
    if (vt == (PyDosVTable far *)0) {
        pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                        (const char far *)"cannot allocate class vtable");
        return (PyDosVTable far *)0;
    }

    /* Auxiliary arrays remain absent until they are actually needed. */
    _fmemset(vt, 0, sizeof(PyDosVTable));

    if (method_capacity > 0) {
        vt->methods = (PyDosMethodSlot far *)pydos_mem_alloc(
            PYDOS_MEM_METADATA,
            (unsigned long)method_capacity * sizeof(PyDosMethodSlot));
        if (vt->methods == (PyDosMethodSlot far *)0) {
            pydos_far_free(vt);
            pydos_exc_raise(PYDOS_EXC_MEMORY_ERROR,
                            (const char far *)"cannot allocate vtable methods");
            return (PyDosVTable far *)0;
        }
        vt->method_capacity = (unsigned char)method_capacity;
    }

    return vt;
}

PyDosVTable far * PYDOS_API pydos_vtable_create(void)
{
    return pydos_vtable_create_sized(0);
}

void PYDOS_API pydos_vtable_destroy(PyDosVTable far *vtable)
{
    unsigned char i;
    if (vtable == (PyDosVTable far *)0) return;
    if (vtable->methods != (PyDosMethodSlot far *)0) {
        for (i = 0; i < vtable->method_count; i++)
            pydos_code_ref_release(vtable->methods[i].code_ref);
        pydos_far_free(vtable->methods);
    }
    if (vtable->mro != (PyDosVTable far * far *)0)
        pydos_far_free(vtable->mro);
    pydos_far_free(vtable);
}

int PYDOS_API pydos_vtable_add_mro(PyDosVTable far *vtable,
                                    PyDosVTable far *ancestor)
{
    if (vtable == (PyDosVTable far *)0 ||
        ancestor == (PyDosVTable far *)0)
        return 0;
    if (!reserve_mro(vtable, (unsigned int)vtable->mro_len + 1U))
        return 0;
    vtable->mro[vtable->mro_len++] = ancestor;
    return 1;
}

void PYDOS_API pydos_vtable_add_method(PyDosVTable far *vtable,
                             const char far *name,
                             void (far *func)(void))
{
    PyDosCodeRef far *reference;

    if (func == (void (far *)(void))0) return;
    reference = pydos_code_ref_new_native(func, PYDOS_CODE_NATIVE);
    if (reference == (PyDosCodeRef far *)0) return;
    pydos_vtable_add_code_ref(vtable, name, reference);
    pydos_code_ref_release(reference);
}

void PYDOS_API pydos_vtable_add_code_ref(PyDosVTable far *vtable,
                             const char far *name,
                             PyDosCodeRef far *code_ref)
{
    unsigned int hash;
    unsigned char idx;

    if (vtable == (PyDosVTable far *)0 ||
        name == (const char far *)0 ||
        code_ref == (PyDosCodeRef far *)0) {
        return;
    }

    if (!reserve_methods(vtable,
                         (unsigned int)vtable->method_count + 1U)) {
        return;
    }

    hash = djb2_hash_cstr_far(name);
    idx = vtable->method_count;

    vtable->methods[idx].name_hash = hash;
    vtable->methods[idx].name = name;
    vtable->methods[idx].code_ref = code_ref;
    pydos_code_ref_retain(code_ref);
    vtable->methods[idx].defaults = (PyDosObj far *)0;
    vtable->methods[idx].arg_count = 0;
    vtable->methods[idx].flags = 0;
    vtable->method_count++;

    /* Also set special slot if this is a dunder method */
    {
        void (far *native_entry)(void);
        native_entry = pydos_code_ref_native_entry(code_ref);
        if (native_entry != (void (far *)(void))0)
            pydos_vtable_set_special(vtable, name, native_entry);
    }
}

void PYDOS_API pydos_vtable_add_method_sig(PyDosVTable far *vtable,
                             const char far *name,
                             void (far *func)(void),
                             unsigned int arg_count)
{
    unsigned char before;

    if (vtable == (PyDosVTable far *)0) return;
    if (arg_count > 255U) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"method has too many arguments");
        return;
    }
    before = vtable->method_count;
    pydos_vtable_add_method(vtable, name, func);
    if (vtable->method_count > before) {
        PyDosMethodSlot far *slot;
        slot = &vtable->methods[vtable->method_count - 1];
        slot->arg_count = (unsigned char)arg_count;
        slot->flags |= PYDOS_METHOD_SIGNATURE_KNOWN;
    }
}

void PYDOS_API pydos_vtable_add_code_ref_sig(
                             PyDosVTable far *vtable,
                             const char far *name,
                             PyDosCodeRef far *code_ref,
                             unsigned int arg_count)
{
    unsigned char before;

    if (vtable == (PyDosVTable far *)0) return;
    if (arg_count > 255U) {
        pydos_exc_raise(PYDOS_EXC_TYPE_ERROR,
                        (const char far *)"method has too many arguments");
        return;
    }
    before = vtable->method_count;
    pydos_vtable_add_code_ref(vtable, name, code_ref);
    if (vtable->method_count > before) {
        PyDosMethodSlot far *slot;
        slot = &vtable->methods[vtable->method_count - 1];
        slot->arg_count = (unsigned char)arg_count;
        slot->flags |= PYDOS_METHOD_SIGNATURE_KNOWN;
    }
}

void (far * PYDOS_API pydos_vtable_lookup(PyDosVTable far *vtable,
                                unsigned int name_hash))(void)
{
    PyDosMethodSlot far *slot;

    slot = pydos_vtable_lookup_slot(vtable, name_hash);
    return slot != (PyDosMethodSlot far *)0
           ? pydos_code_ref_native_entry(slot->code_ref)
           : (void (far *)(void))0;
}

PyDosCodeRef far * PYDOS_API pydos_vtable_lookup_code_ref(
                                PyDosVTable far *vtable,
                                unsigned int name_hash)
{
    PyDosMethodSlot far *slot;
    slot = pydos_vtable_lookup_slot(vtable, name_hash);
    return slot != (PyDosMethodSlot far *)0
           ? slot->code_ref : (PyDosCodeRef far *)0;
}

PyDosMethodSlot far * PYDOS_API pydos_vtable_lookup_slot(
                                PyDosVTable far *vtable,
                                unsigned int name_hash)
{
    unsigned char i;
    unsigned char m;

    if (vtable == (PyDosVTable far *)0) {
        return (PyDosMethodSlot far *)0;
    }

    /* Search this vtable's methods */
    for (i = 0; i < vtable->method_count; i++) {
        if (vtable->methods[i].name_hash == name_hash) {
            return &vtable->methods[i];
        }
    }

    /* Search MRO chain */
    for (m = 0; m < vtable->mro_len; m++) {
        if (vtable->mro[m] != (PyDosVTable far *)0) {
            for (i = 0; i < vtable->mro[m]->method_count; i++) {
                if (vtable->mro[m]->methods[i].name_hash == name_hash) {
                    return &vtable->mro[m]->methods[i];
                }
            }
        }
    }

    return (PyDosMethodSlot far *)0;
}

static void (far *own_special(PyDosVTable far *vtable,
                              unsigned int slot))(void)
{
    unsigned char i;
    unsigned char encoded;

    if (vtable == (PyDosVTable far *)0 || slot >= VSLOT_COUNT)
        return (void (far *)(void))0;
    encoded = (unsigned char)(slot + 1U);
    for (i = 0; i < vtable->method_count; i++) {
        if ((vtable->methods[i].flags & PYDOS_METHOD_SPECIAL_MASK) == encoded)
            return pydos_code_ref_native_entry(vtable->methods[i].code_ref);
    }
    return (void (far *)(void))0;
}

void (far * PYDOS_API pydos_vtable_get_special(
                                PyDosVTable far *vtable,
                                unsigned int slot))(void)
{
    void (far *entry)(void);
    unsigned char i;

    entry = own_special(vtable, slot);
    if (entry != (void (far *)(void))0) return entry;
    if (vtable == (PyDosVTable far *)0) return (void (far *)(void))0;
    for (i = 0; i < vtable->mro_len; i++) {
        entry = own_special(vtable->mro[i], slot);
        if (entry != (void (far *)(void))0) return entry;
    }
    return (void (far *)(void))0;
}

int PYDOS_API pydos_vtable_owns_special(PyDosVTable far *vtable,
                                        unsigned int slot)
{
    return own_special(vtable, slot) != (void (far *)(void))0;
}

void PYDOS_API pydos_vtable_inherit(PyDosVTable far *child,
                          PyDosVTable far *parent)
{
    int i;

    if (child == (PyDosVTable far *)0 ||
        parent == (PyDosVTable far *)0) {
        return;
    }

    /* Add parent to child's MRO */
    if (!pydos_vtable_add_mro(child, parent)) return;

    /* Also inherit parent's MRO entries */
    for (i = 0; i < parent->mro_len; i++) {
        if (!pydos_vtable_add_mro(child, parent->mro[i])) return;
    }
}

/*
 * Set a special slot by dunder name.
 * Uses binary search on the sorted slot_map table.
 */
void PYDOS_API pydos_vtable_set_special(PyDosVTable far *vtable,
                              const char far *slot_name,
                              void (far *func)(void))
{
    int idx;

    if (vtable == (PyDosVTable far *)0 ||
        slot_name == (const char far *)0) {
        return;
    }

    idx = find_slot_index(slot_name);
    if (idx >= 0 && idx < VSLOT_COUNT) {
        unsigned char i;
        unsigned int hash = djb2_hash_cstr_far(slot_name);
        for (i = 0; i < vtable->method_count; i++) {
            if (vtable->methods[i].name_hash == hash) {
                vtable->methods[i].flags =
                    (unsigned char)((vtable->methods[i].flags &
                                     PYDOS_METHOD_SIGNATURE_KNOWN) |
                                    (unsigned char)(idx + 1));
                break;
            }
        }
    }
}

void PYDOS_API pydos_vtable_set_name(PyDosVTable far *vtable,
                            const char far *name)
{
    if (vtable == (PyDosVTable far *)0) {
        return;
    }
    vtable->class_name = name;
}

void PYDOS_API pydos_vtable_init(void)
{
    int i;

    /* Initialize all builtin vtable pointers to NULL */
    for (i = 0; i < PYDT_MAX; i++) {
        pydos_builtin_vtables[i] = (PyDosVTable far *)0;
    }

    /* Create basic vtables for built-in types.
     * These are empty vtables; special methods are
     * dispatched directly by the compiler-generated code. */
    pydos_builtin_vtables[PYDT_NONE] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_BOOL] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_INT] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_FLOAT] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_STR] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_LIST] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_DICT] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_TUPLE] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_SET] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_BYTES] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_FROZENSET] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_BYTEARRAY] = pydos_vtable_create();
    pydos_builtin_vtables[PYDT_COMPLEX] = pydos_vtable_create();
}

void PYDOS_API pydos_vtable_shutdown(void)
{
    int i;

    for (i = 0; i < PYDT_MAX; i++) {
        if (pydos_builtin_vtables[i] != (PyDosVTable far *)0) {
            pydos_vtable_destroy(pydos_builtin_vtables[i]);
            pydos_builtin_vtables[i] = (PyDosVTable far *)0;
        }
    }
}
