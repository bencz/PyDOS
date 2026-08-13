/*
 * t_func.c - Unit tests for pydos_func_new (PYDT_FUNCTION)
 *
 * Tests function object creation, type checking, code pointer storage,
 * name field, defaults field, reference counting, and type_name.
 */

#include "testfw.h"
#include "../runtime/pdos_obj.h"
#include "../runtime/pdos_vtb.h"
#include "../runtime/pdos_mem.h"
#include "../runtime/pdos_lst.h"
#include "../runtime/pdos_cod.h"

/* Dummy code functions for testing.
 * Watcom aggressively merges functions with identical or trivially
 * similar bodies (COMDAT folding).  Each function must have a unique
 * side-effect so the optimizer cannot fold them together. */
static volatile int dummy_sink_a = 0;
static void far dummy_code(void)
{
    dummy_sink_a = 1;
}

static volatile int dummy_sink_b = 0;
static void far another_code(void)
{
    dummy_sink_b = 2;
}

static PyDosObj far * PYDOS_API bound_add(PyDosObj far *self,
                                           PyDosObj far *value)
{
    return pydos_obj_new_int(self->v.int_val + value->v.int_val);
}

static PyDosObj far * PYDOS_API bound_add_default(
    PyDosObj far *self, PyDosObj far *value, PyDosObj far *bias)
{
    return pydos_obj_new_int(self->v.int_val + value->v.int_val +
                             bias->v.int_val);
}

static PyDosObj far * PYDOS_API return_class_hook(PyDosObj far *cls)
{
    return cls;
}

static PyDosObj far * PYDOS_API c3_method_a(PyDosObj far *self)
{
    (void)self;
    return pydos_obj_new_str((const char far *)"A", 1);
}

static PyDosObj far * PYDOS_API c3_method_c(PyDosObj far *self)
{
    (void)self;
    return pydos_obj_new_str((const char far *)"C", 1);
}

static PyDosObj far * PYDOS_API c3_method_dynamic(PyDosObj far *self)
{
    (void)self;
    return pydos_obj_new_str((const char far *)"dynamic", 7);
}

static PyDosObj far * PYDOS_API metaclass_init(PyDosObj far *cls)
{
    PyDosObj far *flag;
    flag = pydos_obj_new_bool(1);
    pydos_obj_set_attr(cls, (const char far *)"meta_initialized", flag);
    PYDOS_DECREF(flag);
    return pydos_obj_new_none();
}

static PyDosObj far * PYDOS_API metaclass_echo(PyDosObj far *cls,
                                                PyDosObj far *value)
{
    (void)cls;
    PYDOS_INCREF(value);
    return value;
}

/* ------------------------------------------------------------------ */
/* Function object creation                                            */
/* ------------------------------------------------------------------ */

TEST(func_new_basic)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    PYDOS_DECREF(f);
}

TEST(func_new_type)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->type, PYDT_FUNCTION);
    PYDOS_DECREF(f);
}

TEST(func_new_refcount)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_EQ(f->refcount, 1);
    PYDOS_DECREF(f);
}

TEST(func_new_code_ptr)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_TRUE(pydos_code_ref_native_entry(f->v.func.code_ref) ==
                (void (far *)(void))dummy_code);
    PYDOS_DECREF(f);
}

TEST(func_new_name)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_STR_EQ(f->v.func.name, "test_fn");
    PYDOS_DECREF(f);
}

TEST(func_new_defaults_null)
{
    PyDosObj far *f = pydos_func_new(dummy_code, (const char far *)"test_fn");
    ASSERT_NOT_NULL(f);
    ASSERT_NULL(f->v.func.defaults);
    ASSERT_NULL(f->v.func.bound_self);
    PYDOS_DECREF(f);
}

