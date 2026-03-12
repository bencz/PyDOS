/*
 * pydos_rt.c - Runtime init/shutdown for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * This module initializes all runtime subsystems in the correct order
 * and shuts them down in reverse order.
 */

#include "pdos_rt.h"
#include "pdos_obj.h"
#include "pdos_io.h"
#include "pdos_str.h"
#include "pdos_int.h"
#include "pdos_blt.h"
#include "pdos_exc.h"
#include "pdos_vtb.h"
#include "pdos_itn.h"
#include "pdos_lst.h"
#include "pdos_dic.h"
#include "pdos_gen.h"
#include "pdos_cll.h"
#include "pdos_fzs.h"
#include "pdos_cpx.h"
#include "pdos_bya.h"

/*
 * Forward declarations for memory and GC subsystem functions.
 * These are defined in pydos_mem.c and pydos_gc.c respectively,
 * which are compiled separately as part of the runtime library.
 */
extern void PYDOS_API pydos_mem_init(void);
extern void PYDOS_API pydos_mem_shutdown(void);
extern void PYDOS_API pydos_gc_init(void);
extern void PYDOS_API pydos_gc_shutdown(void);
extern void PYDOS_API pydos_gc_collect(void);
extern int PYDOS_API pydos_gc_add_root(PyDosObj far * far *root);
extern void PYDOS_API pydos_arena_init(void);
extern void PYDOS_API pydos_arena_shutdown(void);

/* Global namespace dictionary */
PyDosObj far * PYDOS_API pydos_globals = (PyDosObj far *)0;

/* Active closure for closure-passing mechanism.
 * Set by the caller before an indirect call through a function object.
 * Read by the callee's prologue to capture the closure list.
 * DOS is single-threaded, so a global variable is safe.
 * Symbol naming controlled by #pragma aux in pdos_rt.h, not PYDOS_API,
 * because __cdecl naming pragmas do not apply to variables. */
PyDosObj far * pydos_active_closure = (PyDosObj far *)0;

/*
 * pydos_rt_init - Initialize ALL runtime subsystems in correct order.
 *
 * Order:
 *  1. Memory allocator
 *  2. Garbage collector
 *  3. Object system
 *  4. String interning
 *  5. String operations
 *  6. Integer operations
 *  7. DOS I/O
 *  8. VTable mechanism
 *  9. Exception handling
 * 10. Dictionary operations (creates sentinel)
 * 11. List operations
 * 12. Generator helpers
 * 13. Built-in functions
 * 14. Create global namespace
 */
void PYDOS_API pydos_rt_init(void)
{
    /* 1. Memory allocator */
    pydos_mem_init();

    /* 2. Garbage collector */
    pydos_gc_init();

    /* 2b. Arena scope allocator */
    pydos_arena_init();

    /* 3. Object system (singletons: None, True, False) */
    pydos_obj_init();

    /* 4. String interning table */
    pydos_intern_init();

    /* 5. String operations */
    pydos_str_init();

    /* 6. Integer operations */
    pydos_int_init();

    /* 7. DOS I/O layer */
    pydos_io_init();

    /* 8. VTable mechanism (creates builtin type vtables) */
    pydos_vtable_init();

    /* 9. Exception handling */
    pydos_exc_init();

    /* 10. Dictionary operations (creates deleted sentinel) */
    pydos_dict_init();

    /* 11. List operations */
    pydos_list_init();

    /* 12. Generator helpers */
    pydos_gen_init();

    /* 12b. Cell objects (closures) */
    pydos_cell_init();

    /* 12c. Coroutine / async helpers */
    pydos_cor_init();

    /* 12d. Frozenset operations */
    pydos_frozenset_init();

    /* 13. Built-in functions */
    pydos_builtins_init();

    /* 14. Create global namespace as empty dict */
    pydos_globals = pydos_dict_new(32);

    /* Register globals dict as GC root */
    if (pydos_globals != (PyDosObj far *)0) {
        pydos_gc_add_root(&pydos_globals);
    }
}

/*
 * pydos_rt_shutdown - Shut down all subsystems in reverse order.
 *
 * Performs a final GC collection sweep before tearing down subsystems.
 */
void PYDOS_API pydos_rt_shutdown(void)
{
    /* Release global namespace */
    if (pydos_globals != (PyDosObj far *)0) {
        PYDOS_DECREF(pydos_globals);
        pydos_globals = (PyDosObj far *)0;
    }

    /* Final garbage collection sweep */
    pydos_gc_collect();

    /* Shut down in reverse order of initialization */
    pydos_builtins_shutdown();
    pydos_frozenset_shutdown();
    pydos_cor_shutdown();
    pydos_cell_shutdown();
    pydos_gen_shutdown();
    pydos_list_shutdown();
    pydos_dict_shutdown();
    pydos_exc_shutdown();
    pydos_vtable_shutdown();
    pydos_io_shutdown();
    pydos_int_shutdown();
    pydos_str_shutdown();
    pydos_intern_shutdown();
    pydos_obj_shutdown();
    pydos_arena_shutdown();
    pydos_gc_shutdown();
    pydos_mem_shutdown();
}
