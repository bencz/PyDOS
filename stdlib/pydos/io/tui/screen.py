"""Immediate-mode DOS text screen."""

from pydos.io.tui.canvas import Canvas
from pydos.io.tui.constants import Color


class Screen:
    def __init__(self, fg=Color.LIGHT_GRAY, bg=Color.BLACK):
        self.width = _pydos_tui_width()
        self.height = _pydos_tui_height()
        self.fg = fg
        self.bg = bg

    def clear(self, fg=None, bg=None):
        if fg is None:
            fg = self.fg
        if bg is None:
            bg = self.bg
        _pydos_tui_clear(fg, bg)

    def write(self, x, y, text, fg=None, bg=None):
        if fg is None:
            fg = self.fg
        if bg is None:
            bg = self.bg
        _pydos_tui_write_at(x, y, str(text), fg, bg)

    def move_cursor(self, x, y):
        _pydos_tui_cursor(x, y)

    def show_cursor(self):
        _pydos_tui_cursor_visible(True)

    def hide_cursor(self):
        _pydos_tui_cursor_visible(False)

    def present(self, canvas, fg=None, bg=None):
        y = 0
        while y < canvas.height and y < self.height:
            self.write(0, y, canvas.get_line(y), fg, bg)
            y += 1

    def draw_box(self, x, y, width, height, title="", fg=None, bg=None):
        canvas = Canvas(width, height)
        canvas.draw_box(0, 0, width, height, title)
        row = 0
        while row < height:
            self.write(x, y + row, canvas.get_line(row), fg, bg)
            row += 1

    def __enter__(self):
        self.clear()
        self.hide_cursor()
        return self

    def __exit__(self, exc_type, exc_value, traceback):
        self.show_cursor()
        self.move_cursor(0, self.height - 1)
        return False
