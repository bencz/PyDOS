/*
 * t_vtb.c - Unit tests for pdos_vtb module
 *
 * Tests vtable creation, method add/lookup, inheritance,
 * and override behavior.
 */

#include "testfw.h"
#include "../runtime/pdos_vtb.h"
#include "../runtime/pdos_obj.h"

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

/* DJB2 hash matching the runtime's method name hashing */
static unsigned int djb2(const char far *s)
{
    unsigned int h = 5381;
    while (*s) {
        h = ((h << 5) + h) + (unsigned char)*s;
        s++;
    }
    return h;
}

/* Dummy functions to use as method slot values */
static void far dummy_func_a(void) { }
static void far dummy_func_b(void) { }
static void far dummy_func_c(void) { }
static void far dummy_func_child(void) { }

/* ------------------------------------------------------------------ */
/* vtable_create: returns non-null vtable                              */
/* ------------------------------------------------------------------ */

TEST(vtable_create)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);
}

/* ------------------------------------------------------------------ */
/* vtable_create_empty: method_count == 0                              */
/* ------------------------------------------------------------------ */

TEST(vtable_create_empty)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);
    ASSERT_EQ(vt->method_count, 0);
}

/* ------------------------------------------------------------------ */
/* vtable_add_method: add a method, method_count == 1                  */
/* ------------------------------------------------------------------ */

TEST(vtable_add_method)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);
    pydos_vtable_add_method(vt,
        (const char far *)"test",
        (void (far *)(void))dummy_func_a);
    ASSERT_EQ(vt->method_count, 1);
}

/* ------------------------------------------------------------------ */
/* vtable_lookup_found: add "test", lookup by hash returns non-null    */
/* ------------------------------------------------------------------ */

TEST(vtable_lookup_found)
{
    PyDosVTable far *vt;
    void (far *result)(void);
    unsigned int h;

    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);
    pydos_vtable_add_method(vt,
        (const char far *)"test",
        (void (far *)(void))dummy_func_a);

    h = djb2((const char far *)"test");
    result = pydos_vtable_lookup(vt, h);
    ASSERT_NOT_NULL((void far *)result);
}

/* ------------------------------------------------------------------ */
/* vtable_lookup_missing: lookup non-existent hash returns null        */
/* ------------------------------------------------------------------ */

TEST(vtable_lookup_missing)
{
    PyDosVTable far *vt;
    void (far *result)(void);
    unsigned int h;

    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    h = djb2((const char far *)"nonexistent");
    result = pydos_vtable_lookup(vt, h);
    ASSERT_NULL((void far *)result);
}

/* ------------------------------------------------------------------ */
/* vtable_add_multiple: add 3 methods, method_count == 3               */
/* ------------------------------------------------------------------ */

TEST(vtable_add_multiple)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_add_method(vt,
        (const char far *)"alpha",
        (void (far *)(void))dummy_func_a);
    pydos_vtable_add_method(vt,
        (const char far *)"beta",
        (void (far *)(void))dummy_func_b);
    pydos_vtable_add_method(vt,
        (const char far *)"gamma",
        (void (far *)(void))dummy_func_c);

    ASSERT_EQ(vt->method_count, 3);
}

/* ------------------------------------------------------------------ */
/* vtable_inherit: parent has method, child inherits, lookup finds it  */
/* ------------------------------------------------------------------ */

TEST(vtable_inherit)
{
    PyDosVTable far *parent;
    PyDosVTable far *child;
    void (far *result)(void);
    unsigned int h;

    parent = pydos_vtable_create();
    ASSERT_NOT_NULL(parent);
    pydos_vtable_add_method(parent,
        (const char far *)"inherited_method",
        (void (far *)(void))dummy_func_a);

    child = pydos_vtable_create();
    ASSERT_NOT_NULL(child);
    pydos_vtable_inherit(child, parent);

    h = djb2((const char far *)"inherited_method");
    result = pydos_vtable_lookup(child, h);
    ASSERT_NOT_NULL((void far *)result);
}

