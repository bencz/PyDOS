"""Private bridges to BIOS/DOS terminal primitives."""

from _internal import internal_implementation


@internal_implementation("pydos_tui_clear_")
def _pydos_tui_clear(fg: int = 7, bg: int = 0) -> None: ...


@internal_implementation("pydos_tui_write_at_")
def _pydos_tui_write_at(x: int, y: int, text: str,
                        fg: int = 7, bg: int = 0) -> None: ...


@internal_implementation("pydos_tui_cursor_")
def _pydos_tui_cursor(x: int, y: int) -> None: ...


@internal_implementation("pydos_tui_cursor_visible_")
def _pydos_tui_cursor_visible(visible: bool) -> None: ...


@internal_implementation("pydos_tui_key_available_")
def _pydos_tui_key_available() -> bool: ...


@internal_implementation("pydos_tui_read_key_")
def _pydos_tui_read_key() -> object: ...


@internal_implementation("pydos_tui_ticks_ms_")
def _pydos_tui_ticks_ms() -> int: ...


@internal_implementation("pydos_tui_delay_ms_")
def _pydos_tui_delay_ms(milliseconds: int) -> None: ...


@internal_implementation("pydos_tui_width_")
def _pydos_tui_width() -> int: ...


@internal_implementation("pydos_tui_height_")
def _pydos_tui_height() -> int: ...
