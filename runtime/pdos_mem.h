/*
 * pydos_mem.h - Far heap memory wrappers for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_MEM_H
#define PDOS_MEM_H

#include "pdos_obj.h"

void far *      PYDOS_API pydos_far_alloc(unsigned long size);
void            PYDOS_API pydos_far_free(void far *p);
void far *      PYDOS_API pydos_far_realloc(void far *p, unsigned long newsize);
unsigned long   PYDOS_API pydos_mem_avail(void);
void            PYDOS_API pydos_mem_init(void);
void            PYDOS_API pydos_mem_shutdown(void);

/* Debug statistics */
unsigned long   PYDOS_API pydos_mem_total_allocs(void);
unsigned long   PYDOS_API pydos_mem_total_bytes(void);
unsigned long   PYDOS_API pydos_mem_current_allocs(void);
unsigned long   PYDOS_API pydos_mem_current_bytes(void);

#endif /* PDOS_MEM_H */
