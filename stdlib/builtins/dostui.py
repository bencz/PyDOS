"""Private bridges to the BIOS/DOS terminal engine (pdos_tui.c).

Video works against a shadow of text video memory: ``present`` receives
the two string planes of a pydos.tui Buffer and writes only the cells
that changed.  Keyboard, mouse and clock results come packed in ints —
the layouts are documented in runtime/pdos_tui.c and decoded by
pydos.tui.keys / pydos.tui.input.
"""

from _internal import internal_implementation


@internal_implementation("pydos_tui_probe_")
def _pydos_tui_probe() -> int: ...


@internal_implementation("pydos_tui_present_")
def _pydos_tui_present(glyphs: list, attrs: list,
                       x: int = 0, y: int = 0) -> None: ...


@internal_implementation("pydos_tui_fill_")
def _pydos_tui_fill(x: int, y: int, width: int, height: int,
                    char_code: int = 32, attr: int = 7) -> None: ...


@internal_implementation("pydos_tui_scroll_")
def _pydos_tui_scroll(x: int, y: int, width: int, height: int,
                      lines: int, attr: int = 7) -> None: ...


@internal_implementation("pydos_tui_cursor_")
def _pydos_tui_cursor(x: int, y: int) -> None: ...


@internal_implementation("pydos_tui_cursor_shape_")
def _pydos_tui_cursor_shape(kind: int) -> None: ...


@internal_implementation("pydos_tui_set_rows_")
def _pydos_tui_set_rows(rows: int) -> int: ...


@internal_implementation("pydos_tui_blink_")
def _pydos_tui_blink(enabled: bool) -> None: ...


@internal_implementation("pydos_tui_save_video_")
def _pydos_tui_save_video() -> int: ...


@internal_implementation("pydos_tui_restore_video_")
def _pydos_tui_restore_video(state: int) -> None: ...


@internal_implementation("pydos_tui_vsync_")
def _pydos_tui_vsync() -> None: ...


@internal_implementation("pydos_tui_key_event_")
def _pydos_tui_key_event() -> int: ...


@internal_implementation("pydos_tui_shift_state_")
def _pydos_tui_shift_state() -> int: ...


@internal_implementation("pydos_tui_mouse_init_")
def _pydos_tui_mouse_init() -> int: ...


@internal_implementation("pydos_tui_mouse_poll_")
def _pydos_tui_mouse_poll() -> int: ...


@internal_implementation("pydos_tui_mouse_show_")
def _pydos_tui_mouse_show(visible: bool) -> None: ...


@internal_implementation("pydos_tui_ticks_ms_")
def _pydos_tui_ticks_ms() -> int: ...


@internal_implementation("pydos_tui_sleep_ms_")
def _pydos_tui_sleep_ms(milliseconds: int) -> None: ...
