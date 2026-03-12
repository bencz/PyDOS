/*
 * pydos_mem.c - Far heap memory wrappers for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#include "pdos_mem.h"
#ifndef PYDOS_32BIT
#include <malloc.h>
#endif
#include <string.h>

/* ------------------------------------------------------------------ */
/* Allocation tracking statistics                                      */
/* ------------------------------------------------------------------ */
static unsigned long stat_total_allocs  = 0UL;
static unsigned long stat_total_bytes   = 0UL;
static unsigned long stat_cur_allocs    = 0UL;
static unsigned long stat_cur_bytes     = 0UL;

/* ------------------------------------------------------------------ */
/* pydos_far_alloc — allocate from far heap with tracking              */
/* ------------------------------------------------------------------ */
void far * PYDOS_API pydos_far_alloc(unsigned long size)
{
    void far *p;

    if (size == 0UL) {
        return (void far *)0;
    }

#ifndef PYDOS_32BIT
    /*
     * _fmalloc takes a size_t (unsigned int on 16-bit).
     * For allocations > 64K we cannot satisfy them in one block
     * on a 16-bit real-mode system; fail gracefully.
     */
    if (size > 0xFFF0UL) {
        return (void far *)0;
    }
#endif

    p = _fmalloc((unsigned int)size);
    if (p != (void far *)0) {
        stat_total_allocs++;
        stat_total_bytes += size;
        stat_cur_allocs++;
        stat_cur_bytes += size;
    }
    return p;
}

/* ------------------------------------------------------------------ */
/* pydos_far_free — release far heap memory with tracking              */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_far_free(void far *p)
{
    if (p == (void far *)0) {
        return;
    }

    /*
     * We cannot recover the exact size of the allocation from
     * _fmalloc, so we decrement the alloc count only.
     * For byte tracking we use _fmsize if available.
     */
#ifdef __WATCOMC__
    {
        unsigned int sz;
        sz = _fmsize(p);
        if (stat_cur_bytes >= (unsigned long)sz) {
            stat_cur_bytes -= (unsigned long)sz;
        } else {
            stat_cur_bytes = 0UL;
        }
    }
#endif

    if (stat_cur_allocs > 0UL) {
        stat_cur_allocs--;
    }

    _ffree(p);
}

/* ------------------------------------------------------------------ */
/* pydos_far_realloc — reallocate far heap block with tracking         */
/* ------------------------------------------------------------------ */
void far * PYDOS_API pydos_far_realloc(void far *p, unsigned long newsize)
{
    void far *np;
    unsigned int old_sz;

#ifndef PYDOS_32BIT
    if (newsize > 0xFFF0UL) {
        return (void far *)0;
    }
#endif

    if (p == (void far *)0) {
        return pydos_far_alloc(newsize);
    }

    if (newsize == 0UL) {
        pydos_far_free(p);
        return (void far *)0;
    }

    /* Track the old size before realloc */
#ifdef __WATCOMC__
    old_sz = _fmsize(p);
#else
    old_sz = 0;
#endif

    np = _frealloc(p, (unsigned int)newsize);
    if (np != (void far *)0) {
        /* Adjust byte tracking: remove old, add new */
        if (stat_cur_bytes >= (unsigned long)old_sz) {
            stat_cur_bytes -= (unsigned long)old_sz;
        } else {
            stat_cur_bytes = 0UL;
        }
        stat_cur_bytes += newsize;
        stat_total_bytes += newsize;  /* total is cumulative */
    }
    return np;
}

/* ------------------------------------------------------------------ */
/* pydos_mem_avail — estimate available far heap memory                 */
/* ------------------------------------------------------------------ */
unsigned long PYDOS_API pydos_mem_avail(void)
{
#ifdef __WATCOMC__
    /*
     * _memavl() returns the size of the largest contiguous block
     * in the near heap.  For far heap, _fmemavl() or _freect()
     * may be more useful, but _memavl is always available.
     *
     * _fheapgrow() can be used to grow the far heap first, then
     * _freect(0) returns approximate free far memory.
     */
    unsigned long avail;

    _fheapgrow();
    avail = (unsigned long)_memavl();

    /*
     * Also try to estimate far heap free space.
     * _freect(sz) returns number of sz-byte blocks free in far heap.
     * Using block size of 16 gives a rough estimate.
     */
    {
        unsigned long far_free;
        far_free = (unsigned long)_freect(16) * 16UL;
        if (far_free > avail) {
            avail = far_free;
        }
    }

    return avail;
#else
    return 0UL;
#endif
}

/* ------------------------------------------------------------------ */
/* Statistics accessors                                                 */
/* ------------------------------------------------------------------ */

unsigned long PYDOS_API pydos_mem_total_allocs(void)
{
    return stat_total_allocs;
}

unsigned long PYDOS_API pydos_mem_total_bytes(void)
{
    return stat_total_bytes;
}

unsigned long PYDOS_API pydos_mem_current_allocs(void)
{
    return stat_cur_allocs;
}

unsigned long PYDOS_API pydos_mem_current_bytes(void)
{
    return stat_cur_bytes;
}

/* ------------------------------------------------------------------ */
/* pydos_mem_init / pydos_mem_shutdown                                 */
/* ------------------------------------------------------------------ */

void PYDOS_API pydos_mem_init(void)
{
    stat_total_allocs = 0UL;
    stat_total_bytes = 0UL;
    stat_cur_allocs = 0UL;
    stat_cur_bytes = 0UL;

#ifdef __WATCOMC__
    /* Pre-grow the far heap to make more memory available */
    _fheapgrow();
#endif
}

void PYDOS_API pydos_mem_shutdown(void)
{
    /*
     * In debug builds, check for leaks.
     * stat_cur_allocs should be 0 at this point.
     */
#ifdef __WATCOMC__
    _fheapshrink();
#endif
}