/* ------------------------------------------------------------------ */
/* vtable_override: parent has method, child adds same name,           */
/*                  lookup returns child's version                     */
/* ------------------------------------------------------------------ */

TEST(vtable_override)
{
    PyDosVTable far *parent;
    PyDosVTable far *child;
    void (far *result)(void);
    unsigned int h;

    parent = pydos_vtable_create();
    ASSERT_NOT_NULL(parent);
    pydos_vtable_add_method(parent,
        (const char far *)"do_thing",
        (void (far *)(void))dummy_func_a);

    child = pydos_vtable_create();
    ASSERT_NOT_NULL(child);
    pydos_vtable_inherit(child, parent);
    pydos_vtable_add_method(child,
        (const char far *)"do_thing",
        (void (far *)(void))dummy_func_child);

    h = djb2((const char far *)"do_thing");
    result = pydos_vtable_lookup(child, h);
    ASSERT_NOT_NULL((void far *)result);
    ASSERT_TRUE((void (far *)(void))result ==
                (void (far *)(void))dummy_func_child);
}

/* ------------------------------------------------------------------ */
/* vtable_slot_count: VSLOT_COUNT must be 73                           */
/* ------------------------------------------------------------------ */

TEST(vtable_slot_count)
{
    ASSERT_EQ(VSLOT_COUNT, 73);
}

/* ------------------------------------------------------------------ */
/* vtable_set_special_add: __add__ sets VSLOT_ADD                      */
/* ------------------------------------------------------------------ */

TEST(vtable_set_special_add)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    ASSERT_NULL((void far *)vt->slots[VSLOT_ADD]);

    pydos_vtable_set_special(vt,
        (const char far *)"__add__",
        (void (far *)(void))dummy_func_a);

    ASSERT_NOT_NULL((void far *)vt->slots[VSLOT_ADD]);
    ASSERT_TRUE(vt->slots[VSLOT_ADD] == (void (far *)(void))dummy_func_a);
}

/* ------------------------------------------------------------------ */
/* vtable_set_special_init: __init__ sets VSLOT_INIT (slot 0)          */
/* ------------------------------------------------------------------ */

TEST(vtable_set_special_init)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_set_special(vt,
        (const char far *)"__init__",
        (void (far *)(void))dummy_func_b);

    ASSERT_TRUE(vt->slots[VSLOT_INIT] == (void (far *)(void))dummy_func_b);
}

/* ------------------------------------------------------------------ */
/* vtable_set_special_iter: __iter__ sets VSLOT_ITER                   */
/* ------------------------------------------------------------------ */

TEST(vtable_set_special_iter)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_set_special(vt,
        (const char far *)"__iter__",
        (void (far *)(void))dummy_func_c);

    ASSERT_TRUE(vt->slots[VSLOT_ITER] == (void (far *)(void))dummy_func_c);
}

/* ------------------------------------------------------------------ */
/* vtable_set_special_unknown: non-dunder doesn't set any slot         */
/* ------------------------------------------------------------------ */

TEST(vtable_set_special_unknown)
{
    PyDosVTable far *vt;
    int i;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_set_special(vt,
        (const char far *)"regular_method",
        (void (far *)(void))dummy_func_a);

    /* No slot should be set */
    for (i = 0; i < VSLOT_COUNT; i++) {
        ASSERT_NULL((void far *)vt->slots[i]);
    }
}

/* ------------------------------------------------------------------ */
/* vtable_add_method_sets_slot: add_method sets dunder slot             */
/* ------------------------------------------------------------------ */

TEST(vtable_add_method_sets_slot)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_add_method(vt,
        (const char far *)"__str__",
        (void (far *)(void))dummy_func_a);

    ASSERT_EQ(vt->method_count, 1);
    ASSERT_TRUE(vt->slots[VSLOT_STR] == (void (far *)(void))dummy_func_a);
}

/* ------------------------------------------------------------------ */
/* vtable_inherit_slots: child inherits parent's slots                 */
/* ------------------------------------------------------------------ */

