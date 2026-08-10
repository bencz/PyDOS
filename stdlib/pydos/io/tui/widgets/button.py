"""Focusable command button."""

from pydos.io.tui.constants import Key
from pydos.io.tui.widgets.base import Widget


class Button(Widget):
    def __init__(self, x, y, width, text, on_click, hotkey=None):
        super().__init__(x, y, width, 1)
        self.text = text
        self.on_click = on_click
        self.hotkey = hotkey

    def activate(self):
        if self.enabled and self.on_click is not None:
            self.on_click()

    def draw(self, canvas, focused=False):
        if not self.visible:
            return
        inner_width = self.width - 2
        label = self.text
        if len(label) > inner_width:
            label = label[:inner_width]
        left = (inner_width - len(label)) // 2
        right = inner_width - len(label) - left
        if focused:
            value = ">" + " " * left + label + " " * right + "<"
        else:
            value = "[" + " " * left + label + " " * right + "]"
        canvas.draw_text(self.x, self.y, value)

    def handle_key(self, key):
        if not self.enabled:
            return False
        if (key == Key.ENTER or key == 32
                or (self.hotkey is not None and key == self.hotkey)):
            self.activate()
            return True
        return False
