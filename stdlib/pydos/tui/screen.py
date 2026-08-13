"""The physical text screen.

A context manager that owns the video state: entering saves the mode,
switches blink off (so all 16 background colors work) and hides the
cursor; leaving restores everything even when an exception unwinds:

    with Screen() as screen:          # or Screen(50) for 80x50
        screen.present(buffer)
        screen.cursor(x, y, Cursor.UNDERLINE)

``present`` hands the buffer's two string planes to the C engine, which
compares them against its shadow of video memory and writes only the
cells that changed — the cost of a frame is proportional to what moved,
not to the screen size.
"""

from pydos.tui.buffer import Buffer


class Cursor:
    HIDDEN = 0
    UNDERLINE = 1
    BLOCK = 2


class Screen:
    def __init__(self, rows: int = 0) -> None:
        if rows == 43 or rows == 50:
            _pydos_tui_set_rows(rows)
        packed: int = _pydos_tui_probe()
        self.width = packed & 255
        self.height = (packed >> 8) & 255
        self.mono = ((packed >> 16) & 1) != 0
        self.saved_state = -1

    def present(self, buffer: Buffer, x: int = 0, y: int = 0) -> None:
        _pydos_tui_present(buffer.glyph_rows, buffer.attr_rows, x, y)

    def cursor(self, x: int, y: int, shape: int = 1) -> None:
        _pydos_tui_cursor(x, y)
        _pydos_tui_cursor_shape(shape)

    def hide_cursor(self) -> None:
        _pydos_tui_cursor_shape(Cursor.HIDDEN)

    def set_rows(self, rows: int) -> int:
        """Switch 25/43/50 lines; returns (and adopts) the real count."""
        effective: int = _pydos_tui_set_rows(rows)
        packed: int = _pydos_tui_probe()
        self.width = packed & 255
        self.height = (packed >> 8) & 255
        return effective

    def __enter__(self):
        self.saved_state = _pydos_tui_save_video()
        _pydos_tui_blink(False)
        self.hide_cursor()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        if self.saved_state >= 0:
            _pydos_tui_restore_video(self.saved_state)
            self.saved_state = -1
        return False
