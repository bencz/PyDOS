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

#define MEM_HEADER_MAGIC 0x5044U

typedef struct PyDosMemHeader {
    /* Keep the user payload aligned as Open Watcom's far heap expects.
     * Reducing this field to 16 bits makes the header six bytes on 8086 and
     * shifts every payload off the allocator's four-byte alignment. */
    unsigned long size;
    unsigned short magic;
    unsigned char kind;
    unsigned char reserved;
} PyDosMemHeader;

/* The payload starts immediately after the header.  A compile-time failure
 * is preferable to silently returning misaligned far pointers on a new ABI. */
typedef char PyDosMemHeaderPreservesAlignment[
    (sizeof(PyDosMemHeader) % sizeof(unsigned long)) == 0 ? 1 : -1];

/* ------------------------------------------------------------------ */
/* Allocation tracking statistics                                      */
/* ------------------------------------------------------------------ */
static unsigned long stat_total_allocs  = 0UL;
static unsigned long stat_total_bytes   = 0UL;
static unsigned long stat_cur_allocs    = 0UL;
static unsigned long stat_cur_bytes     = 0UL;
static unsigned long stat_peak_bytes    = 0UL;
static unsigned long stat_failed_allocs = 0UL;
static unsigned long stat_kind_cur[PYDOS_MEM_KIND_COUNT];
static unsigned long stat_kind_peak[PYDOS_MEM_KIND_COUNT];

static int valid_kind(PyDosMemKind kind)
{
    return (int)kind >= 0 && (int)kind < PYDOS_MEM_KIND_COUNT;
}

static void stats_add(PyDosMemKind kind, unsigned long size)
{
    stat_total_allocs++;
    stat_total_bytes += size;
    stat_cur_allocs++;
    stat_cur_bytes += size;
    if (stat_cur_bytes > stat_peak_bytes) stat_peak_bytes = stat_cur_bytes;
    if (valid_kind(kind)) {
        stat_kind_cur[(int)kind] += size;
        if (stat_kind_cur[(int)kind] > stat_kind_peak[(int)kind])
            stat_kind_peak[(int)kind] = stat_kind_cur[(int)kind];
    }
}

static void stats_remove(PyDosMemKind kind, unsigned long size)
{
    if (stat_cur_allocs > 0UL) stat_cur_allocs--;
    stat_cur_bytes = stat_cur_bytes >= size ? stat_cur_bytes - size : 0UL;
    if (valid_kind(kind)) {
        int index = (int)kind;
        stat_kind_cur[index] = stat_kind_cur[index] >= size
                               ? stat_kind_cur[index] - size : 0UL;
    }
}

/* ------------------------------------------------------------------ */
/* pydos_far_alloc — allocate from far heap with tracking              */
/* ------------------------------------------------------------------ */
void far * PYDOS_API pydos_mem_alloc(PyDosMemKind kind, unsigned long size)
{
    PyDosMemHeader far *header;
    unsigned long raw_size;

    if (size == 0UL) {
        return (void far *)0;
    }

    if (!valid_kind(kind)) kind = PYDOS_MEM_GENERAL;
    raw_size = size + (unsigned long)sizeof(PyDosMemHeader);
    if (raw_size < size) {
        stat_failed_allocs++;
        return (void far *)0;
    }

#ifndef PYDOS_32BIT
    /*
     * _fmalloc takes a size_t (unsigned int on 16-bit).
     * For allocations > 64K we cannot satisfy them in one block
     * on a 16-bit real-mode system; fail gracefully.
     */
    if (raw_size > 0xFFF0UL) {
        stat_failed_allocs++;
        return (void far *)0;
    }
#endif

    header = (PyDosMemHeader far *)_fmalloc((unsigned int)raw_size);
    if (header == (PyDosMemHeader far *)0) {
        stat_failed_allocs++;
        return (void far *)0;
    }
    header->size = size;
    header->magic = MEM_HEADER_MAGIC;
    header->kind = (unsigned char)kind;
    header->reserved = 0;
    stats_add(kind, size);
    return (void far *)(header + 1);
}

