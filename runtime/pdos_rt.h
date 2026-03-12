/*
 * pydos_rt.h - Runtime init/shutdown for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 *
 * This header includes ALL other runtime headers for convenience.
 */

#ifndef PDOS_RT_H
#define PDOS_RT_H

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
#include "pdos_asn.h"

/* Global namespace dictionary */
extern PyDosObj far * PYDOS_API pydos_globals;

/* Active closure for closure-passing mechanism (set before indirect call).
 * Referenced directly from generated assembly as pydos_active_closure_.
 * Explicit pragma ensures the trailing underscore on both wcc and wcc386,
 * since __cdecl naming on variables is unreliable. */
extern PyDosObj far * pydos_active_closure;
#ifdef __WATCOMC__
#pragma aux pydos_active_closure "*_"
#endif

/* Sent value for generator send()/next() protocol.
 * Set before calling resume(), read inside at yield resumption.
 * Same pattern as pydos_active_closure. */
extern PyDosObj far * pydos_gen_sent;
#ifdef __WATCOMC__
#pragma aux pydos_gen_sent "*_"
#endif

/* Initialize ALL runtime subsystems in correct order */
void PYDOS_API pydos_rt_init(void);

/* Shut down all subsystems in reverse order */
void PYDOS_API pydos_rt_shutdown(void);

#endif /* PDOS_RT_H */
