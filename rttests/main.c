/*
 * main.c - PyDOS Runtime Unit Test Runner
 *
 * Initializes the runtime, runs all test suites, reports results.
 */

#include <stdio.h>
#include "testfw.h"
#include "../runtime/pdos_rt.h"

/* Global counters */
int tf_pass = 0;
int tf_fail = 0;

/* Test suite runners (defined in t_*.c files) */
extern void run_obj_tests(void);
extern void run_mem_tests(void);
extern void run_str_tests(void);
extern void run_int_tests(void);
extern void run_lst_tests(void);
extern void run_dic_tests(void);
extern void run_exc_tests(void);
extern void run_gc_tests(void);
extern void run_vtb_tests(void);
extern void run_itn_tests(void);
extern void run_blt_tests(void);
extern void run_io_tests(void);
extern void run_gen_tests(void);
extern void run_func_tests(void);
extern void run_rt_tests(void);
extern void run_e2e_tests(void);
extern void run_gcs_tests(void);
extern void run_sjn_tests(void);
extern void run_arn_tests(void);
extern void run_set_tests(void);
extern void run_cll_tests(void);
extern void run_asn_tests(void);
extern void run_unp_tests(void);
extern void run_excg_tests(void);
extern void run_fzs_tests(void);
extern void run_cpx_tests(void);
extern void run_bya_tests(void);

int main(void)
{
    printf("PyDOS Runtime Unit Tests\n");
    printf("========================\n");

    /* Initialize all runtime subsystems */
    pydos_rt_init();

    /* Run test suites */
    run_obj_tests();
    run_mem_tests();
    run_str_tests();
    run_int_tests();
    run_lst_tests();
    run_dic_tests();
    run_exc_tests();
    run_gc_tests();
    run_vtb_tests();
    run_itn_tests();
    run_blt_tests();
    run_io_tests();
    run_gen_tests();
    run_func_tests();
    run_rt_tests();
    run_e2e_tests();
    run_gcs_tests();
    run_sjn_tests();
    run_arn_tests();
    run_set_tests();
    run_cll_tests();
    run_asn_tests();
    run_unp_tests();
    run_excg_tests();
    run_fzs_tests();
    run_cpx_tests();
    run_bya_tests();

    /* Summary */
    printf("\n========================\n");
    printf("Results: %d passed, %d failed, %d total\n",
           tf_pass, tf_fail, tf_pass + tf_fail);

    /* Shutdown */
    pydos_rt_shutdown();

    return tf_fail > 0 ? 1 : 0;
}