TEST(vtable_inherit_slots)
{
    PyDosVTable far *parent;
    PyDosVTable far *child;

    parent = pydos_vtable_create();
    ASSERT_NOT_NULL(parent);
    pydos_vtable_set_special(parent,
        (const char far *)"__eq__",
        (void (far *)(void))dummy_func_a);
    pydos_vtable_set_special(parent,
        (const char far *)"__len__",
        (void (far *)(void))dummy_func_b);

    child = pydos_vtable_create();
    ASSERT_NOT_NULL(child);
    pydos_vtable_inherit(child, parent);

    /* Child should have inherited both slots */
    ASSERT_TRUE(child->slots[VSLOT_EQ] == (void (far *)(void))dummy_func_a);
    ASSERT_TRUE(child->slots[VSLOT_LEN] == (void (far *)(void))dummy_func_b);
}

/* ------------------------------------------------------------------ */
/* vtable_inherit_no_override: child's existing slot not overwritten   */
/* ------------------------------------------------------------------ */

TEST(vtable_inherit_no_override)
{
    PyDosVTable far *parent;
    PyDosVTable far *child;

    parent = pydos_vtable_create();
    ASSERT_NOT_NULL(parent);
    pydos_vtable_set_special(parent,
        (const char far *)"__add__",
        (void (far *)(void))dummy_func_a);

    child = pydos_vtable_create();
    ASSERT_NOT_NULL(child);
    /* Set child's __add__ first */
    pydos_vtable_set_special(child,
        (const char far *)"__add__",
        (void (far *)(void))dummy_func_child);
    pydos_vtable_inherit(child, parent);

    /* Child's version should remain */
    ASSERT_TRUE(child->slots[VSLOT_ADD] == (void (far *)(void))dummy_func_child);
}

/* ------------------------------------------------------------------ */
/* vtable_inplace_slots: __iadd__ etc set correct slots                */
/* ------------------------------------------------------------------ */

TEST(vtable_inplace_slots)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_set_special(vt,
        (const char far *)"__iadd__",
        (void (far *)(void))dummy_func_a);
    pydos_vtable_set_special(vt,
        (const char far *)"__imul__",
        (void (far *)(void))dummy_func_b);

    ASSERT_TRUE(vt->slots[VSLOT_IADD] == (void (far *)(void))dummy_func_a);
    ASSERT_TRUE(vt->slots[VSLOT_IMUL] == (void (far *)(void))dummy_func_b);
}

/* ------------------------------------------------------------------ */
/* vtable_reflected_slots: __radd__ etc set correct slots              */
/* ------------------------------------------------------------------ */

TEST(vtable_reflected_slots)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_set_special(vt,
        (const char far *)"__radd__",
        (void (far *)(void))dummy_func_a);
    pydos_vtable_set_special(vt,
        (const char far *)"__rpow__",
        (void (far *)(void))dummy_func_b);
    pydos_vtable_set_special(vt,
        (const char far *)"__rtruediv__",
        (void (far *)(void))dummy_func_c);

    ASSERT_TRUE(vt->slots[VSLOT_RADD] == (void (far *)(void))dummy_func_a);
    ASSERT_TRUE(vt->slots[VSLOT_RPOW] == (void (far *)(void))dummy_func_b);
    ASSERT_TRUE(vt->slots[VSLOT_RTRUEDIV] == (void (far *)(void))dummy_func_c);
}

/* ------------------------------------------------------------------ */
/* vtable_matmul_slot: __matmul__ sets VSLOT_MATMUL                    */
/* ------------------------------------------------------------------ */

TEST(vtable_matmul_slot)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    pydos_vtable_set_special(vt,
        (const char far *)"__matmul__",
        (void (far *)(void))dummy_func_a);
    pydos_vtable_set_special(vt,
        (const char far *)"__rmatmul__",
        (void (far *)(void))dummy_func_b);

    ASSERT_TRUE(vt->slots[VSLOT_MATMUL] == (void (far *)(void))dummy_func_a);
    ASSERT_TRUE(vt->slots[VSLOT_RMATMUL] == (void (far *)(void))dummy_func_b);
}

