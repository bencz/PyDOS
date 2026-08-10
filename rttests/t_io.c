/*
 * t_io.c - Unit tests for pdos_io module
 *
 * The I/O module uses DOS INT 21h calls, so on macOS with compat stubs
 * the functions are no-ops. These tests verify the functions do not crash.
 */

#include "testfw.h"
#include "../runtime/pdos_io.h"

/* ------------------------------------------------------------------ */
/* Smoke tests (must not crash)                                        */
/* ------------------------------------------------------------------ */

TEST(io_write_far)
{
    int ret;
    ret = pydos_dos_write_far((const char far *)"test", 4);
    /* On DOS returns bytes written; on macOS stub returns 0 or len */
    (void)ret;
    ASSERT_TRUE(1);
}

TEST(io_write_near)
{
    int ret;
    ret = pydos_dos_write("test", 4);
    (void)ret;
    ASSERT_TRUE(1);
}

TEST(io_putchar)
{
    pydos_dos_putchar('X');
    ASSERT_TRUE(1);
}

TEST(io_print_str)
{
    pydos_dos_print_str((const char far *)"hello");
    ASSERT_TRUE(1);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_io_tests(void)
{
    SUITE("pdos_io");
    RUN(io_write_far);
    RUN(io_write_near);
    RUN(io_putchar);
    RUN(io_print_str);
}