TEST(func_bound_method_call)
{
    PyDosObj far *self;
    PyDosObj far *value;
    PyDosObj far *method;
    PyDosObj far *result;
    PyDosObj far *argv[1];

    self = pydos_obj_new_int(40L);
    value = pydos_obj_new_int(2L);
    method = pydos_bound_method_new((void (far *)(void))bound_add,
                                    self, (const char far *)"add");
    ASSERT_NOT_NULL(method);
    ASSERT_TRUE(method->v.func.bound_self == self);
    argv[0] = value;
    result = pydos_obj_call(method, 1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(method);
    PYDOS_DECREF(value);
    PYDOS_DECREF(self);
}

TEST(func_signature_metadata)
{
    PyDosObj far *func;
    PyDosObj far *self;
    PyDosObj far *bound;

    func = pydos_func_new((void (far *)(void))bound_add,
                          (const char far *)"add");
    self = pydos_obj_new_int(40L);
    ASSERT_NOT_NULL(func);
    ASSERT_FALSE(func->v.func.signature_known);
    pydos_func_set_arg_count(func, 2);
    ASSERT_TRUE(func->v.func.signature_known);
    ASSERT_EQ(func->v.func.arg_count, 2);

    bound = pydos_func_bind(func, self);
    ASSERT_NOT_NULL(bound);
    ASSERT_TRUE(bound->v.func.signature_known);
    ASSERT_EQ(bound->v.func.arg_count, 2);

    PYDOS_DECREF(bound);
    PYDOS_DECREF(self);
    PYDOS_DECREF(func);
}

TEST(func_dynamic_defaults)
{
    PyDosObj far *func;
    PyDosObj far *bound;
    PyDosObj far *self;
    PyDosObj far *value;
    PyDosObj far *bias;
    PyDosObj far *defaults;
    PyDosObj far *result;
    PyDosObj far *argv[1];

    func = pydos_func_new((void (far *)(void))bound_add_default,
                          (const char far *)"add_default");
    self = pydos_obj_new_int(40L);
    value = pydos_obj_new_int(1L);
    bias = pydos_obj_new_int(1L);
    defaults = pydos_list_new(1);
    ASSERT_NOT_NULL(func);
    ASSERT_NOT_NULL(defaults);
    pydos_list_append(defaults, bias);
    defaults->type = PYDT_TUPLE;
    pydos_func_set_arg_count(func, 3);
    pydos_func_set_defaults(func, defaults);

    bound = pydos_func_bind(func, self);
    ASSERT_NOT_NULL(bound);
    argv[0] = value;
    result = pydos_obj_call(bound, 1, argv);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_INT);
    ASSERT_EQ(result->v.int_val, 42L);

    PYDOS_DECREF(result);
    PYDOS_DECREF(bound);
    PYDOS_DECREF(defaults);
    PYDOS_DECREF(bias);
    PYDOS_DECREF(value);
    PYDOS_DECREF(self);
    PYDOS_DECREF(func);
}

TEST(vtable_signature_metadata)
{
    PyDosVTable far *vtable;
    PyDosMethodSlot far *slot;

    vtable = pydos_vtable_create();
    ASSERT_NOT_NULL(vtable);
    pydos_vtable_add_method_sig(vtable, (const char far *)"add",
                                (void (far *)(void))bound_add, 2);
    slot = pydos_vtable_lookup_slot(vtable, vtable->methods[0].name_hash);
    ASSERT_NOT_NULL(slot);
    ASSERT_TRUE(PYDOS_METHOD_HAS_SIGNATURE(slot));
    ASSERT_EQ(slot->arg_count, 2);
    ASSERT_TRUE(pydos_code_ref_native_entry(slot->code_ref) ==
                (void (far *)(void))bound_add);

    pydos_vtable_destroy(vtable);
}

