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
#include "pdos_obj.h"
#include <string.h>

#include "pdos_mem.h"

/* Built-in type vtables, indexed by PyDosType */
PyDosVTable far * PYDOS_API pydos_builtin_vtables[PYDT_MAX];

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

/*
 * fstrcmp_far - Compare two far null-terminated strings.
 * Returns 0 if equal, non-zero otherwise.
 */
static int fstrcmp_far(const char far *a, const char far *b)
{
    while (*a && *b) {
        if (*a != *b) {
            return 1;
        }
        a++;
        b++;
    }
    return (*a != *b) ? 1 : 0;
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
    { "__repr__",      VSLOT_REPR },
    { "__reversed__",  VSLOT_REVERSED },
    { "__rfloordiv__", VSLOT_RFLOORDIV },
    { "__rmatmul__",   VSLOT_RMATMUL },
    { "__rmod__",      VSLOT_RMOD },
    { "__rmul__",      VSLOT_RMUL },
    { "__rpow__",      VSLOT_RPOW },
    { "__rshift__",    VSLOT_RSHIFT },
    { "__rsub__",      VSLOT_RSUB },
    { "__rtruediv__",  VSLOT_RTRUEDIV },
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

PyDosVTable far * PYDOS_API pydos_vtable_create(void)
{
    PyDosVTable far *vt;

    vt = (PyDosVTable far *)pydos_far_alloc((unsigned long)sizeof(PyDosVTable));
    if (vt == (PyDosVTable far *)0) {
        return (PyDosVTable far *)0;
    }

    /* Zero out the entire structure (slots[], methods[], MRO all zeroed) */
    _fmemset(vt, 0, sizeof(PyDosVTable));

    return vt;
}

void PYDOS_API pydos_vtable_add_method(PyDosVTable far *vtable,
                             const char far *name,
                             void (far *func)(void))
{
    unsigned int hash;
    unsigned char idx;

    if (vtable == (PyDosVTable far *)0 ||
        name == (const char far *)0 ||
        func == (void (far *)(void))0) {
        return;
    }

    if (vtable->method_count >= PYDOS_VTABLE_MAX_METHODS) {
        return;
    }

    hash = djb2_hash_cstr_far(name);
    idx = vtable->method_count;

    vtable->methods[idx].name_hash = hash;
    vtable->methods[idx].name = name;
    vtable->methods[idx].func = func;
    vtable->method_count++;

    /* Also set special slot if this is a dunder method */
    pydos_vtable_set_special(vtable, name, func);
}

void (far * PYDOS_API pydos_vtable_lookup(PyDosVTable far *vtable,
                                unsigned int name_hash))(void)
{
    unsigned char i;
    unsigned char m;

    if (vtable == (PyDosVTable far *)0) {
        return (void (far *)(void))0;
    }

    /* Search this vtable's methods */
    for (i = 0; i < vtable->method_count; i++) {
        if (vtable->methods[i].name_hash == name_hash) {
            return vtable->methods[i].func;
        }
    }

    /* Search MRO chain */
    for (m = 0; m < vtable->mro_len; m++) {
        if (vtable->mro[m] != (PyDosVTable far *)0) {
            for (i = 0; i < vtable->mro[m]->method_count; i++) {
                if (vtable->mro[m]->methods[i].name_hash == name_hash) {
                    return vtable->mro[m]->methods[i].func;
                }
            }
        }
    }

    return (void (far *)(void))0;
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
    if (child->mro_len < PYDOS_VTABLE_MAX_MRO) {
        child->mro[child->mro_len] = parent;
        child->mro_len++;
    }

    /* Copy parent's slots where child hasn't set them */
    for (i = 0; i < VSLOT_COUNT; i++) {
        if (child->slots[i] == (void (far *)(void))0) {
            child->slots[i] = parent->slots[i];
        }
    }

    /* Also inherit parent's MRO entries */
    for (i = 0; i < parent->mro_len; i++) {
        if (child->mro_len < PYDOS_VTABLE_MAX_MRO) {
            child->mro[child->mro_len] = parent->mro[i];
            child->mro_len++;
        }
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
        vtable->slots[idx] = func;
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
}

void PYDOS_API pydos_vtable_shutdown(void)
{
    int i;

    for (i = 0; i < PYDT_MAX; i++) {
        if (pydos_builtin_vtables[i] != (PyDosVTable far *)0) {
            pydos_far_free(pydos_builtin_vtables[i]);
            pydos_builtin_vtables[i] = (PyDosVTable far *)0;
        }
    }
}
