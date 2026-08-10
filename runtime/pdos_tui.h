/*
 * pdos_tui.h - Minimal DOS text terminal primitives for PyDOS.
 *
 * Layout, widgets, buffering and application policy belong to the Python
 * stdlib.  This module only exposes BIOS/DOS operations that Python cannot
 * implement portably or efficiently.
 */

#ifndef PDOS_TUI_H
#define PDOS_TUI_H

#include "pdos_obj.h"

PyDosObj far * PYDOS_API pydos_tui_clear(int argc,
                                          PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_write_at(int argc,
                                             PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_cursor(int argc,
                                          PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_cursor_visible(
    int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_key_available(
    int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_read_key(int argc,
                                             PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_ticks_ms(int argc,
                                             PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_delay_ms(int argc,
                                             PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_width(int argc,
                                         PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_height(int argc,
                                          PyDosObj far * far *argv);

#endif /* PDOS_TUI_H */