TEST(class_metaclass_protocol)
{
    PyDosVTable far *meta_vtable;
    PyDosVTable far *class_vtable;
    PyDosObj far *metaclass;
    PyDosObj far *cls;
    PyDosObj far *applied;
    PyDosObj far *marker;
    PyDosObj far *echo;
    PyDosObj far *value;
    PyDosObj far *result;
    PyDosObj far *args[1];

    meta_vtable = pydos_vtable_create();
    class_vtable = pydos_vtable_create();
    ASSERT_NOT_NULL(meta_vtable);
    ASSERT_NOT_NULL(class_vtable);
    pydos_vtable_add_method_sig(
        meta_vtable, (const char far *)"__pydos_metaclass_init__",
        (void (far *)(void))metaclass_init, 1);
    pydos_vtable_add_method_sig(
        meta_vtable, (const char far *)"echo",
        (void (far *)(void))metaclass_echo, 2);
    metaclass = pydos_class_new((const char far *)"Meta", meta_vtable);
    cls = pydos_class_new((const char far *)"Subject", class_vtable);
    ASSERT_NOT_NULL(metaclass);
    ASSERT_NOT_NULL(cls);

    applied = pydos_class_apply_metaclass(cls, metaclass);
    ASSERT_NOT_NULL(applied);
    ASSERT_TRUE(cls->v.cls.metaclass == metaclass);
    marker = pydos_obj_get_attr(cls,
                                (const char far *)"meta_initialized");
    ASSERT_NOT_NULL(marker);
    ASSERT_TRUE(pydos_obj_is_truthy(marker));

    echo = pydos_obj_get_attr(cls, (const char far *)"echo");
    value = pydos_obj_new_int(73L);
    ASSERT_NOT_NULL(echo);
    ASSERT_TRUE(echo->v.func.bound_self == cls);
    args[0] = value;
    result = pydos_obj_call(echo, 1, args);
    ASSERT_NOT_NULL(result);
    ASSERT_TRUE(result == value);

    PYDOS_DECREF(result);
    PYDOS_DECREF(value);
    PYDOS_DECREF(echo);
    PYDOS_DECREF(marker);
    PYDOS_DECREF(applied);
    PYDOS_DECREF(cls);
    PYDOS_DECREF(metaclass);
    pydos_vtable_destroy(class_vtable);
    pydos_vtable_destroy(meta_vtable);
}

TEST(func_attributes)
{
    PyDosObj far *func;
    PyDosObj far *flag;
    PyDosObj far *loaded;
    func = pydos_func_new(dummy_code, (const char far *)"marked");
    flag = pydos_obj_new_bool(1);
    pydos_obj_set_attr(func, (const char far *)"marker", flag);
    ASSERT_TRUE(pydos_obj_has_attr(func, (const char far *)"marker"));
    loaded = pydos_obj_get_attr(func, (const char far *)"marker");
    ASSERT_NOT_NULL(loaded);
    ASSERT_TRUE(pydos_obj_is_truthy(loaded));
    PYDOS_DECREF(loaded);
    PYDOS_DECREF(flag);
    PYDOS_DECREF(func);
}

TEST(class_function_attribute_shadows_vtable)
{
    PyDosVTable far *vtable;
    PyDosObj far *cls;
    PyDosObj far *func;
    PyDosObj far *loaded;

    vtable = pydos_vtable_create();
    ASSERT_NOT_NULL(vtable);
    pydos_vtable_add_method(vtable, (const char far *)"value",
                            (void (far *)(void))dummy_code);
    cls = pydos_class_new((const char far *)"Decorated", vtable);
    ASSERT_NOT_NULL(cls);
    func = pydos_func_new(another_code,
                          (const char far *)"Decorated__value");
    ASSERT_NOT_NULL(func);
    pydos_obj_set_attr(cls, (const char far *)"value", func);

    loaded = pydos_obj_get_attr(cls, (const char far *)"value");
    ASSERT_NOT_NULL(loaded);
    ASSERT_TRUE(loaded == func);

    PYDOS_DECREF(loaded);
    PYDOS_DECREF(func);
    PYDOS_DECREF(cls);
}

TEST(inherited_class_hook_preserves_class)
{
    PyDosObj far *base;
    PyDosObj far *derived;
    PyDosObj far *hook;
    PyDosObj far *result;
    unsigned int refcount;

    base = pydos_class_new((const char far *)"HookBase",
                           (PyDosVTable far *)0);
    derived = pydos_class_new((const char far *)"HookChild",
                              (PyDosVTable far *)0);
    hook = pydos_func_new((void (far *)(void))return_class_hook,
                          (const char far *)"class_hook");
    ASSERT_NOT_NULL(base);
    ASSERT_NOT_NULL(derived);
    ASSERT_NOT_NULL(hook);
    pydos_obj_set_attr(base,
                       (const char far *)"__pydos_metaclass_hook__",
                       hook);
    pydos_class_add_base(derived, base);
    refcount = derived->refcount;

    result = pydos_class_apply_inherited_hook(derived);
    ASSERT_NOT_NULL(result);
    ASSERT_EQ(result->type, PYDT_NONE);
    ASSERT_EQ(derived->type, PYDT_CLASS);
    ASSERT_EQ(derived->refcount, refcount);

    PYDOS_DECREF(result);
    PYDOS_DECREF(hook);
    PYDOS_DECREF(derived);
    PYDOS_DECREF(base);
}

