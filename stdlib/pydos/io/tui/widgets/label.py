"""Static text widget."""

from pydos.io.tui.widgets.base import Widget


class Label(Widget):
    def __init__(self, x, y, text, width=0):
        if width <= 0:
            width = len(text)
        super().__init__(x, y, width, 1)
        self.text = text

    def draw(self, canvas, focused=False):
        if self.visible:
            canvas.draw_text(self.x, self.y, self.text[:self.width])
