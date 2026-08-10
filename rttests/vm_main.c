/* Focused PBC VM test runner. */

#include <stdio.h>
#include "testfw.h"
#include "../runtime/pdos_rt.h"

int tf_pass = 0;
int tf_fail = 0;

extern void run_vm_tests(void);

int main(void)
{
    pydos_rt_init();
    run_vm_tests();
    printf("\nVM results: %d passed, %d failed, %d total\n",
           tf_pass, tf_fail, tf_pass + tf_fail);
    pydos_rt_shutdown();
    return tf_fail == 0 ? 0 : 1;
}