TEST(class_c3_mro_and_vtable_order)
{
    PyDosVTable far *a_vtable;
    PyDosVTable far *b_vtable;
    PyDosVTable far *c_vtable;
    PyDosVTable far *d_vtable;
    PyDosObj far *a;
    PyDosObj far *b;
    PyDosObj far *c;
    PyDosObj far *d;
    PyDosObj far *a_label;
    PyDosObj far *c_label;
    PyDosObj far *loaded;
    PyDosObj far *instance;
    PyDosObj far *replacement;
    PyDosObj far *call_args[1];
    PyDosObj far *call_result;

    a_vtable = pydos_vtable_create();
    b_vtable = pydos_vtable_create();
    c_vtable = pydos_vtable_create();
    d_vtable = pydos_vtable_create();
    ASSERT_NOT_NULL(a_vtable);
    ASSERT_NOT_NULL(b_vtable);
    ASSERT_NOT_NULL(c_vtable);
    ASSERT_NOT_NULL(d_vtable);
    pydos_vtable_add_method(a_vtable, (const char far *)"who",
                            (void (far *)(void))c3_method_a);
    pydos_vtable_add_method(c_vtable, (const char far *)"who",
                            (void (far *)(void))c3_method_c);

    a = pydos_class_new((const char far *)"A", a_vtable);
    b = pydos_class_new((const char far *)"B", b_vtable);
    c = pydos_class_new((const char far *)"C", c_vtable);
    d = pydos_class_new((const char far *)"D", d_vtable);
    ASSERT_NOT_NULL(a);
    ASSERT_NOT_NULL(b);
    ASSERT_NOT_NULL(c);
    ASSERT_NOT_NULL(d);
    pydos_class_add_object_base(a);
    pydos_class_add_base(b, a);
    pydos_class_add_base(c, a);
    pydos_class_add_base(d, b);
    pydos_class_add_base(d, c);

    ASSERT_EQ(d->v.cls.mro_len, 5);
    ASSERT_TRUE(d->v.cls.mro[0] == d);
    ASSERT_TRUE(d->v.cls.mro[1] == b);
    ASSERT_TRUE(d->v.cls.mro[2] == c);
    ASSERT_TRUE(d->v.cls.mro[3] == a);
    ASSERT_EQ(d_vtable->mro_len, 3);
    ASSERT_TRUE(d_vtable->mro[0] == b_vtable);
    ASSERT_TRUE(d_vtable->mro[1] == c_vtable);
    ASSERT_TRUE(d_vtable->mro[2] == a_vtable);

    a_label = pydos_obj_new_str((const char far *)"A", 1);
    c_label = pydos_obj_new_str((const char far *)"C", 1);
    pydos_obj_set_attr(a, (const char far *)"label", a_label);
    pydos_obj_set_attr(c, (const char far *)"label", c_label);
    loaded = pydos_obj_get_attr(d, (const char far *)"label");
    ASSERT_NOT_NULL(loaded);
    ASSERT_STR_EQ(loaded->v.str.data, "C");

    instance = pydos_obj_alloc_type(PYDT_INSTANCE);
    ASSERT_NOT_NULL(instance);
    pydos_obj_set_vtable(instance, d_vtable);
    pydos_obj_set_class(instance, d);
    call_args[0] = instance;
    call_result = pydos_obj_call_method_guarded(
        (const char far *)"who", (void (far *)(void))c3_method_c,
        1, call_args);
    ASSERT_NOT_NULL(call_result);
    ASSERT_STR_EQ(call_result->v.str.data, "C");
    PYDOS_DECREF(call_result);

    replacement = pydos_func_new((void (far *)(void))c3_method_dynamic,
                                 (const char far *)"replacement");
    ASSERT_NOT_NULL(replacement);
    pydos_obj_set_attr(d, (const char far *)"who", replacement);
    call_result = pydos_obj_call_method_guarded(
        (const char far *)"who", (void (far *)(void))c3_method_c,
        1, call_args);
    ASSERT_NOT_NULL(call_result);
    ASSERT_STR_EQ(call_result->v.str.data, "dynamic");
    PYDOS_DECREF(call_result);
    pydos_obj_del_attr(d, (const char far *)"who");
    PYDOS_DECREF(replacement);

    PYDOS_DECREF(instance);
    PYDOS_DECREF(loaded);
    PYDOS_DECREF(c_label);
    PYDOS_DECREF(a_label);
    PYDOS_DECREF(d);
    PYDOS_DECREF(c);
    PYDOS_DECREF(b);
    PYDOS_DECREF(a);
    pydos_vtable_destroy(d_vtable);
    pydos_vtable_destroy(c_vtable);
    pydos_vtable_destroy(b_vtable);
    pydos_vtable_destroy(a_vtable);
}

