"""DOS smoke test of the TUI engine primitives.

Prints only what is stable under DOSEMU and the CPython stubs: probed
dimensions, idle input, clock monotonicity and video state round-trips.
The pixel-exact behavior is covered natively by rttests/t_tui.c.
"""

packed = _pydos_tui_probe()
print(packed & 255, (packed >> 8) & 255)

print(_pydos_tui_key_event())
print(_pydos_tui_shift_state() >= 0)
print(_pydos_tui_mouse_init() >= 0)

rows = _pydos_tui_set_rows(50)
print(rows == 50 or rows == 25)
print(_pydos_tui_set_rows(25))

state = _pydos_tui_save_video()
print(state >= 0)
print(_pydos_tui_fill(0, 0, 2, 1, 65, 7))
print(_pydos_tui_scroll(0, 0, 2, 2, 1, 7))
print(_pydos_tui_cursor(0, 0))
print(_pydos_tui_cursor_shape(1))
print(_pydos_tui_blink(False))
print(_pydos_tui_vsync())
print(_pydos_tui_restore_video(state))

start = _pydos_tui_ticks_ms()
_pydos_tui_sleep_ms(60)
finish = _pydos_tui_ticks_ms()
print(start >= 0, finish - start >= 55)
