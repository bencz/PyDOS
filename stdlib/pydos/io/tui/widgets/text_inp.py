"""Single-line editable text field model.

The physical filename is the DOS 8.3 alias for widgets.text_input.
"""

from pydos.io.tui.constants import Key
from pydos.io.tui.widgets.base import Widget


class TextInput(Widget):
    def __init__(self, x, y, width, value="", max_length=255):
        super().__init__(x, y, width, 1)
        self.value = value
        self.cursor = len(value)
        self.offset = 0
        self.max_length = max_length
        self.insert_mode = True
        self.ensure_visible()

    def ensure_visible(self):
        if self.cursor < self.offset:
            self.offset = self.cursor
        if self.cursor >= self.offset + self.width:
            self.offset = self.cursor - self.width + 1
        if self.offset < 0:
            self.offset = 0

    def set_value(self, value):
        self.value = value[:self.max_length]
        self.cursor = len(self.value)
        self.offset = 0
        self.ensure_visible()

    def display_text(self):
        shown = self.value[self.offset:self.offset + self.width]
        return shown.ljust(self.width)

    def cursor_x(self):
        return self.x + self.cursor - self.offset

    def draw(self, canvas, focused=False):
        if self.visible:
            canvas.draw_text(self.x, self.y, self.display_text())

    def insert(self, text):
        if len(text) == 0:
            return
        available = self.max_length - len(self.value)
        if self.insert_mode:
            if available <= 0:
                return
            text = text[:available]
            self.value = (self.value[:self.cursor] + text
                          + self.value[self.cursor:])
        else:
            end = self.cursor + len(text)
            if end > self.max_length:
                text = text[:self.max_length - self.cursor]
                end = self.cursor + len(text)
            self.value = (self.value[:self.cursor] + text
                          + self.value[end:])
        self.cursor += len(text)
        self.ensure_visible()

    def backspace(self):
        if self.cursor > 0:
            self.value = (self.value[:self.cursor - 1]
                          + self.value[self.cursor:])
            self.cursor -= 1
            self.ensure_visible()

    def delete(self):
        if self.cursor < len(self.value):
            self.value = (self.value[:self.cursor]
                          + self.value[self.cursor + 1:])

    def handle_key(self, key):
        if not self.enabled:
            return False
        if key == Key.LEFT:
            if self.cursor > 0:
                self.cursor -= 1
        elif key == Key.RIGHT:
            if self.cursor < len(self.value):
                self.cursor += 1
        elif key == Key.HOME:
            self.cursor = 0
        elif key == Key.END:
            self.cursor = len(self.value)
        elif key == Key.BACKSPACE:
            self.backspace()
        elif key == Key.DELETE:
            self.delete()
        elif key == Key.INSERT:
            self.insert_mode = not self.insert_mode
        elif key is not None and key >= 32 and key <= 126:
            self.insert(chr(key))
        else:
            return False
        self.ensure_visible()
        return True
