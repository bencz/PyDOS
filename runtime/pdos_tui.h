/*
 * pdos_tui.h - DOS text terminal primitives for PyDOS.
 *
 * Layout, widgets, buffering and application policy belong to the Python
 * stdlib.  This module only exposes BIOS/DOS operations that Python cannot
 * implement portably or efficiently.
 *
 * The engine: direct video memory with a shadow-buffer diff (present),
 * extended keyboard (INT 16h), mouse (INT 33h) and BIOS tick timing.
 */

#ifndef PDOS_TUI_H
#define PDOS_TUI_H

#include "pdos_obj.h"

/* video */
PyDosObj far * PYDOS_API pydos_tui_probe(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_present(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_fill(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_scroll(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_cursor(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_cursor_shape(int argc,
                                                PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_set_rows(int argc,
                                            PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_blink(int argc, PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_save_video(int argc,
                                              PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_restore_video(int argc,
                                                 PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_vsync(int argc, PyDosObj far * far *argv);

/* keyboard */
PyDosObj far * PYDOS_API pydos_tui_key_event(int argc,
                                             PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_shift_state(int argc,
                                               PyDosObj far * far *argv);

/* mouse */
PyDosObj far * PYDOS_API pydos_tui_mouse_init(int argc,
                                              PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_mouse_poll(int argc,
                                              PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_mouse_show(int argc,
                                              PyDosObj far * far *argv);

/* time */
PyDosObj far * PYDOS_API pydos_tui_ticks_ms(int argc,
                                            PyDosObj far * far *argv);
PyDosObj far * PYDOS_API pydos_tui_sleep_ms(int argc,
                                            PyDosObj far * far *argv);

/* Pure packing helpers and inspection hooks.  Internal: exposed only for
 * rttests (they stay outside stdlib.idx, costing no builtin slots).
 * pydos_tui_debug_vram returns the live cell plane (the fake VRAM on the
 * host build) or NULL when only selector-based access is available;
 * pydos_tui_debug_writes returns the number of cells written since the
 * previous call and resets the counter. */
long PYDOS_API pydos_tui_pack_key(unsigned int ax, unsigned int shift_flags);
long PYDOS_API pydos_tui_pack_mouse(unsigned int col, unsigned int row,
                                    unsigned int buttons,
                                    unsigned int left_press,
                                    unsigned int left_release,
                                    unsigned int right_press,
                                    unsigned int right_release);
unsigned short far * PYDOS_API pydos_tui_debug_vram(void);
long PYDOS_API pydos_tui_debug_writes(void);

#endif /* PDOS_TUI_H */
