/*
 * pydos_builtins.h - Built-in functions for PyDOS runtime
 *
 * Python-to-8086 DOS compiler runtime.
 * Open Watcom C, large memory model (-ml), 8086 real-mode DOS.
 */

#ifndef PDOS_BLT_H
#define PDOS_BLT_H

#include "pdos_obj.h"

/* All builtins follow: PyDosObj far * func(int argc, PyDosObj far * far *argv) */

PyDosObj far * PYDOS_API pydos_builtin_print(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_input(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_len(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_range(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_type(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_isinstance(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_issubclass(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_int_conv(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_str_conv(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_bool_conv(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_abs(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_ord(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_chr(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_hex(int argc, PyDosObj far * far *argv);

/* Phase 3: additional builtins */
PyDosObj far * PYDOS_API pydos_builtin_float_conv(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_repr(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_hash(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_id(int argc, PyDosObj far * far *argv);

/* Phase 4: additional builtins */
PyDosObj far * PYDOS_API pydos_builtin_list_conv(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_dict_conv(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_iter(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_open(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_hasattr(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_getattr(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_setattr(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_super(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_builtin_next(int argc, PyDosObj far * far *argv);

void PYDOS_API pydos_builtins_init(void);
void PYDOS_API pydos_builtins_shutdown(void);

#endif /* PDOS_BLT_H */
