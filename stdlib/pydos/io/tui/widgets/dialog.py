"""Framed modal content that can be composed onto a Canvas."""

from pydos.io.tui.widgets.base import Widget


class Dialog(Widget):
    def __init__(self, x, y, width, height, title="", lines=None):
        super().__init__(x, y, width, height)
        self.title = title
        if lines is None:
            lines = []
        self.lines = lines

    def set_lines(self, lines):
        self.lines = lines

    def draw(self, canvas, focused=False):
        if not self.visible:
            return
        canvas.draw_box(self.x, self.y, self.width, self.height,
                        self.title)
        inner_row = 1
        while inner_row < self.height - 1:
            canvas.draw_hline(self.x + 1, self.y + inner_row,
                              self.width - 2, " ")
            inner_row += 1
        available = self.width - 4
        row = 0
        while row < len(self.lines) and row < self.height - 2:
            canvas.draw_text(self.x + 2, self.y + 1 + row,
                             self.lines[row][:available])
            row += 1
