/*
 * t_mem.c - Unit tests for pdos_mem module
 *
 * Tests far heap allocation, reallocation, freeing,
 * and allocation statistics tracking.
 */

#include "testfw.h"
#include "../runtime/pdos_mem.h"

/* ------------------------------------------------------------------ */
/* Basic allocation                                                    */
/* ------------------------------------------------------------------ */

TEST(alloc_basic)
{
    void far *p = pydos_far_alloc(100UL);
    ASSERT_NOT_NULL(p);
    pydos_far_free(p);
}

TEST(alloc_zero)
{
    /* Implementation-defined: may return NULL or valid pointer.
     * Just verify it does not crash. */
    void far *p = pydos_far_alloc(0UL);
    pydos_far_free(p);
}

TEST(alloc_small)
{
    void far *p = pydos_far_alloc(1UL);
    ASSERT_NOT_NULL(p);
    pydos_far_free(p);
}

TEST(alloc_large)
{
    void far *p = pydos_far_alloc(4096UL);
    ASSERT_NOT_NULL(p);
    pydos_far_free(p);
}

/* ------------------------------------------------------------------ */
/* Reallocation                                                        */
/* ------------------------------------------------------------------ */

TEST(realloc_grow)
{
    unsigned char far *p;
    unsigned char far *q;
    int i;
    int ok;

    p = (unsigned char far *)pydos_far_alloc(64UL);
    ASSERT_NOT_NULL(p);

    /* Write a recognizable byte pattern */
    for (i = 0; i < 64; i++) {
        p[i] = (unsigned char)(i & 0xFF);
    }

    /* Grow to 256 bytes */
    q = (unsigned char far *)pydos_far_realloc(p, 256UL);
    ASSERT_NOT_NULL(q);

    /* Verify the original 64 bytes are preserved */
    ok = 1;
    for (i = 0; i < 64; i++) {
        if (q[i] != (unsigned char)(i & 0xFF)) {
            ok = 0;
            break;
        }
    }
    ASSERT_TRUE(ok);

    pydos_far_free(q);
}

TEST(realloc_shrink)
{
    void far *p;
    void far *q;

    p = pydos_far_alloc(256UL);
    ASSERT_NOT_NULL(p);

    q = pydos_far_realloc(p, 64UL);
    ASSERT_NOT_NULL(q);

    pydos_far_free(q);
}

TEST(realloc_null)
{
    /* realloc(NULL, size) should behave like alloc */
    void far *p = pydos_far_realloc((void far *)0, 100UL);
    ASSERT_NOT_NULL(p);
    pydos_far_free(p);
}

/* ------------------------------------------------------------------ */
/* Free                                                                */
/* ------------------------------------------------------------------ */

TEST(free_null)
{
    /* free(NULL) must not crash */
    pydos_far_free((void far *)0);
}

/* ------------------------------------------------------------------ */
/* Statistics                                                          */
/* ------------------------------------------------------------------ */

TEST(stats_allocs)
{
    unsigned long before;
    unsigned long after_alloc;
    unsigned long after_free;
    void far *p;

    before = pydos_mem_current_allocs();

    p = pydos_far_alloc(32UL);
    ASSERT_NOT_NULL(p);
    after_alloc = pydos_mem_current_allocs();
    ASSERT_EQ(after_alloc, before + 1);

    pydos_far_free(p);
    after_free = pydos_mem_current_allocs();
    ASSERT_EQ(after_free, before);
}

TEST(stats_bytes)
{
    unsigned long before;
    unsigned long after_alloc;
    void far *p;

    before = pydos_mem_current_bytes();

    p = pydos_far_alloc(128UL);
    ASSERT_NOT_NULL(p);
    after_alloc = pydos_mem_current_bytes();
    ASSERT_TRUE(after_alloc >= before + 128);

    pydos_far_free(p);
}

TEST(avail_positive)
{
    unsigned long avail = pydos_mem_avail();
    ASSERT_TRUE(avail > 0);
}

/* ------------------------------------------------------------------ */
/* Public runner                                                       */
/* ------------------------------------------------------------------ */

void run_mem_tests(void)
{
    SUITE("pdos_mem");

    RUN(alloc_basic);
    RUN(alloc_zero);
    RUN(alloc_small);
    RUN(alloc_large);
    RUN(realloc_grow);
    RUN(realloc_shrink);
    RUN(realloc_null);
    RUN(free_null);
    RUN(stats_allocs);
    RUN(stats_bytes);
    RUN(avail_positive);
}