/* ------------------------------------------------------------------ */
/* Different function objects                                           */
/* ------------------------------------------------------------------ */

TEST(func_two_different)
{
    PyDosObj far *f1;
    PyDosObj far *f2;

    f1 = pydos_func_new(dummy_code, (const char far *)"fn_a");
    f2 = pydos_func_new(another_code, (const char far *)"fn_b");
    ASSERT_NOT_NULL(f1);
    ASSERT_NOT_NULL(f2);

    /* Different code pointers */
    ASSERT_TRUE(pydos_code_ref_native_entry(f1->v.func.code_ref) !=
                pydos_code_ref_native_entry(f2->v.func.code_ref));

    /* Different names */
    ASSERT_STR_EQ(f1->v.func.name, "fn_a");
    ASSERT_STR_EQ(f2->v.func.name, "fn_b");

    PYDOS_DECREF(f1);
    PYDOS_DECREF(f2);
}

TEST(func_type_name)
{
    PyDosObj far *f;
    const char far *tn;

    f = pydos_func_new(dummy_code, (const char far *)"my_func");
    ASSERT_NOT_NULL(f);

    tn = pydos_obj_type_name(f);
    ASSERT_STR_EQ(tn, "function");

    PYDOS_DECREF(f);
}

TEST(func_is_truthy)
{
    PyDosObj far *f;

    f = pydos_func_new(dummy_code, (const char far *)"truthy_fn");
    ASSERT_NOT_NULL(f);

    /* Function objects are always truthy */
    ASSERT_TRUE(pydos_obj_is_truthy(f));

    PYDOS_DECREF(f);
}

TEST(func_to_str)
{
    PyDosObj far *f;
    PyDosObj far *s;

    f = pydos_func_new(dummy_code, (const char far *)"show_me");
    ASSERT_NOT_NULL(f);

    s = pydos_obj_to_str(f);
    ASSERT_NOT_NULL(s);
    ASSERT_EQ(s->type, PYDT_STR);

    PYDOS_DECREF(s);
    PYDOS_DECREF(f);
}

TEST(func_code_reference_is_shared_by_bound_method)
{
    PyDosObj far *function;
    PyDosObj far *self;
    PyDosObj far *bound;
    PyDosCodeRef far *reference;

    function = pydos_func_new((void (far *)(void))bound_add,
                              (const char far *)"shared_ref");
    self = pydos_obj_new_int(10);
    ASSERT_NOT_NULL(function);
    ASSERT_NOT_NULL(self);
    reference = function->v.func.code_ref;
    ASSERT_EQ(pydos_code_ref_kind(reference), PYDOS_CODE_NATIVE);
    ASSERT_EQ(reference->refcount, 1);
    bound = pydos_func_bind(function, self);
    ASSERT_NOT_NULL(bound);
    ASSERT_TRUE(bound->v.func.code_ref == reference);
    ASSERT_EQ(reference->refcount, 2);
    PYDOS_DECREF(function);
    ASSERT_EQ(reference->refcount, 1);
    PYDOS_DECREF(bound);
    PYDOS_DECREF(self);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_func_tests(void)
{
    SUITE("pdos_func");
    RUN(func_new_basic);
    RUN(func_new_type);
    RUN(func_new_refcount);
    RUN(func_new_code_ptr);
    RUN(func_new_name);
    RUN(func_new_defaults_null);
    RUN(func_bound_method_call);
    RUN(func_signature_metadata);
    RUN(func_dynamic_defaults);
    RUN(vtable_signature_metadata);
    RUN(class_metaclass_protocol);
    RUN(func_attributes);
    RUN(class_function_attribute_shadows_vtable);
    RUN(inherited_class_hook_preserves_class);
    RUN(class_c3_mro_and_vtable_order);
    RUN(func_two_different);
    RUN(func_type_name);
    RUN(func_is_truthy);
    RUN(func_to_str);
    RUN(func_code_reference_is_shared_by_bound_method);
}