void far * PYDOS_API pydos_far_alloc(unsigned long size)
{
    return pydos_mem_alloc(PYDOS_MEM_GENERAL, size);
}

/* ------------------------------------------------------------------ */
/* pydos_far_free — release far heap memory with tracking              */
/* ------------------------------------------------------------------ */
void PYDOS_API pydos_far_free(void far *p)
{
    PyDosMemHeader far *header;
    PyDosMemKind kind;
    unsigned long size;

    if (p == (void far *)0) {
        return;
    }

    header = ((PyDosMemHeader far *)p) - 1;
    if (header->magic != MEM_HEADER_MAGIC) return;
    size = header->size;
    kind = (PyDosMemKind)header->kind;
    header->magic = 0;
    stats_remove(kind, size);
    _ffree(header);
}

/* ------------------------------------------------------------------ */
/* pydos_far_realloc — reallocate far heap block with tracking         */
/* ------------------------------------------------------------------ */
void far * PYDOS_API pydos_mem_realloc(void far *p, unsigned long newsize)
{
    PyDosMemHeader far *header;
    PyDosMemHeader far *new_header;
    unsigned long old_size;
    unsigned long raw_size;
    PyDosMemKind kind;

#ifndef PYDOS_32BIT
    if (newsize + (unsigned long)sizeof(PyDosMemHeader) > 0xFFF0UL) {
        stat_failed_allocs++;
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

    header = ((PyDosMemHeader far *)p) - 1;
    if (header->magic != MEM_HEADER_MAGIC) return (void far *)0;
    old_size = header->size;
    kind = (PyDosMemKind)header->kind;
    raw_size = newsize + (unsigned long)sizeof(PyDosMemHeader);
    if (raw_size < newsize) {
        stat_failed_allocs++;
        return (void far *)0;
    }
    new_header = (PyDosMemHeader far *)_frealloc(
        header, (unsigned int)raw_size);
    if (new_header == (PyDosMemHeader far *)0) {
        stat_failed_allocs++;
        return (void far *)0;
    }
    stats_remove(kind, old_size);
    /* Realloc is not a new allocation, but cumulative bytes records traffic. */
    if (stat_total_allocs > 0UL) stat_total_allocs--;
    stats_add(kind, newsize);
    new_header->size = newsize;
    new_header->magic = MEM_HEADER_MAGIC;
    new_header->kind = (unsigned char)kind;
    return (void far *)(new_header + 1);
}

void far * PYDOS_API pydos_far_realloc(void far *p, unsigned long newsize)
{
    return pydos_mem_realloc(p, newsize);
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
#elif defined(_COMPAT_DOS_H)
    /* Native runtime tests compile through compat/dos.h, whose _memavl()
     * supplies a deterministic flat-host heap estimate. */
    return (unsigned long)_memavl();
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

unsigned long PYDOS_API pydos_mem_peak_bytes(void)
{
    return stat_peak_bytes;
}

unsigned long PYDOS_API pydos_mem_kind_current_bytes(PyDosMemKind kind)
{
    if (!valid_kind(kind)) return 0UL;
    return stat_kind_cur[(int)kind];
}

unsigned long PYDOS_API pydos_mem_kind_peak_bytes(PyDosMemKind kind)
{
    if (!valid_kind(kind)) return 0UL;
    return stat_kind_peak[(int)kind];
}

unsigned long PYDOS_API pydos_mem_failed_allocs(void)
{
    return stat_failed_allocs;
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
    stat_peak_bytes = 0UL;
    stat_failed_allocs = 0UL;
    memset(stat_kind_cur, 0, sizeof(stat_kind_cur));
    memset(stat_kind_peak, 0, sizeof(stat_kind_peak));

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