/* ------------------------------------------------------------------ */
/* vtable_all_slots_zero: freshly created vtable has all slots null    */
/* ------------------------------------------------------------------ */

TEST(vtable_all_slots_zero)
{
    PyDosVTable far *vt;
    int i;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    for (i = 0; i < VSLOT_COUNT; i++) {
        ASSERT_NULL((void far *)vt->slots[i]);
    }
}

/* ------------------------------------------------------------------ */
/* vtable_set_name: class_name stored and repr uses it                 */
/* ------------------------------------------------------------------ */

TEST(vtable_set_name)
{
    PyDosVTable far *vt;
    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);

    /* Initially NULL */
    ASSERT_NULL((void far *)vt->class_name);

    pydos_vtable_set_name(vt, (const char far *)"Foo");
    ASSERT_NOT_NULL((void far *)vt->class_name);
    ASSERT_STR_EQ(vt->class_name, "Foo");
}

/* ------------------------------------------------------------------ */
/* vtable_class_name_repr: instance to_str uses class_name             */
/* ------------------------------------------------------------------ */

TEST(vtable_class_name_repr)
{
    PyDosVTable far *vt;
    PyDosObj far *inst;
    PyDosObj far *s;

    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);
    pydos_vtable_set_name(vt, (const char far *)"Foo");

    /* Create a raw PYDT_INSTANCE object */
    inst = pydos_obj_alloc();
    ASSERT_NOT_NULL(inst);
    inst->type = PYDT_INSTANCE;
    inst->v.instance.attrs = (PyDosObj far *)0;
    inst->v.instance.vtable = vt;
    inst->v.instance.cls = (PyDosObj far *)0;

    s = pydos_obj_to_str(inst);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "<__main__.Foo object>");

    PYDOS_DECREF(s);
    PYDOS_DECREF(inst);
}

/* ------------------------------------------------------------------ */
/* vtable_class_name_null_fallback: no class_name falls back           */
/* ------------------------------------------------------------------ */

TEST(vtable_class_name_null_fallback)
{
    PyDosVTable far *vt;
    PyDosObj far *inst;
    PyDosObj far *s;

    vt = pydos_vtable_create();
    ASSERT_NOT_NULL(vt);
    /* Do NOT set class_name — should remain NULL from _fmemset */

    inst = pydos_obj_alloc();
    ASSERT_NOT_NULL(inst);
    inst->type = PYDT_INSTANCE;
    inst->v.instance.attrs = (PyDosObj far *)0;
    inst->v.instance.vtable = vt;
    inst->v.instance.cls = (PyDosObj far *)0;

    s = pydos_obj_to_str(inst);
    ASSERT_NOT_NULL(s);
    ASSERT_STR_EQ(s->v.str.data, "<instance>");

    PYDOS_DECREF(s);
    PYDOS_DECREF(inst);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_vtb_tests(void)
{
    SUITE("pdos_vtb");

    RUN(vtable_create);
    RUN(vtable_create_empty);
    RUN(vtable_add_method);
    RUN(vtable_lookup_found);
    RUN(vtable_lookup_missing);
    RUN(vtable_add_multiple);
    RUN(vtable_inherit);
    RUN(vtable_override);
    RUN(vtable_slot_count);
    RUN(vtable_set_special_add);
    RUN(vtable_set_special_init);
    RUN(vtable_set_special_iter);
    RUN(vtable_set_special_unknown);
    RUN(vtable_add_method_sets_slot);
    RUN(vtable_inherit_slots);
    RUN(vtable_inherit_no_override);
    RUN(vtable_inplace_slots);
    RUN(vtable_reflected_slots);
    RUN(vtable_matmul_slot);
    RUN(vtable_all_slots_zero);
    RUN(vtable_set_name);
    RUN(vtable_class_name_repr);
    RUN(vtable_class_name_null_fallback);
}
