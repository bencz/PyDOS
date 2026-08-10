/*
 * pdos_mon.h - Low-overhead Python 3.12 sys.monitoring support
 */

#ifndef PDOS_MON_H
#define PDOS_MON_H

#include "pdos_obj.h"

/* Read directly by generated assembly on every Python function entry/return.
 * Variables do not reliably inherit Watcom's __cdecl name decoration. */
extern unsigned char pydos_monitoring_active;
#ifdef __WATCOMC__
#pragma aux pydos_monitoring_active "*_"
#endif

void PYDOS_API pydos_monitoring_init(void);
void PYDOS_API pydos_monitoring_shutdown(void);
PyDosObj far * PYDOS_API pydos_monitoring_new(void);
void PYDOS_API pydos_monitoring_py_start(void (far *code)(void));
void PYDOS_API pydos_monitoring_py_return(void (far *code)(void),
                                          PyDosObj far *value);

#endif /* PDOS_MON_H */
